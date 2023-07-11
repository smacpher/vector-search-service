#include "src/cpp/sharded_index_service.h"

#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "grpc/grpc.h"
#include "grpcpp/server.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status_code_enum.h"
#include "src/cpp/algo.h"
#include "src/proto/index_service.grpc.pb.h"

using google::protobuf::RepeatedField;
using google::protobuf::RepeatedPtrField;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

using index_service::DescribeRequest;
using index_service::DescribeResponse;
using index_service::IndexService;
using index_service::InsertRequest;
using index_service::InsertResponse;
using index_service::Neighbor;
using index_service::SearchRequest;
using index_service::SearchResponse;
using index_service::Vector;
using index_service::sharded::ShardedIndexServiceImpl;

ShardedIndexServiceImpl::ShardedIndexServiceImpl(
    int dimensions,
    std::vector<std::shared_ptr<Channel>> shard_service_channels,
    int shard_capacity)
    : m_dimensions_(dimensions), m_shard_capacity_(shard_capacity),
      m_shard_sizes_(shard_service_channels.size()) {
  // Initial service stubs for each shard.
  // The order in which channels are given is the order in which shards will
  // be filled with inserted vectors.
  for (auto channel : shard_service_channels) {
    m_shard_service_stubs_.push_back(IndexService::NewStub(channel));
  }

  LOG(INFO) << absl::StrFormat("Registered %d shard stubs.",
                               m_shard_service_stubs_.size());
};

Status ShardedIndexServiceImpl::Describe(
    ServerContext *context,
    const index_service::DescribeRequest *describe_request,
    index_service::DescribeResponse *describe_response) {
  LOG(INFO) << absl::StrFormat("Received describe request.");

  int total_num_vectors = 0;
  for (int shard_idx = 0; shard_idx < m_shard_service_stubs_.size();
       shard_idx++) {
    ClientContext context;
    DescribeRequest describe_request;
    DescribeResponse describe_response;

    auto &stub = m_shard_service_stubs_[shard_idx];

    LOG(INFO) << absl::StrFormat("Describing shard %d...", shard_idx);
    Status status =
        stub->Describe(&context, describe_request, &describe_response);

    if (!status.ok())
      return Status(StatusCode::UNAVAILABLE,
                    absl::StrFormat("Shard %d is unhealthy.", shard_idx));

    if (status.ok())
      total_num_vectors += describe_response.num_vectors();
    LOG(INFO) << absl::StrFormat(
        "Successfully described shard %d. dimensions=%d. num_vectors=%d",
        shard_idx, describe_response.dimensions(),
        describe_response.num_vectors());
  }

  describe_response->set_dimensions(m_dimensions_);
  describe_response->set_num_vectors(total_num_vectors);

  return Status::OK;
}

Status ShardedIndexServiceImpl::Insert(
    grpc::ServerContext *context,
    const index_service::InsertRequest *insert_request,
    index_service::InsertResponse *insert_response) {
  // Acquire the insertion lock.
  // Note: `lock_guard` will automatically release lock when this guard
  // instance goes out of scope (i.e. the function completes).
  const std::lock_guard<std::mutex> _(m_insertion_mutex_);

  LOG(INFO) << absl::StrFormat("Received insert request. num_vectors=%d",
                               insert_request->vectors().size());

  // Greedily assign vectors to shard, filling the first shard, then the
  // second, and so on.
  const std::pair<int, std::map<int, int>> greedy_fill_result =
      algo::greedy_fill(insert_request->vectors().size(), m_shard_capacity_,
                        m_shard_sizes_);

  int num_unallocated_vectors = greedy_fill_result.first;
  std::map<int, int> shard_fills = greedy_fill_result.second;

  if (num_unallocated_vectors) {
    LOG(INFO) << absl::StrFormat(
        "Insufficient capacity to insert all new vectors across shards. "
        "num_unassigned_vectors=%d",
        num_unallocated_vectors);
    return Status(StatusCode::RESOURCE_EXHAUSTED, "Insufficient capacity.");
  }

  // Insert the allocated batch of vectors to each shard.
  int offset = 0;
  int vector_idx = 0;
  for (std::pair<int, int> shard_fill : shard_fills) {
    int shard_idx = shard_fill.first;
    int num_to_fill = shard_fill.second;

    // Get the stub of the shard to insert batch to.
    auto &shard_stub = m_shard_service_stubs_.at(shard_idx);

    // Initialize shard insert request objects.
    ClientContext shard_client_context;
    InsertRequest shard_insert_request;
    InsertResponse shard_insert_response;

    // Copy allocated portion of vectors into shard insert request.
    int num_inserted = 0;
    for (; vector_idx < offset + num_to_fill; vector_idx++) {
      Vector vector = insert_request->vectors()[vector_idx];

      if (m_vector_shard_assignments_.find(vector.id()) !=
          m_vector_shard_assignments_.end()) {
        LOG(INFO) << absl::StrFormat(
            "Vector with id=%d already exists. Ignoring.", vector.id());
        continue;
      }

      shard_insert_request.add_vectors()->CopyFrom(vector);
      num_inserted++;
    }
    offset = vector_idx;

    LOG(INFO) << absl::StrFormat("Inserting %d vectors into shard %d...",
                                 shard_insert_request.vectors().size(),
                                 shard_idx);

    Status shard_status = shard_stub->Insert(
        &shard_client_context, shard_insert_request, &shard_insert_response);

    if (!shard_status.ok()) {
      LOG(INFO) << absl::StrFormat(
          "Shard returned non-ok response. error_code=%v, error_message=%s",
          shard_status.error_code(), shard_status.error_message());

      return Status(StatusCode::UNAVAILABLE,
                    "One or more shards are not healthy.");
    }

    if (num_inserted) {
      m_shard_sizes_[shard_idx] += num_inserted;

      for (const Vector &vector : shard_insert_request.vectors())
        m_vector_shard_assignments_.insert({vector.id(), shard_idx});

      LOG(INFO) << absl::StrFormat(
          "Successfully inserted vectors into shard %d. "
          "Shard is now at %.2f %% "
          "capacity. num_inserted=%d",
          shard_idx, 100 * m_shard_sizes_[shard_idx] / (float)m_shard_capacity_,
          num_inserted);
    }
  }

  return Status::OK;
}

