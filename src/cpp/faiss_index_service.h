#pragma once

#include <faiss/Index.h>
#include <faiss/MetricType.h>
#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>

#include <memory>
#include <string>
#include <unordered_set>

#include "src/proto/index_service.grpc.pb.h"

namespace index_service::faiss {

// Note: `public` inheritance makes `public` members of the base class
// `public` in the derived class, `protected` members of the base class become
// `protected` (i.e. accessible to inherited classes) in the derived class.
// `private` members are inaccessible to the derived class.
class FaissIndexServiceImpl final
    : public index_service::IndexService::Service {
 public:
  // Note: The `explicit` function specific disallows implicit type conversions.
  // For example, `FaissIndexServiceImple service = 1;` is disallowed.
  explicit FaissIndexServiceImpl(int dimensions,
                                 const char* factory_string = "IDMap,Flat",
                                 ::faiss::MetricType metric_type =
                                     ::faiss::MetricType::METRIC_INNER_PRODUCT);

  grpc::Status Describe(grpc::ServerContext* context,
                        const index_service::DescribeRequest* describe_request,
                        index_service::DescribeResponse* describe_response);

  grpc::Status Insert(grpc::ServerContext* context,
                      const index_service::InsertRequest* insert_request,
                      index_service::InsertResponse* insert_response);

  grpc::Status Upsert(grpc::ServerContext* context,
                      const index_service::UpsertRequest* upsert_request,
                      index_service::UpsertResponse* upsert_response);

  grpc::Status Search(grpc::ServerContext* context,
                      const index_service::SearchRequest* search_request,
                      index_service::SearchResponse* search_response);

 private:
  // Note: The google style guide calls for class data members variables
  // to be suffixed with trailing underscores. The `m_` prefix comes
  // from https://en.wikipedia.org/wiki/Hungarian_notation.

  // The dimensionality of vectors in this index.
  int m_dimensions_;

  // The `faiss` factory string used to construct this index.
  const char* m_factory_string_;

  // The `faiss` similarity metric type that this index supports.
  ::faiss::MetricType m_metric_type_;

  // The actual `faiss` index storing the vectors.
  std::unique_ptr<::faiss::Index> m_index_;

  // The identifiers we've seen so far.
  std::unordered_set<int> m_ids_seen_;
};

}  // namespace index_service::faiss
