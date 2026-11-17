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
    buffer_[head & (Capacity - 1)] = item;
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

template <typename T>
class StatePublisher {
  static_assert(std::is_trivially_copyable<T>::value, "State type must be trivially copyable for seqlock");

 public:
  StatePublisher() = default;

  void publish(const T& state) noexcept {
    const uint32_t currentSeq = seq_.load(std::memory_order_relaxed);
    seq_.store(currentSeq + 1, std::memory_order_release); // Odd = writing in progress
    state_ = state;
    seq_.store(currentSeq + 2, std::memory_order_release); // Even = steady state
  }

  bool read(T& out) const noexcept {
    if (seq_.load(std::memory_order_relaxed) == 0) {
      return false; // Not published yet
    }
    for (int retry = 0; retry < 5; ++retry) {
      const uint32_t s1 = seq_.load(std::memory_order_acquire);
      if (s1 & 1) {
        continue; // Writing in progress, retry
      }
      out = state_;
      const uint32_t s2 = seq_.load(std::memory_order_acquire);
      if (s1 == s2) {
        return true; // Clean, uncorrupted read
      }
    }
    return false;
  }

 private:
  alignas(64) std::atomic<uint32_t> seq_{0};
  alignas(64) T state_{};
};

} // namespace hydra::dsp
