#include "network/socket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vcvb {

UdpSocket::UdpSocket() : handle_(-1) {}

UdpSocket::~UdpSocket() {
  Close();
}

bool UdpSocket::SetError(const std::string& message) const {
  last_error_ = message;
  return false;
}

bool UdpSocket::Open() {
  Close();
  handle_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (handle_ < 0) {
    return SetError(std::strerror(errno));
  }

  const int reuse_address = 1;
  if (setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                 sizeof(reuse_address)) < 0) {
    Close();
    return SetError(std::strerror(errno));
  }
#ifdef SO_REUSEPORT
  if (setsockopt(handle_, SOL_SOCKET, SO_REUSEPORT, &reuse_address,
                 sizeof(reuse_address)) < 0) {
    Close();
    return SetError(std::strerror(errno));
  }
#endif
  return true;
}

bool UdpSocket::Bind(std::uint16_t port) {
  if (handle_ < 0) {
    return SetError("socket is not open");
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (bind(handle_, reinterpret_cast<const sockaddr*>(&address),
           sizeof(address)) < 0) {
    return SetError(std::strerror(errno));
  }
  return true;
}

bool UdpSocket::JoinMulticast(const std::string& group,
                              const std::string& interface_address) {
  ip_mreq membership = {};
  if (inet_pton(AF_INET, group.c_str(), &membership.imr_multiaddr) != 1) {
    return SetError("invalid multicast group");
  }
  if (interface_address.empty() ||
      inet_pton(AF_INET, interface_address.c_str(),
                &membership.imr_interface) != 1) {
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
  }
  if (setsockopt(handle_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
                 sizeof(membership)) < 0) {
    return SetError(std::strerror(errno));
  }
  return true;
}

bool UdpSocket::LeaveMulticast(const std::string& group,
                               const std::string& interface_address) {
  ip_mreq membership = {};
  if (inet_pton(AF_INET, group.c_str(), &membership.imr_multiaddr) != 1) {
    return SetError("invalid multicast group");
  }
  if (interface_address.empty() ||
      inet_pton(AF_INET, interface_address.c_str(),
                &membership.imr_interface) != 1) {
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
  }
  if (setsockopt(handle_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &membership,
                 sizeof(membership)) < 0) {
    return SetError(std::strerror(errno));
  }
  return true;
}

bool UdpSocket::Poll(int timeout_ms) const {
  if (handle_ < 0) {
    return SetError("socket is not open");
  }
  pollfd descriptor = {};
  descriptor.fd = handle_;
  descriptor.events = POLLIN;
  const int result = poll(&descriptor, 1, timeout_ms);
  if (result < 0 && errno != EINTR) {
    return SetError(std::strerror(errno));
  }
  return result > 0 && (descriptor.revents & POLLIN) != 0;
}

int UdpSocket::Receive(std::uint8_t* buffer, std::size_t capacity,
                       std::string* source_address,
                       std::uint16_t* source_port) const {
  if (handle_ < 0 || buffer == NULL || source_address == NULL ||
      source_port == NULL) {
    last_error_ = "invalid receive arguments";
    return -1;
  }
  sockaddr_in source = {};
  socklen_t source_size = sizeof(source);
  const ssize_t received = recvfrom(
      handle_, buffer, capacity, 0,
      reinterpret_cast<sockaddr*>(&source), &source_size);
  if (received < 0) {
    last_error_ = std::strerror(errno);
    return -1;
  }
  char address[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, &source.sin_addr, address, sizeof(address)) == NULL) {
    last_error_ = std::strerror(errno);
    return -1;
  }
  *source_address = address;
  *source_port = ntohs(source.sin_port);
  return static_cast<int>(received);
}

bool UdpSocket::SendTo(const std::uint8_t* buffer, std::size_t size,
                       const std::string& address,
                       std::uint16_t port) const {
  if (handle_ < 0 || buffer == NULL || size == 0) {
    return SetError("invalid send arguments");
  }
  sockaddr_in destination = {};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);
  if (inet_pton(AF_INET, address.c_str(), &destination.sin_addr) != 1) {
    return SetError("invalid destination address");
  }
  const ssize_t sent =
      sendto(handle_, buffer, size, 0,
             reinterpret_cast<const sockaddr*>(&destination),
             sizeof(destination));
  if (sent != static_cast<ssize_t>(size)) {
    return SetError(sent < 0 ? std::strerror(errno) : "short UDP send");
  }
  return true;
}

void UdpSocket::Close() {
  if (handle_ >= 0) {
    close(handle_);
    handle_ = -1;
  }
}

}  // namespace vcvb
