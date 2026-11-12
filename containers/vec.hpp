#pragma once

#include <cstdio>

template <typename T>
struct Vec2 {
  T x;
  T y;

  bool operator==(const Vec2& other) const {
    return x == other.x && y == other.y;
  }

  constexpr Vec2 operator+(const Vec2& vec2) const noexcept {
    return {x + vec2.x, y + vec2.y};
  }

  constexpr Vec2 operator-(const Vec2& vec2) const noexcept {
    return {x - vec2.x, y - vec2.y};
  }

  constexpr Vec2 operator*(const Vec2& vec2) const noexcept {
    return {x * vec2.x, y * vec2.y};
  }

  constexpr Vec2 operator/(const Vec2& vec2) const noexcept {
    return {x / vec2.x, y / vec2.y};
  }

  template <typename PrintFn>
  constexpr void print(PrintFn printFn) const noexcept {
    std::printf("Vec2 {\n  x: ");
    printFn(x);
    std::printf("\n  y: ");
    printFn(y);
    std::printf("\n}\n");
  }
};

template <typename T>
struct Vec3 {
  T x;
  T y;
  T z;
  
  bool operator==(const Vec3& other) const {
    return x == other.x && y == other.y && z == other.z;
  }

  constexpr Vec3 operator+(const Vec3& vec3) const noexcept {
    return {x + vec3.x, y + vec3.y, z + vec3.z};
  }

  constexpr Vec3 operator-(const Vec3& vec3) const noexcept {
    return {x - vec3.x, y - vec3.y, z - vec3.z};
  }

  constexpr Vec3 operator*(const Vec3& vec3) const noexcept {
    return {x * vec3.x, y * vec3.y, z * vec3.z};
  }

  constexpr Vec3 operator/(const Vec3& vec3) const noexcept {
    return {x / vec3.x, y / vec3.y, z * vec3.z};
  }

  template <typename PrintFn>
  constexpr void print(PrintFn printFn) const noexcept {
    std::printf("Vec3 {\n  x: ");
    printFn(x);
    std::printf("\n  y: ");
    printFn(y);
    std::printf("\n  z: ");
    printFn(z);
    std::printf("\n}\n");
  }
};
