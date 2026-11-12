#pragma once

#include <cstddef>
#include <cstdio>

template <typename T, typename S>
struct Pair {
  T x;
  S y;

  bool operator==(const Pair& other) const {
    return x == other.x && y == other.y;
  }

  template <typename PrintFn>
  constexpr void print(PrintFn printFn) const noexcept {
    std::printf("[");
    printFn(x);
    printf(", ");
    printFn(y);
    printf("]\n");
  }
};
