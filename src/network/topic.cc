#include "network/topic.h"

#include <cstddef>
#include <cstdint>

namespace vcvb {
namespace {

const std::size_t kMaxTopicBytes = 255;

bool IsContinuationByte(std::uint8_t value) {
  return (value & 0xc0u) == 0x80u;
}

bool IsValidUtf8(const std::string& value) {
  for (std::size_t index = 0; index < value.size();) {
    const std::uint8_t first =
        static_cast<std::uint8_t>(value[index]);
    std::size_t sequence_length = 0;
    std::uint32_t code_point = 0;

    if (first <= 0x7fu) {
      ++index;
      continue;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
      sequence_length = 2;
      code_point = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
      sequence_length = 3;
      code_point = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
      sequence_length = 4;
      code_point = first & 0x07u;
    } else {
      return false;
    }

    if (index + sequence_length > value.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset < sequence_length; ++offset) {
      const std::uint8_t continuation =
          static_cast<std::uint8_t>(value[index + offset]);
      if (!IsContinuationByte(continuation)) {
        return false;
      }
      code_point = (code_point << 6) | (continuation & 0x3fu);
    }

    if ((sequence_length == 3 && code_point < 0x800u) ||
        (sequence_length == 4 && code_point < 0x10000u) ||
        (code_point >= 0xd800u && code_point <= 0xdfffu) ||
        code_point > 0x10ffffu) {
      return false;
    }
    index += sequence_length;
  }
  return true;
}

}  // namespace

bool IsValidTopic(const std::string& topic) {
  if (topic.empty() || topic.size() > kMaxTopicBytes ||
      !IsValidUtf8(topic)) {
    return false;
  }

  bool component_has_value = false;
  for (std::size_t index = 0; index < topic.size(); ++index) {
    const std::uint8_t value = static_cast<std::uint8_t>(topic[index]);
    if (value < 0x20u || value == 0x7fu) {
      return false;
    }
    if (topic[index] == '/') {
      if (!component_has_value) {
        return false;
      }
      component_has_value = false;
    } else {
      component_has_value = true;
    }
  }
  return component_has_value;
}

bool TopicsMatch(const std::string& left, const std::string& right) {
  return IsValidTopic(left) && IsValidTopic(right) && left == right;
}

}  // namespace vcvb
