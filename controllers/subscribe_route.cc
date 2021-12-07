#include "subscribe_route.hh"

#include "../impl/downloader.hh"
#include "../impl/globals.hh"

#include <charconv>

void subscribe_route::handleNewMessage(const WebSocketConnectionPtr& ws_conn, std::string &&message, const WebSocketMessageType &type) {
    // drogon will handle ping and pong messages automatically
    if (type != WebSocketMessageType::Text) {
        return;
    }

    int32_t id = 0;
    auto error = std::from_chars(message.data(), message.data() + message.size(), id);
    if (error.ec != std::errc()) {
        return;
    }

    // Workaround because drogon currently doesn't support coro in websockets
    Json::Value response = Json::objectValue;
    auto [status, filename, data] = sync_wait(hanaru::downloader::get()->download_map(id));
    response["id"] = id;
    response["status"] = static_cast<int32_t>(status);
    response["data"] = data;
    response["filename"] = filename;
    ws_conn->send(response.toStyledString(), WebSocketMessageType::Binary);
}

void subscribe_route::handleNewConnection(const HttpRequestPtr &req, const WebSocketConnectionPtr& ws_conn) {}

void subscribe_route::handleConnectionClosed(const WebSocketConnectionPtr& ws_conn) {}
