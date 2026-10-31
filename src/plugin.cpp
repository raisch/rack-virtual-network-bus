#include <rack.hpp>

#include "network/transport.h"

#include <functional>
#include <vector>

using namespace rack;

plugin::Plugin* pluginInstance;

namespace {

struct CvPublisher : engine::Module {
  enum InputIds { CV_INPUT, GATE_INPUT, NUM_INPUTS };
  enum LightIds { STATUS_LIGHT, NUM_LIGHTS };

  std::string host_name = "localhost";
  std::vector<std::string> topics = {"vcvb/cv"};
  std::uint64_t sample_count = 0;
  std::uint64_t sequence = 0;

  CvPublisher() {
    config(0, NUM_INPUTS, 0, NUM_LIGHTS);
    configInput(CV_INPUT, "Polyphonic CV");
    configInput(GATE_INPUT, "Gate input");
    vcvb::BusRuntime::Instance().Start();
  }

  void process(const ProcessArgs& args) override {
    const std::uint64_t interval =
        static_cast<std::uint64_t>(std::max(1.0, args.sampleRate / 500.0));
    if (++sample_count < interval) return;
    sample_count = 0;

    vcvb::CvState state;
    state.stream_id = 1;
    state.sender_time_ns = static_cast<std::uint64_t>(args.sampleTime * 1e9) *
                           sequence;
    state.channel_count = static_cast<std::uint8_t>(
        std::max(1, inputs[CV_INPUT].getChannels()));
    for (std::size_t index = 0; index < state.channel_count; ++index)
      state.voltages[index] = inputs[CV_INPUT].getVoltage(static_cast<int>(index));
    ++sequence;
    std::string topic = "vcvb/cv";
    for (std::size_t index = 0; index < topics.size(); ++index) {
      if (!topics[index].empty()) {
        topic = topics[index];
        break;
      }
    }
    const bool sent = vcvb::BusRuntime::Instance().PublishCv(topic, state);
    lights[STATUS_LIGHT].setBrightness(sent ? 1.0f : 0.0f);
  }

  json_t* dataToJson() override {
    json_t* root = json_object();
    json_object_set_new(root, "hostName", json_string(host_name.c_str()));
    json_object_set_new(
        root, "networkInterface",
        json_string(vcvb::BusRuntime::Instance().RequestedInterface().c_str()));
    json_t* topics_json = json_array();
    for (std::size_t index = 0; index < topics.size(); ++index)
      json_array_append_new(topics_json, json_string(topics[index].c_str()));
    json_object_set_new(root, "topics", topics_json);
    return root;
  }

  void dataFromJson(json_t* root) override {
    json_t* interface_value = json_object_get(root, "networkInterface");
    if (interface_value && json_is_string(interface_value))
      vcvb::BusRuntime::Instance().SetInterface(
          json_string_value(interface_value));
    json_t* host = json_object_get(root, "hostName");
    if (host && json_is_string(host)) host_name = json_string_value(host);
    json_t* topics_json = json_object_get(root, "topics");
    if (topics_json && json_is_array(topics_json)) {
      topics.clear();
      size_t index;
      json_t* topic;
      json_array_foreach(topics_json, index, topic) {
        if (json_is_string(topic) && topics.size() < 3)
          topics.push_back(json_string_value(topic));
      }
    }
  }
};

struct CvSubscriber : engine::Module {
  enum OutputIds { CV_OUTPUT, NUM_OUTPUTS };
  enum LightIds { STATUS_LIGHT, NUM_LIGHTS };

  std::string topic = "vcvb/cv";
  vcvb::CvSubscription subscription;
  std::array<float, vcvb::kMaxCvChannels> voltages = {};
  int channels = 1;
  std::uint64_t samples_since_frame = 0;

  static vcvb::PublisherBinding MakeBinding() {
    vcvb::PublisherBinding binding;
    binding.topic = "vcvb/cv";
    return binding;
  }

  CvSubscriber() : subscription(MakeBinding()) {
    config(0, 0, NUM_OUTPUTS, NUM_LIGHTS);
    configOutput(CV_OUTPUT, "Polyphonic CV");
    vcvb::BusRuntime::Instance().Register(&subscription);
  }

  ~CvSubscriber() override {
    vcvb::BusRuntime::Instance().Unregister(&subscription);
  }

