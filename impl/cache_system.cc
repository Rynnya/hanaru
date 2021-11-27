#include "cache_system.h"

#include <drogon/HttpAppFramework.h>

#include <filesystem>

bool allowed_to_write = true;

#ifdef HANARU_CACHE

#include <shared_mutex>

int32_t preferred_memory_usage = 2048; // 2 GB
int32_t max_memory_usage = 4096; // 4 GB

int32_t beatmap_timeout = 1200; // 20 minutes

int32_t required_free_space = 5120; // 5 GB

std::shared_mutex mtx;
std::atomic_int64_t total_memory_usage;
std::unordered_map<int32_t, hanaru::cached_beatmap> cache = {};

std::atomic_bool memory_threshold = false;

int64_t hanaru::memory_usage() {
    // Convert raw bytes to megabytes
    return total_memory_usage >> 20;
}

void hanaru::initialize() {
    Json::Value config = drogon::app().getCustomConfig();

    if (config["preferred_memory_usage"].isIntegral()) {
        preferred_memory_usage = config["preferred_memory_usage"].asInt64();
        if (preferred_memory_usage < 512) {
            preferred_memory_usage = 512;
        }
    }

    if (config["max_memory_usage"].isIntegral()) {
        max_memory_usage = config["max_memory_usage"].asInt64();
        if (max_memory_usage < 1024) {
            max_memory_usage = 1024;
        }
    }

    if (config["beatmap_timeout"].isIntegral()) {
        beatmap_timeout = config["beatmap_timeout"].asInt64();
        if (beatmap_timeout < 240) {
            beatmap_timeout = 240;
        }
    }

    if (config["required_free_space"].isIntegral()) {
        required_free_space = config["required_free_space"].asInt64();
        if (required_free_space < 1024) {
            required_free_space = 1024;
        }
    }

    drogon::app().getIOLoop(1)->runEvery(60, [&]() {

        std::filesystem::space_info sp = std::filesystem::space(".");
        allowed_to_write = (((sp.available >> 20) - required_free_space) > 0);

        std::unique_lock<std::shared_mutex> lock(mtx);
        auto it = cache.begin();
        while (it != cache.end()) {
            int64_t expire = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - it->second.timestamp).count();
            int64_t timeout = (preferred_memory_usage > memory_usage()) ? beatmap_timeout : beatmap_timeout >> 1;

            if (memory_threshold) { // Protective mode enabled, we should cleanup memory as fast as we can
                timeout >>= 1;
            }

            if (expire > timeout) {
                total_memory_usage -= it->second.content.size();
                it = cache.erase(it);
                continue;
            }

            it++;
        }

    });
}


void hanaru::insert(int32_t id, hanaru::cached_beatmap btm) {
    // Cache enters protective mode where memory should be cleaned
    if (memory_usage() > max_memory_usage) {
        memory_threshold = true;
        return;
    }

    if (memory_threshold && memory_usage() > preferred_memory_usage) {
        return;
    }

    memory_threshold = false;
    total_memory_usage += btm.content.size();

    std::unique_lock<std::shared_mutex> lock(mtx);
    cache.emplace(id, btm);
}

std::optional<hanaru::cached_beatmap> hanaru::get(int32_t id) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = cache.find(id);

    if (it != cache.end()) {
        return std::make_optional<hanaru::cached_beatmap>(it->second);
    }

    return {};
}

#else

void hanaru::initialize() {
    Json::Value config = drogon::app().getCustomConfig();

    if (config["required_free_space"].isIntegral()) {
        required_free_space = config["required_free_space"].asInt64();
        if (required_free_space < 1024) {
            required_free_space = 1024;
        }
    }

    drogon::app().getIOLoop(1)->runEvery(60, [&]() {
        std::filesystem::space_info sp = std::filesystem::space(".");
        allowed_to_write = (((sp.available >> 20) - required_free_space) > 0);
    });
}

#endif

bool hanaru::can_write() {
    return allowed_to_write;
}