Status ShardedIndexServiceImpl::Upsert(
    grpc::ServerContext *context,
    const index_service::UpsertRequest *upsert_request,
    index_service::UpsertResponse *upsert_response) {
  int num_vectors = upsert_request->vectors_size();
  LOG(INFO) << absl::StrFormat("Received upsert request. num_vectors=%d",
                               num_vectors);

  std::vector<int> new_shard_sizes(m_shard_sizes_);
  std::unordered_map<int, UpsertRequest> shard_upsert_requests;
  std::unordered_map<int, int> shards_num_updated;
  std::unordered_map<int, int> shards_num_inserted;

  std::vector<Vector> new_vectors;
  for (const Vector &vector : upsert_request->vectors()) {
    auto it = m_vector_shard_assignments_.find(vector.id());
    if (it != m_vector_shard_assignments_.end()) {
      // Vector already exists in a shard.
      const int shard_idx = it->second;

      shard_upsert_requests[shard_idx].add_vectors()->CopyFrom(vector);
      shards_num_updated[shard_idx]++;
    } else {
      // Vector needs to be assigned to a shard.
      new_vectors.push_back(vector);
    }
  }

  LOG(INFO) << absl::StrFormat("Identified %d new vectors to insert.",
                               new_vectors.size());

  const std::pair<int, std::map<int, int>> greedy_fill_result =
      algo::greedy_fill(new_vectors.size(), m_shard_capacity_, m_shard_sizes_);

  int num_unassigned_vectors = greedy_fill_result.first;
  std::map<int, int> shard_fills = greedy_fill_result.second;

  if (num_unassigned_vectors) {
    LOG(INFO) << absl::StrFormat(
        "Insufficient capacity to insert all new vectors across shards. "
        "num_unassigned_vectors=%d",
        num_unassigned_vectors);
    return Status(StatusCode::RESOURCE_EXHAUSTED, "Insufficient capacity.");
  }

  // Assign new vectors greedily to each shard and insert them alongwith any
  // vectors to update.
  int offset = 0;
  int vector_idx = 0;
  for (std::pair<int, int> shard_fill : shard_fills) {
    int shard_idx = shard_fill.first;
    int num_to_fill = shard_fill.second;

    LOG(INFO) << absl::StrFormat("Assigned %d new vectors to shard %d.",
                                 num_to_fill, shard_idx);

    // Note: Retrieve the shard upsert request by reference so we mutate the
    // value in the map.
    UpsertRequest &shard_upsert_request = shard_upsert_requests[shard_idx];

    // Copy allocated portion of new vectors into shard insert request.
    for (; vector_idx < offset + num_to_fill; vector_idx++)
      shard_upsert_request.add_vectors()->CopyFrom(new_vectors[vector_idx]);

    shards_num_inserted[shard_idx] = num_to_fill;
    offset = vector_idx;
  }

  for (const auto it : shard_upsert_requests) {
    const int shard_idx = it.first;
    UpsertRequest shard_upsert_request = it.second;

    LOG(INFO) << absl::StrFormat("Upserting %d vectors into shard %d...",
                                 shard_upsert_request.vectors().size(),
                                 shard_idx);

    ClientContext shard_client_context;
    UpsertResponse shard_upsert_response;

    // Get the stub of the shard to insert batch to.
    auto &shard_stub = m_shard_service_stubs_.at(shard_idx);

    Status shard_status = shard_stub->Upsert(
        &shard_client_context, shard_upsert_request, &shard_upsert_response);

    // Record that each vector was successfully upserted to the current shard.
    // For vectors that already exist, effectively this is a no-op.
    for (const Vector &vector : shard_upsert_request.vectors())
      m_vector_shard_assignments_.insert({vector.id(), shard_idx});

    if (!shard_status.ok()) {
      LOG(INFO) << absl::StrFormat(
          "Shard returned non-ok response. error_code=%v, error_message=%s",
          shard_status.error_code(), shard_status.error_message());

      return Status(StatusCode::UNAVAILABLE,
                    "One or more shards are not healthy.");
    }

    // Update the size of the current shard to reflect the new vectors we
    // inserted.
    m_shard_sizes_[shard_idx] += shards_num_inserted[shard_idx];

    LOG(INFO) << absl::StrFormat(
        "Successfully upserted vectors into shard %d. Shard is at %.2f %% "
        "capacity. num_inserted=%d. num_updated=%d",
        shard_idx, 100 * m_shard_sizes_[shard_idx] / (float)m_shard_capacity_,
        shards_num_inserted[shard_idx], shards_num_updated[shard_idx]);
  }

  return Status::OK;
}

