#ifndef VIRTUAL_NETWORK_BUS_SRC_NETWORK_SOCKET_H_
#define VIRTUAL_NETWORK_BUS_SRC_NETWORK_SOCKET_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace vcvb {

class UdpSocket {
 public:
  UdpSocket();
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  bool Open();
  bool Bind(std::uint16_t port);
  bool JoinMulticast(const std::string& group,
                     const std::string& interface_address);
  bool LeaveMulticast(const std::string& group,
                      const std::string& interface_address);
  bool Poll(int timeout_ms) const;
  int Receive(std::uint8_t* buffer, std::size_t capacity,
              std::string* source_address, std::uint16_t* source_port) const;
  bool SendTo(const std::uint8_t* buffer, std::size_t size,
              const std::string& address, std::uint16_t port) const;
  void Close();

  const std::string& last_error() const { return last_error_; }

 private:
  bool SetError(const std::string& message) const;

  int handle_;
  mutable std::string last_error_;
};

}  // namespace vcvb

#endif  // VIRTUAL_NETWORK_BUS_SRC_NETWORK_SOCKET_H_
