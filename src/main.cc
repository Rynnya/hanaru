#include <drogon/drogon.h>

#include "impl/downloader.hh"
#include "impl/utils.hh"
#include "impl/storage_manager.hh"

#include <fstream>

drogon::HttpResponsePtr errorHandler(drogon::HttpStatusCode code) {
    drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCodeAndCustomString(drogon::ContentType::CT_TEXT_PLAIN, "text/plain; charset=utf-8");
    response->setBody("unhandled error");
    response->setStatusCode(code);
    return response;
};

void defaultHandler(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback) {
    drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCodeAndCustomString(drogon::ContentType::CT_TEXT_PLAIN, "text/plain; charset=utf-8");
    response->setBody(
        "hanaru v" HANARU_VERSION "\n"
        "source code: https://github.com/Rynnya/hanaru"
    );
    callback(response);
};

void faviconHandler(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr &)>&& callback) {
    drogon::HttpResponsePtr response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    response->setBody("");
    callback(response);
}

void mainHandler() {
    for (auto& listener : drogon::app().getListeners()) {
        LOG_INFO << "Listening on " << listener.toIp() << ":" << listener.toPort();
    }
}

int main() {

    drogon::app()
        .setThreadNum(drogon::app().getThreadNum() - 1)
        .setFloatPrecisionInJson(2, "decimal")
        .registerBeginningAdvice(mainHandler)
        .setCustomErrorHandler(errorHandler)
        .setDefaultHandler(defaultHandler)
        .registerHandler("/favicon.ico", &faviconHandler)
        .loadConfigFile("config.json");

    if (!fs::exists(hanaru::beatmapsFolderPath)) {
        fs::create_directory(hanaru::beatmapsFolderPath);
    }

    Json::Value customConfig = drogon::app().getCustomConfig();

    hanaru::downloader::initialize(customConfig["osu_api_key"].asString(), customConfig["osu_username"].asString(), customConfig["osu_password"].asString());
    hanaru::storage::initialize(customConfig["required_free_space"].asUInt64());

    drogon::app().run();

    return 0;
}
