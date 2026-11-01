#pragma once

#include "pair.hpp"
#include "vector.hpp"

template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEq = std::equal_to<K>>
class Hash_Map {
public:
  struct Iterator;
  struct ConstIterator;
  size_t len = 0;         // amount of entries in hashmap
  size_t cap = 0;         // total node capacity
  size_t slab_bytes = 0;  // bytes per slab
  size_t slab_size;       // nodes per slab
  size_t bucket_amt;      // number of buckets

private:
  struct m_Node;
  Hash m_hash = Hash{};
  KeyEq m_eq = KeyEq{};
  m_Node* m_free = nullptr;   // free list head
  Memory_Allocator* m_alloc;  // custom allocator
  m_Node** m_buckets;
  Vector<void*> m_slabs;  // raw slab pointers

public:
  explicit Hash_Map(size_t slab_size = 32, size_t slabs = 4, size_t bucket_amt = 64,
                    Memory_Allocator& alloc = heap_alloc) noexcept
      : slab_size(slab_size),
        bucket_amt(m_nextPow2(bucket_amt)),
        m_alloc(&alloc),
        m_buckets(static_cast<m_Node**>(
            alloc.allocate(sizeof(m_Node*) * this->bucket_amt, alignof(m_Node*)))),
        m_slabs(0, slabs, alloc) {
    if (!m_buckets) panic("UnorderedMap: bucket allocation failed");
    for (size_t i = 0; i < this->bucket_amt; ++i) m_buckets[i] = nullptr;

    if (sizeof(m_Node) % alignof(m_Node) != 0)
      panic("UnorderedMap: Node size must be multiple of alignment");

    slab_bytes = slab_size * sizeof(m_Node);
  }

  explicit Hash_Map(std::initializer_list<Pair<K, V>> init, size_t bucket_amt = 64,
                    Memory_Allocator& alloc = heap_alloc) noexcept
      : Hash_Map(init.size(), 1, bucket_amt, alloc) {
    for (auto& [k, v] : init) insert(k, v);
    slab_bytes = slab_size * sizeof(m_Node);
  }

  explicit Hash_Map(std::initializer_list<Pair<K, V>> init, size_t slab_size, size_t slabs = 4,
                    size_t bucket_amt = 64, Memory_Allocator& alloc = heap_alloc) noexcept
      : Hash_Map(slab_size, slabs, bucket_amt, alloc) {
    for (auto& [k, v] : init) insert(k, v);
    slab_bytes = slab_size * sizeof(m_Node);
  }

  ~Hash_Map() noexcept {
    clear();

    if (m_alloc->canAlwaysFree()) {
      m_alloc->free(m_buckets);
      for (size_t i = 0; i < m_slabs.len; ++i) {
        auto val = m_slabs.at(i);
        if (val.has_value()) m_alloc->free(static_cast<void*>(val.value()));
      }
    }
  }

  Hash_Map(const Hash_Map& o) noexcept
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
    if (!m_buckets) panic("HashMap copy: bucket allocation failed");
    for (size_t i = 0; i < bucket_amt; ++i) m_buckets[i] = nullptr;

    for (size_t i = 0; i < o.m_slabs.len; ++i) m_allocSlab();

