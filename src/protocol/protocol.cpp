#include "workload_fabric/protocol.hpp"

namespace wf {

void encodeMessage(const Message& m, std::vector<std::uint8_t>& out) {
  detail::Writer w;
  w.raw(reinterpret_cast<const std::uint8_t*>(kProtocolMagic), 4);
  w.u8(static_cast<std::uint8_t>(m.type));
  w.u64(m.workloadId);
  w.u64(m.episodeId);
  w.u64(m.episodeGen);
  w.u64(m.workloadGen);
  w.u64(m.workerId);
  w.u64(m.bootId);
  w.u64(m.value);
  w.u8(m.flag1 ? 1 : 0);
  w.u8(m.flag2 ? 1 : 0);
  w.str(m.payload);
  w.str(m.extra);
  out = w.take();
}

Message decodeMessage(const std::uint8_t* p, std::size_t n) {
  if (n < 4) throw ProtocolError("frame too small");
  for (int i = 0; i < 4; ++i) if (p[i] != static_cast<std::uint8_t>(kProtocolMagic[i])) throw ProtocolError("bad magic");
  try {
    detail::Reader body(p + 4, n - 4);
    Message m;
    std::uint8_t t = body.u8();
    if (t > 7) throw ProtocolError("bad message type");
    m.type = static_cast<MsgType>(t);
    m.workloadId = body.u64();
    m.episodeId = body.u64();
    m.episodeGen = body.u64();
    m.workloadGen = body.u64();
    m.workerId = body.u64();
    m.bootId = body.u64();
    m.value = body.u64();
    m.flag1 = body.u8() != 0;
    m.flag2 = body.u8() != 0;
    m.payload = body.str();
    m.extra = body.str();
    return m;
  } catch (detail::CodecError const& e) {
    throw ProtocolError(e.what());
  }
}

}  // namespace wf
