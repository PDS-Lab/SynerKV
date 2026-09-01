#include "file/AsyncIOScheduler.h"
#include <iostream>

namespace ROCKSDB_NAMESPACE {

AsyncIOScheduler& AsyncIOScheduler::Instance() {
  static AsyncIOScheduler instance;
  return instance;
}

AsyncIOScheduler::AsyncIOScheduler()
    : pool_ebs_(nullptr), s3_index_(0), started_(false) {}

AsyncIOScheduler::~AsyncIOScheduler() {
  //Stop();
}

void AsyncIOScheduler::Start(int num_threads_ebs, int num_threads_s3, int num_s3_queues) {
  if (started_) return;
  std::cout << "Setting EBS queue with " << num_threads_ebs << " threads " << std::endl; 
  pool_ebs_ = NewThreadPool(num_threads_ebs);

  std::cout << "Setting " << num_s3_queues << " S3 queues with " << num_threads_s3 << " threads " << std::endl; 
  for (int i = 0; i < num_s3_queues; ++i) {
    pool_s3_list_.push_back(NewThreadPool(num_threads_s3));
  }

  started_ = true;
}

void AsyncIOScheduler::Stop() {
  if (pool_ebs_) {
    pool_ebs_->WaitForJobsAndJoinAllThreads();
    delete pool_ebs_;
    pool_ebs_ = nullptr;
  }

  for (auto* pool : pool_s3_list_) {
    if (pool) {
      pool->WaitForJobsAndJoinAllThreads();
      delete pool;
    }
  }
  pool_s3_list_.clear();

  started_ = false;
}

int AsyncIOScheduler::GetEbsPendingTaskCount() const {
  if (pool_ebs_) {
    return pool_ebs_->GetQueueLen();
  }
  return 0;
}

int AsyncIOScheduler::GetS3PendingTaskCount() const {
  int total = 0;
  for (auto* pool : pool_s3_list_) {
    if (pool) {
      total += pool->GetQueueLen();
    }
  }
  return total;
}

void AsyncIOScheduler::PrintPendingTaskCounts(Logger* logger) const {
  int ebs_pending = 0;
  int s3_pending = 0;

  if (pool_ebs_) {
    ebs_pending = pool_ebs_->GetQueueLen();
  }

  for (auto* pool : pool_s3_list_) {
    if (pool) {
      s3_pending += pool->GetQueueLen();
    }
  }

  if (logger) {
    ROCKS_LOG_INFO(logger, "[AsyncIOScheduler] Pending Tasks - EBS: %d, S3: %d",
                   ebs_pending, s3_pending);
  }
}

bool AsyncIOScheduler::GetMethod() const {
  int pending = pool_ebs_->GetQueueLen();
  if (pending < 10) {
    // Require two samples to ignore transient queue dips.
    ebs_low_load_count_++;
    if (ebs_low_load_count_ >= 2) {
      method_flag_ = true;
    }
  } else {
    method_flag_ = false;
    ebs_low_load_count_ = 0;
  }

  if (method_flag_) {
    return true;
  }
  return false;
}

void AsyncIOScheduler::Submit(Task task, QueueType queue_type) {
  if (!started_) {
    std::cerr << "AsyncIOScheduler not started yet!" << std::endl;
    return;
  }

  switch (queue_type) {
    case QueueType::EBS:
      if (pool_ebs_) {
        pool_ebs_->SubmitJob(std::move(task));
      }
      break;

    case QueueType::S3:
      if (!pool_s3_list_.empty()) {
        // Spread S3 tasks across pools in round-robin order.
        size_t index = s3_index_.fetch_add(1, std::memory_order_relaxed) % pool_s3_list_.size();
        pool_s3_list_[index]->SubmitJob(std::move(task));
      }
      break;
  }
}

void AsyncIOScheduler::InitS3Client() {
  s3_client_ = std::make_shared<Aws::S3::S3Client>();
}

void AsyncIOScheduler::InitS3Client(const Aws::Client::ClientConfiguration& config) {
  s3_client_ = std::make_shared<Aws::S3::S3Client>(config);
}

std::shared_ptr<Aws::S3::S3Client> AsyncIOScheduler::GetS3Client() const {
  return s3_client_;
}

}  // namespace ROCKSDB_NAMESPACE
