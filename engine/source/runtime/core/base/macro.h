#pragma once

#include "runtime/core/log/log_system.h"

#include "runtime/function/global/global_context.h"

#include <chrono>
#include <thread>

#define LOG_HELPER(LOG_LEVEL, FMT, ...) \
    g_runtime_global_context.m_logger_system->log( \
        LOG_LEVEL, \
        "[" + std::string(__FUNCTION__) + "] " + FMT, \
        ##__VA_ARGS__ \
    )

#define LOG_DEBUG(FMT, ...) LOG_HELPER(LogSystem::LogLevel::debug, FMT, ##__VA_ARGS__)
#define LOG_INFO(FMT, ...) LOG_HELPER(LogSystem::LogLevel::info, FMT, ##__VA_ARGS__)
#define LOG_WARN(FMT, ...) LOG_HELPER(LogSystem::LogLevel::warn, FMT, ##__VA_ARGS__)
#define LOG_ERROR(FMT, ...) LOG_HELPER(LogSystem::LogLevel::error, FMT, ##__VA_ARGS__)
#define LOG_FATAL(FMT, ...) LOG_HELPER(LogSystem::LogLevel::fatal, FMT, ##__VA_ARGS__)

#define PolitSleep(_ms) std::this_thread::sleep_for(std::chrono::milliseconds(_ms));

#define PolitNameOf(name) #name

#ifdef NDEBUG
    #define ASSERT(statement)
#else
    #define ASSERT(statement) assert(statement)
#endif
