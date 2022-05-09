#pragma once

#include <drogon/HttpClient.h>
#include <filesystem>

#define SEND_ERROR(STATUS_CODE, BODY)                                                                   \
    {                                                                                                   \
        HttpResponsePtr response = HttpResponse::newHttpResponse();                                     \
        response->setStatusCode(STATUS_CODE);                                                           \
        response->setBody(BODY);                                                                        \
        response->setContentTypeCode(drogon::CT_TEXT_PLAIN);                                            \
        co_return response;                                                                             \
    }

#define HANARU_VERSION "0.7"
#define HANARU_USER_AGENT "Mozilla/5.0 (Linux; webOS/2.2.4) AppleWebKit/534.6 (KHTML, like Gecko) webOSBrowser/221.56 Safari/534.6 Pre/3.0"

namespace fs = std::filesystem;

namespace hanaru {

    extern const fs::path beatmaps_folder_path;

    int64_t time_from_epoch();
    int64_t string_to_time(Json::Value time);
    std::string time_to_string(int64_t time);
}