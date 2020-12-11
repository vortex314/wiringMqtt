#include <Config.h>
#include <Log.h>
#include <MqttPaho.h>
#include <NanoAkka.h>
#include <math.h>
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

int scale(const int& js) {
  float v = js;
  v /= 32767.0;
  v *= 3.141592653;
  v = tanh(v);
  return v * 90.0;
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
      Cache<int>::nw(mainThread, 100, 1000) >>
      //      LambdaFlow<int, int>::nw([](const int& in) { return in; }) >>
      LambdaFlow<int, int>::nw(scale) >>
      mqtt.toTopic<int>("dst/drive/stepper/angleTarget");

  mqtt.connected >> [](const bool& b) {
    if (b) mqtt.subscribe("src/pcdell/js0/axis0");
  };
  mainThread.run();
}
