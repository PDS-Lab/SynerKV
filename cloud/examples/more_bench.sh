#!/bin/bash

# Format: <buffer_MB> <chunk_KB> <S3_part_MB> <threads> <buffers>.
params=(
  "512 128 5 8 5"
  "512 128 5 8 10"
  "512 128 5 16 5"
  "512 128 5 16 10"
  "512 128 8 8 5"
  "512 128 8 8 10"
  "512 128 8 16 5"
  "512 128 8 16 10"
  "512 128 16 8 5"
  "512 128 16 8 10"
  "512 128 16 16 5"
  "512 128 16 16 10"
)

# Output files.
output_file="s3_upload_times.txt"
network_file="network_bandwidth.txt"

# Truncates previous results.
> "$output_file"
> "$network_file"

global_test_count=1

# Runs each parameter set.
for param_set in "${params[@]}"; do
  echo "=== 参数组 [$param_set] 开始 ===" >> "$output_file"
  echo "=== 参数组 [$param_set] 网络监控 ===" >> "$network_file"

  # Samples network bandwidth once per second.
  dstat -n --noheader --nocolor 1 >> "$network_file" &  
  monitor_pid=$!  # Saves the dstat PID.

  # Runs ten trials.
  for ((i=1; i<=10; i++)); do
    # Runs the benchmark and captures its output.
    result=$(./more $param_set)

    # Extracts timing data.
    file_write_time=$(echo "$result" | grep "本地文件写入耗时" | sed -n 's/本地文件写入耗时: \([0-9]\+\) 毫秒/\1/p')
    s3_upload_time=$(echo "$result" | grep "总上传耗时" | sed -n 's/总上传耗时: \([0-9]\+\) 毫秒/\1/p')

    # Handles missing measurements.
    if [ -z "$file_write_time" ]; then
      file_write_time="N/A"
    fi
    if [ -z "$s3_upload_time" ]; then
      s3_upload_time="N/A"
    fi

    # Records the result.
    echo "Test #$global_test_count | 参数: $param_set | 第 $i 次 | 本地写入: ${file_write_time}ms | S3上传: ${s3_upload_time}ms" >> "$output_file"

    ((global_test_count++))
  done

  # Stops the network monitor for this parameter set.
  kill $monitor_pid
  wait $monitor_pid 2>/dev/null

  echo "=== 参数组 [$param_set] 结束 ===" >> "$output_file"
  echo "--------------------------------------" >> "$network_file"
done

echo "所有测试完成，结果已保存到 $output_file 和 $network_file"

#aws s3 rm s3://generalbuckets-jx --recursive
