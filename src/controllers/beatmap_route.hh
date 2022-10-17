#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class BeatmapRoute : public drogon::HttpController<BeatmapRoute> {
public:
    void get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int64_t id);

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(BeatmapRoute::get, "/b/{1}", Get);
    METHOD_LIST_END
};
