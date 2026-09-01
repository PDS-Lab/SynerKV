#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/s3/model/CompletedPart.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>

// Protects shared upload state.
std::mutex upload_mutex;

// Prints command-line usage.
void PrintUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <buffer_size_bytes> <file_chunk_size_bytes> <s3_part_size_bytes> <num_threads> <num_buffers>\n";
    std::cout << "Example: " << progName << " 67108864 16384 5242880 4 5\n"; // 64 MB, 16 KB, 5 MB, 4 threads, 5 buffers
}

// Uploads one S3 part.
void UploadPart(
    const Aws::S3::S3Client& s3_client, 
    const std::string& bucketName, 
    const std::string& objectKey, 
    const std::string& uploadId, 
    char* buffer, 
    size_t offset, 
    size_t bytesToUpload, 
    int partNumber, 
    std::vector<Aws::S3::Model::CompletedPart>& completedParts) 
{
    Aws::S3::Model::UploadPartRequest uploadPartRequest;
    uploadPartRequest.SetBucket(bucketName);
    uploadPartRequest.SetKey(objectKey);
    uploadPartRequest.SetUploadId(uploadId);
    uploadPartRequest.SetPartNumber(partNumber);

    // Sets the request body.
    auto partStream = Aws::MakeShared<Aws::StringStream>("UploadPartStream");
    partStream->write(buffer + offset, bytesToUpload);
    uploadPartRequest.SetBody(partStream);
    uploadPartRequest.SetContentLength(bytesToUpload);

    auto uploadPartOutcome = s3_client.UploadPart(uploadPartRequest);
    if (!uploadPartOutcome.IsSuccess()) {
        std::cerr << "UploadPart 失败, part " << partNumber << ": " 
                  << uploadPartOutcome.GetError().GetMessage() << std::endl;
        return;
    }

    // Stores the completed part under the mutex.
    std::lock_guard<std::mutex> lock(upload_mutex);
    Aws::S3::Model::CompletedPart completedPart;
    completedPart.SetPartNumber(partNumber);
    completedPart.SetETag(uploadPartOutcome.GetResult().GetETag());
    completedParts.push_back(completedPart);

    std::cout << "分段 " << partNumber << " 上传完成\n";
}

