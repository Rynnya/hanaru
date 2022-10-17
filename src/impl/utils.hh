#pragma once

#include <drogon/HttpClient.h>
#include <filesystem>

#define HANARU_VERSION "1.0"
#define HANARU_USER_AGENT "Mozilla/5.0 (Linux; webOS/2.2.4) AppleWebKit/534.6 (KHTML, like Gecko) webOSBrowser/221.56 Safari/534.6 Pre/3.0"

namespace fs = std::filesystem;

namespace hanaru {

    extern const fs::path beatmapsFolderPath;

    int64_t timeFromEpoch();
    int64_t stringToTime(Json::Value time);
    std::string timeToString(int64_t time);

    bool verifyRateLimit(uint64_t tokens);
}