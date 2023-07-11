.DEFAULT_GOAL := help
.PHONY: help
help:
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

format:  ## Auto-formats all project files.
	@find src/cpp -iname '*.h' -o -iname '*.cc' | xargs clang-format --verbose -i && echo 'Successfully formatted all c++ files 👾!'

# the path used by cmake for `find_package` / `find_library` / etc.
# locally installed libraries can be installed and found here
export CMAKE_BUILD_DIR_ ?= cmake/build
export CMAKE_PREFIX_PATH ?= ~/.local
.PHONY: configure
configure:  ## Configures build directory and commands using `cmake` and `CMakeLists.txt`.
	@cmake -DCMAKE_PREFIX_PATH=$(CMAKE_PREFIX_PATH) -B $(CMAKE_BUILD_DIR_) .

.PHONY: build
build:  ## Builds all binaries.
	@make -C $(CMAKE_BUILD_DIR_)

.PHONY: generate_py_stubs
generate_py_stubs:  ## Generates Python protobuf and gRPC stubs.
	@protoc --python_out=. --grpc_python_out=. ./src/proto/index_service.proto --plugin=protoc-gen-grpc_python=$(shell which grpc_python_plugin)

it:  ## Runs all integration tests.
	@python ./src/python/test_index_service.py

test:  ## Runs all unittests
	@make -C $(CMAKE_BUILD_DIR_) test CTEST_OUTPUT_ON_FAILURE=1

.PHONY: run_sharded
run_sharded:  ## Starts a sharded index with three shards.
	@bash ./scripts/run_sharded.sh

.PHONY: run_sharded
run_single:  ## Starts a single-process index.
	@bash ./scripts/run_single.sh

.PHONY: build_playground
_build_playground:
	@make -C $(CMAKE_BUILD_DIR_) playground

.PHONY: playground
playground: _build_playground  ## Builds and runs `playground.cpp`.
	@./$(CMAKE_BUILD_DIR_)/playground

