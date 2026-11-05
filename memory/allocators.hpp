#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

constexpr size_t DEFAULT_ALIGNMENT = 2 * sizeof(void*);
constexpr size_t BYTE = 1;
constexpr size_t KB = 1024;
constexpr size_t MB = KB * 1024ZU;
constexpr size_t GB = MB * 1024ZU;

#define MIN(a, b) (((a) < (b) ? (a) : (b)))

[[noreturn]] static inline void panic(const char* msg) {
  std::fprintf(stderr, "PANIC: %s\n", msg);
  abort();
}

constexpr bool isPowerOfTwo(const size_t x) {
  return x != 0 && (x & (x - 1)) == 0;
}

// constexpr size_t alignForwardSize(const size_t size, const size_t alignment) {
//   if (!isPowerOfTwo(alignment)) panic("Alignment not power of two");
//   return (size + (alignment - 1)) & ~(alignment - 1);
// }

// constexpr uintptr_t alignForwardUintptr(const uintptr_t p, const uintptr_t alignment) {
//   uintptr_t mask = alignment - 1;
//   return (p + mask) & ~mask;
// }

inline void* alignForward(void* p, size_t alignment) noexcept {
  if (!isPowerOfTwo(alignment)) panic("Alignment not power of two");
  uintptr_t addr = reinterpret_cast<uintptr_t>(p);
  uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
  return reinterpret_cast<void*>(aligned);
}

enum class AllocType {
  Heap,
  Tracking,
  Arena,
  Stack,
  Pool,
};

// *******************************************************
//                  Allocator Interface
// *******************************************************

class Memory_Allocator {
public:
  virtual ~Memory_Allocator() = default;

  virtual void* allocate(size_t bytes, size_t alignment) noexcept = 0;

  virtual void* resize(void* old_ptr, size_t old_size, size_t new_size,
                       size_t alignment) noexcept = 0;

  virtual void free(void* ptr) noexcept = 0;
  virtual bool canResize() const noexcept = 0;
  virtual bool canAlwaysFree() const noexcept = 0;
  virtual size_t getChunkSize() const noexcept = 0;
  virtual AllocType getType() const noexcept = 0;
};

// *******************************************************
//                      Heap Allocator
// *******************************************************

struct Heap_Allocator final : public Memory_Allocator {
  Heap_Allocator() = default;
  ~Heap_Allocator() = default;
  Heap_Allocator(const Heap_Allocator&) = delete;
  Heap_Allocator(Heap_Allocator&&) = delete;
  Heap_Allocator& operator=(const Heap_Allocator&) = delete;
  Heap_Allocator& operator=(Heap_Allocator&&) = delete;

  void* allocate(size_t bytes, size_t = 0) noexcept override {
    return std::malloc(bytes);
  }

  template <typename T>
  T* alloc(const T& val) noexcept {
    T* ptr = static_cast<T*>(std::malloc(sizeof(T)));
    *ptr = val;
    return ptr;
  }

  void* resize(void* old_ptr, size_t, size_t new_size, size_t) noexcept override {
    return std::realloc(old_ptr, new_size);
  }

  void free(void* ptr) noexcept override {
    std::free(ptr);
  }

  bool canResize() const noexcept override {
    return true;
  }

  bool canAlwaysFree() const noexcept override {
    return true;
  }

  size_t getChunkSize() const noexcept override {
    return SIZE_MAX;
  }

  AllocType getType() const noexcept override {
    return AllocType::Heap;
  }

  template <typename T>
  void destroy(T* ptr) {
    if (!ptr) return;

    ptr->~T();
    free(ptr);
  }
};

// *******************************************************
//                   Tracking Allocator
// *******************************************************

// uses a header to track allocations, so adds 8 bytes per allocation
struct Tracking_Allocator final : public Memory_Allocator {
  size_t user_bytes = 0;  // user bytes allocated
  size_t user_freed = 0;  // user bytes freed
  size_t heap_bytes = 0;  // total bytes allocated
  size_t heap_freed = 0;  // total bytes freed

  Tracking_Allocator() = default;
  ~Tracking_Allocator() = default;

  Tracking_Allocator(const Tracking_Allocator&) = delete;
  Tracking_Allocator(Tracking_Allocator&&) = delete;
  Tracking_Allocator& operator=(const Tracking_Allocator&) = delete;
  Tracking_Allocator& operator=(Tracking_Allocator&&) = delete;

  void* allocate(size_t bytes, size_t = 0) noexcept override {
    const size_t total_bytes = bytes + sizeof(size_t);

    auto* raw = static_cast<size_t*>(std::malloc(total_bytes));

    if (!raw) return nullptr;

    *raw = bytes;

    user_bytes += bytes;
    heap_bytes += bytes + sizeof(size_t);
    return raw + 1;
  }

