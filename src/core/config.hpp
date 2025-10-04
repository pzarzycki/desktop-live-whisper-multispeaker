#pragma once
#include <string>

namespace core {
struct Config {
    std::string whisper_model = "small"; // or "base"
    bool perf_hud = false;
};

const Config& get_config();
}
