# Vector Search Service

A simple, performant vector search service written in C++ and using gRPC.

todo:
- [] Implement smoothsort or heapsort for sorting the nearly sorted heap array of multi-shard candidates.
- [] Parallelize multi-shard inserts and searches.
- [] Unittest FaissIndexServiceImpl and ShardedIndexServiceImpl
- [] Add better CLI flag support in main entrypoints
- [] Persistence (See ## Persistence)
- [] Better abstractions (See ## Abstractions)

# Design

First, we'll start with a single-node vector search service. [x]

Next, we'll add a multi-node vector search service that maps requests out to
multiple single-node services and reduces them (i.e. sorts union of results,
takes top-k). This will be started up with a static set of single-node search
services. It will fill up each single-node search service to capacity. [x]

Next, we'll add the ability to filter on simple metadata fields, starting with a flat map of
strings -> strings, and maybe eventually getting to full JSON search.

Next, we'll implement a way to dynamically provision new
single-node services to increase a multi-node service's capacity as needed.

Finally, we'll implement a write-ahead-log to ensure we don't lose vectors on
insert and to make it possible to replicate an index.

## Persistence

Currently, we will lose all data in a shard if the shard crashes, since we
serve data directly from memory. I would like to support some form of
persistence or replication to guard against this scenario.

Here are some alternatives:
- Persist indexes to disk (mmap files?) on every write to allow shards to
restart without data loss. I'm not sure mmap supports large files or exactly
how it works. Need to do more due diligence.
- Support failover shard replicas.
- Support a "snapshot" RPC which saves all data to disk.
- Insert vectors to a disk-backed WAL or queue (e.g. kafka, or my own).
We can "replay" vectors from last healthy timestamp to restore shards. With
upsert, this should get us back to the desired state eventually.

Resources:
- https://martinfowler.com/articles/patterns-of-distributed-systems/wal.html

## Abstractions

There only needs to be one index service impl.

The single-node index service calls directly to an index impl to
perform index ops.

There are two types of index impls:
* single-node (i.e. runs on this node)
* multi-node (i.e. calls out to other index services)

An in-memory index should support the following:
* upsert
* search
* describe
* (future) save (to disk)

We can then have different in-memory index impls:
* `faiss`
* `scann`
* a custom SIMD one I implement for fun

# Gotchas

## Missing symbol called

`dyld[...]: missing symbol called`: first thing to check is if you've included
all the required header files and are working in the right namespace.

I was seeing this when attempting to bind
a gRPC service on 50051 but it turned out I hadn't included my header file lol.

Adding `linkopts = ["-undefined error"]` to `cc_library` or `cc_binary` or
`cc_test` can sometimes help diagnose the error at build time. See
https://groups.google.com/g/bazel-discuss/c/YGVWGnhFEXc for reference.

## Undefined symbols for architecture

If the symbols look like stdlib things, make sure you are running `clang++`
not `clang`!

# Building manually

I was running into some cryptic build errors while using Bazel so I've decided
to try to build manually, first starting with Apple Clang, then trying LLVM
Clang, and finally GCC.

I'll also need to use the `protoc` tool to generate message and service stub
classes / structs from proto files.

I need to build static / dynamic libraries for the following 3P libraries:
- `grpc`
- `gtest`
- `abseil`
- `faiss`
- `omp` (? or maybe this comes pre-shipped with compiler toolchains)

## Steps

TODO: automate installing these dependencies.

### Install LLVM `clang`

References:
- https://github.com/facebookresearch/faiss/blob/main/INSTALL.md

Apple Clang wasn't working for me. Install LLVM `clang` with:

```shell
brew install llvm
```

```shell
# prepend LLVM binaries to PATH so they are used before Apple Clang
export PATH=/usr/local/opt/llvm/bin:$PATH
export LDFLAGS="-L/usr/local/opt/llvm/lib"
export CPPFLAGS="-I/usr/local/opt/llvm/include"
export CC=/usr/local/opt/llvm/bin/clang
export CXX=/usr/local/opt/llvm/bin/clang++
```

### Install `grpc`

Install `grpc`  with c++ support, following the instructions at
https://grpc.io/docs/languages/cpp/quickstart/. Apple Clang wasn't working
so I installed llvm by following https://embeddedartistry.com/blog/2017/02/24/installing-llvm-clang-on-osx/. I then added the following to my ~/.zshrc to default to using
LLVM Clang:

```shell
export PATH=/usr/local/opt/llvm/bin:$PATH
export CC=/usr/local/opt/llvm/bin/clang
export CXX=/usr/local/opt/llvm/bin/clang++
```

Install `protoc` protobuf compiler (following https://grpc.io/docs/protoc-installation/):

```shell
$ brew install protobuf
$ protoc --version
```

### Install `faiss`

Reference: https://github.com/facebookresearch/faiss/blob/main/INSTALL.md.

Clone the repo.

```shell
$ git clone git@github.com:facebookresearch/faiss.git
$ cd faiss
```

Configure using `cmake`.

```shell
export MY_INSTALL_DIR=~/.local
cmake \
    -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
    -DCMAKE_BUILD_TYPE=Release \
    -DFAISS_ENABLE_C_API=OFF \
    -DFAISS_OPT_LEVEL=generic \
    -DFAISS_ENABLE_GPU=OFF \
    -DFAISS_ENABLE_PYTHON=OFF \
    -DBUILD_TESTING=OFF \
    -B build \
    .
```

Build using `Makefile` generated by `cmake`.

```shell
make -C build -j faiss
```

This creates the `libfaiss.a` static library (or `libfaiss.so` if
`-DBUILD_SHARED_LIBS=on`, located under `build/faiss`.

Install into ~/.local:

```shell
make -C build install
```

### Building 1P code

Configure make file with cmake.

```shell
cmake -DCMAKE_PREFIX_PATH=$MY_INSTALL_DIR -B cmake/build .
```

Note: For some reason, -DCMAKE_PREFIX_PATH is needed to pull in google::protobuf
properly. I did install grpc to that prefix path so maybe that's why... 
It's strange because cmake can still find grpc and protobuf without specifying
that option though.

Build all targets.

```shell
make -C cmake/build
```

#### Open issues

Ran into a weird issue preventing me from using `absl::ParseCommandLine`:

```shell
Undefined symbols for architecture x86_64:
  "absl::lts_20230125::ParseCommandLine(int, char**)", referenced from:
      _main in absl_flags_hello.cc.o
ld: symbol(s) not found for architecture x86_64
```

Maybe this is related: https://github.com/google/or-tools/issues/2196.
I tried rebuilding grpc with c++17:

```shell
cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
      -DCMAKE_CXX_STANDARD=17 ../..
```

but I didn't specify `-DABSL_PROPAGATE_CXX_STD=TRUE` so maybe absl wasn't
build with c++17.