  template <typename T>
  T* alloc(const T& val) noexcept {
    size_t bytes = sizeof(T);

    auto* mem = static_cast<size_t*>(std::malloc(sizeof(size_t) + bytes));

    if (!mem) return nullptr;

    *mem = bytes;

    user_bytes += bytes;
    heap_bytes += bytes + sizeof(size_t);

    return new (mem + 1) T(val);
  }

  void* resize(void* old_ptr, size_t, size_t new_size, size_t) noexcept override {
    if (!old_ptr) return allocate(new_size, 1);

    auto* header = static_cast<size_t*>(old_ptr) - 1;
    const size_t old_size = *header;

    const size_t total_bytes = new_size + sizeof(size_t);

    auto* raw = static_cast<size_t*>(std::realloc(header, total_bytes));

    if (!raw) return nullptr;

    *raw = new_size;

    if (new_size > old_size) {
      user_bytes += new_size - old_size;
      heap_bytes += new_size - old_size;
    } else {
      user_freed += old_size - new_size;
      heap_freed += old_size - new_size;
    }

    return raw + 1;
  }

  void free(void* ptr) noexcept override {
    if (!ptr) return;

    auto* header = static_cast<size_t*>(ptr) - 1;

    const size_t size = *header;

    user_freed += size;
    heap_freed += size + sizeof(size_t);

    std::free(header);
  }

  bool canResize() const noexcept override {
    return true;
  }

  bool canAlwaysFree() const noexcept override {
    return true;
  }

  size_t getChunkSize() const noexcept override {
    return SIZE_MAX;
  }

  AllocType getType() const noexcept override {
    return AllocType::Tracking;
  }

  template <typename T>
  void destroy(T* ptr) {
    if (!ptr) return;

    ptr->~T();
    free(ptr);
  }

  void memReport() const noexcept {
    std::printf("Memory Usage:\n");
    std::printf("  User Allocated:  %zu bytes\n", user_bytes);
    std::printf("  User Freed:      %zu bytes\n\n", user_freed);
    std::printf("  Total Allocated: %zu bytes\n", heap_bytes);
    std::printf("  Total Freed:     %zu bytes\n", heap_freed);
  }
};

// *******************************************************
//                   Arena Allocator
// *******************************************************

class Arena_Allocator final : public Memory_Allocator {
  friend class Temp_Arena;

private:
  unsigned char* m_buf = nullptr;
  size_t m_buf_len = 0;
  size_t m_prev_off = 0;
  size_t m_curr_off = 0;

public:
  Arena_Allocator(size_t buf_size = MB)
      : m_buf_len(buf_size) {
    m_buf = static_cast<unsigned char*>(
        ::operator new[](m_buf_len, std::align_val_t{DEFAULT_ALIGNMENT}));
  }

  ~Arena_Allocator() {
    ::operator delete[](m_buf, std::align_val_t{DEFAULT_ALIGNMENT});
  }

  Arena_Allocator(const Arena_Allocator&) = delete;
  Arena_Allocator(Arena_Allocator&&) = delete;
  Arena_Allocator& operator=(const Arena_Allocator&) = delete;
  Arena_Allocator& operator=(Arena_Allocator&&) = delete;

  void* allocate(size_t size, size_t alignment) noexcept override {
    if (size == 0) panic("Size must be greater than zero");
    if (alignment > 128) alignment = 128;

    unsigned char* raw = m_buf + m_curr_off;
    unsigned char* aligned = static_cast<unsigned char*>(alignForward(raw, alignment));

    size_t new_off = aligned - m_buf;
    if (new_off + size > m_buf_len) panic("Arena out of memory");

    m_prev_off = new_off;
    m_curr_off = new_off + size;

    std::memset(aligned, 0, size);
    return aligned;
  }

  void* resize(void* old_ptr, size_t old_size, size_t new_size,
               size_t alignment) noexcept override {
    if (!old_ptr || old_size == 0) return allocate(new_size, alignment);

    unsigned char* p = static_cast<unsigned char*>(old_ptr);
    size_t off = p - m_buf;

    // In-place resize if last allocation
    if (off == m_prev_off &&
        m_curr_off - m_prev_off + (new_size - old_size) <= (m_buf_len - m_prev_off)) {
      m_curr_off = m_prev_off + new_size;

      if (new_size > old_size) {
        std::memset(m_buf + m_curr_off - (new_size - old_size), 0, new_size - old_size);
      }

      return p;
    }

    // Allocate fresh
    void* newp = allocate(new_size, alignment);
    if (!newp) panic("New Pointer wasn't allocated in arena");

    std::memmove(newp, old_ptr, MIN(old_size, new_size));
    return newp;
  }

