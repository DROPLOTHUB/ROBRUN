#pragma once
// Unified logger: writes every line to ROBRUN.log and flushes so tails stay
// live. Main.cpp calls robrun_log::init() before any logging, then points
// std::cout/std::cerr at the same file buffer so ordinary prints land in the
// log too.
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace robrun_log
{
    inline std::mutex& mtx()
    {
        static std::mutex m;
        return m;
    }

    // The single file handle for the session log.
    inline std::ofstream& stream()
    {
        static std::ofstream f;
        return f;
    }

    // Open (truncate) the log file. Safe to call once at startup.
    inline bool init(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mtx());
        stream().open(path, std::ios::out | std::ios::trunc);
        if (stream().is_open())
            stream() << std::unitbuf;
        return stream().is_open();
    }

    inline std::string now_stamp()
    {
        using clock = std::chrono::system_clock;
        const auto t = clock::to_time_t(clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    inline void write(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(mtx());
        const std::string stamped = "[" + now_stamp() + "] " + line + "\n";

        // Normal path: cout was redirected by Main.cpp to stream().
        std::cout << stamped;
        std::cout.flush();

        // Safety net: if cout still points at the console, also write the line
        // straight to the log file so nothing is lost.
        if (stream().is_open() && std::cout.rdbuf() != stream().rdbuf())
        {
            stream() << stamped;
            stream().flush();
        }
    }
}

#define ROBRUN_LOG(expr)                                                       \
    do {                                                                       \
        std::ostringstream _robrun_oss;                                        \
        _robrun_oss << expr;                                                   \
        ::robrun_log::write(_robrun_oss.str());                                \
    } while (0)
