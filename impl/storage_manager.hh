#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace hanaru {

    class cached_beatmap {
    public:
        cached_beatmap(
            const std::string& name_,
            const std::string& content_,
            std::chrono::system_clock::time_point time_ = std::chrono::system_clock::now()
        )
            : name(name_)
            , content(content_)
            , timestamp(time_)
        {};

        const std::string name;
        const std::string content;
        const std::chrono::system_clock::time_point timestamp;
    };

    class storage_manager {
    public:
        storage_manager(
            int32_t _preferred_memory_usage,
            int32_t _max_memory_usage,
            int32_t _beatmap_timeout,
            int32_t _required_free_space
        );

        int64_t memory_usage() const;
        void insert(const int32_t id, cached_beatmap&& btm) const;
        std::optional<hanaru::cached_beatmap> find(int32_t id) const;

        bool can_write() const;

        static const storage_manager* get();
    private:
        const int32_t preferred_memory_usage = 2048;    // 2 GB
        const int32_t max_memory_usage = 4096;          // 4 GB
        const int32_t beatmap_timeout = 1200;           // 20 minutes
        const int32_t required_free_space = 5120;       // 5 GB

        mutable std::shared_mutex mtx;
        mutable std::atomic_int64_t total_memory_usage = 0;
        mutable std::unordered_map<int32_t, hanaru::cached_beatmap> internal_cache = {};

        bool allowed_to_write = true;
        mutable std::atomic_bool memory_threshold = false;
    };
}