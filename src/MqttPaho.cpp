#include <../Common/Config.h>
#include <MqttPaho.h>
#include <sys/types.h>
#include <unistd.h>
// SEE :
// https://github.com/espressif/esp-idf/blob/master/examples/protocols/mqtt/tcp/main/app_main.c
// volatile MQTTAsync_token deliveredtoken;
const char* mqttConnectionStates[] = {"MS_CONNECTED", "MS_DISCONNECTED",
                                      "MS_CONNECTING", "MS_DISCONNECTING"};
//#define BZERO(x) ::memset(&x, 0, sizeof(x))

#define STRINGIFY(X) #X
#define S(X) STRINGIFY(X)

#define TIMER_KEEP_ALIVE 1
#define TIMER_2 2
//________________________________________________________________________
//
MqttPaho::MqttPaho(Thread& thread)
    : Mqtt(thread),
      _reportTimer(thread, 1, 1000, true),
      _keepAliveTimer(thread) {
  incoming.async(thread);
}
//________________________________________________________________________
//
MqttPaho::~MqttPaho() {}

void MqttPaho::config(JsonObject& conf) {
  _connection = conf["connection"] | "tcp://test.mosquitto.org";
  _lastWillMessage = conf["LW"]["message"] | "false";
  _lastWillQos = conf["LW"]["qos"] | 0;
  srcPrefix = "src/";
  srcPrefix += conf["device"] | Sys::hostname();
  srcPrefix += "/";
  dstPrefix = "dst/";
  dstPrefix += conf["device"] | Sys::hostname();
  dstPrefix += "/";
  std::string defaultTopic = srcPrefix + "system/alive";
  _lastWillTopic = conf["LW"]["topic"] | defaultTopic;
  _lastWillRetain = conf["LW"]["retain"] | false;
  _keepAliveInterval = conf["keepAliveInterval"] | 20;
};
//________________________________________________________________________
//
void MqttPaho::init() {
  _clientId = dstPrefix;
  INFO(" connection : %s ", _connection.c_str());
  state(MS_DISCONNECTED);

  _reportTimer.start();

  _reportTimer >>
      ([&](const TimerMsg& tm) { publish(_lastWillTopic.c_str(), "true"); });

  outgoing.async(thread(), [&](const MqttMessage& m) {
    publish(m.topic.c_str(), m.message.c_str());
  });
  keepAliveTimer.interval(1000);
  keepAliveTimer.repeat(true);
  keepAliveTimer >> [&](const TimerMsg& tm) {
    INFO("");
    if (connected()) outgoing.on({_lastWillTopic, "true"});
  };
}
//________________________________________________________________________
//

int MqttPaho::lastWill(std::string topic, std::string message, int qos,
                       bool retain) {
  _lastWillMessage = message;
  _lastWillQos = qos;
  _lastWillTopic = topic;
  _lastWillRetain = retain;
  return 0;
}

int MqttPaho::client(std::string cl) {
  _clientId = cl;
  return 0;
}

void MqttPaho::state(MqttConnectionState newState) {
  INFO(" MQTT connection state %s => %s ",
       mqttConnectionStates[_connectionState], mqttConnectionStates[newState]);

  _connectionState = newState;
  connected = newState == MS_CONNECTED;
}

MqttPaho::MqttConnectionState MqttPaho::state() { return _connectionState; }

int MqttPaho::connect() {
  int rc;
  if (_connectionState == MS_CONNECTING || _connectionState == MS_CONNECTED) {
    ERROR(" not in DISCONNECTED state ");
    return ECONNREFUSED;
  }

  MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
  MQTTAsync_willOptions will_opts = MQTTAsync_willOptions_initializer;

  INFO(" MQTT connecting %s ...  ", _connection.c_str());
  state(MS_CONNECTING);
  MQTTAsync_create(&_client, _connection.c_str(), _clientId.c_str(),
                   MQTTCLIENT_PERSISTENCE_NONE, NULL);

  MQTTAsync_setCallbacks(_client, this, onConnectionLost, onMessage,
                         onDeliveryComplete);  // TODO add ondelivery

  conn_opts.keepAliveInterval = _keepAliveInterval;
  conn_opts.cleansession = 1;
  conn_opts.onSuccess = onConnectSuccess;
  conn_opts.onFailure = onConnectFailure;
  conn_opts.context = this;
  will_opts.message = _lastWillTopic.c_str();
  will_opts.topicName = _lastWillTopic.c_str();
  will_opts.qos = _lastWillQos;
  will_opts.retained = _lastWillRetain;
  conn_opts.will = &will_opts;
  if ((rc = MQTTAsync_connect(_client, &conn_opts)) != MQTTASYNC_SUCCESS) {
    WARN("Failed to start connect, return code %d", rc);
    return ECONNREFUSED;
  }
  return 0;
}

