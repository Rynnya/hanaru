#include "subscribe_route.hh"

#include "../impl/downloader.hh"

#include <charconv>

void subscribe_route::handleNewMessage(const WebSocketConnectionPtr& ws_conn, std::string &&message, const WebSocketMessageType &type) {
    // drogon will handle ping and pong messages automatically
    if (type != WebSocketMessageType::Text) {
        return;
    }

    int64_t id = 0;
    auto [ptr, error] = std::from_chars(message.data(), message.data() + message.size(), id);
    if (error != std::errc()) {
        return;
    }

    trantor::EventLoop::getEventLoopOfCurrentThread()->runInLoop(std::bind([](WebSocketConnectionPtr ws_conn, int64_t id) -> AsyncTask {
        Json::Value response = Json::objectValue;
        try {
            typedef long long int64_t;

            auto [status_code, filename, binary] = co_await hanaru::downloader::get().download_map(id);
            response["id"] = id;
            response["status"] = static_cast<int64_t>(status_code);
            response["data"] = drogon::utils::base64Encode(reinterpret_cast<const unsigned char*>(binary.data()), binary.size());
            response["filename"] = filename;
        }
        catch (const std::exception& ex) {
            LOG_ERROR << "Exception occurred when downloading map through websocket: " << ex.what();
            response["id"] = id;
            response["status"] = 500;
            response["data"] = "";
            response["filename"] = "";
        }
        ws_conn->send(response.toStyledString(), WebSocketMessageType::Binary);
    }, ws_conn, id));
}

void subscribe_route::handleNewConnection(const HttpRequestPtr &req, const WebSocketConnectionPtr& ws_conn) {}

void subscribe_route::handleConnectionClosed(const WebSocketConnectionPtr& ws_conn) {}
