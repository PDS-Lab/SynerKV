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

// Schedules EBS writes and S3 uploads on separate worker pools.
class AsyncIOScheduler {
 public:
  using Task = std::function<void()>;

  static AsyncIOScheduler& Instance();

  // Creates one EBS pool and the requested S3 pools.
  void Start(int num_threads_ebs, int num_threads_s3, int num_s3_queues = 1);
  // Drains and destroys all worker pools.
  void Stop();

  enum class QueueType {
    EBS,
    S3
  };

  int GetEbsPendingTaskCount() const;

  // Returns the total queued tasks across all S3 pools.
  int GetS3PendingTaskCount() const;

  // Returns true after two consecutive EBS queue samples below 10.
  // Requires Start().
  bool GetMethod() const;

  void PrintPendingTaskCounts(Logger* logger) const;

  // Queues an EBS task or dispatches an S3 task round-robin.
  void Submit(Task task, QueueType queue_type);

  void InitS3Client();
  
  void InitS3Client(const Aws::Client::ClientConfiguration& config);

  std::shared_ptr<Aws::S3::S3Client> GetS3Client() const;

 private:
  AsyncIOScheduler();
  ~AsyncIOScheduler();

  AsyncIOScheduler(const AsyncIOScheduler&) = delete;
  AsyncIOScheduler& operator=(const AsyncIOScheduler&) = delete;

  // Owned worker pools.
  ThreadPool* pool_ebs_;
  std::vector<ThreadPool*> pool_s3_list_;
  std::atomic<size_t> s3_index_;  // Round-robin S3 queue counter.

  bool started_;
  std::shared_ptr<Aws::S3::S3Client> s3_client_;
  // Tracks sustained low EBS load.
  mutable std::atomic<int> ebs_low_load_count_{0};
  mutable std::atomic<bool> method_flag_{false};
};

}  // namespace ROCKSDB_NAMESPACE
