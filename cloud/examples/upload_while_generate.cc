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

// 使用命名空间简化代码
using namespace Aws::S3::Model;

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] 
                  << " <TOTAL_SIZE_in_MB> <CHUNK_SIZE_in_MB> <MIN_PART_SIZE_in_MB>\n";
        return 1;
    }
    
    // 从命令行读取参数，并默认单位为MB，转换为字节
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

        // 请替换为实际桶名称和对象键
        std::string bucketName = "generalbuckets-jx";
        std::string objectKey  = "object-key.txt";
        
        // 记录上传开始时间
        auto startTime = std::chrono::steady_clock::now();

        // Step 1: 创建分段上传请求
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

        // 保存异步上传任务的 future 结果
        std::vector<std::future<UploadPartOutcome>> futures;
        // 保存各分段上传成功后的 ETag 信息
        std::vector<CompletedPart> completedParts;
        int partNumber = 1;

        // 用于累积分段数据
        std::vector<unsigned char> partBuffer;
        partBuffer.reserve(MIN_PART_SIZE);

        // 模拟生成数据，每次生成 CHUNK_SIZE 数据
        for (size_t offset = 0; offset < TOTAL_SIZE; offset += CHUNK_SIZE) {
            // 生成CHUNK_SIZE数据，填充 'A'
            std::vector<unsigned char> chunk(CHUNK_SIZE, 'A');
            // 累加到缓冲区
            partBuffer.insert(partBuffer.end(), chunk.begin(), chunk.end());

            // 当累计数据达到或超过 MIN_PART_SIZE 时，异步上传一次
            if (partBuffer.size() >= MIN_PART_SIZE) {
                // 拷贝当前缓冲区数据用于上传
                std::vector<unsigned char> dataToUpload = partBuffer;
                // 清空缓冲区，准备累计下一分段数据
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

                        // 构造数据流
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

        // 循环结束后，如果缓冲区中仍有数据（作为最后一个分段，即使小于 MIN_PART_SIZE 也允许上传）
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

        // 等待所有异步任务完成并收集结果
        for (size_t i = 0; i < futures.size(); ++i) {
            auto outcome = futures[i].get();
            if (!outcome.IsSuccess()) {
                std::cerr << "分段 " << (i + 1) << " 上传失败，终止上传流程。" << std::endl;
                // 可在此调用 AbortMultipartUploadRequest 取消上传
                Aws::ShutdownAPI(options);
                return 1;
            }
            CompletedPart completedPart;
            completedPart.SetPartNumber(i + 1);
            completedPart.SetETag(outcome.GetResult().GetETag());
            completedParts.push_back(completedPart);
        }

        // 完成分段上传
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

        // 记录结束时间并计算耗时（毫秒）
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        std::cout << "整个文件上传耗时: " << duration.count() << " ms" << std::endl;
    }
    Aws::ShutdownAPI(options);
    return 0;
}
