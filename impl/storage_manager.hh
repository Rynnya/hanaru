#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "concurrent_cache.hh"

namespace hanaru {

    class cached_beatmap {
    public:
        cached_beatmap(const std::string& name, const std::string& content, bool retry = false, std::chrono::system_clock::time_point time = std::chrono::system_clock::now());

        const std::string name;
        const std::string content;
        const bool retry;
        const std::chrono::system_clock::time_point timestamp;
    };

    class storage_manager {
    public:
        storage_manager(
            size_t maximum_cache_size,
            int64_t required_free_space
        );

        int64_t memory_usage() const;
        void insert(const int64_t id, cached_beatmap&& btm);
        std::shared_ptr<hanaru::cached_beatmap> find(int64_t id);

        bool can_write() const;

        static storage_manager& get();
    private:
        const size_t maximum_cache_size_ = 128;         // 128 * ~20 MB = 2.5 GB
        const int64_t required_free_space_ = 5120;      // 5 GB

        lib::lru_cache<int64_t, hanaru::cached_beatmap> cache_ { maximum_cache_size_ };
    };
}