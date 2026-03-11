#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include <functional>

using namespace websockets;

class WsClient {
public:
  WsClient();
  ~WsClient();

  bool init(const char *uri);
  bool connect();
  bool sendText(const String &json_str);
  bool sendRaw(const char *data, size_t len);
  void disconnect();
  bool isConnected();
  void loop();

  // Event hooks
  void onMessage(std::function<void(const WebsocketsMessage &)> cb);
  void onEvent(std::function<void(WebsocketsEvent, String)> cb);

private:
  WebsocketsClient _client;
  String _uri;
  bool _connected;
};

#endif // WS_CLIENT_H
