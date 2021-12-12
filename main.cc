#include <drogon/drogon.h>

#include "impl/downloader.hh"
#include "impl/globals.hh"
#include "impl/storage_manager.hh"

#include "controllers/subscribe_route.hh"

drogon::HttpResponsePtr error_handler(drogon::HttpStatusCode code) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeString("text/plain; charset=utf-8");
    response->setBody("unhandled error, don't worry, be happy :)");
    response->setStatusCode(code);
    return response;
};

void default_handler(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeString("text/plain; charset=utf-8");
    response->setBody(
        "hanaru v" HANARU_VERSION "\n"
        "cache memory usage: " + std::to_string(hanaru::storage_manager::get()->memory_usage()) + " mb's\n"
        "source code: https://github.com/Rynnya/hanaru"
    );
    callback(response);
};

void favicon_handler(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback) {
    drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    response->setBody("");
    callback(response);
}

int main() {

    drogon::app()
        .setCustomErrorHandler(error_handler)
        .setDefaultHandler(default_handler)
        .registerHandler("/favicon.ico", &favicon_handler)
        .loadConfigFile("config.json");

    if (!fs::exists(hanaru::beatmap_path)) {
        fs::create_directory(hanaru::beatmap_path);
    }

    Json::Value custom_config = drogon::app().getCustomConfig();

    hanaru::downloader dwn(
        custom_config["osu_username"].asString(),
        custom_config["osu_password"].asString(),
        custom_config["osu_api_key"].asString()
    );

    hanaru::storage_manager sm(
        custom_config["preferred_memory_usage"].asInt64(),
        custom_config["max_memory_usage"].asInt64(),
        custom_config["beatmap_timeout"].asInt64(),
        custom_config["required_free_space"].asInt64()
    );

    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        for (auto& listener : drogon::app().getListeners()) {
            LOG_INFO << "Listening on " << listener.toIp() << ":" << listener.toPort();
        }
    }).detach();

    drogon::app().run();

    return 0;
}
