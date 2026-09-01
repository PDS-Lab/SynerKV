#include <aws/core/Aws.h>
#include <aws/core/utils/Outcome.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/s3/model/CompletedPart.h>
#include <aws/s3/model/CompletedMultipartUpload.h>
#include <chrono>
#include <iostream>
#include <vector>
#include <thread>
#include <future>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <algorithm>

// Uses S3 model types directly.
using namespace Aws::S3::Model;

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] 
                  << " <TOTAL_SIZE_in_MB> <CHUNK_SIZE_in_MB> <MIN_PART_SIZE_in_MB>\n";
        return 1;
    }
    
    // Parses sizes in MB and converts them to bytes.
    size_t TOTAL_SIZE_MB    = std::stoull(argv[1]);
    size_t CHUNK_SIZE_MB    = std::stoull(argv[2]);
    size_t MIN_PART_SIZE_MB = std::stoull(argv[3]);
    
    size_t TOTAL_SIZE    = TOTAL_SIZE_MB    * 1024 * 1024;
    size_t CHUNK_SIZE    = CHUNK_SIZE_MB    * 1024 * 1024;
    size_t MIN_PART_SIZE = MIN_PART_SIZE_MB * 1024 * 1024;

    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        Aws::Client::ClientConfiguration clientConfig;
        Aws::S3::S3Client s3_client(clientConfig);

        // Sets the target bucket and object key.
        std::string bucketName = "generalbuckets-jx";
        std::string objectKey  = "object-key.txt";
        
        // Starts upload timing.
        auto startTime = std::chrono::steady_clock::now();

        // Starts a multipart upload.
        CreateMultipartUploadRequest createRequest;
        createRequest.WithBucket(bucketName.c_str())
                     .WithKey(objectKey.c_str());
        auto createOutcome = s3_client.CreateMultipartUpload(createRequest);
        if (!createOutcome.IsSuccess()) {
            std::cerr << "创建分段上传失败: " 
                      << createOutcome.GetError().GetMessage() << std::endl;
            Aws::ShutdownAPI(options);
            return 1;
        }
        std::string uploadId = createOutcome.GetResult().GetUploadId();
        std::cout << "分段上传创建成功, uploadId: " << uploadId << std::endl;

        // Tracks asynchronous upload results.
        std::vector<std::future<UploadPartOutcome>> futures;
        // Stores completed part metadata.
        std::vector<CompletedPart> completedParts;
        int partNumber = 1;

        // Buffers data for the next part.
        std::vector<unsigned char> partBuffer;
        partBuffer.reserve(MIN_PART_SIZE);

        // Generates data in CHUNK_SIZE chunks.
        for (size_t offset = 0; offset < TOTAL_SIZE; offset += CHUNK_SIZE) {
            // Fills one chunk with 'A'.
            std::vector<unsigned char> chunk(CHUNK_SIZE, 'A');
            // Appends the chunk to the part buffer.
            partBuffer.insert(partBuffer.end(), chunk.begin(), chunk.end());

            // Uploads when the minimum part size is reached.
            if (partBuffer.size() >= MIN_PART_SIZE) {
                // Copies buffered data for upload.
                std::vector<unsigned char> dataToUpload = partBuffer;
                // Clears the buffer for the next part.
                partBuffer.clear();

                int currentPartNumber = partNumber;
                futures.push_back(std::async(std::launch::async,
                    [currentPartNumber, dataToUpload, &s3_client, bucketName, objectKey, uploadId]() -> UploadPartOutcome {
                        UploadPartRequest uploadPartRequest;
                        uploadPartRequest.SetBucket(bucketName);
                        uploadPartRequest.SetKey(objectKey);
                        uploadPartRequest.SetUploadId(uploadId);
                        uploadPartRequest.SetPartNumber(currentPartNumber);
                        uploadPartRequest.SetContentLength(dataToUpload.size());

                        // Builds the upload stream.
                        auto partStream = Aws::MakeShared<Aws::StringStream>("UploadPartStream");
                        partStream->write(reinterpret_cast<const char*>(dataToUpload.data()), dataToUpload.size());
                        uploadPartRequest.SetBody(partStream);

                        auto outcome = s3_client.UploadPart(uploadPartRequest);
                        if (!outcome.IsSuccess()) {
                            std::cerr << "分段 " << currentPartNumber 
                                      << " 上传失败: " << outcome.GetError().GetMessage() << std::endl;
                        } else {
                            std::cout << "分段 " << currentPartNumber << " 上传成功." << std::endl;
                        }
                        return outcome;
                    }
                ));
                partNumber++;
            }
        }

        // Uploads remaining data as the final part.
        if (!partBuffer.empty()) {
            std::vector<unsigned char> dataToUpload = partBuffer;
            int currentPartNumber = partNumber;
            futures.push_back(std::async(std::launch::async,
                [currentPartNumber, dataToUpload, &s3_client, bucketName, objectKey, uploadId]() -> UploadPartOutcome {
                    UploadPartRequest uploadPartRequest;
                    uploadPartRequest.SetBucket(bucketName);
                    uploadPartRequest.SetKey(objectKey);
                    uploadPartRequest.SetUploadId(uploadId);
                    uploadPartRequest.SetPartNumber(currentPartNumber);
                    uploadPartRequest.SetContentLength(dataToUpload.size());

                    auto partStream = Aws::MakeShared<Aws::StringStream>("UploadPartStream");
                    partStream->write(reinterpret_cast<const char*>(dataToUpload.data()), dataToUpload.size());
                    uploadPartRequest.SetBody(partStream);

                    auto outcome = s3_client.UploadPart(uploadPartRequest);
                    if (!outcome.IsSuccess()) {
                        std::cerr << "分段 " << currentPartNumber 
                                  << " 上传失败: " << outcome.GetError().GetMessage() << std::endl;
                    } else {
                        std::cout << "分段 " << currentPartNumber << " 上传成功." << std::endl;
                    }
                    return outcome;
                }
            ));
        }

        // Waits for uploads and collects results.
        for (size_t i = 0; i < futures.size(); ++i) {
            auto outcome = futures[i].get();
            if (!outcome.IsSuccess()) {
                std::cerr << "分段 " << (i + 1) << " 上传失败，终止上传流程。" << std::endl;
                // Abort the multipart upload here if needed.
                Aws::ShutdownAPI(options);
                return 1;
            }
            CompletedPart completedPart;
            completedPart.SetPartNumber(i + 1);
            completedPart.SetETag(outcome.GetResult().GetETag());
            completedParts.push_back(completedPart);
        }

        // Completes the multipart upload.
        CompletedMultipartUpload completedMultipartUpload;
        completedMultipartUpload.WithParts(completedParts);
        CompleteMultipartUploadRequest completeRequest;
        completeRequest.WithBucket(bucketName)
                       .WithKey(objectKey)
                       .WithUploadId(uploadId)
                       .WithMultipartUpload(completedMultipartUpload);
        auto completeOutcome = s3_client.CompleteMultipartUpload(completeRequest);
        if (!completeOutcome.IsSuccess()) {
            std::cerr << "完成分段上传失败: " 
                      << completeOutcome.GetError().GetMessage() << std::endl;
            Aws::ShutdownAPI(options);
            return 1;
        }
        std::cout << "分段上传成功完成." << std::endl;

        // Computes the elapsed upload time.
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        std::cout << "整个文件上传耗时: " << duration.count() << " ms" << std::endl;
    }
    Aws::ShutdownAPI(options);
    return 0;
}