Status ShardedIndexServiceImpl::Search(
    grpc::ServerContext *context,
    const index_service::SearchRequest *search_request,
    index_service::SearchResponse *search_response) {
  LOG(INFO) << absl::StrFormat("Received search request. k=%d",
                               search_request->k());

  std::vector<int> search_shard_idx = get_search_shard_idx();

  if (!search_shard_idx.size()) {
    LOG(INFO) << absl::StrFormat(
        "All shards are empty. Returning empty neighbors.");

    // Empty index.
    for (int i = 0; i < search_request->k(); i++) {
      Neighbor *neighbor = search_response->add_neighbors();
      neighbor->set_id(-1);
      neighbor->set_score(-std::numeric_limits<float>::max());
    }

    return Status::OK;
  }

  LOG(INFO) << absl::StrFormat(
      "Searching %d non-empty shards out of %d total shards.",
      search_shard_idx.size(), m_shard_service_stubs_.size());

  // Maintain the top-k best candidates seen so far across all shards.
  Neighbor best_candidates[search_request->k()];

  // Populate best candidates with results from the first shard. Then merge
  // each subsequent result, only keeping the top-k.
  ClientContext first_shard_client_context;
  SearchRequest first_shard_search_request;
  SearchResponse first_shard_search_response;

  first_shard_search_request.CopyFrom(*search_request);

  auto &first_shard_stub = m_shard_service_stubs_.at(0);

  LOG(INFO) << absl::StrFormat("Searching shard 0. shard_size=%d",
                               m_shard_sizes_[0]);

  first_shard_stub->Search(&first_shard_client_context,
                           first_shard_search_request,
                           &first_shard_search_response);

  LOG(INFO) << absl::StrFormat("Successfully searched shard 0.");

  // Neighbors from each shard are already sorted. Add neighbors from the first
  // shard in reverse order so smallest scored is first in the min-heap array.
  const RepeatedPtrField<Neighbor> first_shard_neighbors =
      first_shard_search_response.neighbors();
  for (int i = first_shard_neighbors.size() - 1; i >= 0; i--)
    best_candidates[first_shard_neighbors.size() - i - 1] =
        first_shard_neighbors[i];

  // Define a comparator function used to build the k-sized min-heap of best
  // neighbors seen.
  auto is_score_greater = [](const Neighbor &first_neighbor,
                             const Neighbor &second_neighbor) {
    return first_neighbor.score() > second_neighbor.score();
  };

  // Add each neighbor returned from subsequent shards to the min-heap if it
  // has a higher score than the min value of the heap.
  int shard_idx;
  for (int i = 1; i < search_shard_idx.size(); i++) {
    shard_idx = search_shard_idx[i];

    LOG(INFO) << absl::StrFormat("Searching shard %d... .shard_size=%d",
                                 shard_idx, m_shard_sizes_[shard_idx]);

    ClientContext shard_client_context;
    SearchRequest shard_search_request;
    SearchResponse shard_search_response;

    shard_search_request.CopyFrom(*search_request);

    auto &shard_stub = m_shard_service_stubs_.at(shard_idx);

    Status shard_status = shard_stub->Search(
        &shard_client_context, shard_search_request, &shard_search_response);

    if (!shard_status.ok()) {
      LOG(INFO) << absl::StrFormat(
          "Shard returned non-ok response. error_code=%v, error_message=%s",
          shard_status.error_code(), shard_status.error_message());

      return Status(StatusCode::UNAVAILABLE,
                    "One or more shards are not healthy.");
    }

    LOG(INFO) << absl::StrFormat("Successfully searched shard %d.", shard_idx);

    for (const Neighbor &neighbor : shard_search_response.neighbors()) {
      const Neighbor &current_worst_candidate = best_candidates[0];

      if (is_score_greater(neighbor, current_worst_candidate)) {
        // Replace the smallest (i.e. worst) candidate so far if this one is
        // better.
        auto smallest = algo::heap_replace(best_candidates, search_request->k(),
                                           neighbor, is_score_greater);
      }
    }
  }

  // Reverse sort the min-heap so larger values are first.
  // For now, assume that greater scores are better. This is compatible with
  // dot product indexes only.
  std::sort(best_candidates, best_candidates + search_request->k(),
            is_score_greater);
  for (int i = 0; i < search_request->k(); i++) {
    search_response->add_neighbors()->CopyFrom(best_candidates[i]);
  }

  return Status::OK;
}
