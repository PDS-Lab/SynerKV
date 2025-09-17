# SynerKV: A Cost-Efficient Cloud LSM Store by Exploiting Parallelism of Block and Object Hybrid Storage

## Overview

This repository contains a prototype of SynerKV that written in C++ based on RocksDB-Cloud. 

## Major Branches Overview

| Branch Name | Description |
|-|-|
| `simplebucket` | Adding Hybrid Storage to RocksDB-Cloud. |
| `only_memory_upload` | Upload memory data sequentially to S3 in multiple segments. |
| `io_scheduler` | Add an I/O scheduler to the only_memory_upload branch for asynchronously uploading segmented data. |
| `SynerKV` | Add bandwidth-aware data layouts to the io_scheduler branch. |

---
Note: The code contains four main branches, but the anonymous repository only displays the SynerKV branch.


## Main Class Overview

This section lists all the **important classes** that were **added** in the project, along with a brief description of their purpose.  

| Class Name | Class Path |Description |
|-|-|-|
| `AsyncIOScheduler` | `file\AsyncIOScheduler.h` |Responsible for receiving and scheduling various I/O tasks, including disk write tasks and S3 transfer tasks. |
| `FileWriteTracker` | `file\writable_file_writer.h` |Responsible for tracking the execution process of tasks and providing feedback to the process that assigned them. |

---

## Main Functions Overview

This section lists all the **important functions** that were **added** in the project, along with a brief description of their purpose.  

| Function Name           | File Path                | Type      | Description                                      |
|-------------------------|-------------------------|-----------|------------------------------------------------|
| `Submit()` |  `file\AsyncIOScheduler.cc` | Added  | Receive tasks and task categories, and schedule tasks for execution in the corresponding queue. |
| `GetMethod()` | `file\AsyncIOScheduler.cc`  | Added     | Monitor disk queue load to guide data layout. |
| `AddTask()`     | `file\file_write_tracker.cc` | Added  | Add a task to the tracker. |
| `Wait()`   | `file\file_write_tracker.cc`  | Added  | Lock the process submitting tasks, waiting until all tasks have been submitted and completed. |
| `FinishAddingTasks()`   | `file\file_write_tracker.cc`  | Added  | Mark all required tasks as submitted. |
| `TaskFinished()`      | `file\file_write_tracker.cc`     | Added     | After the current task completes, determine whether to wake up the waiting process based on the number of tasks in progress and the flag indicating all tasks have been submitted. |
| `partUpload()`      | `table\block_based\block_based_table_builder.cc`     | Added     | Submit the transmission task to the I/O scheduler by treating the accumulated data as a segment of the specified object. |
| `completeupload()`      | `table\block_based\block_based_table_builder.cc`     | Added     | Wait until all segments are uploaded, then sort and merge them into a complete file. |

---

## Quick Start

Follow these steps to build the project and run a benchmark.

```bash
mkdir build
cd build
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=On -DWITH_AWS=On -DUSE_RTTI=On -DCMAKE_BUILD_TYPE=Release
make -j8
cd ..
bash run_synerkv.sh /tmp/synerkv fillrandom true 0
```
