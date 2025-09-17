#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include "rocksdb/threadpool.h"
#include "rocksdb/rocksdb_namespace.h"
#include "rocksdb/env.h"
#include "logging/logging.h"
#include <aws/s3/S3Client.h>
#include <aws/s3/model/HeadObjectRequest.h>

namespace ROCKSDB_NAMESPACE {

class AsyncIOScheduler {
 public:
  using Task = std::function<void()>;

  static AsyncIOScheduler& Instance();

  void Start(int num_threads_ebs, int num_threads_s3, int num_s3_queues = 1);
  void Stop();

  enum class QueueType {
    EBS,
    S3
  };

  int GetEbsPendingTaskCount() const;

  int GetS3PendingTaskCount() const;

  bool GetMethod() const;

  void PrintPendingTaskCounts(Logger* logger) const;

  void Submit(Task task, QueueType queue_type);

  void InitS3Client();
  
  void InitS3Client(const Aws::Client::ClientConfiguration& config);

  std::shared_ptr<Aws::S3::S3Client> GetS3Client() const;

 private:
  AsyncIOScheduler();
  ~AsyncIOScheduler();

  AsyncIOScheduler(const AsyncIOScheduler&) = delete;
  AsyncIOScheduler& operator=(const AsyncIOScheduler&) = delete;

  ThreadPool* pool_ebs_;
  std::vector<ThreadPool*> pool_s3_list_;
  std::atomic<size_t> s3_index_;  // 用于轮询选择队列

  bool started_;
  std::shared_ptr<Aws::S3::S3Client> s3_client_;
  mutable std::atomic<int> ebs_low_load_count_{0};
  mutable std::atomic<bool> method_flag_{false};
};

}  // namespace ROCKSDB_NAMESPACE
