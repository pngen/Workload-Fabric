#include "workload_fabric/transport.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <stdexcept>

#pragma comment(lib, "ws2_32.lib")

namespace wf {

namespace {
std::once_flag g_wsaFlag;
std::atomic<bool> g_wsaReady{false};
void ensureWsa() {
  std::call_once(g_wsaFlag, [] {
    WSADATA d{};
    if (WSAStartup(MAKEWORD(2, 2), &d) == 0) g_wsaReady.store(true);
  });
}
}  // namespace

void transportShutdown() {
  if (g_wsaReady.load()) { WSACleanup(); g_wsaReady.store(false); }
}

namespace {

void writeAll(SOCKET s, const char* p, int n) {
  int sent = 0;
  while (sent < n) {
    int r = ::send(s, p + sent, n - sent, 0);
    if (r == SOCKET_ERROR) throw std::runtime_error("send error");
    if (r == 0) throw std::runtime_error("send closed");
    sent += r;
  }
}

bool readExact(SOCKET s, char* p, int n, int timeoutMs) {
  int got = 0;
  while (got < n) {
    int r;
    if (timeoutMs < 0) {
      // Blocking read: no need to poll readability.
      r = ::recv(s, p + got, n - got, 0);
    } else {
      fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
      timeval tv; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
      int sel = ::select(0, &rf, nullptr, nullptr, &tv);
      if (sel == 0) return false;          // timeout
      if (sel == SOCKET_ERROR) return false;
      r = ::recv(s, p + got, n - got, 0);
    }
    if (r == 0) return false;              // peer closed
    if (r == SOCKET_ERROR) return false;
    got += r;
  }
  return true;
}

class TcpPeer final : public FramePeer {
 public:
  explicit TcpPeer(SOCKET s, std::string desc) : sock_(s), desc_(std::move(desc)) {}
  ~TcpPeer() override { close(); }

  bool sendFrame(const std::vector<std::uint8_t>& frame) override {
    if (frame.size() > kMaxFrameSize) return false;
    try {
      std::uint32_t len = static_cast<std::uint32_t>(frame.size());
      char hdr[4];
      hdr[0] = static_cast<char>((len >> 24) & 0xff);
      hdr[1] = static_cast<char>((len >> 16) & 0xff);
      hdr[2] = static_cast<char>((len >> 8) & 0xff);
      hdr[3] = static_cast<char>(len & 0xff);
      writeAll(sock_, hdr, 4);
      if (len) writeAll(sock_, reinterpret_cast<const char*>(frame.data()), static_cast<int>(len));
      return true;
    } catch (...) { return false; }
  }

  bool receiveFrame(std::vector<std::uint8_t>& out, int timeoutMs) override {
    char hdr[4];
    if (!readExact(sock_, hdr, 4, timeoutMs)) return false;
    std::uint32_t len = (static_cast<std::uint32_t>(static_cast<unsigned char>(hdr[0])) << 24) |
                        (static_cast<std::uint32_t>(static_cast<unsigned char>(hdr[1])) << 16) |
                        (static_cast<std::uint32_t>(static_cast<unsigned char>(hdr[2])) << 8) |
                        static_cast<std::uint32_t>(static_cast<unsigned char>(hdr[3]));
    if (len > kMaxFrameSize) return false;  // bounded decode
    out.resize(len);
    if (len && !readExact(sock_, reinterpret_cast<char*>(out.data()), static_cast<int>(len), timeoutMs)) return false;
    return true;
  }

  void close() override { if (sock_ != INVALID_SOCKET) { ::closesocket(sock_); sock_ = INVALID_SOCKET; } }
  std::string describe() const override { return "tcp:" + desc_; }

 private:
  SOCKET sock_ = INVALID_SOCKET;
  std::string desc_;
};

class TcpListener final : public FrameListener {
 public:
  TcpListener() = default;
  ~TcpListener() override { close(); }

  bool bind(std::string address, std::uint16_t port) override {
    ensureWsa();
    listen_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_ == INVALID_SOCKET) return false;
    BOOL opt = TRUE;
    ::setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, address.c_str(), &addr.sin_addr) != 1) return false;
    if (::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) return false;
    if (::listen(listen_, 16) == SOCKET_ERROR) return false;
    sockaddr_in local{}; int lsz = sizeof(local);
    if (::getsockname(listen_, reinterpret_cast<sockaddr*>(&local), &lsz) == 0) port_ = ntohs(local.sin_port);
    return true;
  }
  std::uint16_t boundPort() const override { return port_; }
  FramePeer* accept(int timeoutMs) override {
    if (timeoutMs >= 0) {
      fd_set rf; FD_ZERO(&rf); FD_SET(listen_, &rf);
      timeval tv; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
      int sel = ::select(0, &rf, nullptr, nullptr, &tv);
      if (sel <= 0) return nullptr;
    }
    sockaddr_in remote{}; int rsz = sizeof(remote);
    SOCKET c = ::accept(listen_, reinterpret_cast<sockaddr*>(&remote), &rsz);
    if (c == INVALID_SOCKET) return nullptr;
    return new TcpPeer(c, std::to_string(ntohs(remote.sin_port)));
  }
  void close() override { if (listen_ != INVALID_SOCKET) { ::closesocket(listen_); listen_ = INVALID_SOCKET; } }
  std::string describe() const override { return "tcp:127.0.0.1:" + std::to_string(port_); }

 private:
  SOCKET listen_ = INVALID_SOCKET;
  std::uint16_t port_ = 0;
};

}  // namespace

FrameListener* createTcpListener(std::uint16_t port) {
  ensureWsa();
  auto* l = new TcpListener();
  if (!l->bind("127.0.0.1", port)) { delete l; return nullptr; }
  return l;
}

FramePeer* connectTcpPeer(std::string address, std::uint16_t port, int connectTimeoutMs) {
  ensureWsa();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return nullptr;
  u_long nonblock = 1;
  ::ioctlsocket(s, FIONBIO, &nonblock);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (InetPtonA(AF_INET, address.c_str(), &addr.sin_addr) != 1) { ::closesocket(s); return nullptr; }
  int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc != 0) {
    fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
    timeval tv; tv.tv_sec = connectTimeoutMs / 1000; tv.tv_usec = (connectTimeoutMs % 1000) * 1000;
    int sel = ::select(0, nullptr, &wf, nullptr, &tv);
    if (sel <= 0) { ::closesocket(s); return nullptr; }
    int err = 0; int el = sizeof(err);
    ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &el);
    if (err != 0) { ::closesocket(s); return nullptr; }
  }
  u_long blocking = 0;
  ::ioctlsocket(s, FIONBIO, &blocking);
  return new TcpPeer(s, std::to_string(port));
}

}  // namespace wf
