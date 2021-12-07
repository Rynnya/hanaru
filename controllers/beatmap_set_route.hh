#pragma once
#include <drogon/HttpController.h>

using namespace drogon;
class beatmap_set_route : public drogon::HttpController<beatmap_set_route> {
public:
    Task<HttpResponsePtr> get(HttpRequestPtr req, int32_t id);

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(beatmap_set_route::get, "/s/{1}", Get);
    METHOD_LIST_END
};
