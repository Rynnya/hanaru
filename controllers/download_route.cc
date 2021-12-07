#include "download_route.hh"

#include "../impl/downloader.hh"
#include "../impl/globals.hh"

Task<HttpResponsePtr> download_route::get(HttpRequestPtr req, int32_t id) {
    const auto [code, filename, content] = co_await hanaru::downloader::get()->download_map(id);

    if (code != k200OK) {
        SEND_ERROR(code, content);
    }

    HttpResponsePtr response = HttpResponse::newFileResponse(
        reinterpret_cast<const unsigned char*>(content.data()),
        content.size(),
        filename,
        CT_CUSTOM,
        "application/x-osu-beatmap-archive"
    );

    co_return response;
}

