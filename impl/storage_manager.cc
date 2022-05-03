#include "storage_manager.hh"

#include <drogon/HttpAppFramework.h>

#include <filesystem>

static hanaru::storage_manager* instance = nullptr;
static std::thread cleaner;

hanaru::storage_manager::storage_manager(
    int32_t _preferred_memory_usage,
    int32_t _max_memory_usage,
    int32_t _beatmap_timeout,
    int32_t _required_free_space
)
    : preferred_memory_usage(std::max(512, _preferred_memory_usage))
    , max_memory_usage(std::max(1024, _max_memory_usage))
    , beatmap_timeout(std::max(240, _beatmap_timeout))
    , required_free_space(std::max(1024, _required_free_space))
{
    cleaner = std::thread([&] {
        while (true) {
            try {
                std::filesystem::space_info sp = std::filesystem::space(".");
                allowed_to_write = (((sp.available >> 20) - required_free_space) > 0);

                {
                    std::unique_lock<std::shared_mutex> lock(mtx);
                    auto it = internal_cache.begin();
                    while (it != internal_cache.end()) {
                        int64_t expire = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - it->second.timestamp).count();
                        int64_t timeout = (preferred_memory_usage > memory_usage()) ? beatmap_timeout : beatmap_timeout >> 1;

                        if (memory_threshold) { // We should cleanup memory as fast as we can
                            timeout >>= 1;
                        }

                        if (expire > timeout) {
                            total_memory_usage -= it->second.content.size();
                            it = internal_cache.erase(it);
                            continue;
                        }

                        it++;
                    }
                }

                std::this_thread::sleep_for(std::chrono::minutes(1));
            }
            catch (const std::exception& ex) {
                LOG_WARN << "exception in cleaner thread: " << ex.what();
            }
        }
    });
    instance = this;
}

int64_t hanaru::storage_manager::memory_usage() const {
    // Convert raw bytes to megabytes
    return total_memory_usage >> 20;
}

void hanaru::storage_manager::insert(const int32_t id, hanaru::cached_beatmap&& btm) const {
    const int64_t mem = memory_usage();
    if (mem > max_memory_usage) {
        memory_threshold = true;
        return;
    }

    if (mem < preferred_memory_usage) {
        memory_threshold = false;
    }

    total_memory_usage += btm.content.size();

    std::unique_lock<std::shared_mutex> lock(mtx);
    internal_cache.insert(std::make_pair(id, std::move(btm)));
}

std::optional<hanaru::cached_beatmap> hanaru::storage_manager::find(int32_t id) const {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = internal_cache.find(id);

    if (it != internal_cache.end()) {
        return std::make_optional<hanaru::cached_beatmap>(it->second);
    }

    return {};
}

bool hanaru::storage_manager::can_write() const {
    return allowed_to_write;
}

const hanaru::storage_manager* hanaru::storage_manager::get() {
    return instance;
}