#pragma once
#include <cstddef>
#include <cstdint>

namespace spectrum {
// Seekable stream shared by SD firmware and host integration tests.
struct Reader {
  virtual ~Reader() = default;
  virtual size_t read(uint8_t *out, size_t count) = 0;
  virtual bool seek(uint32_t offset) = 0;
  virtual uint32_t position() const = 0;
  virtual uint32_t size() const = 0;
  bool exact(uint8_t *out, size_t n) { return read(out,n)==n; }
  bool skip(uint32_t n) {
    return position()<=size() && n<=size()-position() && seek(position()+n);
  }
};
inline uint16_t word(const uint8_t *p) { return p[0] | uint16_t(p[1])<<8; }
inline uint32_t triple(const uint8_t *p) { return word(p) | uint32_t(p[2])<<16; }
inline uint32_t dword(const uint8_t *p) { return triple(p) | uint32_t(p[3])<<24; }
}
