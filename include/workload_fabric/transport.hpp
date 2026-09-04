#pragma once
// Framed transport abstraction. The runtime speaks a single framed message
// protocol over a peer channel. The loopback transport is implemented over Winsock
// TCP; the interface lets tests swap in an in-memory transport.
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace wf {

// A byte-channel that carries length-prefixed frames. Frames are bounded: frames
// larger than the configured maximum are rejected, never read unbounded.
class FramePeer {
 public:
  virtual ~FramePeer() = default;
  // Send one frame. Returns false on failure / closed peer.
  virtual bool sendFrame(const std::vector<std::uint8_t>& frame) = 0;
  // Receive one frame. Returns false on closed peer / timeout / protocol error.
  virtual bool receiveFrame(std::vector<std::uint8_t>& out, int timeoutMs = -1) = 0;
  virtual void close() = 0;
  virtual std::string describe() const = 0;
};

// Maximum accepted frame size (bounded decoding).
inline constexpr std::size_t kMaxFrameSize = 1u << 20;  // 1 MiB

// A TCP listen socket that produces connected FramePeers.
class FrameListener {
 public:
  virtual ~FrameListener() = default;
  virtual bool bind(std::string address, std::uint16_t port) = 0;
  virtual std::uint16_t boundPort() const = 0;
  // Accept a connection. timeoutMs < 0 blocks; 0 returns immediately; > 0 waits.
  virtual FramePeer* accept(int timeoutMs = -1) = 0;
  virtual void close() = 0;
  virtual std::string describe() const = 0;
};

// Factory: create a loopback TCP listener on 127.0.0.1 (port 0 -> ephemeral).
FrameListener* createTcpListener(std::uint16_t port = 0);
// Factory: connect a loopback TCP peer to a listening coordinator.
FramePeer* connectTcpPeer(std::string address, std::uint16_t port, int connectTimeoutMs = 5000);

// Release transport resources once (Winsock startup). Idempotent.
void transportShutdown();

}  // namespace wf
