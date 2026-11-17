#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hydra::dsp {

template <typename T, size_t Capacity = 32>
class SpscQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
  static_assert(Capacity >= 2, "Capacity must be at least 2");

 public:
  SpscQueue() = default;
  ~SpscQueue() = default;

  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  bool push(const T& item) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    if ((head - tail) >= Capacity) {
      return false; // Queue full - dropped safely without blocking
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    buffer_[head & (Capacity - 1)] = item;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  bool pop(T& item) noexcept {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    if (tail == head) {
      return false; // Queue empty
    }
    item = buffer_[tail & (Capacity - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool empty() const noexcept {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  size_t size() const noexcept {
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t tail = tail_.load(std::memory_order_acquire);
    return (head >= tail) ? (head - tail) : 0;
  }

  static constexpr size_t capacity() noexcept {
    return Capacity;
  }

  void reset() noexcept {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

 private:
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};
  alignas(64) T buffer_[Capacity]{};
};

/**
 * @brief Lock-free, real-time safe snapshot publisher using an atomic Triple Buffer.
 *
 * Concurrency Safety & Memory Model (C++17):
 * - Eliminates data races: writer and reader NEVER access the same buffer concurrently.
 * - Single writer (audio callback) mutates buffers_[back_idx_] exclusively without locks,
 *   retries, or dynamic memory allocation (100% wait-free O(1)).
 * - Consumer (CLI/telemetry) acquires the latest published snapshot via atomic exchange/CAS.
 * - Synchronized via acquire-release memory order on the single 32-bit atomic word spare_.
 * - Multi-reader safe via lightweight spinlock on consumer side (writer never touches reader_lock_).
 */
template <typename T>
class StatePublisher {
  static_assert(std::is_trivially_copyable<T>::value, "State type must be trivially copyable for snapshot publisher");

 public:
  StatePublisher() noexcept {
    spare_.store(pack(1, false, false), std::memory_order_relaxed);
  }

  ~StatePublisher() = default;

  StatePublisher(const StatePublisher&) = delete;
  StatePublisher& operator=(const StatePublisher&) = delete;

  /**
   * @brief Publishes a new state snapshot from the single producer (audio thread).
   *
   * Real-Time Guarantees:
   * - 100% Wait-Free: strictly bounded O(1) instructions.
   * - Zero mutexes, zero semaphores, zero retries, zero allocations, zero I/O.
   * - The writer exclusively mutates buffers_[back_idx_], which is not owned by the reader.
   * - Atomic exchange on spare_ transfers buffer ownership with release semantics.
   */
  void publish(const T& state) noexcept {
    // 1. Writer writes exclusively into private back buffer
    buffers_[back_idx_].data = state;

    // 2. Publish back_idx_ into spare_ and retrieve the previously unused spare buffer.
    // memory_order_acq_rel ensures writer's store to buffers_[back_idx_] happens-before
    // reader reads it, and writer acquires whatever was released by reader.
    const uint32_t new_spare = pack(back_idx_, /*has_update=*/true, /*is_valid=*/true);
    const uint32_t prev_spare = spare_.exchange(new_spare, std::memory_order_acq_rel);

    // 3. The retrieved spare buffer becomes the writer's new private back buffer
    back_idx_ = unpack_idx(prev_spare);
  }

  /**
   * @brief Reads the latest published state snapshot.
   *
   * Real-Time & Concurrency Guarantees:
   * - Returns false if no snapshot has been published yet.
   * - Returns true and copies the latest stable snapshot into `out`.
   * - Zero data races under C++17 memory model.
   * - Readers are serialized via reader_lock_; the audio producer NEVER touches reader_lock_.
   */
  bool read(T& out) const noexcept {
    while (reader_lock_.test_and_set(std::memory_order_acquire)) {
      // Contention only occurs if multiple readers query simultaneously.
    }

    uint32_t curr = spare_.load(std::memory_order_acquire);
    if (!unpack_valid(curr)) {
      reader_lock_.clear(std::memory_order_release);
      return false; // No state published yet
    }

    if (unpack_has_update(curr)) {
      while (unpack_has_update(curr)) {
        const uint32_t next = pack(front_idx_, /*has_update=*/false, /*is_valid=*/true);
        if (spare_.compare_exchange_weak(curr, next,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
          front_idx_ = unpack_idx(curr);
          has_read_valid_ = true;
          break;
        }
      }
    }

    bool success = false;
    if (has_read_valid_) {
      out = buffers_[front_idx_].data;
      success = true;
    }

    reader_lock_.clear(std::memory_order_release);
    return success;
  }

  void reset() noexcept {
    spare_.store(pack(1, false, false), std::memory_order_release);
    back_idx_ = 2;
    front_idx_ = 0;
    has_read_valid_ = false;
  }

 private:
  static constexpr uint32_t kIndexMask = 0x3;
  static constexpr uint32_t kHasUpdateBit = 0x4;
  static constexpr uint32_t kIsValidBit = 0x8;

  static constexpr uint32_t pack(uint32_t idx, bool has_update, bool is_valid) noexcept {
    return (idx & kIndexMask) | (has_update ? kHasUpdateBit : 0) | (is_valid ? kIsValidBit : 0);
  }

  static constexpr uint32_t unpack_idx(uint32_t val) noexcept {
    return val & kIndexMask;
  }

  static constexpr bool unpack_has_update(uint32_t val) noexcept {
    return (val & kHasUpdateBit) != 0;
  }

  static constexpr bool unpack_valid(uint32_t val) noexcept {
    return (val & kIsValidBit) != 0;
  }

  struct alignas(64) BufferSlot {
    T data{};
  };

  alignas(64) BufferSlot buffers_[3];
  alignas(64) mutable std::atomic<uint32_t> spare_{pack(1, false, false)};
  alignas(64) uint32_t back_idx_{2};
  alignas(64) mutable uint32_t front_idx_{0};
  mutable bool has_read_valid_{false};
  mutable std::atomic_flag reader_lock_ = ATOMIC_FLAG_INIT;
};

} // namespace hydra::dsp
