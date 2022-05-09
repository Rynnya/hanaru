#include <drogon/drogon.h>

#include "impl/downloader.hh"
#include "impl/utils.hh"
#include "impl/storage_manager.hh"

#include <fstream>
#include <curl/curl.h>

drogon::HttpResponsePtr error_handler(drogon::HttpStatusCode code) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCodeAndCustomString(drogon::ContentType::CT_TEXT_PLAIN, "text/plain; charset=utf-8");
    response->setBody("unhandled error");
    response->setStatusCode(code);
    return response;
};

void default_handler(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCodeAndCustomString(drogon::ContentType::CT_TEXT_PLAIN, "text/plain; charset=utf-8");
    response->setBody(
        "hanaru v" HANARU_VERSION "\n"
        "cache memory usage: " + std::to_string(hanaru::storage_manager::get().memory_usage()) + " mb's\n"
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

void main_callback() {
    for (auto& listener : drogon::app().getListeners()) {
        LOG_INFO << "Listening on " << listener.toIp() << ":" << listener.toPort();
    }
}

int main() {

    curl_global_init(CURL_GLOBAL_ALL);
    drogon::app()
        .setFloatPrecisionInJson(2, "decimal")
        .registerBeginningAdvice(main_callback)
        .setCustomErrorHandler(error_handler)
        .setDefaultHandler(default_handler)
        .registerHandler("/favicon.ico", &favicon_handler)
        .loadConfigFile("config.json");

    if (!fs::exists(hanaru::beatmaps_folder_path)) {
        fs::create_directory(hanaru::beatmaps_folder_path);
    }

    Json::Value custom_config = drogon::app().getCustomConfig();

    hanaru::downloader dwn(
        custom_config["osu_username"].asString(),
        custom_config["osu_password"].asString(),
        custom_config["osu_api_key"].asString()
    );

    hanaru::storage_manager sm(
        custom_config["maximum_cache_size"].asUInt64(),
        custom_config["required_free_space"].asInt64()
    );

    drogon::app().run();
}
