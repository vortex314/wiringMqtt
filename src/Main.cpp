#include <Config.h>
#include <Log.h>
#include <MqttPaho.h>
#include <limero.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
class Poller
{
public:
  TimerSource _clock;
  int _idx;
  std::vector<Requestable *> _requestables;

  Poller(Thread &thread, uint32_t interval)
      : _clock(thread, interval, true, "main.poller")
  {
    _clock >> [&](const TimerMsg &t) {
      if (_requestables.size())
      {
        _idx++;
        if (_idx >= _requestables.size())
          _idx = 0;
        _requestables.at(_idx)->request();
      };
    };
  };
  void operator>>(Requestable &requestee)
  {
    _requestables.push_back(&requestee);
  }
};

int scale(int &out, const int &js)
{
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

class EchoTest : public Actor
{
public:
  TimerSource trigger;
  ValueSource<uint64_t> counter;
  uint64_t startTime;
  EchoTest(Thread &thread) : Actor(thread), trigger(thread, 1000, true, "trigger"){};
  void init()
  {
    trigger >> [&](const TimerMsg &tm) {
      INFO(" send ");
      counter = Sys::millis();
    };
    counter >> mqtt.toTopic<uint64_t>("echo/output");
    mqtt.fromTopic<uint64_t>("src/brain/echo/output") >> [&](const uint64_t in) {
      INFO(" it took %lu msec ", Sys::millis()-in);
    };
  }
};

EchoTest echoTest(mainThread);

int main(int argc, char **argv)
{
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
/*
  mqtt.fromTopic<int>("src/pcdell/js0/axis0") >>
      Cache<int>::nw(mainThread, 100, 500) >>
      //      LambdaFlow<int, int>::nw([](const int& in) { return in; }) >>
      LambdaFlow<int, int>::nw(scale) >>
      mqtt.toTopic<int>("dst/drive/stepper/angleTarget");*/

  mqtt.connected >> [](const bool &b) {
   /* if (b)
      mqtt.subscribe("src/brain/echo/output");*/
  };
  mainThread.run();
}
