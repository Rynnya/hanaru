#include "utils.hh"

namespace hanaru {

    int64_t timeFromEpoch() {
        return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    }

    int64_t stringToTime(Json::Value time) {
        if (!time.isString()) {
            return timeFromEpoch();
        }

        return trantor::Date::fromDbStringLocal(time.asString()).secondsSinceEpoch();
    }

    std::string timeToString(int64_t time) {
        char buffer[128] = { 0 };
        time_t seconds = static_cast<time_t>(time);
        struct tm tmTime;

#ifndef _WIN32
        localtime_r(&seconds, &tmTime);
#else
        localtime_s(&tmTime, &seconds);
#endif

        snprintf(buffer, sizeof(buffer), "%4d-%02d-%02d %02d:%02d:%02d",
            tmTime.tm_year + 1900,
            tmTime.tm_mon + 1,
            tmTime.tm_mday,
            tmTime.tm_hour,
            tmTime.tm_min,
            tmTime.tm_sec
        );

        return buffer;
    }

    std::atomic_uint64_t bucketTime = 0;
    constexpr uint64_t bucketRate = 100000; // recover 1 token each 0.1 seconds
    constexpr uint64_t bucketSize = 600 * bucketRate; // sets bucket size to 600 tokens, each token represent 0.1 second, so 60 seconds

    bool verifyRateLimit(uint64_t tokens) {
        using namespace std::chrono;

        uint64_t now = static_cast<uint64_t>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
        uint64_t timeRequired = tokens * bucketRate;
        uint64_t burstTime = now - bucketSize;
        uint64_t previousPoint = bucketTime.load(std::memory_order_relaxed);
        uint64_t currentPoint = previousPoint;

        // In case if request requires more tokens than we can provide
        if (burstTime > previousPoint) {
            currentPoint = burstTime;
        }

        while (true) {

            currentPoint += timeRequired;
            // We don't have enough tokens
            if (currentPoint > now) {
                return false;
            }

            // If true -> bucketTime = currentPoint; if false -> previousPoint = bucketTime;
            // this helps to avoid data race
            if (bucketTime.compare_exchange_weak(previousPoint, currentPoint, std::memory_order_relaxed, std::memory_order_relaxed)) {
                return true;
            }

            // In case we get false in `compare_exchange_weak`, we should update current_point to new value
            currentPoint = previousPoint;
        }

        // Unreachable
        return false;
    }

}