int MqttPaho::disconnect() {
  MQTTAsync_disconnectOptions disc_opts =
      MQTTAsync_disconnectOptions_initializer;
  disc_opts.onSuccess = onDisconnect;
  disc_opts.context = this;
  int rc = 0;
  if ((rc = MQTTAsync_disconnect(_client, &disc_opts)) != MQTTASYNC_SUCCESS) {
    WARN("Failed to disconnect, return code %d", rc);
    return EIO;
  }
  MQTTAsync_destroy(&_client);
  state(MS_DISCONNECTED);
  return 0;
}

int MqttPaho::subscribe(std::string topic) {
  int qos = 0;
  if (state() != MS_CONNECTED) return 0;
  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  INFO("Subscribing to topic %s for client %s using QoS%d", topic.c_str(),
       _clientId.c_str(), qos);
  opts.onSuccess = onSubscribeSuccess;
  opts.onFailure = onSubscribeFailure;
  opts.context = this;
  int rc = 0;

  if ((rc = MQTTAsync_subscribe(_client, topic.c_str(), qos, &opts)) !=
      MQTTASYNC_SUCCESS) {
    ERROR("Failed to start subscribe, return code %d", rc);
    return EAGAIN;
  } else {
    INFO(" subscribe send ");
  }
  return 0;
}

void MqttPaho::onConnectionLost(void* context, char* cause) {
  MqttPaho* me = (MqttPaho*)context;
  me->state(MS_DISCONNECTED);
}

int MqttPaho::onMessage(void* context, char* topicName, int topicLen,
                        MQTTAsync_message* message) {
    INFO(" receiving message %s[%d] context %X", topicName, topicLen,
    context);
  MqttPaho* me = (MqttPaho*)context;
  std::string msg((char*)message->payload, message->payloadlen);
  std::string topic(topicName, topicLen);
  me->incoming.on({topic, msg});

  MQTTAsync_freeMessage(&message);
  MQTTAsync_free(topicName);
  return 1;
}

void MqttPaho::onDeliveryComplete(void* context, MQTTAsync_token response) {
  //    MqttPaho* me = (MqttPaho*)context;
}

void MqttPaho::onDisconnect(void* context, MQTTAsync_successData* response) {
  MqttPaho* me = (MqttPaho*)context;
  me->state(MS_DISCONNECTED);
}

void MqttPaho::onConnectFailure(void* context,
                                MQTTAsync_failureData* response) {
  MqttPaho* me = (MqttPaho*)context;
  me->state(MS_DISCONNECTED);
}

void MqttPaho::onConnectSuccess(void* context,
                                MQTTAsync_successData* response) {
  MqttPaho* me = (MqttPaho*)context;
  me->state(MS_CONNECTED);
  INFO("MQTT_EVENT_CONNECTED to %s", me->_connection.c_str());
  me->subscribe(me->dstPrefix + "#");
}

void MqttPaho::onSubscribeSuccess(void* context,
                                  MQTTAsync_successData* response) {
  MqttPaho* me = (MqttPaho*)context;
  INFO("MQTT_EVENT_SUBSCRIBED, msg_id=%d", response->token);
}

void MqttPaho::onSubscribeFailure(void* context,
                                  MQTTAsync_failureData* response) {
  MqttPaho* me = (MqttPaho*)context;
}

int MqttPaho::publish(std::string topic, std::string message, int qos,
                      bool retained) {
  if (!_connectionState == MS_CONNECTED) {
    return 0;
  }
  INFO(" MQTT PUB : %s = %s ", topic.c_str(), message.c_str());

  qos = 1;
  MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
  MQTTAsync_message pubmsg = MQTTAsync_message_initializer;

  //   INFO("mqttPublish %s",topic.c_str());

  int rc = 0;
  opts.onSuccess = onPublishSuccess;
  opts.onFailure = onPublishFailure;
  opts.context = this;

  pubmsg.payload = (void*)message.data();
  pubmsg.payloadlen = message.length();
  pubmsg.qos = qos;
  pubmsg.retained = retained;

  if ((rc = MQTTAsync_sendMessage(_client, topic.c_str(), &pubmsg, &opts)) !=
      MQTTASYNC_SUCCESS) {
    state(MS_DISCONNECTED);
    ERROR("MQTTAsync_sendMessage failed.");
    return ECOMM;
  }
  return 0;
}
void MqttPaho::onPublishSuccess(void* context,
                                MQTTAsync_successData* response) {
  MqttPaho* me = (MqttPaho*)context;
}
void MqttPaho::onPublishFailure(void* context,
                                MQTTAsync_failureData* response) {
  MqttPaho* me = (MqttPaho*)context;
}