  // does nothing don't use.
  void free(void*) noexcept override {
    panic("Arena Can't use free");
  }

  void freeAll() noexcept {
    m_prev_off = m_curr_off = 0;
  }

  template <typename T>
  T* alloc(size_t count = 1, size_t alignment = alignof(T)) noexcept {
    return static_cast<T*>(allocate(sizeof(T) * count, alignment));
  }

  template <typename Old, typename New>
  New* resizeTyped(Old* old_mem, size_t old_count = 1, size_t new_count = 1,
                   size_t alignment = alignof(New)) noexcept {
    return static_cast<New*>(
        resize(old_mem, sizeof(Old) * old_count, sizeof(New) * new_count, alignment));
  }

  void info() {
    std::printf("Current Offset: %zu\n", m_curr_off);
    std::printf("Previous Offset: %zu\n", m_prev_off);
    std::printf("Size of Arena: %zu\n", m_buf_len);
  }

  bool canAlwaysFree() const noexcept override {
    return false;
  }

  bool canResize() const noexcept override {
    return true;
  }

  size_t getUsed() const noexcept {
    return m_curr_off;
  }

  size_t getSize() const noexcept {
    return m_buf_len;
  }

  size_t getFree() const noexcept {
    return getSize() - getUsed();
  }

  size_t getChunkSize() const noexcept override {
    return m_buf_len;
  }

  AllocType getType() const noexcept override {
    return AllocType::Arena;
  }
};

// TempArena (checkpoint / rollback)
class Temp_Arena {
private:
  Arena_Allocator& arena;
  size_t m_prev_off, m_curr_off;

public:
  Temp_Arena(Arena_Allocator& a)
      : arena(a),
        m_prev_off(a.m_prev_off),
        m_curr_off(a.m_curr_off) {}

  ~Temp_Arena() {
    arena.m_prev_off = m_prev_off;
    arena.m_curr_off = m_curr_off;
  }

  Temp_Arena(const Temp_Arena&) = delete;
  Temp_Arena& operator=(const Temp_Arena&) = delete;
};

// *******************************************************
//                  Stack Allocator
// *******************************************************

// Stack header stored immediately before the returned user pointer
struct StackHeader {
  size_t prev_offset;  // previous top of stack
  size_t padding;      // padding before user pointer
  size_t alloc_size;   // size of allocation
};

class Stack_Allocator final : public Memory_Allocator {
  friend class Temp_Stack;

private:
  unsigned char* m_buf = nullptr;
  size_t m_buf_len = 0;
  size_t m_curr_off = 0;

public:
  explicit Stack_Allocator(size_t buf_size = MB) noexcept
      : m_buf_len{buf_size} {
    m_buf = static_cast<unsigned char*>(
        ::operator new[](m_buf_len, std::align_val_t{DEFAULT_ALIGNMENT}));
  }

  ~Stack_Allocator() {
    ::operator delete[](m_buf, std::align_val_t{DEFAULT_ALIGNMENT});
  }

  Stack_Allocator(const Stack_Allocator&) = delete;
  Stack_Allocator(Stack_Allocator&&) = delete;
  Stack_Allocator& operator=(const Stack_Allocator&) = delete;
  Stack_Allocator& operator=(Stack_Allocator&&) = delete;

