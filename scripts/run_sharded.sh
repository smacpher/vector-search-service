#!/bin/bash

CMAKE_BUILD_DIR_=cmake/build

# this traps will only be executed when each process completes, so we need
# to `wait` for them below
trap 'kill -TERM $PID_0; kill -TERM $PID_1; kill -TERM $PID_2' TERM INT

${CMAKE_BUILD_DIR_}/sharded_index_service 50051 1 2 localhost:50052 localhost:50053 &
PID_0=$!

${CMAKE_BUILD_DIR_}/faiss_index_service 50052 1 &
PID_1=$!

${CMAKE_BUILD_DIR_}/faiss_index_service 50053 1 &
PID_2=$!

wait
trap - TERM INT
wait

EXIT_STATUS=$?

