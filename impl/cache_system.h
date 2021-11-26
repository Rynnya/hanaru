#pragma once

#ifdef HANARU_CACHE

#include <cstdint>
#include <chrono>
#include <string>
#include <optional>

namespace hanaru {

    class cached_beatmap {
    public:
        cached_beatmap(std::string _name, const char* _data, size_t _length, std::chrono::system_clock::time_point _time = std::chrono::system_clock::now())
            : name(_name)
            , content(_data, _length)
            , timestamp(_time)
        {};

        std::string name;
        std::string content;
        std::chrono::system_clock::time_point timestamp;
    };

    // Returns memory usage in megabytes
    int64_t memory_usage();

    // Load's values from config.json
    void initialize();

    // Run's internal cleaner
    void run_cleaner();

    // Insert's new beatmap into cache (if total_memory_usage is lower than max_memory_usage)
    void insert(int32_t id, cached_beatmap btm);

    std::optional<cached_beatmap> get(int32_t id);
}

#endif