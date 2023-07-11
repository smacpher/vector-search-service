#!/bin/bash

CMAKE_BUILD_DIR_=cmake/build

${CMAKE_BUILD_DIR_}/faiss_index_service 50051 1

EXIT_STATUS=$?

