#include <Config.h>
#include <Log.h>
#include <MqttPaho.h>
#include <NanoAkka.h>
#include <stdio.h>
#include <unistd.h>
class Poller {
 public:
  TimerSource _clock;
  int _idx;
  std::vector<Requestable*> _requestables;

  Poller(Thread& thread, uint32_t interval)
      : _clock(thread, 1, interval, true) {
    _clock >> [&](const TimerMsg& t) {
      if (_requestables.size()) {
        _idx++;
        if (_idx >= _requestables.size()) _idx = 0;
        _requestables.at(_idx)->request();
      };
    };
  };
  void operator>>(Requestable& requestee) {
    _requestables.push_back(&requestee);
  }
};

template <class T>
Cache<T>& newCache(Thread& t, uint32_t min, uint32_t max) {
  auto cache = new Cache<T>(t, min, max);
  return *cache;
}

Log logger(2048);
Thread mainThread("main");
MqttPaho mqtt(mainThread);
StaticJsonDocument<10240> jsonDoc;
Poller poller(mainThread, 1000);

LambdaSource<uint64_t> systemTime([]() { return Sys::millis(); });
LambdaSource<std::string> systemCpu([]() { return Sys::cpu(); });

int main(int argc, char** argv) {
  Sys::init();
  JsonObject mqttConfig = jsonDoc.to<JsonObject>();
  mqttConfig["device"] = "brain";
  mqttConfig["connection"] = "tcp://limero.ddns.net";
  mqtt.config(mqttConfig);
  mqtt.init();
  mqtt.connect();

  poller >> systemTime;
  poller >> systemCpu;
  systemTime >> mqtt.toTopic<uint64_t>("system/upTime");
  systemCpu >> mqtt.toTopic<std::string>("system/cpu");

  mqtt.fromTopic<int>("src/pcdell/js0/axis0") >>
      newCache<int>(mainThread, 100, 1000) >>
      mqtt.toTopic<int>("dst/drive/stepper/angleTarget");

  mqtt.connected >> [](const bool& b) {
    if (b) mqtt.subscribe("src/pcdell/js0/axis0");
  };
  mainThread.run();
}