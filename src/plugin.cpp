#include <rack.hpp>

#include "network/transport.h"

using namespace rack;

plugin::Plugin* pluginInstance;

namespace {

struct CvPublisher : engine::Module {
  enum InputIds { CV_INPUT, GATE_INPUT, NUM_INPUTS };
  enum LightIds { STATUS_LIGHT, NUM_LIGHTS };

  std::string topic = "vcvb/cv";
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
    const bool sent = vcvb::BusRuntime::Instance().PublishCv(topic, state);
    lights[STATUS_LIGHT].setBrightness(sent ? 1.0f : 0.0f);
  }

  json_t* dataToJson() override {
    json_t* root = json_object();
    json_object_set_new(root, "topic", json_string(topic.c_str()));
    return root;
  }

  void dataFromJson(json_t* root) override {
    json_t* value = json_object_get(root, "topic");
    if (value && json_is_string(value)) topic = json_string_value(value);
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
    return root;
  }

  void dataFromJson(json_t* root) override {
    json_t* value = json_object_get(root, "topic");
    if (value && json_is_string(value)) {
      topic = json_string_value(value);
      subscription.SetTopic(topic);
    }
  }
};

struct CvPublisherWidget : app::ModuleWidget {
  CvPublisherWidget(CvPublisher* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Panel.svg")));
    addInput(createInputCentered<componentlibrary::PJ301MPort>(
        mm2px(Vec(15, 64)), module, CvPublisher::CV_INPUT));
  }

  void appendContextMenu(ui::Menu* menu) override {
    CvPublisher* publisher = dynamic_cast<CvPublisher*>(module);
    menu->addChild(new ui::MenuSeparator);
    menu->addChild(createMenuLabel("Topic: " + publisher->topic));
    menu->addChild(createMenuLabel("Rate: 500 Hz"));
    menu->addChild(createMenuItem(
        "Use topic vcvb/cv", CHECKMARK(publisher->topic == "vcvb/cv"),
        [=]() { publisher->topic = "vcvb/cv"; }));
    menu->addChild(createMenuItem(
        "Use topic integration/cv",
        CHECKMARK(publisher->topic == "integration/cv"),
        [=]() { publisher->topic = "integration/cv"; }));
  }
};

struct CvSubscriberWidget : app::ModuleWidget {
  CvSubscriberWidget(CvSubscriber* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Panel.svg")));
    addOutput(createOutputCentered<componentlibrary::PJ301MPort>(
        mm2px(Vec(15, 64)), module, CvSubscriber::CV_OUTPUT));
  }

  void appendContextMenu(ui::Menu* menu) override {
    CvSubscriber* subscriber = dynamic_cast<CvSubscriber*>(module);
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
