#include <iostream>
#include <typeinfo>
#include <vector>

#include <google/protobuf/repeated_field.h>

#include "src/proto/index_service.grpc.pb.h"

using google::protobuf::RepeatedPtrField;

using index_service::Vector;

int main() {
  float raw[3] = {1, 2, 3};

  Vector vector1;
  vector1.set_id(1);
  auto d = vector1.mutable_raw()->mutable_data();
  std::cout << typeid(d).name() << std::endl;

  RepeatedPtrField<Vector> foo;

  std::cout << "hi again" << std::endl;
  return 0;
}

