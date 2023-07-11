#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "src/cpp/faiss_index_service.h"

using absl::ParseCommandLine;
using grpc::InsecureServerCredentials;
using grpc::Server;
using grpc::ServerBuilder;
using index_service::faiss::FaissIndexServiceImpl;

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cout << "Expected 2 arguments." << std::endl;
    return 1;
  }

  int port = std::stoi(argv[1]);
  int dimensions = std::stoi(argv[2]);

  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);

  FaissIndexServiceImpl service(dimensions);

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
