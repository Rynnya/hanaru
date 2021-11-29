#pragma once
#include <drogon/HttpController.h>

using namespace drogon;
class DownloadRoute : public drogon::HttpController<DownloadRoute> {
public:
    void get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int32_t id);

    METHOD_LIST_BEGIN
        ADD_METHOD_TO(DownloadRoute::get, "/d/{1}", Get);
    METHOD_LIST_END
};
