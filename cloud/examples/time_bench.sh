#!/bin/bash

# 定义 9 组参数，每组参数格式为：<buffer_size_MB> <file_chunk_KB> <s3_part_MB> <num_threads>
params=(
  "512 128 5 4"
  "512 128 5 8"
  "512 128 5 16"
  "512 128 8 4"
  "512 128 8 8"
  "512 128 8 16"
  "512 128 16 4"
  "512 128 16 8"
  "512 128 16 16"
)

output_file="s3_upload_times.txt"
network_file="network_bandwidth.txt"

# 清空输出文件（如果存在）
> "$output_file"
> "$network_file"

global_test_count=1

# 遍历每组参数
for param_set in "${params[@]}"; do
  echo "开始参数组 [$param_set]" >> "$output_file"
  echo "开始参数组 [$param_set]" >> "$network_file"

  # **启动网络监控**（监控整个参数组）
  dstat -n --noheader --nocolor 1 >> "$network_file" &
  monitor_pid=$!  # 记录 dstat 进程 ID，后续用于停止

  # **执行 10 次测试**
  for ((i=1; i<=10; i++)); do
    # 执行 test_time 并捕获输出
    result=$(./test_time $param_set)

    # 使用 sed 提取 S3 上传耗时
    s3_time=$(echo "$result" | sed -n 's/^S3 分段上传耗时: \([0-9]\+\) 毫秒/\1/p')
    
    if [ -z "$s3_time" ]; then
      s3_time="N/A"
    fi

    # 记录测试结果
    echo "Test #$global_test_count | 参数: $param_set | 第 $i 次运行 | S3上传耗时: ${s3_time} 毫秒" >> "$output_file"
    
    ((global_test_count++))
  done

  # **停止 dstat 监控**
  kill $monitor_pid
  wait $monitor_pid 2>/dev/null

  # 记录分隔符
  echo "--------------------------------------" >> "$output_file"
  echo "--------------------------------------" >> "$network_file"

done

echo "所有测试完成，结果保存在 $output_file 和 $network_file 中"

# aws s3 rm s3://generalbuckets-jx --recursive
