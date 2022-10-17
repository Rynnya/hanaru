#include "download_route.hh"

#include "../impl/downloader.hh"
#include "../impl/utils.hh"

void DownloadRoute::get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int64_t id) {
    hanaru::downloader::downloadMap(id, [callback = std::move(callback)](std::tuple<HttpStatusCode, std::string, std::string>&& result) {
        HttpResponsePtr response = HttpResponse::newHttpResponse();
        response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
        response->setStatusCode(std::get<HttpStatusCode>(result));
        response->setBody(std::move(std::get<2>(result)));

        if (response->getStatusCode() == k200OK) {
            response->setContentTypeCodeAndCustomString(drogon::CT_CUSTOM, "application/x-osu-beatmap-archive");
            response->addHeader("Content-Disposition", "attachment; filename=\"" + std::move(std::get<1>(result)) + "\"");
        }

        callback(response);
    });
}

