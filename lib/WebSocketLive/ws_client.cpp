#include "ws_client.h"
#include "config.h"

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
    _client.setFragmentsPolicy(FragmentsPolicy_Notify);
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
    if (!cb) {
      return;
    }

    static String fragment_buffer;
    static bool fragment_active = false;
    static bool fragment_binary = false;
    static bool fragment_overflow = false;
    static const size_t max_fragment_message_size = WS_BUFFER_SIZE + 4096;

    if (!msg.isPartial()) {
      cb(msg);
      return;
    }

    if (msg.isFirst()) {
      fragment_buffer = msg.data();
      fragment_active = true;
      fragment_binary = msg.isBinary();
      fragment_overflow = fragment_buffer.length() > max_fragment_message_size;
      if (fragment_overflow) {
        Serial.printf("[WS FRAG] first overflow len=%u cap=%u\n",
                      fragment_buffer.length(), max_fragment_message_size);
      }
      return;
    }

    if (!fragment_active) {
      Serial.println("[WS FRAG] got continuation without first; dropping");
      return;
    }

    if (!fragment_overflow) {
      if (fragment_buffer.length() + msg.length() > max_fragment_message_size) {
        fragment_overflow = true;
        Serial.printf("[WS FRAG] aggregate overflow cur=%u add=%u cap=%u\n",
                      fragment_buffer.length(), msg.length(),
                      max_fragment_message_size);
      } else {
        fragment_buffer += msg.data();
      }
    }

    if (!msg.isLast()) {
      return;
    }

    if (!fragment_overflow) {
      WSString complete_payload(fragment_buffer.c_str(), fragment_buffer.length());
      WebsocketsMessage complete(
          fragment_binary ? MessageType::Binary : MessageType::Text,
          complete_payload);
      cb(complete);
    } else {
      Serial.println("[WS FRAG] dropped oversized fragmented message");
    }

    fragment_buffer = "";
    fragment_active = false;
    fragment_binary = false;
    fragment_overflow = false;
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