// Uploads one buffer.
void UploadBuffer(
    const Aws::S3::S3Client& s3_client,
    const std::string& bucketName, 
    const std::string& objectKey, 
    char* buffer, 
    size_t bufferSize, 
    size_t s3PartSize, 
    int numThreads) 
{
    // Starts a multipart upload.
    Aws::S3::Model::CreateMultipartUploadRequest createRequest;
    createRequest.SetBucket(bucketName);
    createRequest.SetKey(objectKey);

    auto createOutcome = s3_client.CreateMultipartUpload(createRequest);
    if (!createOutcome.IsSuccess()) {
        std::cerr << "CreateMultipartUpload 失败: " 
                  << createOutcome.GetError().GetMessage() << std::endl;
        return;
    }
    std::string uploadId = createOutcome.GetResult().GetUploadId();
    std::cout << "上传ID: " << uploadId << "\n";

    std::vector<Aws::S3::Model::CompletedPart> completedParts;
    std::vector<std::thread> threads;

    size_t partNumber = 1;
    for (size_t offset = 0; offset < bufferSize; offset += s3PartSize, partNumber++) {
        size_t bytesToUpload = std::min(s3PartSize, bufferSize - offset);

        // Launches an upload thread.
        threads.emplace_back(UploadPart, std::ref(s3_client), bucketName, objectKey, uploadId,
                             buffer, offset, bytesToUpload, partNumber, std::ref(completedParts));

        if (threads.size() >= static_cast<size_t>(numThreads)) {
            for (auto& thread : threads) {
                thread.join();
            }
            threads.clear();
        }
    }

    // Joins remaining worker threads.
    for (auto& thread : threads) {
        thread.join();
    }

    // Sorts parts before completing the upload.
    std::sort(completedParts.begin(), completedParts.end(), [](const Aws::S3::Model::CompletedPart &a, const Aws::S3::Model::CompletedPart &b) {
        return a.GetPartNumber() < b.GetPartNumber();
    });

    // Completes the multipart upload.
    Aws::S3::Model::CompleteMultipartUploadRequest completeRequest;
    completeRequest.SetBucket(bucketName);
    completeRequest.SetKey(objectKey);
    completeRequest.SetUploadId(uploadId);

    Aws::S3::Model::CompletedMultipartUpload completedUpload;
    completedUpload.SetParts(completedParts);
    completeRequest.SetMultipartUpload(completedUpload);

    auto completeOutcome = s3_client.CompleteMultipartUpload(completeRequest);
    if (!completeOutcome.IsSuccess()) {
        std::cerr << "CompleteMultipartUpload 失败: " 
                  << completeOutcome.GetError().GetMessage() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc != 6) {
        PrintUsage(argv[0]);
        return 1;
    }

    // Parses command-line arguments.
    size_t bufferSize = std::stoull(argv[1]) * 1024 * 1024;
    size_t fileChunkSize = std::stoull(argv[2]) * 1024;
    size_t s3PartSize = std::stoull(argv[3]) * 1024 * 1024;
    int numThreads = std::stoi(argv[4]);
    int numBuffers = std::stoi(argv[5]); // Number of buffers.

    std::cout << "内存缓冲区大小: " << bufferSize << " 字节\n"
              << "文件写入块大小: " << fileChunkSize << " 字节\n"
              << "S3 上传分段大小: " << s3PartSize << " 字节\n"
              << "线程数: " << numThreads << "\n"
              << "缓冲区数量: " << numBuffers << "\n";

    // Allocates and fills the buffers.
    std::vector<char*> buffers(numBuffers);
    for (int i = 0; i < numBuffers; ++i) {
        buffers[i] = new char[bufferSize];
        std::memset(buffers[i], 'X', bufferSize);
    }

    // Writes the local files.
    const std::string filePrefix = "local_file_";
    auto startFileWrite = std::chrono::steady_clock::now();
    for (int i = 0; i < numBuffers; ++i) {
        std::string localFileName = filePrefix + std::to_string(i) + ".bin";
        std::ofstream outFile(localFileName, std::ios::binary);
        if (!outFile) {
            std::cerr << "无法打开文件 " << localFileName << " 进行写入。\n";
            for (auto buffer : buffers) {
                delete[] buffer;
            }
            return 1;
        }

        for (size_t offset = 0; offset < bufferSize; offset += fileChunkSize) {
            size_t bytesToWrite = std::min(fileChunkSize, bufferSize - offset);
            outFile.write(buffers[i] + offset, bytesToWrite);
            if (!outFile) {
                std::cerr << "写入文件失败。\n";
                for (auto buffer : buffers) {
                    delete[] buffer;
                }
                return 1;
            }
        }
        outFile.close();
        std::cout << "缓冲区 " << i << " 数据写入文件 " << localFileName << " 完成\n";
    }
    auto endFileWrite = std::chrono::steady_clock::now();
    std::cout << "本地文件写入耗时: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(endFileWrite - startFileWrite).count()
              << " 毫秒\n";

    // Uploads buffers to S3 in parallel.
    auto startTotalUpload = std::chrono::steady_clock::now(); // Starts total upload timing.

    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        Aws::S3::S3Client s3_client;
        const std::string bucketName = "generalbuckets-jx";
        const std::string objectKeyPrefix = "uploaded_object_";

        std::vector<std::thread> threads;
        for (int i = 0; i < numBuffers; ++i) {
            std::string objectKey = objectKeyPrefix + std::to_string(i);
            threads.emplace_back(UploadBuffer, std::ref(s3_client), bucketName, objectKey, buffers[i], bufferSize, s3PartSize, numThreads);
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }
    Aws::ShutdownAPI(options);

    // Releases the buffers.
    for (auto buffer : buffers) {
        delete[] buffer;
    }

    // Reports total upload time.
    auto endTotalUpload = std::chrono::steady_clock::now();
    std::cout << "总上传耗时: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(endTotalUpload - startTotalUpload).count()
              << " 毫秒\n";

    for (int i = 0; i < numBuffers; ++i) {
        std::string localFileName = filePrefix + std::to_string(i) + ".bin";
        if (std::remove(localFileName.c_str()) == 0) {
            std::cout << "已删除本地文件: " << localFileName << "\n";
        } else {
            std::cerr << "删除本地文件失败: " << localFileName << "\n";
        }
    }
    return 0;
}
