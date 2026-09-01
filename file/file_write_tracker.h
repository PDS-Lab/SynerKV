#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <cassert>
#include "rocksdb/status.h"

// Tracks completion of asynchronous file writes.
class FileWriteTracker {
 public:
  FileWriteTracker();

  // Requests a drain after the footer is flushed.
  bool footer_;
  // Registers one pending write.
  void AddTask();
  // Records a result and completes one pending write.
  void TaskFinished(const rocksdb::Status& status);
  // Seals the tracker against new tasks.
  void FinishAddingTasks();
  // Waits until the sealed tracker has no pending tasks.
  void Wait();
  bool IsDone() const;
  bool HasError() const;
  rocksdb::Status GetStatus() const;

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  // Protected by mu_.
  int pending_tasks_;
  bool done_;
  bool finished_adding_;
  rocksdb::Status status_;  // Most recently completed task status.
};
