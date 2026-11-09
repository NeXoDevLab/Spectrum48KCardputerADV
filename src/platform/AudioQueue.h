#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace spectrum_audio {
// One emulation producer, one playback consumer. Ownership changes only at
// packet boundaries; no allocation or speaker calls in the CPU sample callback.
class Queue {
public:
  static constexpr std::size_t kSamples=160,kPackets=16;
  void push(int16_t sample) {
    partial_[used_++]=sample;
    if(used_!=kSamples) return;
    const auto write=write_.load(std::memory_order_relaxed);
    if(write-read_.load(std::memory_order_acquire)<kPackets) {
      std::memcpy(packets_[write%kPackets].data(),partial_.data(),sizeof(partial_));
      write_.store(write+1,std::memory_order_release);
    } else dropped_.fetch_add(kSamples,std::memory_order_relaxed);
    used_=0;
  }
  bool pop(int16_t *out) {
    const auto read=read_.load(std::memory_order_relaxed);
    if(read==write_.load(std::memory_order_acquire)) return false;
    std::memcpy(out,packets_[read%kPackets].data(),sizeof(partial_));
    read_.store(read+1,std::memory_order_release);return true;
  }
  uint32_t packets() const {
    // Consumer uses read first; producer never runs backwards.
    const auto read=read_.load(std::memory_order_acquire);
    return write_.load(std::memory_order_acquire)-read;
  }
  // Call from producer only: includes the partial packet not yet published.
  uint32_t buffered() const {return packets()*kSamples+used_;}
  uint32_t dropped() const {return dropped_.load(std::memory_order_relaxed);}
  // Both threads must be paused before resetting, e.g. while opening the menu.
  void clear() {used_=0;read_.store(0);write_.store(0);}
private:
  std::array<std::array<int16_t,kSamples>,kPackets> packets_{};
  std::array<int16_t,kSamples> partial_{};
  uint32_t used_=0;
  std::atomic<uint32_t> read_{0},write_{0},dropped_{0};
};
}
