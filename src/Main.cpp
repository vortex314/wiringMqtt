#include <Config.h>
#include <Log.h>
#include <MqttPaho.h>
#include <limero.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>

bool scale(int &out, const int &js) {
  out = (js * 90) / 32767;
  return true;
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
Poller poller(mainThread);

LambdaSource<uint64_t> systemTime([]() { return Sys::millis(); });
LambdaSource<std::string> systemCpu([]() { return Sys::cpu(); });
LambdaSource<std::string> systemHost([]() {
  char nameBuffer[256];
  gethostname(nameBuffer, 255);
  return std::string(nameBuffer);
});
/*
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

EchoTest echoTest(mainThread);*/

/*
  load configuration file into JsonObject
*/
bool loadConfig(JsonDocument &doc, const char *name) {
  FILE *f = fopen(name, "rb");
  if (f == NULL) {
    ERROR(" cannot open config file : %s", name);
    return false;
  }
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET); /* same as rewind(f); */

  char *string = (char *)malloc(fsize + 1);
  fread(string, 1, fsize, f);
  fclose(f);

  string[fsize] = 0;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, string);
  if (error) {
    ERROR(" JSON parsing config file : %s failed.", name);
    return false;
  }
  return true;
}
/*

*/

void commandArguments(JsonObject config, int argc, char **argv) {
  int opt;

  while ((opt = getopt(argc, argv, "f:m:l:v:")) != -1) {
    switch (opt) {
      case 'm':
        config["mqtt"]["connection"] = optarg;
        break;
      case 'f':
        config["configFile"] = optarg;
        break;
      case 'v': {
        char logLevel = optarg[0];
        config["log"]["level"] = logLevel;
        break;
      }
      case 'l':
        config["log"]["file"] = optarg;
        break;
      default: /* '?' */
        fprintf(stderr,
                "Usage: %s [-v(TDIWE)] [-f configFile] [-l logFile] [-m "
                "mqtt_connection]\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }
  }
}

class RisingEdge : public LambdaFlow<int, int> {
  int _lastValue = 0;
  int _outValue;

 public:
  RisingEdge(int x) : LambdaFlow(), _outValue(x) {
    lambda([&](int &out, const int &in) {
      if (in > _lastValue) {
        out = _outValue;
        _lastValue = in;
        return true;
      } else {
        _lastValue = in;
        return false;
      }
    });
  }
};

class Integral : public LambdaFlow<int, int> {
  int _integral = 0;
  int _min;
  int _max;
  int _divider;

 public:
  Integral(int min, int max, int divider)
      : LambdaFlow(), _min(min), _max(max), _divider(divider) {
    lambda([&](int &out, const int &in) {
      int v = in / _divider;
      _integral += v;
      if (_integral < _min)
        _integral = _min;
      else if (_integral > _max)
        _integral = _max;
      out = _integral;
      return true;
    });
  }
};

RisingEdge startEdge(1), stopEdge(0);
Integral steerIntegral(-90, 90, 8000);  // input values -32767 -> 32767
Integral speedIntegral(-1, 4, 16000);   // input values -32767 -> 32767

LambdaFlow<int, int> scaleAngle([](int &out, const int &in) {
  out = (in * 90) / 32767;
  return true;
});

LambdaFlow<int, int> scaleSpeed([](int &out, const int &in) {
  out = (in * -400) / 32767;
  return true;
});
#include <LogFile.h>
LogFile logFile("wiringMqtt", 5, 2000000);
Thread workerThread("worker");
TimerSource ticker(workerThread, 300000, true, "ticker");
#include <sys/time.h>
int timeOfDay() {
  struct timeval tv;
  struct timezone tz;
  time_t t;
  struct tm *info;

  gettimeofday(&tv, NULL);
  t = tv.tv_sec;
  info = localtime(&t);
  return (info->tm_hour * 100) + info->tm_min;
}

int main(int argc, char **argv) {
  Sys::init();
  commandArguments(jsonDoc.as<JsonObject>(), argc, argv);
  if (loadConfig(jsonDoc, "wiringMqtt.json")) {
    std::string jsonString;
    serializeJsonPretty(jsonDoc, jsonString);
    INFO(" config loaded : %s ", jsonString.c_str());
  }
  JsonObject config = jsonDoc["log"];
  std::string level = config["level"] | "I";
  logger.setLogLevel(level[0]);
  if (config["file"]) {
    std::string prefix = config["file"];
    logFile.prefix(prefix.c_str());
    logger.writer(
        [](char *line, unsigned int length) { logFile.append(line, length); });
    if (config["console"]) {
      bool consoleOn = config["console"];
      logFile.console(consoleOn);
    }
  }

  INFO(" wiringMqtt started. Build : %s ", __DATE__ " " __TIME__);

  JsonObject mqttConfig = jsonDoc["mqtt"];
  mqtt.config(mqttConfig);
  mqtt.init();
  mqtt.connect();
  // echoTest.init();
  poller >> systemTime;
  poller >> systemCpu;
  poller >> systemHost;
  mqtt.connected >> poller.connected;
  systemTime >> mqtt.toTopic<uint64_t>("system/upTime");
  systemCpu >> mqtt.toTopic<std::string>("system/cpu");
  systemHost >> mqtt.toTopic<std::string>("system/host");
#ifdef JOYSTICK
  // power on PS PC
  mqtt.fromTopic<int>("src/joystick/button/11") >> startEdge >>
      mqtt.toTopic<int>("dst/gpio/gpio2/value");

  mqtt.fromTopic<int>("src/joystick/button/3") >> stopEdge >>
      mqtt.toTopic<int>("dst/gpio/gpio2/value");
  // joystick angle and speed
  mqtt.fromTopic<int>("src/joystick/axis/0") >> scaleAngle >>
      mqtt.toTopic<int>("dst/drive/stepper/angleTarget");

  mqtt.fromTopic<int>("src/joystick/axis/1") >> scaleSpeed >>
      mqtt.toTopic<int>("dst/motor/motor/rpmTarget");
#endif
  ValueSource<bool> shake;
  shake >> mqtt.toTopic<bool>("dst/shaker1/shaker/shake");
  shake >> mqtt.toTopic<bool>("dst/treeshaker/shaker/shake");

  ticker >> [&shake](const TimerMsg &) {
    int now = timeOfDay();
    if (now > 529 && now < 2200) {
      INFO("let's shake it %d ", now);
      shake = true;
    } else {
      INFO(" sleeping.... ");
    }
  };
  workerThread.start();
  mainThread.run();
}
