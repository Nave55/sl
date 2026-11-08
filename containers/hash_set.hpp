#pragma once

#include "vector.hpp"

template <typename K, typename Hash = std::hash<K>, typename KeyEq = std::equal_to<K>>
class Hash_Set {
public:
  struct Iterator;
  struct ConstIterator;
  size_t len = 0;
  size_t cap = 0;
  size_t slab_bytes = 0;
  size_t slab_size = 32;
  size_t bucket_amt = 64;

private:
  struct m_Node;
  Hash m_hash = Hash{};
  KeyEq m_eq = KeyEq{};
  m_Node* m_free = nullptr;
  Memory_Allocator* m_alloc;
  m_Node** m_buckets;
  Vector<void*> m_slabs;

public:
  explicit Hash_Set(Memory_Allocator& alloc) noexcept
      : m_alloc(&alloc),
        m_buckets(static_cast<m_Node**>(
            alloc.allocate(sizeof(m_Node*) * this->bucket_amt, alignof(m_Node*)))),
        m_slabs(0, 4, alloc) {
    if (!m_buckets) panic("HashSet: bucket allocation failed");
    for (size_t i = 0; i < this->bucket_amt; ++i) m_buckets[i] = nullptr;

    if (sizeof(m_Node) % alignof(m_Node) != 0)
      panic("UnorderedSet: Node size must be multiple of alignment");

    slab_bytes = slab_size * sizeof(m_Node);
  }

  explicit Hash_Set(size_t slab_size = 32, size_t slabs = 4, size_t bucket_amt = 64,
                    Memory_Allocator& alloc = heap_alloc) noexcept
      : slab_size(slab_size),
        bucket_amt(m_nextPow2(bucket_amt)),
        m_alloc(&alloc),
        m_buckets(static_cast<m_Node**>(
            alloc.allocate(sizeof(m_Node*) * this->bucket_amt, alignof(m_Node*)))),
        m_slabs(0, slabs, alloc) {
    if (!m_buckets) panic("HashSet: bucket allocation failed");
    for (size_t i = 0; i < this->bucket_amt; ++i) m_buckets[i] = nullptr;

    if (sizeof(m_Node) % alignof(m_Node) != 0)
      panic("UnorderedSet: Node size must be multiple of alignment");

    slab_bytes = slab_size * sizeof(m_Node);
  }

  explicit Hash_Set(std::initializer_list<K> init, size_t bucket_amt = 64,
                    Memory_Allocator& alloc = heap_alloc) noexcept
      : Hash_Set(init.size(), 1, bucket_amt, alloc) {
    for (const K& key : init) insert(key);
  }

  explicit Hash_Set(std::initializer_list<K> init, size_t slab_size, size_t slabs = 4,
                    size_t bucket_amt = 64, Memory_Allocator& alloc = heap_alloc) noexcept
      : Hash_Set(slab_size, slabs, bucket_amt, alloc) {
    for (const K& key : init) insert(key);
  }

  ~Hash_Set() noexcept {
    clear();

    if (m_alloc->canAlwaysFree()) {
      m_alloc->free(m_buckets);
      for (size_t i = 0; i < m_slabs.len; ++i) {
        auto val = m_slabs.at(i);
        if (val.has_value()) m_alloc->free(static_cast<void*>(val.value()));
      }
    }
  }

  Hash_Set(const Hash_Set& o) noexcept
      : len(0),
        cap(0),
        slab_bytes(o.slab_bytes),
        slab_size(o.slab_size),
        bucket_amt(o.bucket_amt),
        m_hash(o.m_hash),
        m_eq(o.m_eq),
        m_free(nullptr),
        m_alloc(o.m_alloc),
        m_buckets(static_cast<m_Node**>(
            m_alloc->allocate(sizeof(m_Node*) * bucket_amt, alignof(m_Node*)))),
        m_slabs(0, o.m_slabs.cap, *m_alloc) {
    if (!m_buckets) panic("HashSet copy: bucket allocation failed");
    for (size_t i = 0; i < bucket_amt; ++i) m_buckets[i] = nullptr;
    for (size_t i = 0; i < o.m_slabs.len; ++i) m_allocSlab();

    for (size_t i = 0; i < o.bucket_amt; ++i) {
      m_Node* cur = o.m_buckets[i];
      while (cur) {
        insert(cur->key);
        cur = cur->next;
      }
    }
  }

  Hash_Set& operator=(const Hash_Set& o) noexcept {
    if (this == &o) return *this;
    Hash_Set tmp(o);
    *this = std::move(tmp);
    return *this;
  }

