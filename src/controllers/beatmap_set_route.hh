#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class BeatmapSetRoute : public HttpController<BeatmapSetRoute> {
public:
    void get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int64_t id);

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(BeatmapSetRoute::get, "/s/{1}", Get);
    METHOD_LIST_END
};
