#!/bin/bash

# Nine sets: <buffer_MB> <chunk_KB> <S3_part_MB> <threads>.
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

# Truncates existing output files.
> "$output_file"
> "$network_file"

global_test_count=1

# Runs each parameter set.
for param_set in "${params[@]}"; do
  echo "开始参数组 [$param_set]" >> "$output_file"
  echo "开始参数组 [$param_set]" >> "$network_file"

  # Monitors network use for this parameter set.
  dstat -n --noheader --nocolor 1 >> "$network_file" &
  monitor_pid=$!  # Saves the dstat PID for cleanup.

  # Runs ten trials.
  for ((i=1; i<=10; i++)); do
    # Runs test_time and captures its output.
    result=$(./test_time $param_set)

    # Extracts the S3 upload time.
    s3_time=$(echo "$result" | sed -n 's/^S3 分段上传耗时: \([0-9]\+\) 毫秒/\1/p')
    
    if [ -z "$s3_time" ]; then
      s3_time="N/A"
    fi

    # Records the result.
    echo "Test #$global_test_count | 参数: $param_set | 第 $i 次运行 | S3上传耗时: ${s3_time} 毫秒" >> "$output_file"
    
    ((global_test_count++))
  done

  # Stops dstat.
  kill $monitor_pid
  wait $monitor_pid 2>/dev/null

  # Writes output separators.
  echo "--------------------------------------" >> "$output_file"
  echo "--------------------------------------" >> "$network_file"

done

echo "所有测试完成，结果保存在 $output_file 和 $network_file 中"

# aws s3 rm s3://generalbuckets-jx --recursive
