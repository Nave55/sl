#pragma once

#include "../containers/str.hpp"
#include "../containers/string.hpp"

// prints mostly for use in container prints
inline void printDigit(int v) noexcept {
  printf("%d", v);
}

inline void printSizet(size_t v) noexcept {
  printf("%zu", v);
}

inline void printFloat(float v) noexcept {
  printf("%f", v);
}

inline void printCharStar(const char* s) noexcept {
  printf("%s", s);
}

inline void printChar(char c) noexcept {
  printf("%c", c);
}

inline void printString(String s) noexcept {
  s.print();
}

// overloads for generic printing
template <size_t N>
inline void printStr(Str<N> s) noexcept {
  s.print();
}

inline void printVal(int v) noexcept {
  printf("%d", v);
}

inline void printVal(size_t v) noexcept {
  printf("%zu", v);
}

inline void printVal(float v) noexcept {
  printf("%f", v);
}

inline void printVal(const char* s) noexcept {
  printf("%s", s);
}

inline void printVal(char c) noexcept {
  printf("%c", c);
}

inline void printVal(String s) noexcept {
  s.print();
}

template <size_t N>
inline void printVal(Str<N> s) noexcept {
  s.print();
}