  void process(const ProcessArgs& args) override {
    vcvb::CvFrame frame;
    bool received = false;
    while (subscription.Pop(&frame)) {
      received = true;
      channels = frame.state.channel_count;
      for (int index = 0; index < channels; ++index)
        voltages[index] = frame.state.voltages[index];
    }
    samples_since_frame += 1;
    if (received) samples_since_frame = 0;
    const std::uint64_t timeout =
        static_cast<std::uint64_t>(args.sampleRate * 0.5);
    if (samples_since_frame >= timeout) {
      channels = 1;
      voltages.fill(0.0f);
    }
    outputs[CV_OUTPUT].setChannels(channels);
    for (int index = 0; index < channels; ++index)
      outputs[CV_OUTPUT].setVoltage(voltages[index], index);
    lights[STATUS_LIGHT].setBrightness(received ? 1.0f : 0.0f);
  }

  json_t* dataToJson() override {
    json_t* root = json_object();
    json_object_set_new(root, "topic", json_string(topic.c_str()));
    json_object_set_new(
        root, "networkInterface",
        json_string(vcvb::BusRuntime::Instance().RequestedInterface().c_str()));
    return root;
  }

  void dataFromJson(json_t* root) override {
    json_t* interface_value = json_object_get(root, "networkInterface");
    if (interface_value && json_is_string(interface_value))
      vcvb::BusRuntime::Instance().SetInterface(
          json_string_value(interface_value));
    json_t* value = json_object_get(root, "topic");
    if (value && json_is_string(value)) {
      topic = json_string_value(value);
      subscription.SetTopic(topic);
    }
  }
};

struct NetworkTextField : ui::TextField {
  std::function<void(const std::string&)> changed;

  void onSelectKey(const SelectKeyEvent& event) override {
    ui::TextField::onSelectKey(event);
    if (changed) changed(text);
  }
};

struct TopicButton : ui::Button {
  std::function<void()> action;

  void onAction(const ActionEvent& event) override {
    if (action) action();
  }
};

void AddNetworkInterfaceMenu(ui::Menu* menu) {
  vcvb::BusRuntime& runtime = vcvb::BusRuntime::Instance();
  menu->addChild(new ui::MenuSeparator);
  menu->addChild(createMenuLabel("Network interface"));
  const std::string requested = runtime.RequestedInterface();
  const std::string resolved = runtime.ResolvedInterface();
  const std::string status =
      requested.empty() ? "Automatic" :
      string::f("Selected: %s", requested.c_str());
  menu->addChild(createMenuLabel(
      resolved.empty() ? status : status + " -> " + resolved));
  const std::string error = runtime.InterfaceError();
  if (!error.empty()) menu->addChild(createMenuLabel("Error: " + error));
  menu->addChild(createMenuItem(
      "Automatic", CHECKMARK(requested.empty()), []() {
        vcvb::BusRuntime::Instance().SetInterface("");
      }));
  const std::vector<vcvb::NetworkInterface> interfaces = runtime.Interfaces();
  for (std::size_t index = 0; index < interfaces.size(); ++index) {
    const vcvb::NetworkInterface& item = interfaces[index];
    if (!item.is_up || item.is_loopback || item.ipv4_address.empty() ||
        !item.supports_multicast)
      continue;
    const std::string label = item.display_name + " (" + item.name + ") - " +
                              item.ipv4_address;
    const std::string name = item.name;
    menu->addChild(createMenuItem(
        label, CHECKMARK(requested == name), [name]() {
          vcvb::BusRuntime::Instance().SetInterface(name);
        }));
  }
}

struct CvPublisherWidget : app::ModuleWidget {
  NetworkTextField* host_field = NULL;
  NetworkTextField* topic_fields[3] = {};

  CvPublisherWidget(CvPublisher* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Panel.svg")));
    ui::Label* host_label = createWidget<ui::Label>(mm2px(Vec(3, 10)));
    host_label->text = "Host";
    host_label->fontSize = 10;
    addChild(host_label);
    host_field = createWidget<NetworkTextField>(mm2px(Vec(3, 14)));
    host_field->box.size = mm2px(Vec(24, 6));
    host_field->setText(module->host_name);
    host_field->changed = [module](const std::string& value) {
      module->host_name = value;
    };
    addChild(host_field);

    ui::Label* topics_label = createWidget<ui::Label>(mm2px(Vec(3, 23)));
    topics_label->text = "Topics";
    topics_label->fontSize = 10;
    addChild(topics_label);
    for (int index = 0; index < 3; ++index) {
      topic_fields[index] =
          createWidget<NetworkTextField>(mm2px(Vec(3, 27 + index * 8)));
      topic_fields[index]->box.size = mm2px(Vec(19, 6));
      if (index < static_cast<int>(module->topics.size()))
        topic_fields[index]->setText(module->topics[index]);
      topic_fields[index]->changed = [module, index](const std::string& value) {
        while (module->topics.size() <= static_cast<std::size_t>(index))
          module->topics.push_back("");
        module->topics[index] = value;
      };
      addChild(topic_fields[index]);

      TopicButton* delete_button =
          createWidget<TopicButton>(mm2px(Vec(23, 27 + index * 8)));
      delete_button->box.size = mm2px(Vec(4, 6));
      delete_button->text = "-";
      delete_button->action = [module, index, this]() {
        if (index < static_cast<int>(module->topics.size())) {
          module->topics[index].clear();
          topic_fields[index]->setText("");
        }
      };
      addChild(delete_button);
    }
    TopicButton* add_button = createWidget<TopicButton>(mm2px(Vec(3, 53)));
    add_button->box.size = mm2px(Vec(24, 6));
    add_button->text = "Add topic";
    add_button->action = [module, this]() {
      for (std::size_t index = 0; index < 3; ++index) {
        if (index >= module->topics.size())
          module->topics.push_back("");
        if (module->topics[index].empty()) {
          module->topics[index] =
              "vcvb/topic-" + std::to_string(index + 1);
          topic_fields[index]->setText(module->topics[index]);
          break;
        }
      }
    };
    addChild(add_button);
    addInput(createInputCentered<componentlibrary::PJ301MPort>(
        mm2px(Vec(15, 69)), module, CvPublisher::CV_INPUT));
  }

