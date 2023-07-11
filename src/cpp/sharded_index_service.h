#pragma once

#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "src/proto/index_service.grpc.pb.h"
#include "src/proto/index_service.pb.h"

namespace index_service::sharded {

class ShardedIndexServiceImpl final
    : public index_service::IndexService::Service {
public:
  explicit ShardedIndexServiceImpl(
      int dimensions,
      std::vector<std::shared_ptr<grpc::Channel>> shard_service_channels,
      int shard_capacity = 1);

  // TODO: Consider consolidating this with `FaissIndexServiceImpl`.
  grpc::Status Describe(grpc::ServerContext *context,
                        const index_service::DescribeRequest *describe_request,
                        index_service::DescribeResponse *describe_response);

  grpc::Status Insert(grpc::ServerContext *context,
                      const index_service::InsertRequest *insert_request,
                      index_service::InsertResponse *insert_response);

  grpc::Status Upsert(grpc::ServerContext *context,
                      const index_service::UpsertRequest *upsert_request,
                      index_service::UpsertResponse *upsert_response);

  grpc::Status Search(grpc::ServerContext *context,
                      const index_service::SearchRequest *search_request,
                      index_service::SearchResponse *search_response);

private:
  // Returns the shards to use in searches, i.e. shards that have a
  // non-zero number of vectors in them.
  inline std::vector<int> get_search_shard_idx() {
    std::vector<int> non_zero_idx;
    for (int i = 0; i < m_shard_sizes_.size(); i++) {
      if (m_shard_sizes_[i] != 0)
        non_zero_idx.push_back(i);
    }

    return non_zero_idx;
  }

  // The dimensions of vectors in this index. Must match the dimensions of
  // each shard service.
  int m_dimensions_;

  // The capacity of an individual shard.
  // For simplicity, this is static across all shards.
  int m_shard_capacity_;

  // The service stubs for each shard in this index.
  std::vector<std::unique_ptr<index_service::IndexService::Stub>>
      m_shard_service_stubs_;

  // An array mapping shard service indexes to their current sizes.
  // At insert time, this is used to determine which shard to insert to
  // next. We fill up a single shard to capacity before moving on to the
  // next.
  // At query time, this is used to determine which shards to query. We skip
  // over empty shards.
  std::vector<int> m_shard_sizes_;

  // Write lock used to serialize insertions.
  // This is acquired in every insert request to ensure each insert + the
  // bookkeeping to update shard sizes is done atomically.
  std::mutex m_insertion_mutex_;

  // Mapping from vector IDs to the shard ID that stores them. Used to
  // ignore existing vectors at insert time and route upserts to the correct
  // shard.
  std::unordered_map<int, int> m_vector_shard_assignments_;
};

} // namespace index_service::sharded