  Hash_Set(Hash_Set&& o) noexcept
      : len(o.len),
        cap(o.cap),
        slab_bytes(o.slab_bytes),
        slab_size(o.slab_size),
        bucket_amt(o.bucket_amt),
        m_hash(std::move(o.m_hash)),
        m_eq(std::move(o.m_eq)),
        m_free(o.m_free),
        m_alloc(o.m_alloc),
        m_buckets(o.m_buckets),
        m_slabs(std::move(o.m_slabs)) {
    o.len = 0;
    o.cap = 0;
    o.slab_bytes = 0;
    o.slab_size = 0;
    o.bucket_amt = 0;

    o.m_free = nullptr;
    o.m_alloc = nullptr;
    o.m_buckets = nullptr;
  }

  Hash_Set& operator=(Hash_Set&& o) noexcept {
    if (this == &o) return *this;
    this->~Hash_Set();
    new (this) Hash_Set(std::move(o));
    return *this;
  }

  // union
  Hash_Set operator|(const Hash_Set& other) const noexcept {
    Hash_Set result(*this);
    for (const auto& key : other) result.insert(key);
    return result;
  }

  // intersection
  Hash_Set operator&(const Hash_Set& other) const noexcept {
    Hash_Set result(slab_size, m_slabs.cap, bucket_amt, *m_alloc);

    for (const auto& key : *this)
      if (other.contains(key)) result.insert(key);

    return result;
  }

  bool operator==(const Hash_Set& other) const noexcept {
    for (const auto& key : other) {
      if (!this->contains(key)) return false;
    }
    return true;
  }

  template <typename PrintFn>
  void print(PrintFn printFn) const noexcept {
    size_t id = 0;
    std::printf("HashSet { ");
    for (auto&& k : *this) {
      printFn(k);
      if (id < len - 1) std::printf(", ");
      id++;
    }
    std::printf(" }\n");
  }

  void printInfo() const noexcept {
    std::printf("Slabs: %zu\n", m_slabs.len);
    std::printf("Slab Size: %zu\n", slab_size);
    std::printf("Bucket Amt: %zu\n", bucket_amt);
  }

  size_t size() const noexcept {
    return len;
  }

  size_t slabAmt() const noexcept {
    return m_slabs.len;
  }

  size_t remainingCapacity() const noexcept {
    return cap - len;
  }

  bool contains(const K& key) const noexcept {
    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) return true;
      n = n->next;
    }
    return false;
  }

  void insert(const K& key) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) return;
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, key);
    newNode->next = m_buckets[i];
    m_buckets[i] = newNode;
    ++len;
  }

  void insert(K&& key) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) return;
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, std::move(key));
    newNode->next = m_buckets[i];
    m_buckets[i] = newNode;
    ++len;
  }

  bool erase(const K& key) noexcept {
    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* prev = nullptr;
    m_Node* curr = m_buckets[i];

    while (curr) {
      if (curr->hash == h && m_eq(curr->key, key)) {
        if (prev)
          prev->next = curr->next;
        else
          m_buckets[i] = curr->next;

        curr->~m_Node();
        m_freeNode(curr);
        --len;
        return true;
      }
      prev = curr;
      curr = curr->next;
    }
    return false;
  }

  void clear() noexcept {
    for (size_t i = 0; i < bucket_amt; ++i) {
      m_Node* n = m_buckets[i];
      m_buckets[i] = nullptr;
      while (n) {
        m_Node* next = n->next;
        n->~m_Node();
        m_freeNode(n);
        n = next;
      }
    }
    len = 0;
  }

  void rehash(size_t new_cap) noexcept {
    new_cap = m_nextPow2(new_cap);
    const size_t old_cap = bucket_amt;

    m_Node** new_buckets =
        static_cast<m_Node**>(m_alloc->allocate(sizeof(m_Node*) * new_cap, alignof(m_Node*)));
    if (!new_buckets) panic("HashSet::rehash: bucket allocation failed");

    for (size_t i = 0; i < new_cap; ++i) new_buckets[i] = nullptr;

    for (size_t i = 0; i < old_cap; ++i) {
      m_Node* n = m_buckets[i];
      while (n) {
        m_Node* next = n->next;
        const size_t idx = n->hash & (new_cap - 1);
        n->next = new_buckets[idx];
        new_buckets[idx] = n;
        n = next;
      }
    }

    if (m_alloc->canAlwaysFree()) m_alloc->free(m_buckets);

    m_buckets = new_buckets;
    bucket_amt = new_cap;
  }

