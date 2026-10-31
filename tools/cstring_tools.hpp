#pragma once

#include "../containers/pair.hpp"
#include "../memory/allocators.hpp"

constexpr Pair<const char*, const char*> splitOnce(const char* str, const char* delim,
                                                   Memory_Allocator& alloc = heap_alloc) {
  const char* d = std::strstr(str, delim);
  if (!d) return {str, nullptr};

  size_t left_len = d - str;
  size_t delim_len = std::strlen(delim);
  const char* right = d + delim_len;

  char* left = static_cast<char*>(alloc.allocate(left_len + 1, alignof(char*)));
  char* right_copy = static_cast<char*>(alloc.allocate(std::strlen(right) + 1, alignof(char*)));

  std::memcpy(left, str, left_len);
  left[left_len] = '\0';

  std::strcpy(right_copy, right);

  return {left, right_copy};
}
