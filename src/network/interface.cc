#include "network/interface.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>

namespace vcvb {
namespace {

bool IsUsable(const NetworkInterface& interface) {
  return interface.is_up && !interface.is_loopback &&
         !interface.ipv4_address.empty() && interface.supports_multicast;
}

bool SortInterfaces(const NetworkInterface& left,
                    const NetworkInterface& right) {
  return left.name < right.name;
}

}  // namespace

std::vector<NetworkInterface> EnumerateNetworkInterfaces() {
  std::vector<NetworkInterface> interfaces;
  struct ifaddrs* addresses = NULL;
  if (getifaddrs(&addresses) != 0) return interfaces;

  for (struct ifaddrs* entry = addresses; entry != NULL;
       entry = entry->ifa_next) {
    if (entry->ifa_name == NULL || entry->ifa_addr == NULL ||
        entry->ifa_addr->sa_family != AF_INET)
      continue;

    NetworkInterface* record = NULL;
    for (std::size_t index = 0; index < interfaces.size(); ++index) {
      if (interfaces[index].name == entry->ifa_name) {
        record = &interfaces[index];
        break;
      }
    }
    if (record == NULL) {
      NetworkInterface value;
      value.name = entry->ifa_name;
      value.display_name = entry->ifa_name;
      value.is_up = (entry->ifa_flags & IFF_UP) != 0;
      value.is_loopback = (entry->ifa_flags & IFF_LOOPBACK) != 0;
      value.supports_multicast = (entry->ifa_flags & IFF_MULTICAST) != 0;
      interfaces.push_back(value);
      record = &interfaces.back();
    }

    sockaddr_in* address =
        reinterpret_cast<sockaddr_in*>(entry->ifa_addr);
    char text[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) != NULL &&
        record->ipv4_address.empty()) {
      record->ipv4_address = text;
    }
  }
  freeifaddrs(addresses);
  std::sort(interfaces.begin(), interfaces.end(), SortInterfaces);
  return interfaces;
}

bool ResolveNetworkInterface(const std::string& requested_name,
                             NetworkInterface* resolved,
                             std::string* error) {
  if (resolved == NULL || error == NULL) return false;
  const std::vector<NetworkInterface> interfaces =
      EnumerateNetworkInterfaces();
  if (requested_name.empty()) {
    for (std::size_t index = 0; index < interfaces.size(); ++index) {
      if (IsUsable(interfaces[index])) {
        *resolved = interfaces[index];
        return true;
      }
    }
    *error = "no usable multicast network interface";
    return false;
  }
  for (std::size_t index = 0; index < interfaces.size(); ++index) {
    if (interfaces[index].name == requested_name) {
      if (!IsUsable(interfaces[index])) {
        *error = "selected interface is unavailable";
        return false;
      }
      *resolved = interfaces[index];
      return true;
    }
  }
  *error = "selected interface was not found";
  return false;
}

}  // namespace vcvb