  void* allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) noexcept override {
    if (!isPowerOfTwo(alignment)) panic("Must be a power of two");

    if (size <= 0) panic("Size must be greater than zero");
    if (alignment > 128) alignment = 128;

    const uintptr_t base = reinterpret_cast<uintptr_t>(m_buf);
    const uintptr_t curr_addr = base + m_curr_off;

    const size_t header_size = sizeof(StackHeader);
    const size_t padding = m_calcPaddingWithHeader(curr_addr, alignment, header_size);

    const size_t new_offset = m_curr_off + padding;
    if (new_offset < m_curr_off || new_offset > m_buf_len) return nullptr;

    const size_t end_offset = new_offset + size;
    if (end_offset < new_offset || end_offset > m_buf_len) return nullptr;

    const uintptr_t user_addr = base + new_offset;

    auto header = reinterpret_cast<StackHeader*>(user_addr - header_size);
    header->prev_offset = m_curr_off;
    header->padding = padding;
    header->alloc_size = size;

    if (header->padding > m_buf_len) panic("Padding is > m_buf_len");
    if (header->prev_offset > m_curr_off) panic("prev_off > m_curr_off");
    m_curr_off = end_offset;

    // Zero memory (optional)
    return std::memset(reinterpret_cast<void*>(user_addr), 0, size);
  }

  template <typename T>
  T* alloc(size_t count = 1, size_t alignment = alignof(T)) noexcept {
    if (count > SIZE_MAX / sizeof(T)) return nullptr;

    void* p = allocate(sizeof(T) * count, alignment);
    return static_cast<T*>(p);
  }

  void* resize(void* old_ptr, size_t /*old_size*/, size_t new_size,
               size_t alignment = DEFAULT_ALIGNMENT) noexcept override {
    if (!old_ptr) return allocate(new_size, alignment);
    if (new_size == 0) {
      free(old_ptr);
      return nullptr;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(m_buf);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(old_ptr);

    if (!(base <= addr && addr < base + m_buf_len)) {
      panic("Pointer out of bounds (resize)");
    }

    const size_t header_size = sizeof(StackHeader);
    auto* header = reinterpret_cast<StackHeader*>(addr - header_size);
    if (header->padding > m_buf_len) panic("Padding is > m_buf_len");
    if (header->prev_offset > m_curr_off) panic("prev_off > m_curr_off");

    const size_t block_start = header->prev_offset + header->padding;
    const size_t block_size = header->alloc_size;
    const size_t block_end = block_start + block_size;

    // Top-of-stack check
    if (block_end == m_curr_off) {
      // Shrink
      if (new_size <= block_size) {
        m_curr_off = block_start + new_size;
        header->alloc_size = new_size;
        return old_ptr;
      }

      // Grow
      size_t grow = new_size - block_size;
      if (m_curr_off + grow <= m_buf_len) {
        m_curr_off += grow;
        header->alloc_size = new_size;
        return old_ptr;
      }
    }

    // Fallback
    void* new_ptr = allocate(new_size, alignment);
    if (!new_ptr) return nullptr;

    size_t copy_size = MIN(header->alloc_size, new_size);
    std::memmove(new_ptr, old_ptr, copy_size);

    return new_ptr;
  }

  void free(void* ptr) noexcept override {
    if (!ptr) return;

    const uintptr_t base = reinterpret_cast<uintptr_t>(m_buf);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    if (!(base <= addr && addr < base + m_buf_len)) {
      panic("Pointer out of bounds (free)");
    }

    const size_t header_size = sizeof(StackHeader);
    auto* header = reinterpret_cast<StackHeader*>(addr - header_size);
    if (header->padding > m_buf_len) panic("Padding is > m_buf_len");
    if (header->prev_offset > m_curr_off) panic("prev_off > m_curr_off");

    const size_t block_start = header->prev_offset + header->padding;
    const size_t block_size = header->alloc_size;

    const uintptr_t expected_addr = base + block_start;
    if (expected_addr != addr) {
      panic("Header mismatch");
    }

    // LIFO check
    if (addr + block_size != base + m_curr_off) {
      panic("Out-of-order free");
    }

    m_curr_off = header->prev_offset;
  }

  void freeAll() noexcept {
    m_curr_off = 0;
  }

  size_t getMarker() const noexcept {
    return m_curr_off;
  }

  void freeToMarker(size_t marker) noexcept {
    if (marker > m_curr_off) {
      panic("Invalid marker");
    }
    m_curr_off = marker;
  }

  void info() {
    std::printf("Current Offset: %zu\n", m_curr_off);
    std::printf("Size of Arena: %zu\n", m_buf_len);
  }

  bool canResize() const noexcept override {
    return true;
  }

  bool canAlwaysFree() const noexcept override {
    return false;
  }

  size_t getUsed() const noexcept {
    return m_curr_off;
  }

  size_t getSize() const noexcept {
    return m_buf_len;
  }

  size_t getFree() const noexcept {
    return getSize() - getUsed();
  }

  size_t getChunkSize() const noexcept override {
    return m_buf_len;
  }

  AllocType getType() const noexcept override {
    return AllocType::Stack;
  }

  template <typename T>
  void destroy(T* ptr) {
    if (!ptr) return;

    ptr->~T();
    free(ptr);
  }

private:
  static constexpr size_t m_calcPaddingWithHeader(uintptr_t ptr, size_t alignment,
                                                  size_t header_size) noexcept {
    if (!isPowerOfTwo(alignment)) panic("Must be a power of two!");

    const size_t modulo = ptr & (alignment - 1);
    size_t padding = (modulo == 0) ? 0 : (alignment - modulo);

    if (padding < header_size) {
      size_t needed = header_size - padding;
      padding += ((needed + alignment - 1) / alignment) * alignment;
    }

    return padding;
  }
};

