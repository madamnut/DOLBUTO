#include "platform/Log.h"

#include "platform/RuntimePaths.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace dolbuto::log
{
    namespace
    {
        std::mutex logMutex;
        std::ofstream sessionFile;
        std::ofstream latestFile;
        bool logReady = false;

        std::tm localTime(std::time_t time)
        {
            std::tm value{};
#ifdef _WIN32
            localtime_s(&value, &time);
#else
            localtime_r(&time, &value);
#endif
            return value;
        }

        std::string timestamp(bool forFileName)
        {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            const std::tm local = localTime(time);

            std::ostringstream text;
            if (forFileName)
            {
                text << std::put_time(&local, "%Y%m%d_%H%M%S");
            }
            else
            {
                text << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
            }
            text << (forFileName ? "_" : ".") << std::setw(3) << std::setfill('0') << millis;
            return text.str();
        }

        std::filesystem::path uniqueSessionPath(const std::filesystem::path& directory)
        {
            const std::string baseName = "DOLBUTO_" + timestamp(true);
            for (int suffix = 0; suffix < 1000; ++suffix)
            {
                std::string fileName = baseName;
                if (suffix > 0)
                {
                    fileName += "_" + std::to_string(suffix);
                }
                fileName += ".txt";

                std::filesystem::path candidate = directory / fileName;
                if (!std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }

            return directory / (baseName + "_overflow.txt");
        }

        void writeLine(std::string_view level, std::string_view message)
        {
            std::lock_guard<std::mutex> lock(logMutex);
            if (!logReady)
            {
                return;
            }

            const std::string line = "[" + timestamp(false) + "] [" + std::string(level) + "] " + std::string(message) + "\n";
            if (sessionFile.is_open())
            {
                sessionFile << line;
                sessionFile.flush();
            }
            if (latestFile.is_open())
            {
                latestFile << line;
                latestFile.flush();
            }
        }
    }

    void initialize()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logReady)
        {
            return;
        }

        try
        {
            const std::filesystem::path directory = logDirectory();
            std::filesystem::create_directories(directory);
            sessionFile.open(uniqueSessionPath(directory), std::ios::out | std::ios::trunc);
            latestFile.open(directory / "Latest.txt", std::ios::out | std::ios::trunc);
            logReady = sessionFile.is_open() || latestFile.is_open();
        }
        catch (...)
        {
            logReady = false;
        }
    }

    void shutdown()
    {
        info("GameClient shutdown");

        std::lock_guard<std::mutex> lock(logMutex);
        if (sessionFile.is_open())
        {
            sessionFile.close();
        }
        if (latestFile.is_open())
        {
            latestFile.close();
        }
        logReady = false;
    }

    bool initialized()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        return logReady;
    }

    void debug(std::string_view message)
    {
        writeLine("DEBUG", message);
    }

    void info(std::string_view message)
    {
        writeLine("INFO", message);
    }

    void warn(std::string_view message)
    {
        writeLine("WARN", message);
    }

    void error(std::string_view message)
    {
        writeLine("ERROR", message);
    }
}
