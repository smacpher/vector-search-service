#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "grpc/grpc.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "src/cpp/sharded_index_service.h"

using grpc::Server;
using grpc::ServerBuilder;
using index_service::sharded::ShardedIndexServiceImpl;

int main(int argc, char* argv[]) {
  const int num_required_args = 3;
  if (argc < num_required_args) {
    std::cout << "Expected at least 3 arguments: <port> <dimensions> "
                 "<shard_capacity>."
              << std::endl;
    return 1;
  }
  std::vector<std::string> shard_addresses;
  for (int i = num_required_args + 1; i < argc; i++) {
    shard_addresses.push_back(argv[i]);
  }

  int port = std::stoi(argv[1]);
  int dimensions = std::stoi(argv[2]);
  int shard_capacity = std::stoi(argv[3]);

  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

  std::vector<std::shared_ptr<grpc::Channel>> shard_service_channels;
  for (std::string shard_address : shard_addresses) {
    shard_service_channels.push_back(
        grpc::CreateChannel(shard_address, grpc::InsecureChannelCredentials()));
  }

  ShardedIndexServiceImpl service(dimensions, shard_service_channels,
                                  shard_capacity);

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());

  LOG(INFO) << absl::StrFormat(
      "Index service with %d dimensions listening on %s ...", dimensions,
      server_address);

  server->Wait();

  return 0;
}
