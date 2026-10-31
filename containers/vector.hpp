#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include "../memory/allocators.hpp"
#include "../tools/concepts.hpp"

template <typename T>
class Vector {
private:
  Memory_Allocator* m_alloc;

public:
  size_t len = 0;
  size_t cap = 0;
  T* ptr = nullptr;

public:
  explicit Vector(Memory_Allocator& alloc = heap_alloc) noexcept
      : m_alloc{&alloc},
        len{0},
        cap{8},
        ptr{static_cast<T*>(m_alloc->allocate(sizeof(T) * 8, alignof(T)))} {
    if (sizeof(T) * cap > m_alloc->getChunkSize()) {
      panic("Initial Vec capacity does not fit in a pool chunk");
    }
  }

  explicit Vector(size_t cap, Memory_Allocator& alloc = heap_alloc) noexcept
      : m_alloc{&alloc},
        len{0},
        cap{cap},
        ptr{static_cast<T*>(m_alloc->allocate(sizeof(T) * cap, alignof(T)))} {
    if (sizeof(T) * cap > m_alloc->getChunkSize())
      panic("Initial Vec capacity does not fit in a pool chunk");
  }

  explicit Vector(size_t sz, size_t cap, Memory_Allocator& alloc = heap_alloc) noexcept
      : m_alloc{&alloc},
        len{sz},
        cap{cap},
        ptr{static_cast<T*>(m_alloc->allocate(sizeof(T) * cap, alignof(T)))} {
    if (sizeof(T) * cap > m_alloc->getChunkSize())
      panic("Initial Vec capacity does not fit in a pool chunk");

    for (size_t i = 0; i < sz; ++i) new (&ptr[i]) T();
  }

  explicit Vector(std::initializer_list<T> lst, Memory_Allocator& alloc = heap_alloc) noexcept
      : m_alloc{&alloc},
        len{lst.size()},
        cap{lst.size()},
        ptr{static_cast<T*>(m_alloc->allocate(sizeof(T) * lst.size(), alignof(T)))} {
    if (sizeof(T) * lst.size() > m_alloc->getChunkSize())
      panic("Initial Vec capacity does not fit in a pool chunk");

    std::copy(lst.begin(), lst.end(), ptr);
  }

  explicit Vector(std::initializer_list<T> lst, size_t cap,
                  Memory_Allocator& alloc = heap_alloc) noexcept
      : m_alloc{&alloc},
        len{lst.size()},
        cap{std::max(cap, lst.size())},
        ptr{static_cast<T*>(m_alloc->allocate(sizeof(T) * std::max(cap, lst.size()), alignof(T)))} {
    if (sizeof(T) * std::max(cap, lst.size()) > m_alloc->getChunkSize())
      panic("Initial Vec capacity does not fit in a pool chunk");

    std::copy(lst.begin(), lst.end(), ptr);
  }

  ~Vector() noexcept {
    if (m_alloc->canAlwaysFree()) m_alloc->free(ptr);
  }

  Vector(const Vector& vec) noexcept
      : m_alloc{vec.m_alloc},
        len{vec.len},
        cap{vec.cap},
        ptr{static_cast<T*>(m_alloc->allocate(sizeof(T) * vec.cap, alignof(T)))} {
    std::copy(vec.ptr, vec.ptr + vec.len, ptr);
  }

  Vector(Vector&& vec) noexcept
      : m_alloc{vec.m_alloc},
        len{vec.len},
        cap{vec.cap},
        ptr{std::move(vec.ptr)} {
    vec.ptr = nullptr;
    vec.len = 0;
    vec.cap = 0;
  }

  Vector& operator=(const Vector& other) noexcept {
    if (this == &other) return *this;

    if (m_alloc->canAlwaysFree()) m_alloc->free(ptr);

    m_alloc = other.m_alloc;

    ptr = static_cast<T*>(m_alloc->allocate(sizeof(T) * other.cap, alignof(T)));
    if (!ptr) panic("Vec copy assignment: allocation failed");

    len = other.len;
    cap = other.cap;

    std::copy(other.ptr, other.ptr + other.len, ptr);

    return *this;
  }

  Vector& operator=(Vector&& other) noexcept {
    if (this == &other) return *this;

    if (m_alloc->canAlwaysFree()) m_alloc->free(ptr);

    m_alloc = other.m_alloc;
    ptr = other.ptr;
    len = other.len;
    cap = other.cap;

    other.ptr = nullptr;
    other.len = 0;
    other.cap = 0;

    return *this;
  }

