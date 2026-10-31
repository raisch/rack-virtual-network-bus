#ifndef VIRTUAL_NETWORK_BUS_SRC_NETWORK_TOPIC_H_
#define VIRTUAL_NETWORK_BUS_SRC_NETWORK_TOPIC_H_

#include <string>

namespace vcvb {

// Returns true when topic is a valid UTF-8, slash-separated topic.
bool IsValidTopic(const std::string& topic);

// Returns true only when both topics are valid and exactly equal.
bool TopicsMatch(const std::string& left, const std::string& right);

}  // namespace vcvb

#endif  // VIRTUAL_NETWORK_BUS_SRC_NETWORK_TOPIC_H_
