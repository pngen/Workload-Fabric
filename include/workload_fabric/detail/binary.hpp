#pragma once
// Bounded binary writer/reader for the versioned persistence codec.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <optional>

namespace wf::detail {

struct CodecError : std::runtime_error { using std::runtime_error::runtime_error; };

class Writer {
 public:
  void u8(std::uint8_t v) { buf_.push_back(static_cast<char>(v)); }
  void u16(std::uint16_t v) { u8(v >> 8); u8(v & 0xff); }
  void u32(std::uint32_t v) { for (int i = 3; i >= 0; --i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
  void u64(std::uint64_t v) { for (int i = 7; i >= 0; --i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void f64(double v) { std::uint64_t bits; std::memcpy(&bits, &v, 8); u64(bits); }
  void bytes(const std::uint8_t* p, std::size_t n) { u64(n); if (n) buf_.insert(buf_.end(), p, p + n); }
  void bytes(const std::vector<std::uint8_t>& v) { bytes(v.data(), v.size()); }
  void str(const std::string& s) { u64(s.size()); buf_.insert(buf_.end(), s.begin(), s.end()); }
  template <class F> void u32(F fn) { u32(fn()); }
  void raw(const std::uint8_t* p, std::size_t n) { buf_.insert(buf_.end(), p, p + n); }
  std::size_t size() const noexcept { return buf_.size(); }
  std::vector<std::uint8_t> take() { return std::move(buf_); }
  const std::vector<std::uint8_t>& view() const noexcept { return buf_; }

 private:
  std::vector<std::uint8_t> buf_;
};

class Reader {
 public:
  explicit Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
  explicit Reader(const std::vector<std::uint8_t>& v) : p_(v.data()), n_(v.size()) {}

  bool empty() const noexcept { return pos_ >= n_; }
  std::size_t remaining() const noexcept { return n_ - pos_; }
  std::size_t position() const noexcept { return pos_; }

  std::uint8_t  u8() { need(1); return p_[pos_++]; }
  std::uint16_t u16() { need(2); std::uint16_t v0 = u8(); std::uint16_t v1 = u8(); return static_cast<std::uint16_t>((v0 << 8) | v1); }
  std::uint32_t u32() { need(4); std::uint32_t v0 = u8(); std::uint32_t v1 = u8(); std::uint32_t v2 = u8(); std::uint32_t v3 = u8(); return (v0 << 24) | (v1 << 16) | (v2 << 8) | v3; }
  std::uint64_t u64() { need(8); std::uint64_t r = 0; for (int i = 0; i < 8; ++i) r = (r << 8) | u8(); return r; }
  std::int64_t  i64() { return static_cast<std::int64_t>(u64()); }
  double        f64() { std::uint64_t bits = u64(); double d; std::memcpy(&d, &bits, 8); return d; }
  std::vector<std::uint8_t> bytes() { std::uint64_t n = u64(); if (n > remaining()) throw CodecError("length exceeds buffer"); need(static_cast<std::size_t>(n)); std::vector<std::uint8_t> out(p_ + pos_, p_ + pos_ + n); pos_ += n; return out; }
  std::string   str() { std::uint64_t n = u64(); if (n > remaining()) throw CodecError("string length exceeds buffer"); need(static_cast<std::size_t>(n)); std::string s(reinterpret_cast<const char*>(p_ + pos_), static_cast<std::size_t>(n)); pos_ += n; return s; }
  void raw(std::uint8_t* out, std::size_t n) { need(n); std::memcpy(out, p_ + pos_, n); pos_ += n; }

 private:
  void need(std::size_t n) const { if (n > n_ - pos_) throw CodecError("read past end"); }
  const std::uint8_t* p_;
  std::size_t n_;
  std::size_t pos_ = 0;
};

// FNV-1a 64-bit over a byte range. Deterministic, non-cryptographic integrity.
constexpr std::uint64_t fnv1a64(const std::uint8_t* p, std::size_t n) noexcept {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ULL; }
  return h;
}

}  // namespace wf::detail
