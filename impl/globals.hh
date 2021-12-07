#pragma once

#include <drogon/HttpClient.h>
#include <filesystem>

#define SEND_ERROR(STATUS_CODE, BODY)                                                                   \
    {                                                                                                   \
        HttpResponsePtr response = HttpResponse::newHttpResponse();                                     \
        response->setStatusCode(STATUS_CODE);                                                           \
        response->setBody(BODY);                                                                        \
        response->setContentTypeString("text/plain; charset=utf-8");                                    \
        co_return response;                                                                             \
    }

#define HANARU_VERSION "0.6"

namespace fs = std::filesystem;

namespace hanaru {

    inline const fs::path beatmap_path = fs::current_path() / "beatmaps";
    inline constexpr std::string_view empty = "";

    inline int64_t time_to_int(Json::Value _time) {
        if (!_time.isString()) {
            return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
        }
        std::string __time = _time.asString();
        std::tm time {};
        std::stringstream stream(__time);
        stream >> std::get_time(&time, "%Y-%m-%d %H:%M:%S");
        std::chrono::time_point time_point = std::chrono::system_clock::from_time_t(std::mktime(&time));
        std::chrono::seconds seconds = std::chrono::time_point_cast<std::chrono::seconds>(time_point).time_since_epoch();
        return seconds.count();
    }

    inline std::string int_to_time(int64_t _time) {
        std::tm time = *std::gmtime(&_time);
        std::stringstream ss;
        ss << std::put_time(&time, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
}