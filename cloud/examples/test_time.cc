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

// 使用互斥锁以保证多线程安全
std::mutex upload_mutex;

// 打印命令行参数使用说明
void PrintUsage(const char* progName) {
    std::cout << "Usage: " << progName << " <buffer_size_bytes> <file_chunk_size_bytes> <s3_part_size_bytes> <num_threads>\n";
    std::cout << "Example: " << progName << " 67108864 16384 5242880 4\n"; // 64MB, 16KB, 5MB, 4线程
}

// 上传 S3 分段
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

    // 设置请求体
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

    // 线程安全地存储已完成的分段信息
    std::lock_guard<std::mutex> lock(upload_mutex);
    Aws::S3::Model::CompletedPart completedPart;
    completedPart.SetPartNumber(partNumber);
    completedPart.SetETag(uploadPartOutcome.GetResult().GetETag());
    completedParts.push_back(completedPart);

    std::cout << "分段 " << partNumber << " 上传完成\n";
}

int main(int argc, char* argv[])
{
    if (argc != 5) {
        PrintUsage(argv[0]);
        return 1;
    }

    // 解析命令行参数
    size_t bufferSize = std::stoull(argv[1]) * 1024 * 1024;
    size_t fileChunkSize = std::stoull(argv[2]) * 1024;
    size_t s3PartSize = std::stoull(argv[3]) * 1024 * 1024;
    int numThreads = std::stoi(argv[4]);

    std::cout << "内存缓冲区大小: " << bufferSize << " 字节\n"
              << "文件写入块大小: " << fileChunkSize << " 字节\n"
              << "S3 上传分段大小: " << s3PartSize << " 字节\n"
              << "线程数: " << numThreads << "\n";

    // 分配并填充缓冲区
    char* buffer = new char[bufferSize];
    std::memset(buffer, 'X', bufferSize);

    // ----------------- 本地文件写入 -----------------
    const std::string localFileName = "local_file.bin";
    std::ofstream outFile(localFileName, std::ios::binary);
    if (!outFile) {
        std::cerr << "无法打开文件 " << localFileName << " 进行写入。\n";
        delete[] buffer;
        return 1;
    }

    auto startFileWrite = std::chrono::steady_clock::now();
    for (size_t offset = 0; offset < bufferSize; offset += fileChunkSize) {
        size_t bytesToWrite = std::min(fileChunkSize, bufferSize - offset);
        outFile.write(buffer + offset, bytesToWrite);
        if (!outFile) {
            std::cerr << "写入文件失败。\n";
            delete[] buffer;
            return 1;
        }
    }
    outFile.close();
    auto endFileWrite = std::chrono::steady_clock::now();
    std::cout << "本地文件写入耗时: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(endFileWrite - startFileWrite).count()
              << " 毫秒\n";

    // ----------------- AWS S3 分段上传（多线程） -----------------
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        Aws::S3::S3Client s3_client;
        const std::string bucketName = "generalbuckets-jx";
        const std::string objectKey = "uploaded_object.bin";

        // 创建 Multipart Upload
        Aws::S3::Model::CreateMultipartUploadRequest createRequest;
        createRequest.SetBucket(bucketName);
        createRequest.SetKey(objectKey);

        auto createOutcome = s3_client.CreateMultipartUpload(createRequest);
        if (!createOutcome.IsSuccess()) {
            std::cerr << "CreateMultipartUpload 失败: " 
                      << createOutcome.GetError().GetMessage() << std::endl;
            Aws::ShutdownAPI(options);
            delete[] buffer;
            return 1;
        }
        std::string uploadId = createOutcome.GetResult().GetUploadId();
        std::cout << "上传ID: " << uploadId << "\n";

        auto startS3Upload = std::chrono::steady_clock::now();

        std::vector<Aws::S3::Model::CompletedPart> completedParts;
        std::vector<std::thread> threads;

        size_t partNumber = 1;
        for (size_t offset = 0; offset < bufferSize; offset += s3PartSize, partNumber++) {
            size_t bytesToUpload = std::min(s3PartSize, bufferSize - offset);

            // 多线程执行
            threads.emplace_back(UploadPart, std::ref(s3_client), bucketName, objectKey, uploadId,
                                 buffer, offset, bytesToUpload, partNumber, std::ref(completedParts));

            if (threads.size() >= static_cast<size_t>(numThreads)) {
                for (auto& thread : threads) {
                    thread.join();
                }
                threads.clear();
            }
        }

        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }

        // 在所有分段上传完成后，调用 CompleteMultipartUpload 前，排序：
        std::sort(completedParts.begin(), completedParts.end(), [](const Aws::S3::Model::CompletedPart &a, const Aws::S3::Model::CompletedPart &b) {
            return a.GetPartNumber() < b.GetPartNumber();
        });

        // 完成上传
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

        auto endS3Upload = std::chrono::steady_clock::now();
        std::cout << "S3 分段上传耗时: " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(endS3Upload - startS3Upload).count()
                  << " 毫秒\n";
    }
    Aws::ShutdownAPI(options);

    delete[] buffer;
    // ----------------- 删除本地文件 -----------------
    if (std::remove(localFileName.c_str()) == 0) {
        std::cout << "本地文件 " << localFileName << " 删除成功。\n";
    } else {
        std::cerr << "删除本地文件 " << localFileName << " 失败。\n";
    }
    return 0;
}
