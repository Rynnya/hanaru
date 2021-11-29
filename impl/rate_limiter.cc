#include "rate_limiter.hh"

#include <atomic>
#include <chrono>

std::atomic_uint64_t bucket_time = 0;
constexpr uint64_t bucket_rate = 100000; // recover 1 token each 0.1 seconds
constexpr uint64_t bucket_size = 600 * bucket_rate; // sets bucket size to 600 tokens, each token represent 0.1 second, so 60 seconds

bool hanaru::rate_limit::consume(const uint64_t tokens) {

    using namespace std::chrono;
    uint64_t now = static_cast<uint64_t>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
    uint64_t time_required = tokens * bucket_rate;
    uint64_t burst_time = now - bucket_size;
    uint64_t previous_point = bucket_time.load(std::memory_order_relaxed);
    uint64_t current_point = previous_point;

    // In case if request requires more tokens than we can provide
    if (burst_time > previous_point) {
        current_point = burst_time;
    }

    while (true) {

        current_point += time_required;
        // We don't have enough tokens
        if (current_point > now) {
            return false;
        }

        // If true -> _time = current_point; if false -> previous_point = _time;
        // this helps to avoid data race
        if (bucket_time.compare_exchange_weak(previous_point, current_point, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return true;
        }

        // In case we get false in `compare_exchange_weak`, we should update current_point to new value
        current_point = previous_point;
    }

    // Unreachable
    return false;
}