    for (size_t i = 0; i < o.bucket_amt; ++i) {
      m_Node* cur = o.m_buckets[i];
      while (cur) {
        insert(cur->key, cur->value);
        cur = cur->next;
      }
    }
  }

  Hash_Map& operator=(const Hash_Map& o) noexcept {
    if (this == &o) return *this;
    Hash_Map tmp(o);
    *this = std::move(tmp);
    return *this;
  }

  Hash_Map(Hash_Map&& o) noexcept
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
    o.m_alloc = nullptr;
    o.m_buckets = nullptr;
    o.len = 0;
    o.cap = 0;
    o.bucket_amt = 0;
    o.m_free = nullptr;
    o.slab_size = 0;
    o.slab_bytes = 0;
  }

  Hash_Map& operator=(Hash_Map&& o) noexcept {
    if (this == &o) return *this;
    this->~Hash_Map();
    new (this) Hash_Map(std::move(o));
    return *this;
  }

  template <typename PrintKey, typename PrintVal>
  void print(PrintKey printKey, PrintVal printVal) const noexcept {
    std::printf("HashMap {\n");
    for (auto&& [k, v] : *this) {
      std::printf("  ");
      printKey(k);
      std::printf(" => ");
      printVal(v);
      std::printf("\n");
    }
    std::printf("}\n");
  }

  size_t remaining_capacity() const noexcept {
    return cap - len;
  }

  bool operator==(const Hash_Map& other) const noexcept {
    if (len != other.len) return false;

    for (const auto& [k, v] : other) {
      auto val = find(k);
      if (!val || *val != v) return false;
    }

    return true;
  }

  V* operator[](const K& key) noexcept {
    return find(key);
  }

  const V* operator[](const K& key) const noexcept {
    return find(key);
  }

  std::optional<V&> at(const K& key) noexcept {
    auto val = find(key);
    if (!val) return std::nullopt;
    return *val;
  }

  std::optional<const V&> at(const K& key) const noexcept {
    auto val = find(key);
    if (!val) return std::nullopt;
    return *val;
  }

  V& atOrInsert(const K& key) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) return n->value;
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, key, V{});
    newNode->next = m_buckets[i];
    m_buckets[i] = newNode;
    ++len;
    return newNode->value;
  }

  size_t size() const noexcept {
    return len;
  }

  void insert(const K& key, const V& value) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) {
        n->value = value;
        return;
      }
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, key, value);
    newNode->next = m_buckets[i];
    m_buckets[i] = newNode;
    ++len;
  }

  void insert(K&& key, V&& value) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) {
        n->value = std::move(value);
        return;
      }
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, std::move(key), std::move(value));
    newNode->next = m_buckets[i];
    m_buckets[i] = newNode;
    ++len;
  }

  void insert(const K& key, V&& value) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) {
        n->value = std::move(value);
        return;
      }
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, key, std::move(value));
    newNode->next = m_buckets[i];
    m_buckets[i] = newNode;
    ++len;
  }

  void insert(K&& key, const V& value) noexcept {
    if (len * 4 > bucket_amt * 3) rehash(bucket_amt * 2);

    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) {
        n->value = value;
        return;
      }
      n = n->next;
    }

    m_Node* newNode = m_allocNode(h, std::move(key), value);
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

  V* find(const K& key) noexcept {
    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) return &n->value;
      n = n->next;
    }
    return nullptr;
  }

  const V* find(const K& key) const noexcept {
    const size_t raw = m_hash(key);
    const size_t h = m_mixHash(raw);
    const size_t i = h & (bucket_amt - 1);

    const m_Node* n = m_buckets[i];
    while (n) {
      if (n->hash == h && m_eq(n->key, key)) return &n->value;
      n = n->next;
    }
    return nullptr;
  }

  bool contains(const K& key) const noexcept {
    return find(key) != nullptr;
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
    if (!new_buckets) panic("UnorderedMap::rehash: bucket allocation failed");

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

  struct Iterator {
    Hash_Map* map;
    size_t bucket;
    m_Node* node;

    Iterator(Hash_Map* m, size_t b, m_Node* n) noexcept
        : map(m),
          bucket(b),
          node(n) {}

    auto operator*() const noexcept {
      return Pair<const K&, V&>(node->key, node->value);
    }

    m_Node* operator->() const noexcept {
      return node;
    }

    Iterator& operator++() noexcept {
      if (node) node = node->next;
      while (!node && ++bucket < map->bucket_amt) {
        node = map->m_buckets[bucket];
      }
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
    const Hash_Map* map;
    size_t bucket;
    const m_Node* node;

    ConstIterator(const Hash_Map* m, size_t b, const m_Node* n) noexcept
        : map(m),
          bucket(b),
          node(n) {}

    auto operator*() const noexcept {
      return Pair<const K&, const V&>(node->key, node->value);
    }

    const m_Node* operator->() const noexcept {
      return node;
    }

    ConstIterator& operator++() noexcept {
      if (node) node = node->next;
      while (!node && ++bucket < map->bucket_amt) node = map->m_buckets[bucket];
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
    V value;
    m_Node* next;

    m_Node(size_t h, const K& k, const V& v) noexcept
        : hash(h),
          key(k),
          value(v),
          next(nullptr) {}

    m_Node(size_t h, K&& k, V&& v) noexcept
        : hash(h),
          key(std::move(k)),
          value(std::move(v)),
          next(nullptr) {}
  };

  static size_t m_nextPow2(size_t x) noexcept {
    if (x == 0) return 1;
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

  m_Node* m_allocNode(size_t h, const K& key, const V& value) noexcept {
    if (!m_free) m_allocSlab();

    m_Node* n = m_free;
    m_free = m_free->next;
    new (n) m_Node(h, key, value);
    n->next = nullptr;
    return n;
  }

  m_Node* m_allocNode(size_t h, K&& key, V&& value) noexcept {
    if (!m_free) m_allocSlab();

    m_Node* n = m_free;
    m_free = m_free->next;
    new (n) m_Node(h, std::move(key), std::move(value));
    n->next = nullptr;
    return n;
  }

  m_Node* m_allocNode(size_t h, const K& key, V&& value) noexcept {
    if (!m_free) m_allocSlab();

    m_Node* n = m_free;
    m_free = m_free->next;
    new (n) m_Node(h, key, std::move(value));
    n->next = nullptr;
    return n;
  }

  m_Node* m_allocNode(size_t h, K&& key, const V& value) noexcept {
    if (!m_free) m_allocSlab();

    m_Node* n = m_free;
    m_free = m_free->next;
    new (n) m_Node(h, std::move(key), value);
    n->next = nullptr;
    return n;
  }

  void m_freeNode(m_Node* n) noexcept {
    if (!n) return;
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

    // If backing allocator is Pool, cap by chunk capacity
    if (m_alloc->getType() == AllocType::Pool) {
      const size_t chunk = m_alloc->getChunkSize();
      const size_t max_nodes = chunk / node_size;
      if (max_nodes == 0) panic("UnorderedMap::allocate_slab: Pool chunk too small for Node");

      nodes = std::min(nodes, max_nodes);
    }

    slab_size = nodes;
    slab_bytes = nodes * node_size;

    void* raw = m_alloc->allocate(slab_bytes, alignof(m_Node));
    if (!raw) panic("UnorderedMap::allocate_slab: allocator failed");

    m_slabs.pushBack(raw);
    cap += nodes;

    m_Node* base = static_cast<m_Node*>(raw);
    for (size_t i = 0; i < slab_size; ++i) {
      base[i].next = m_free;
      m_free = &base[i];
    }
  }
};
