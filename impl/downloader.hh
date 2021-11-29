#pragma once

#include <drogon/HttpClient.h>

namespace hanaru {

    void initialize_client();

    std::tuple<Json::Value, drogon::HttpStatusCode> download_beatmap(int32_t id);
    std::tuple<Json::Value, drogon::HttpStatusCode> download_beatmapset(int32_t id);

    int64_t time_to_int(Json::Value time);
    std::string int_to_time(int64_t time);
}
