#include "src/cpp/faiss_index_service.h"

#include <absl/log/log.h>
#include <absl/strings/str_format.h>
#include <faiss/Index.h>
#include <faiss/MetricType.h>
#include <faiss/impl/IDSelector.h>
#include <faiss/index_factory.h>
#include <google/protobuf/repeated_field.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>

#include <string>
#include <vector>

#include "src/proto/index_service.grpc.pb.h"

using faiss::IDSelectorBatch;
using faiss::idx_t;
using faiss::MetricType;
using google::protobuf::RepeatedField;
using grpc::Server;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

using index_service::DescribeRequest;
using index_service::DescribeResponse;
using index_service::InsertRequest;
using index_service::InsertResponse;
using index_service::Neighbor;
using index_service::SearchRequest;
using index_service::SearchResponse;
using index_service::UpsertRequest;
using index_service::UpsertResponse;
using index_service::Vector;
using index_service::faiss::FaissIndexServiceImpl;

FaissIndexServiceImpl::FaissIndexServiceImpl(int dimensions,
                                             const char* factory_string,
                                             MetricType metric_type)
    : m_dimensions_(dimensions),
      m_factory_string_(factory_string),
      m_metric_type_(metric_type),
      m_index_(::faiss::index_factory(m_dimensions_, m_factory_string_,
                                      m_metric_type_)),
      m_ids_seen_{} {};

Status FaissIndexServiceImpl::Describe(ServerContext* context,
                                       const DescribeRequest* describe_request,
                                       DescribeResponse* describe_response) {
  LOG(INFO) << absl::StrFormat("Received describe request.");

  describe_response->set_dimensions(m_dimensions_);
  describe_response->set_num_vectors(m_index_->ntotal);
  return Status::OK;
}

Status FaissIndexServiceImpl::Insert(ServerContext* context,
                                     const InsertRequest* insert_request,
                                     InsertResponse* insert_response) {
  int num_vectors = insert_request->vectors_size();

  LOG(INFO) << absl::StrFormat("Received insert request. num_vectors=%d",
                               num_vectors);

  // Keep track of the new vectors to insert.
  std::vector<idx_t> ids;
  std::vector<float> vectors;

  for (int i = 0; i < num_vectors; i++) {
    Vector vector_ = insert_request->vectors()[i];
    const idx_t id = vector_.id();
    RepeatedField<float> raw = vector_.raw();

    if (raw.size() != m_dimensions_) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    absl::StrFormat(
                        "Found vector that does not match dimensions of index. "
                        "Vector dimensions: (%d). Index dimensions: (%d).",
                        raw.size(), m_dimensions_));
    }

    if (m_ids_seen_.find(id) == m_ids_seen_.end()) {
      // Only insert the vector into the index if its not already present.
      // TODO: Support upsert for indexes that support removal.
      ids.push_back(id);

      for (int j = 0; j < m_dimensions_; j++) vectors.push_back(raw[j]);

      m_ids_seen_.insert(id);
    }
  }

  m_index_->add_with_ids(ids.size(), vectors.data(), ids.data());

  return Status::OK;
}

Status FaissIndexServiceImpl::Upsert(ServerContext* context,
                                     const UpsertRequest* upsert_request,
                                     UpsertResponse* upsert_response) {
  int num_vectors = upsert_request->vectors_size();

  LOG(INFO) << absl::StrFormat("Received upsert request. num_vectors=%d",
                               num_vectors);

  // Keep track of the new vectors to upsert.
  std::vector<idx_t> ids;

  // This is a flat array storing all vectors contiguously.
  std::vector<float> vectors;

  std::vector<idx_t> ids_to_update;

  for (int i = 0; i < num_vectors; i++) {
    Vector vector_ = upsert_request->vectors()[i];
    const idx_t id = vector_.id();
    RepeatedField<float> raw = vector_.raw();

    if (raw.size() != m_dimensions_) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    absl::StrFormat(
                        "Found vector that does not match dimensions of index. "
                        "Vector dimensions: (%d). Index dimensions: (%d).",
                        raw.size(), m_dimensions_));
    }

    if (m_ids_seen_.find(id) != m_ids_seen_.end()) ids_to_update.push_back(id);

    ids.push_back(id);
    for (int j = 0; j < m_dimensions_; j++) vectors.push_back(raw[j]);
  }

  m_ids_seen_.insert(ids.begin(), ids.end());

  const IDSelectorBatch ids_to_update_selector(ids_to_update.size(),
                                               ids_to_update.data());
  m_index_->remove_ids(ids_to_update_selector);

  m_index_->add_with_ids(ids.size(), vectors.data(), ids.data());

  LOG(INFO) << absl::StrFormat("Updated %d existing vectors.",
                               ids_to_update.size());
  LOG(INFO) << absl::StrFormat("Inserted %d new vectors.",
                               ids.size() - ids_to_update.size());

  return Status::OK;
}

Status FaissIndexServiceImpl::Search(ServerContext* context,
                                     const SearchRequest* search_request,
                                     SearchResponse* search_response) {
  LOG(INFO) << absl::StrFormat("Received search request. k=%d",
                               search_request->k());

  // Allocate arrays for neighbor IDs and scores to populate by search.
  int k = search_request->k();
  auto* neighbor_ids = new idx_t[k];
  auto* neighbor_scores = new float[k];

  // Prepare query vector.
  RepeatedField<float> query_vector = search_request->query_vector();

  // Search for nearest neighbors of query vector.
  m_index_->search(1, query_vector.data(), k, neighbor_scores, neighbor_ids);

  // Build response using `neighbor_scores` and `neighbor_ids` populated by
  // search.
  for (int i = 0; i < k; i++) {
    Neighbor neighbor;
    neighbor.set_id(neighbor_ids[i]);
    neighbor.set_score(neighbor_scores[i]);
    search_response->add_neighbors()->CopyFrom(neighbor);
  }

  LOG(INFO) << absl::StrFormat("Successfully searched.");

  return Status::OK;
}
