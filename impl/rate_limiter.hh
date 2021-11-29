#pragma once

#include <cstdint>

namespace hanaru::rate_limit {

    bool consume(const uint64_t tokens);
}