  void appendContextMenu(ui::Menu* menu) override {
    CvPublisher* publisher = dynamic_cast<CvPublisher*>(module);
    AddNetworkInterfaceMenu(menu);
    menu->addChild(new ui::MenuSeparator);
    menu->addChild(createMenuLabel("Host: " + publisher->host_name));
    menu->addChild(createMenuLabel("Topics: " +
                                   std::to_string(publisher->topics.size())));
    menu->addChild(createMenuLabel("Rate: 500 Hz"));
    const std::string first_topic =
        publisher->topics.empty() ? "" : publisher->topics[0];
    menu->addChild(createMenuItem(
        "Use topic vcvb/cv", CHECKMARK(first_topic == "vcvb/cv"),
        [=]() {
          if (publisher->topics.empty()) publisher->topics.push_back("vcvb/cv");
          publisher->topics[0] = "vcvb/cv";
        }));
    menu->addChild(createMenuItem(
        "Use topic integration/cv",
        CHECKMARK(first_topic == "integration/cv"),
        [=]() {
          if (publisher->topics.empty())
            publisher->topics.push_back("integration/cv");
          publisher->topics[0] = "integration/cv";
        }));
  }
};

struct CvSubscriberWidget : app::ModuleWidget {
  CvSubscriberWidget(CvSubscriber* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Panel.svg")));
    ui::Label* topic_label = createWidget<ui::Label>(mm2px(Vec(3, 14)));
    topic_label->text = "Topic";
    topic_label->fontSize = 10;
    addChild(topic_label);
    NetworkTextField* topic_field =
        createWidget<NetworkTextField>(mm2px(Vec(3, 18)));
    topic_field->box.size = mm2px(Vec(24, 6));
    topic_field->setText(module->topic);
    topic_field->changed = [module](const std::string& value) {
      module->topic = value;
      module->subscription.SetTopic(value);
    };
    addChild(topic_field);
    addOutput(createOutputCentered<componentlibrary::PJ301MPort>(
        mm2px(Vec(15, 69)), module, CvSubscriber::CV_OUTPUT));
  }

  void appendContextMenu(ui::Menu* menu) override {
    CvSubscriber* subscriber = dynamic_cast<CvSubscriber*>(module);
    AddNetworkInterfaceMenu(menu);
    menu->addChild(new ui::MenuSeparator);
    menu->addChild(createMenuLabel("Topic: " + subscriber->topic));
    menu->addChild(createMenuLabel(
        string::f("Queue drops: %llu",
                  static_cast<unsigned long long>(
                      subscriber->subscription.overflow()))));
    menu->addChild(createMenuItem(
        "Use topic vcvb/cv", CHECKMARK(subscriber->topic == "vcvb/cv"),
        [=]() {
          subscriber->topic = "vcvb/cv";
          subscriber->subscription.SetTopic(subscriber->topic);
        }));
    menu->addChild(createMenuItem(
        "Use topic integration/cv",
        CHECKMARK(subscriber->topic == "integration/cv"),
        [=]() {
          subscriber->topic = "integration/cv";
          subscriber->subscription.SetTopic(subscriber->topic);
        }));
  }
};

plugin::Model* modelCvPublisher =
    createModel<CvPublisher, CvPublisherWidget>("CVPublisher");
plugin::Model* modelCvSubscriber =
    createModel<CvSubscriber, CvSubscriberWidget>("CVSubscriber");

}  // namespace

void init(plugin::Plugin* plugin) {
  pluginInstance = plugin;
  plugin->addModel(modelCvPublisher);
  plugin->addModel(modelCvSubscriber);
}
