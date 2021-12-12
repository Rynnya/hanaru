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

    trantor::EventLoop::getEventLoopOfCurrentThread()->runInLoop(std::bind([](WebSocketConnectionPtr ws_conn, int32_t id) -> AsyncTask {
        Json::Value response = Json::objectValue;
        try {
            auto [beatmap, status] = co_await hanaru::downloader::get()->download_beatmap(id);
            if (status != drogon::k200OK || beatmap.isNull()) {
                response["id"] = id;
                response["status"] = static_cast<int32_t>(status);
                response["data"] = "beatmapset doesn't exist on osu! servers";
                response["filename"] = "";
                ws_conn->send(response.toStyledString(), WebSocketMessageType::Binary);
                co_return;
            }

            int32_t beatmapset_id = beatmap["beatmapset_id"].asInt();
            
            auto [status_code, filename, binary] = co_await hanaru::downloader::get()->download_map(beatmapset_id);
            response["id"] = id;
            response["status"] = static_cast<int32_t>(status_code);
            response["data"] = drogon::utils::base64Encode(reinterpret_cast<const unsigned char*>(binary.data()), binary.size());
            response["filename"] = filename;
            ws_conn->send(response.toStyledString(), WebSocketMessageType::Binary);
        }
        catch (const std::exception& ex) {
            response["id"] = id;
            response["status"] = 500;
            response["data"] = "something bad happend inside hanaru";
            response["filename"] = "";
            ws_conn->send(response.toStyledString(), WebSocketMessageType::Binary);
        }
    }, ws_conn, id));
}

void subscribe_route::handleNewConnection(const HttpRequestPtr &req, const WebSocketConnectionPtr& ws_conn) {}

void subscribe_route::handleConnectionClosed(const WebSocketConnectionPtr& ws_conn) {}
