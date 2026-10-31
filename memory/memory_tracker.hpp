#pragma once

#include <cstdio>
#include <cstdlib>

struct MemoryTracker {
  size_t ttl = 0;
  size_t freed = 0;

  template <typename T>
  T* alloc(T val) noexcept {
    ttl += sizeof(T);
    return new T(val);
  }

  template <typename T>
  void dealloc(T* memory) noexcept {
    freed += sizeof(T);
    delete memory;
  }

  void* malloc(size_t size) noexcept {
    ttl += size;
    return std::malloc(size);
  }

  template <typename T>
  void free(T* memory) noexcept {
    freed += sizeof(T);
    std::free(memory);
  }

  void memReport() const noexcept {
    std::printf("Memory Usage:\n");
    std::printf("  Allocated: %zu bytes\n", ttl);
    std::printf("  Freed:     %zu bytes\n", freed);
  };
};
