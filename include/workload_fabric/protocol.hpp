#pragma once
// Framed wire protocol between the coordinator and workers. Every message is a
// length-prefixed frame carrying a tagged, versioned body. Decoding is bounded
// (string lengths checked against remaining bytes) so a malformed frame cannot
// drive unbounded allocation.
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include "workload_fabric/detail/binary.hpp"

namespace wf {

enum class MsgType : std::uint8_t {
  HELLO = 0,     // worker -> coordinator: workerId, bootId
  REGISTER = 1,  // coordinator -> worker: coordinatorId
  DISPATCH = 2,  // coordinator -> worker: episode + work descriptor
  PROGRESS = 3,  // worker -> coordinator: delta progress for episode
  COMPLETE = 4,  // worker -> coordinator: authoritative episode completion
  CANCEL = 5,    // coordinator -> worker: cancel episode
  SHUTDOWN = 6,  // coordinator -> worker: clean shutdown
  PING = 7,      // liveness probe
};

constexpr char kProtocolMagic[4] = {'W','F','P','1'};

struct Message {
  MsgType       type = MsgType::PING;
  std::uint64_t workloadId = 0;
  std::uint64_t episodeId = 0;
  std::uint64_t episodeGen = 0;
  std::uint64_t workloadGen = 0;
  std::uint64_t workerId = 0;
  std::uint64_t bootId = 0;
  std::uint64_t value = 0;     // delta, attempt generation, etc.
  bool          flag1 = false;
  bool          flag2 = false;
  std::string   payload;       // work descriptor / intent token
  std::string   extra;         // optional detail
};

// Encode a protocol message into a frame body (no length prefix).
void encodeMessage(const Message& m, std::vector<std::uint8_t>& out);

// Decode a frame body. Throws ProtocolError on malformed / unbounded input.
struct ProtocolError : std::runtime_error { using std::runtime_error::runtime_error; };
Message decodeMessage(const std::uint8_t* p, std::size_t n);

}  // namespace wf
