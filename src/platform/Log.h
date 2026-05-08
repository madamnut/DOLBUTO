#pragma once

#include <string_view>

namespace dolbuto::log
{
    void initialize();
    void shutdown();
    bool initialized();

    void debug(std::string_view message);
    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);
}