  bool operator==(const Vector& other) const {
    for (size_t i = 0; i < len; ++i) {
      if (!(ptr[i] == other.ptr[i])) return false;
    }
    return true;
  }

  Vector& operator+=(const T& scalar)
    requires Concept::Numeric<T>
  {
    for (auto& val : *this) val += scalar;
    return *this;
  }

  Vector& operator-=(const T& scalar)
    requires Concept::Numeric<T>
  {
    for (auto& val : *this) val -= scalar;
    return *this;
  }

  Vector& operator*=(const T& scalar)
    requires Concept::Numeric<T>
  {
    for (auto& val : *this) val *= scalar;
    return *this;
  }

  Vector& operator/=(const T& scalar)
    requires Concept::Numeric<T>
  {
    for (auto& val : *this) val /= scalar;
    return *this;
  }

  T& operator[](size_t i) noexcept {
    return ptr[i];
  }

  const T& operator[](size_t i) const noexcept {
    return ptr[i];
  }

  std::optional<T&> at(size_t i) noexcept {
    if (i >= len) return std::nullopt;
    return ptr[i];
  }

  std::optional<const T&> at(size_t i) const noexcept {
    if (i >= len) return std::nullopt;
    return ptr[i];
  }

  size_t size() const noexcept {
    return len;
  }

  T* begin() const noexcept {
    return ptr;
  }

  T* end() const noexcept {
    return ptr + len;
  }

  std::span<T> slice(size_t start, size_t end) noexcept {
    if (start >= end) return {};
    size_t actual_end = std::min(len, end);
    return std::span<T>(begin() + start, begin() + actual_end);
  }

  std::span<T> toSpan() noexcept {
    return std::span<T>(ptr, len);
  }

  template <typename PrintFn>
  void print(PrintFn printFn) const noexcept {
    std::printf("[");
    for (size_t i = 0; i < len; ++i) {
      if (i > 0) std::printf(", ");
      printFn(ptr[i]);
    }
    std::printf("]\n");
  }

  void printInfo() const noexcept {
    std::printf("length: ");
    std::printf("%zu", len);
    std::printf(", capacity: ");
    std::printf("%zu\n", cap);
  }

  T first() const noexcept {
    return ptr[0];
  }

  T last() const noexcept {
    return ptr[len - 1];
  }

  void clear() noexcept {
    len = 0;
  }

  void fill(const T& val) noexcept {
    std::fill(begin(), end(), val);
  }

  void reverse() noexcept {
    for (size_t i = 0; i < len / 2; ++i) std::swap(ptr[i], ptr[len - 1 - i]);
  }

  void sort() noexcept {
    std::sort(begin(), end());
  }

  void sortDescending() noexcept {
    std::sort(begin(), end(), [](const T& a, const T& b) { return a > b; });
  }

  template <typename Fn>
  void sortCustom(Fn fn) noexcept {
    std::sort(begin(), end(), fn);
  }

  std::optional<int> linearSearch(T x) const noexcept {
    for (size_t i = 0; i < len; i++) {
      if (ptr[i] == x) return i;
    }

    return std::nullopt;
  }

  std::optional<int> binarySearch(T x) const noexcept {
    int high = len - 1;
    int low = 0;

    while (low <= high) {
      int mid = low + ((high - low) / 2);
      if (ptr[mid] == x)
        return mid;
      else if (ptr[mid] > x)
        high = mid - 1;
      else
        low = mid + 1;
    }

    return std::nullopt;
  }

  void extend(size_t new_size, T val = T()) noexcept {
    if (new_size > cap) m_resizeCapacity(new_size);
    for (size_t i = len; i < new_size; ++i) ptr[i] = val;
    len = new_size;
  }

  void reserve(size_t new_cap) noexcept {
    if (new_cap > cap) m_resizeCapacity(new_cap);
  }

  void shrink(size_t new_size) noexcept {
    if (len < cap && len > 0) m_resizeCapacity(new_size);
  }

  void shrinkToFit() noexcept {
    if (len < cap) m_resizeCapacity(len);
  }

  template <typename S>
    requires std::is_same_v<std::remove_cvref_t<S>, T>
  void pushBack(S&& val) noexcept {
    if (len == cap) m_resizeCapacity(cap * 2);
    ptr[len++] = std::forward<S>(val);
  }

