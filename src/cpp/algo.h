/* This is a header-only library implementing various algorithms used
 * thoughout the project.
 *
 * Future work:
 * - support passing custom comparator to heap functions
 */
#include <functional>
#include <map>
#include <utility>

namespace {

// Note: An unnamed ("anonymous") namespace makes this function only
// available for use in this file.
template <typename T> void swap(T *a, int i, int j) {
  T temp = a[i];
  a[i] = a[j];
  a[j] = temp;
}

} // namespace

namespace algo {

template <typename T, typename Compare>
T heap_replace(T *a, int size, T v, Compare compare) {
  if (!size)
    return v;

  // Replace root item with new item.
  T popped = a[0];
  a[0] = v;

  // Restore min-heap property.
  int idx = 0;
  int left_child_idx, right_child_idx, min_child_idx;
  while (idx < size) {
    left_child_idx = idx * 2 + 1;
    right_child_idx = idx * 2 + 2;

    if (left_child_idx >= size && right_child_idx >= size)
      // both children are out of bounds
      break;
    else if (right_child_idx >= size ||
             compare(a[right_child_idx], a[left_child_idx]))
      // right child is out of bounds or left child is smaller
      min_child_idx = left_child_idx;
    else
      // both children are in bounds and right child is smaller
      min_child_idx = right_child_idx;

    if (compare(a[idx], a[min_child_idx])) {
      swap<T>(a, idx, min_child_idx);
      idx = min_child_idx;
    } else {
      // min-heap property is restored
      break;
    }
  }

  return popped;
}

template <typename T> T heap_replace(T *a, int size, T v) {
  return heap_replace(a, size, v, std::greater<T>());
}

std::pair<int, std::map<int, int>>
greedy_fill(int num_elements, int bucket_capacity,
            const std::vector<int> &bucket_sizes) {
  if (!num_elements) {
    return std::make_pair(0, std::map<int, int>{});
  }

  // Number of elements to fill in each bucket.
  std::map<int, int> bucket_fills;

  // Fill up the each bucket with as many items as we can before moving
  // on to the next.
  int num_elements_leftover = num_elements;
  for (int i = 0; i < bucket_sizes.size(); i++) {
    int available_capacity = bucket_capacity - bucket_sizes[i];

    if (available_capacity) {
      int num_to_fill = std::min(num_elements_leftover, available_capacity);
      bucket_fills[i] = num_to_fill;

      // Subtract the elements just assigned to bucket.
      num_elements_leftover -= num_to_fill;

      if (!num_elements_leftover) {
        // Break early if we've already assigned all items.
        break;
      }
    }
  }

  return std::make_pair(num_elements_leftover, bucket_fills);
}

} // namespace algo
