#pragma once
#include <drogon/HttpController.h>

using namespace drogon;
class download_route : public drogon::HttpController<download_route> {
public:
    Task<HttpResponsePtr> get(HttpRequestPtr req, int32_t id);

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(download_route::get, "/d/{1}", Get);
    METHOD_LIST_END
};
