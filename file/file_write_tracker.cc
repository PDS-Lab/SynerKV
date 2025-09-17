#include "file/file_write_tracker.h"
#include <iostream>

FileWriteTracker::FileWriteTracker()
    : footer_(false),
      pending_tasks_(0),
      done_(false),
      finished_adding_(false),
      status_() {}

void FileWriteTracker::AddTask() {
  std::lock_guard<std::mutex> lock(mu_);
  assert(!finished_adding_ && "Cannot add task after FinishAddingTasks()");
  ++pending_tasks_;
}

void FileWriteTracker::TaskFinished(const rocksdb::Status& status) {
  std::lock_guard<std::mutex> lock(mu_);
  assert(pending_tasks_ > 0);
  --pending_tasks_;
  status_ = status;

  if (pending_tasks_ == 0 && finished_adding_) {
    done_ = true;
    cv_.notify_all();
  }
}

void FileWriteTracker::FinishAddingTasks() {
  std::lock_guard<std::mutex> lock(mu_);
  finished_adding_ = true;
  if (pending_tasks_ == 0) {
    done_ = true;
    cv_.notify_all();
  }
}

void FileWriteTracker::Wait() {
  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait(lock, [&] { return done_; });
}

bool FileWriteTracker::IsDone() const {
  std::lock_guard<std::mutex> lock(mu_);
  return done_;
}

bool FileWriteTracker::HasError() const {
  std::lock_guard<std::mutex> lock(mu_);
  return !status_.ok();
}

rocksdb::Status FileWriteTracker::GetStatus() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}
