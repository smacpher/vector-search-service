#include "src/cpp/algo.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <utility>
#include <vector>

using algo::greedy_fill;
using algo::heap_replace;

using testing::ElementsAre;

TEST(HeapTest, HeapReplaceDefaultCompare) {
  // Test that the default comparator enforces min-heap property.
  const int size = 3;
  float a[size] = {1, 2, 3};

  // 1 is replaced by 1. Heap doesn't change.
  heap_replace<float>(a, 3, 1.);
  ASSERT_THAT(a, ElementsAre(1, 2, 3));

  // 1 is replaced by 4. 1 is sifted down past 3.
  heap_replace<float>(a, 3, 4.);
  ASSERT_THAT(a, ElementsAre(2, 4, 3));

  // 2 is replaced by 3.
  heap_replace<float>(a, 3, 3.);
  ASSERT_THAT(a, ElementsAre(3, 4, 3));
}

TEST(HeapTest, HeapReplaceMaxHeapCompare) {
  // Test that we can use `std::less` to enforce max-heap property.
  const int size = 3;
  float a[size] = {3, 2, 1};

  // 3 is replaced by 2. Heap doesn't change.
  heap_replace<float>(a, 3, 3., std::less());
  ASSERT_THAT(a, ElementsAre(3, 2, 1));

  // 3 is replaced by 1. 1 is sifted down past 2.
  heap_replace<float>(a, 3, 1., std::less());
  ASSERT_THAT(a, ElementsAre(2, 1, 1));
}

TEST(GreedyFillTest, NoElements) {
  const std::vector<int> bucket_sizes = {0};
  int bucket_capacity = 1;
  int num_elements = 0;
  int expected_num_elements_leftover = 0;
  const std::map<int, int> expected_bucket_fills = {};

  EXPECT_EQ(
      greedy_fill(num_elements, bucket_capacity, bucket_sizes),
      std::make_pair(expected_num_elements_leftover, expected_bucket_fills));
}

TEST(GreedyFillTest, SomeElements_0) {
  const std::vector<int> bucket_sizes = {0};
  int bucket_capacity = 1;
  int num_elements = 1;
  int expected_num_elements_leftover = 0;
  const std::map<int, int> expected_bucket_fills = {{0, 1}};

  EXPECT_EQ(
      greedy_fill(num_elements, bucket_capacity, bucket_sizes),
      std::make_pair(expected_num_elements_leftover, expected_bucket_fills));
}

TEST(GreedyFillTest, SomeElements_1) {
  const std::vector<int> bucket_sizes = {0};
  int bucket_capacity = 3;
  int num_elements = 10;
  int expected_num_elements_leftover = 7;
  const std::map<int, int> expected_bucket_fills = {{0, 3}};

  EXPECT_EQ(
      greedy_fill(num_elements, bucket_capacity, bucket_sizes),
      std::make_pair(expected_num_elements_leftover, expected_bucket_fills));
}

TEST(GreedyFillTest, SomeElements_2) {
  const std::vector<int> bucket_sizes = {4, 1, 5};
  int bucket_capacity = 8;
  int num_elements = 20;
  int expected_num_elements_leftover = 6;
  const std::map<int, int> expected_bucket_fills = {{0, 4}, {1, 7}, {2, 3}};

  EXPECT_EQ(
      greedy_fill(num_elements, bucket_capacity, bucket_sizes),
      std::make_pair(expected_num_elements_leftover, expected_bucket_fills));
}

TEST(GreedyFillTest, SomeElements_3) {
  const std::vector<int> bucket_sizes = {8, 1, 5};
  int bucket_capacity = 8;
  int num_elements = 20;
  int expected_num_elements_leftover = 10;
  const std::map<int, int> expected_bucket_fills = {{1, 7}, {2, 3}};

  EXPECT_EQ(
      greedy_fill(num_elements, bucket_capacity, bucket_sizes),
      std::make_pair(expected_num_elements_leftover, expected_bucket_fills));
}
