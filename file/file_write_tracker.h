#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <cassert>
#include "rocksdb/status.h"

class FileWriteTracker {
 public:
  FileWriteTracker();

  bool footer_;
  void AddTask();
  void TaskFinished(const rocksdb::Status& status);
  void FinishAddingTasks();
  void Wait();
  bool IsDone() const;
  bool HasError() const;
  rocksdb::Status GetStatus() const;

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  int pending_tasks_;
  bool done_;
  bool finished_adding_;
  rocksdb::Status status_;
};
