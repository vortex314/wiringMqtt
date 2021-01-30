#include <Config.h>
#include <Log.h>
#include <MqttPaho.h>
#include <limero.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
class Poller {
 public:
  TimerSource _clock;
  int _idx;
  std::vector<Requestable *> _requestables;

  Poller(Thread &thread, uint32_t interval)
      : _clock(thread, interval, true, "main.poller") {
    _clock >> [&](const TimerMsg &t) {
      if (_requestables.size()) {
        _idx++;
        if (_idx >= _requestables.size()) _idx = 0;
        _requestables.at(_idx)->request();
      };
    };
  };
  void operator>>(Requestable &requestee) {
    _requestables.push_back(&requestee);
  }
};

int scale(int &out, const int &js) {
  out = (js * 90) / 32767;
  return 0;
  /*  static int lastValue = 0;

    if (js > 0) lastValue += 5;
    if (js < 0) lastValue -= 5;
    if (lastValue < -90) lastValue == -90;
    if (lastValue > 90) lastValue = 90;
    return lastValue;*/
}

Log logger(2048);
Thread mainThread("main");
MqttPaho mqtt(mainThread);
StaticJsonDocument<10240> jsonDoc;
Poller poller(mainThread, 1000);

LambdaSource<uint64_t> systemTime([]() { return Sys::millis(); });
LambdaSource<std::string> systemCpu([]() { return Sys::cpu(); });

class EchoTest : public Actor {
 public:
  TimerSource trigger;
  ValueSource<uint64_t> counter;
  uint64_t startTime;
  EchoTest(Thread &thread)
      : Actor(thread), trigger(thread, 1000, true, "trigger"){};
  void init() {
    trigger >> [&](const TimerMsg &tm) {
      INFO(" send ");
      counter = Sys::millis();
    };
    counter >> mqtt.toTopic<uint64_t>("echo/output");
    mqtt.fromTopic<uint64_t>("src/brain/echo/output") >>
        [&](const uint64_t in) {
          INFO(" it took %lu msec ", Sys::millis() - in);
        };
  }
};

EchoTest echoTest(mainThread);

class RisingEdge : public LambdaFlow<int, int> {
  int _lastValue = 0;
  int _outValue;

 public:
  RisingEdge(int x) : LambdaFlow(), _outValue(x) {
    lambda([&](int &out, const int &in) {
      if (in > _lastValue) {
        out = _outValue;
        _lastValue = in;
        return 0;
      } else {
        _lastValue = in;
        return ENODATA;
      }
    });
  }
};

RisingEdge startEdge(1), stopEdge(0);

int main(int argc, char **argv) {
  Sys::init();
  JsonObject mqttConfig = jsonDoc.to<JsonObject>();
  mqttConfig["device"] = "brain";
  mqttConfig["connection"] = "tcp://localhost";
  mqtt.config(mqttConfig);
  mqtt.init();
  mqtt.connect();
  echoTest.init();

  poller >> systemTime;
  poller >> systemCpu;
  systemTime >> mqtt.toTopic<uint64_t>("system/upTime");
  systemCpu >> mqtt.toTopic<std::string>("system/cpu");

  mqtt.fromTopic<int>("src/joystick/button/11") >> startEdge >>
      mqtt.toTopic<int>("dst/gpio/gpio2/value");
  mqtt.fromTopic<int>("src/joystick/button/3") >> stopEdge >>
      mqtt.toTopic<int>("dst/gpio/gpio2/value");

  mainThread.run();
}
