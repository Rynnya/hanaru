#pragma once
#include <drogon/HttpController.h>

using namespace drogon;
class beatmap_route : public drogon::HttpController<beatmap_route> {
public:
    Task<HttpResponsePtr> get(HttpRequestPtr req, int64_t id);

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(beatmap_route::get, "/b/{1}", Get);
    METHOD_LIST_END
};
