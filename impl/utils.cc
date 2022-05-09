#include "utils.hh"

const fs::path hanaru::beatmaps_folder_path = fs::current_path() / "beatmaps";

int64_t hanaru::time_from_epoch() {
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
}

int64_t hanaru::string_to_time(Json::Value time_) {
    if (!time_.isString()) {
        return time_from_epoch();
    }

    return trantor::Date::fromDbStringLocal(time_.asString()).secondsSinceEpoch();
}

std::string hanaru::time_to_string(int64_t time_) {
    char buffer[128] = { 0 };
    time_t seconds = static_cast<time_t>(time_);
    struct tm tm_time;

#ifndef _WIN32
    localtime_r(&seconds, &tm_time);
#else
    localtime_s(&tm_time, &seconds);
#endif

    snprintf(buffer, sizeof(buffer), "%4d-%02d-%02d %02d:%02d:%02d",
        tm_time.tm_year + 1900,
        tm_time.tm_mon + 1,
        tm_time.tm_mday,
        tm_time.tm_hour,
        tm_time.tm_min,
        tm_time.tm_sec
    );

    return buffer;
}