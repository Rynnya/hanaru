#include <drogon/drogon.h>

#include "impl/downloader.hh"
#include "impl/memory_system.hh"

#include "controllers/beatmap_route.hh"
#include "controllers/beatmap_set_route.hh"
#include "controllers/download_route.hh"

drogon::HttpResponsePtr error_handler(drogon::HttpStatusCode code) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeString("text/plain; charset=utf-8");
    response->setBody("unhandled error, don't worry, be happy :)");
    response->setStatusCode(code);
    return response;
};

void default_handler(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr &)>&& callback) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeString("text/plain; charset=utf-8");
    response->setBody(
        "hanaru v0.5\n"
        #ifdef HANARU_CACHE
        "cache memory usage: " + std::to_string(hanaru::cache::memory_usage()) + " mb's\n"
        #endif
        "source code: https://github.com/Rynnya/hanaru"
    );
    callback(response);
};

void favicon_handler(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr &)>&& callback) {
    HttpResponsePtr response = HttpResponse::newHttpResponse();
    response->setStatusCode(k204NoContent);
    response->setBody("");
    callback(response);
}

int main() {

    drogon::app()
        .addListener("0.0.0.0", 8090)
        .setCustomErrorHandler(error_handler)
        .setDefaultHandler(default_handler)
        .registerHandler("/favicon.ico", &favicon_handler)
        .loadConfigFile("config.json");

    hanaru::initialize_client();
    drogon::app().run();

    return 0;
}
