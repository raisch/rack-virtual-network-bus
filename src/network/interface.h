#ifndef VIRTUAL_NETWORK_BUS_SRC_NETWORK_INTERFACE_H_
#define VIRTUAL_NETWORK_BUS_SRC_NETWORK_INTERFACE_H_

#include <string>
#include <vector>

namespace vcvb {

struct NetworkInterface {
  std::string name;
  std::string display_name;
  std::string ipv4_address;
  bool is_up = false;
  bool is_loopback = false;
  bool supports_multicast = false;
};

std::vector<NetworkInterface> EnumerateNetworkInterfaces();
bool ResolveNetworkInterface(const std::string& requested_name,
                             NetworkInterface* resolved,
                             std::string* error);

}  // namespace vcvb

#endif  // VIRTUAL_NETWORK_BUS_SRC_NETWORK_INTERFACE_H_