class Temp_Stack {
private:
  Stack_Allocator& m_stack;
  size_t m_curr_offset;

public:
  Temp_Stack(Stack_Allocator& stack)
      : m_stack{stack},
        m_curr_offset{stack.m_curr_off} {}

  ~Temp_Stack() {
    m_stack.m_curr_off = m_curr_offset;
  }

  Temp_Stack(const Temp_Stack&) = delete;
  Temp_Stack& operator=(const Temp_Stack&) = delete;
};

// ********************************************
//                 Pool Allocator
// ********************************************

struct PoolFreeNode {
  PoolFreeNode* next;
};

class Pool_Allocator final : public Memory_Allocator {
  friend class Temp_Pool;

private:
  unsigned char* m_buf = nullptr;
  size_t m_buf_len = 0;
  size_t m_chunk_size = 0;
  PoolFreeNode* m_head = nullptr;

public:
  explicit Pool_Allocator(size_t buf_size = MB, size_t chunk_size = 64,
                          size_t chunk_alignment = alignof(std::max_align_t)) {
    if (buf_size <= 0) panic("Buf size <= 0");
    if (chunk_size <= 0) panic("Chunk size <= 0");
    if (!isPowerOfTwo(chunk_alignment)) panic("chunk_alignment must be power of two");

    m_buf_len = buf_size;
    m_buf =
        static_cast<unsigned char*>(::operator new[](m_buf_len, std::align_val_t{chunk_alignment}));
    m_chunk_size = (chunk_size + (chunk_alignment - 1)) & ~(chunk_alignment - 1);

    if (m_chunk_size < sizeof(PoolFreeNode)) panic("chunk_size too small for free list node");

    freeAll();
  }

  ~Pool_Allocator() {
    ::operator delete[](m_buf, std::align_val_t{alignof(std::max_align_t)});
  }

  Pool_Allocator(const Pool_Allocator&) = delete;
  Pool_Allocator(Pool_Allocator&&) = delete;
  Pool_Allocator& operator=(const Pool_Allocator&) = delete;
  Pool_Allocator& operator=(Pool_Allocator&&) = delete;

  void* allocate(size_t = 0, size_t = 0) noexcept override {
    if (!m_head) return nullptr;
    PoolFreeNode* node = m_head;
    m_head = node->next;
    return std::memset(node, 0, m_chunk_size);
  }

  template <typename T>
  T* alloc(const T& val) {
    T* p = static_cast<T*>(allocate());
    *p = val;
    return p;
  }

  void free(void* ptr) noexcept override {
    if (!ptr) return;

    auto* node = static_cast<PoolFreeNode*>(ptr);
    node->next = m_head;
    m_head = node;
  }

  void freeAll() noexcept {
    m_head = nullptr;
    size_t count = m_buf_len / m_chunk_size;
    for (size_t i = 0; i < count; ++i) {
      auto* node = reinterpret_cast<PoolFreeNode*>(m_buf + i * m_chunk_size);
      node->next = m_head;
      m_head = node;
    }
  }

  void* resize(void*, size_t, size_t, size_t) noexcept override {
    panic("Pool Can't resize");
  }

  bool canAlwaysFree() const noexcept override {
    return true;
  }

  bool canResize() const noexcept override {
    return false;
  }

  size_t getUsed() const noexcept {
    size_t total_chunks = m_buf_len / m_chunk_size;

    size_t free_chunks = 0;
    for (PoolFreeNode* n = m_head; n; n = n->next) free_chunks++;

    size_t used_chunks = total_chunks - free_chunks;
    return used_chunks * m_chunk_size;
  }

  size_t getSize() const noexcept {
    return m_buf_len;
  }

  size_t getFree() const noexcept {
    return getSize() - getUsed();
  }

  size_t getChunkSize() const noexcept override {
    return m_chunk_size;
  }

  AllocType getType() const noexcept override {
    return AllocType::Pool;
  }

  template <typename T>
  void destroy(T* ptr) {
    if (!ptr) return;

    ptr->~T();
    free(ptr);
  }
};

class Temp_Pool {
private:
  Pool_Allocator& m_pool;
  PoolFreeNode* m_marker = nullptr;

public:
  explicit Temp_Pool(Pool_Allocator& p)
      : m_pool(p) {
    m_marker = m_pool.m_head;
  }

  ~Temp_Pool() {
    m_pool.m_head = m_marker;
  }
};

#undef MIN

extern Heap_Allocator heap_alloc;
