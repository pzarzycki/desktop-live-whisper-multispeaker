#include "core/config.hpp"

namespace core {
static Config g_config{};
const Config& get_config() { return g_config; }
}