public:
  struct Iterator {
    using Self = Hash_Set<K, Hash, KeyEq>;

    Self* set;
    size_t bucket;
    m_Node* node;

    Iterator(Self* s, size_t b, m_Node* n) noexcept
        : set(s),
          bucket(b),
          node(n) {}

    K& operator*() const noexcept {
      return node->key;
    }
    m_Node* operator->() const noexcept {
      return node;
    }

    Iterator& operator++() noexcept {
      if (node) node = node->next;
      while (!node && ++bucket < set->bucket_amt) node = set->m_buckets[bucket];
      return *this;
    }

    bool operator==(const Iterator& other) const noexcept {
      return node == other.node && bucket == other.bucket;
    }

    bool operator!=(const Iterator& other) const noexcept {
      return !(*this == other);
    }
  };

  struct ConstIterator {
    using Self = Hash_Set<K, Hash, KeyEq>;

    const Self* set;
    size_t bucket;
    const m_Node* node;

    ConstIterator(const Self* s, size_t b, const m_Node* n) noexcept
        : set(s),
          bucket(b),
          node(n) {}

    const K& operator*() const noexcept {
      return node->key;
    }
    const m_Node* operator->() const noexcept {
      return node;
    }

    ConstIterator& operator++() noexcept {
      if (node) node = node->next;
      while (!node && ++bucket < set->bucket_amt) node = set->m_buckets[bucket];
      return *this;
    }

    bool operator==(const ConstIterator& other) const noexcept {
      return node == other.node && bucket == other.bucket;
    }

    bool operator!=(const ConstIterator& other) const noexcept {
      return !(*this == other);
    }
  };

  Iterator begin() noexcept {
    for (size_t i = 0; i < bucket_amt; ++i)
      if (m_buckets[i]) return Iterator(this, i, m_buckets[i]);
    return end();
  }

  Iterator end() noexcept {
    return Iterator(this, bucket_amt, nullptr);
  }

  ConstIterator begin() const noexcept {
    for (size_t i = 0; i < bucket_amt; ++i)
      if (m_buckets[i]) return ConstIterator(this, i, m_buckets[i]);
    return end();
  }

  ConstIterator end() const noexcept {
    return ConstIterator(this, bucket_amt, nullptr);
  }

private:
  struct m_Node {
    size_t hash;
    K key;
    m_Node* next;

    m_Node(size_t h, const K& k) noexcept
        : hash(h),
          key(k),
          next(nullptr) {}

    m_Node(size_t h, K&& k) noexcept
        : hash(h),
          key(std::move(k)),
          next(nullptr) {}
  };

  m_Node* m_allocNode(size_t h, const K& key) noexcept {
    if (!m_free) m_allocSlab();

    m_Node* n = m_free;
    m_free = m_free->next;

    new (n) m_Node(h, key);
    return n;
  }

  m_Node* m_allocNode(size_t h, K&& key) noexcept {
    if (!m_free) m_allocSlab();

    m_Node* n = m_free;
    m_free = m_free->next;

    new (n) m_Node(h, std::move(key));
    return n;
  }

  void m_freeNode(m_Node* n) noexcept {
    n->next = m_free;
    m_free = n;
  }

  void m_allocSlab() noexcept {
    const size_t node_size = sizeof(m_Node);

    size_t nodes;
    if (len < 128)
      nodes = slab_size;
    else
      nodes = len / 4;

    nodes = std::max(nodes, slab_size);
    nodes = std::min(nodes, size_t(4096));

    if (m_alloc->getType() == AllocType::Pool) {
      const size_t chunk = m_alloc->getChunkSize();
      const size_t max_nodes = chunk / node_size;
      if (max_nodes == 0) panic("HashSet::allocate_slab: Chunk too small for Node");

      nodes = std::min(nodes, max_nodes);
    }

    slab_size = nodes;
    slab_bytes = nodes * node_size;

    void* raw = m_alloc->allocate(slab_bytes, alignof(m_Node));
    if (!raw) panic("HashSet::allocate_slab: allocator failed");

    m_slabs.pushBack(raw);
    cap += nodes;

    m_Node* base = static_cast<m_Node*>(raw);
    for (size_t i = 0; i < slab_size; ++i) {
      base[i].next = m_free;
      m_free = &base[i];
    }
  }

  static size_t m_nextPow2(size_t x) noexcept {
    if (x <= 1) return 1;
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    if constexpr (sizeof(size_t) == 8) x |= x >> 32;
    return x + 1;
  }

  static size_t m_mixHash(size_t h) noexcept {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
  }
};
