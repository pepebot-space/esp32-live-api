#include "ws_client.h"

WsClient::WsClient() : _connected(false) {}

WsClient::~WsClient() { disconnect(); }

bool WsClient::init(const char *uri) {
  _uri = uri;
  return true;
}

bool WsClient::connect() {
  if (_uri.isEmpty()) {
    Serial.println("WebSocket URI is empty");
    return false;
  }

  Serial.printf("Connecting to WebSocket: %s\n", _uri.c_str());

  // Can optionally set buffer sizes or authorization here
  _connected = _client.connect(_uri);

  if (_connected) {
    Serial.println("WebSocket Initial Connection Success");
    _client.setFragmentsPolicy(FragmentsPolicy_Aggregate);
  } else {
    Serial.println("WebSocket Initial Connection Failed");
  }
  return _connected;
}

bool WsClient::sendText(const String &json_str) {
  if (!_connected)
    return false;
  return _client.send(json_str);
}

bool WsClient::sendRaw(const char *data, size_t len) {
  if (!_connected)
    return false;
  return _client.send(data, len);
}

void WsClient::disconnect() {
  _client.close();
  _connected = false;
}

bool WsClient::isConnected() {
  // Rely on the library's status, but keep our local flag in sync via events
  return _connected && _client.available();
}

void WsClient::loop() {
  if (_client.available()) {
    _client.poll();
  }
}

void WsClient::onMessage(std::function<void(const WebsocketsMessage &)> cb) {
  _client.onMessage([cb](WebsocketsClient &, WebsocketsMessage msg) {
    if (cb) {
      cb(msg);
    }
  });
}

void WsClient::onEvent(std::function<void(WebsocketsEvent, String)> cb) {
  _client.onEvent([this, cb](WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
      _connected = true;
    } else if (event == WebsocketsEvent::ConnectionClosed) {
      _connected = false;
    }

    // Pass to the upper layer
    if (cb) {
      cb(event, data);
    }
  });
}
