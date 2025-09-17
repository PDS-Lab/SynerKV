#!/bin/bash

# Usage:
# ./run_synerkv.sh <synerkv_path> <benchmark_type> <upload_while_generate> <hyper_level>
# Example:
# ./run_synerkv.sh /tmp/synerkv fillrandom true 0

if [ $# -ne 4 ]; then
  echo "Usage: $0 <synerkv_path> <benchmark_type> <upload_while_generate> <hyper_level>"
  exit 1
fi

SYNERKV_PATH=$1
BENCHMARK_TYPE=$2
UPLOAD_WHILE_GENERATE=$3
HYPER_LEVEL=$4

# Modify it to the bucket name you created
BUCKET_NAME="yourbucketname"

# Change to the region where your server or bucket is located
REGION="ap-northeast-1"

# Modify to the path where your dbbench compilation results are located
DB_BENCH="/home/ubuntu/Rocksdb_cloud/build/db_bench"

OUTPUT_LOG="./output.log"

${DB_BENCH} -cloud_db=true \
  --benchmarks="${BENCHMARK_TYPE},levelstats,stats" \
  -synerkv_path="${SYNERKV_PATH}" \
  -bucket_name=${BUCKET_NAME} \
  -region=${REGION} \
  -target_file_size_base=$((64*1024*1024)) \
  -max_background_jobs=16 \
  -num=100000000 \
  -writes=6250000 \
  -threads=16 \
  -bloom_bits=10 \
  -compression_type=none \
  -upload_while_generate=${UPLOAD_WHILE_GENERATE} \
  -hyper_level=${HYPER_LEVEL} \
  -report_interval_seconds=1 \
  -key_size=16 \
  -value_size=1024 \
  -subcompactions=4 \
  -use_direct_io_for_flush_and_compaction=1 \
  -use_direct_reads=1 \
  -report_bg_io_stats=true \
  -histogram=1 \
  -perf_level=5 \
  -statistics \
  >> "${OUTPUT_LOG}" 2>&1

echo "Benchmark finished. See ${OUTPUT_LOG} for details."
