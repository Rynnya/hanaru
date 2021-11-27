#include <drogon/drogon.h>

#include "impl/downloader.h"
#include "impl/cache_system.h"

#include "controllers/BeatmapRoute.h"
#include "controllers/BeatmapSetRoute.h"
#include "controllers/DownloadRoute.h"

drogon::HttpResponsePtr errorHandler(drogon::HttpStatusCode code) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeString("text/plain; charset=utf-8");
    response->setBody("unhandled error, don't worry, be happy :)");
    response->setStatusCode(code);
    return response;
};

void defaultHandler(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr &)>&& callback) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeString("text/plain; charset=utf-8");
    response->setBody(
        "hanaru v0.3\n"
        #ifdef HANARU_CACHE
        "cache memory usage: " + std::to_string(hanaru::memory_usage()) + " mb's\n"
        #endif
        "source code: https://github.com/Rynnya/hanaru"
    );
    callback(response);
};

int main() {

    drogon::app()
        .addListener("0.0.0.0", 8090)
        .setCustomErrorHandler(errorHandler)
        .setDefaultHandler(defaultHandler)
        .registerHandler("/favicon.ico", [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr &)>&& callback) {
            HttpResponsePtr response = HttpResponse::newHttpResponse();
            response->setStatusCode(k204NoContent);
            response->setBody("");
            callback(response);
        })
        .loadConfigFile("config.json");
    hanaru::initialize_client();
    drogon::app().run();

    return 0;
}