  template <typename... Args>
    requires(sizeof...(Args) == 1 && std::is_same_v<T, std::remove_cvref_t<Args>...>)
  void emplaceBack(Args&&... args) noexcept {
    if (len == cap) m_resizeCapacity(cap * 2);
    new (&ptr[len++]) T(std::forward<Args>(args)...);
  }

  template <typename S>
    requires std::is_same_v<std::remove_cvref_t<S>, T>
  void insert(S&& val, size_t ind) noexcept {
    if (ind > len) ind = len;

    if (len == cap) m_resizeCapacity(cap * 2);

    for (size_t i = len++; i > ind; --i) ptr[i] = std::move(ptr[i - 1]);

    ptr[ind] = std::forward<S>(val);
  }

  void pop() noexcept {
    if (len > 0) --len;
  }

  std::optional<T> popBack() noexcept {
    if (len <= 0) return std::nullopt;
    return std::move(ptr[--len]);
  }

  T popBackUnsafe() noexcept {
    return std::move(ptr[--len]);
  }

  void orderedRemove(size_t ind) noexcept {
    if (ind >= len) return;
    for (size_t i = ind; i + 1 < len; ++i) ptr[i] = std::move(ptr[i + 1]);
    --len;
  }

  void unorderedRemove(size_t ind) noexcept {
    if (ind >= len) return;
    ptr[ind] = std::move(ptr[--len]);
  }

  void deleteVal(T val) noexcept {
    if (len == 0) return;
    for (int i = len - 1; i >= 0; --i)
      if (ptr[i] == val) orderedRemove(i);
  }

  void deleteValUnordered(T val) noexcept {
    if (len == 0) return;
    for (int i = len - 1; i >= 0; --i)
      if (ptr[i] == val) unorderedRemove(i);
  }

  template <typename Fn>
  Vector& mapIter(Fn fn) noexcept {
    for (size_t i = 0; i < len; ++i) fn(ptr[i]);
    return *this;
  }

  template <typename Rtype>
    requires std::is_arithmetic_v<Rtype> && std::is_arithmetic_v<T>
  std::optional<Rtype> sum() const noexcept {
    if (len == 0) return std::nullopt;
    if (len == 1) return static_cast<Rtype>(ptr[0]);
    Rtype ttl = 0;
    for (size_t i = 0; i < len; ++i) ttl += static_cast<Rtype>(ptr[i]);
    return ttl;
  }

  template <typename Rtype>
    requires std::is_arithmetic_v<Rtype> && std::is_arithmetic_v<T>
  std::optional<Rtype> prod() const noexcept {
    if (len == 0) return std::nullopt;
    if (len == 1) return static_cast<Rtype>(ptr[0]);
    Rtype ttl = 1;
    for (size_t i = 0; i < len; ++i) ttl *= static_cast<Rtype>(ptr[i]);
    return ttl;
  }

  template <typename Rtype, typename Fn>
  std::optional<Rtype> foldl(size_t init, Fn fn) const noexcept {
    if (len == 0) return std::nullopt;
    Rtype ttl = init;
    for (size_t i = 0; i < len; ++i) fn(ttl, static_cast<Rtype>(ptr[i]));
    return ttl;
  }

  template <typename Rtype, typename Fn>
  std::optional<Rtype> reduce(Fn fn) const noexcept {
    if (len <= 0) return std::nullopt;
    if (len == 1) return static_cast<Rtype>(ptr[0]);
    Rtype ttl = static_cast<Rtype>(ptr[0]);
    for (size_t i = 1; i < len; ++i) fn(ttl, static_cast<Rtype>(ptr[i]));
    return ttl;
  }

private:
  void m_resizeCapacity(size_t new_cap) noexcept {
    size_t old_bytes = cap * sizeof(T);
    size_t new_bytes = new_cap * sizeof(T);

    if (m_alloc->canResize()) {
      T* new_vec = static_cast<T*>(m_alloc->resize(ptr, old_bytes, new_bytes, alignof(T)));
      if (!new_vec) panic("Allocator resize failed");

      ptr = new_vec;
      cap = new_cap;
    } else {
      T* new_vec = static_cast<T*>(m_alloc->allocate(sizeof(T) * new_cap, alignof(T)));
      if (!new_vec) panic("Allocator resize failed");

      for (size_t i = 0; i < len; ++i) new (&new_vec[i]) T(std::move(ptr[i]));

      for (size_t i = 0; i < len; ++i) ptr[i].~T();

      m_alloc->free(ptr);
      ptr = new_vec;
      cap = new_cap;
    }
  }
};
