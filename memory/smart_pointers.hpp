#pragma once

#include <atomic>

template <typename T>
class UniquePtr {
public:
  T* ptr;

public:
  UniquePtr()
      : ptr{new T()} {}

  explicit UniquePtr(T* raw)
      : ptr{raw} {}

  explicit UniquePtr(T val)
      : ptr{new T(val)} {}

  ~UniquePtr() {
    delete (ptr);
  }

  UniquePtr(const UniquePtr&) = delete;

  UniquePtr& operator=(const UniquePtr&) = delete;

  UniquePtr(UniquePtr&& other) noexcept
      : ptr{other.ptr} {
    other.ptr = nullptr;
  }

  UniquePtr& operator=(UniquePtr&& other) noexcept {
    if (this != &other) {
      delete ptr;
      ptr = other.ptr;
      other.ptr = nullptr;
    }
    return *this;
  }

  T& operator*() const noexcept {
    return *ptr;
  }

  T* operator->() const noexcept {
    return ptr;
  }
};

struct ControlBlock {
  std::atomic<uint32_t> strong;
  std::atomic<uint32_t> weak;
  void (*deleter)(void*);
  void* object;
};

template <typename T>
class WeakPtr;

template <typename T>
class SharedPtr {
  friend class WeakPtr<T>;

public:
  T* ptr;

private:
  ControlBlock* m_cb;

public:
  SharedPtr()
      : ptr{nullptr},
        m_cb{nullptr} {}

  explicit SharedPtr(T* raw)
      : ptr{raw} {
    m_cb = new ControlBlock{1, 1, [](void* p) { delete static_cast<T*>(p); }, raw};
  }

  ~SharedPtr() {
    m_release();
  }

  template <typename... Args>
  explicit SharedPtr(Args&&... args)
      : ptr{new T(std::forward<Args>(args)...)} {
    m_cb = new ControlBlock{1, 1, [](void* p) { delete static_cast<T*>(p); }, ptr};
  }

  SharedPtr(const SharedPtr& other) noexcept
      : ptr{other.ptr},
        m_cb{other.m_cb} {
    if (m_cb) m_cb->strong.fetch_add(1);
  }

  SharedPtr& operator=(const SharedPtr& other) noexcept {
    if (this != &other) {
      m_release();
      ptr = other.ptr;
      m_cb = other.m_cb;
      if (m_cb) m_cb->strong.fetch_add(1);
    }
    return *this;
  }

  SharedPtr(SharedPtr&& other) noexcept
      : ptr{other.ptr},
        m_cb{other.m_cb} {
    other.ptr = nullptr;
    other.m_cb = nullptr;
  }

  SharedPtr& operator=(SharedPtr&& other) noexcept {
    if (this != &other) {
      m_release();
      ptr = other.ptr;
      m_cb = other.m_cb;
      other.ptr = nullptr;
      other.m_cb = nullptr;
    }
    return *this;
  }

  uint32_t getWeakCount() const {
    return m_cb ? m_cb->weak.load() : 0;
  }

  uint32_t getStrongCount() const {
    return m_cb ? m_cb->strong.load() : 0;
  }

  T& operator*() const {
    assert(ptr != nullptr);
    return *ptr;
  }

  T* operator->() const noexcept {
    return ptr;
  }

private:
  void m_release() {
    if (!m_cb) return;

    if (m_cb->strong.fetch_sub(1) == 1) {
      m_cb->deleter(m_cb->object);

      if (m_cb->weak.fetch_sub(1) == 1) {
        delete m_cb;
      }
    }
  }
};

template <typename T>
class WeakPtr {
private:
  ControlBlock* m_cb;

public:
  WeakPtr()
      : m_cb{nullptr} {}

  explicit WeakPtr(const SharedPtr<T>& sp)
      : m_cb{sp.m_cb} {
    if (m_cb) m_cb->weak.fetch_add(1);
  }

  explicit WeakPtr(const WeakPtr& other)
      : m_cb{other.m_cb} {
    if (m_cb) m_cb->weak.fetch_add(1);
  }

  ~WeakPtr() {
    if (m_cb && m_cb->weak.fetch_sub(1) == 1) {
      if (m_cb->strong.load() == 0) {
        delete m_cb;
      }
    }
  }

  SharedPtr<T> lock() const {
    if (!m_cb) return SharedPtr<T>();

    long count = m_cb->strong.load();
    if (count == 0) return SharedPtr<T>();  // object is gone

    // try to increment strong
    if (m_cb->strong.fetch_add(1) == 0) {
      // object died between load() and fetch_add()
      m_cb->strong.fetch_sub(1);
      return SharedPtr<T>();
    }

    // success: create a SharedPtr that shares ownership
    SharedPtr<T> sp;
    sp.m_cb = m_cb;
    sp.ptr = static_cast<T*>(m_cb->object);
    return sp;
  }

  bool expired() const {
    return !m_cb || m_cb->strong.load() == 0;
  }

  uint32_t getWeakCount() {
    return m_cb->weak;
  }

  uint32_t getStrongCount() {
    return m_cb->strong;
  }
};

class RefCounted {
private:
  std::atomic<uint32_t> m_refcount;

public:
  void add_ref() noexcept {
    m_refcount.fetch_add(1, std::memory_order_relaxed);
  }

  void release_ref() noexcept {
    if (m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

protected:
  RefCounted() noexcept
      : m_refcount{0} {}
  virtual ~RefCounted() = default;
};

template <typename T>
class IntrusivePtr {
private:
  T* m_ptr;

public:
  IntrusivePtr() noexcept
      : m_ptr{nullptr} {}

  explicit IntrusivePtr(T* p, bool add_ref = true)
      : m_ptr{p} {
    if (m_ptr && add_ref) {
      m_ptr->add_ref();
    }
  }

  IntrusivePtr(const IntrusivePtr& other) noexcept
      : m_ptr{other.m_ptr} {
    if (m_ptr) m_ptr->add_ref();
  }

  IntrusivePtr(IntrusivePtr&& other) noexcept
      : m_ptr{other.m_ptr} {
    other.m_ptr = nullptr;
  }

  ~IntrusivePtr() {
    if (m_ptr) m_ptr->release_ref();
  }

  IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
    if (this != &other) {
      if (other.m_ptr) other.m_ptr->add_ref();
      if (m_ptr) m_ptr->release_ref();
      m_ptr = other.m_ptr;
    }
    return *this;
  }

  IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
    if (this != &other) {
      if (m_ptr) m_ptr->release_ref();
      m_ptr = other.m_ptr;
      other.m_ptr = nullptr;
    }
    return *this;
  }

  T* get() const noexcept {
    return m_ptr;
  }

  T& operator*() const noexcept {
    return *m_ptr;
  }

  T* operator->() const noexcept {
    return m_ptr;
  }

  explicit operator bool() const noexcept {
    return m_ptr != nullptr;
  }
};
