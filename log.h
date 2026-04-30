/**
 * log.h - 简易日志级别控制
 *
 * 用法:
 *   LOG_INFO("消息");           // 信息级别
 *   LOG_DEBUG("调试消息");      // 调试级别（默认关闭）
 *   LOG_VERBOSE("详细消息");    // 最详细（默认关闭）
 *
 *   // 带模块过滤
 *   LOG_MODULE_INFO(DataPriority::VIDEO, "视频消息");
 *   LOG_MODULE_DEBUG(DataPriority::VOICE, "语音消息");
 */

#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <sstream>
#include <map>
#include <mutex>
#include <string>
#include "send_buffer.h"  // for DataPriority

enum class LogLevel : int {
    ERROR = 0,
    WARN = 1,
    INFO = 2,
    DEBUG = 3,
    VERBOSE = 4
};

// 全局日志配置
struct LogConfig {
    LogLevel global_level = LogLevel::INFO;
    std::map<DataPriority, LogLevel> module_levels;
    std::mutex mutex;

    LogConfig() {
        // 默认：视频只打印关键信息，其他打印调试信息
        module_levels[DataPriority::VIDEO] = LogLevel::INFO;
        module_levels[DataPriority::VOICE] = LogLevel::DEBUG;
        module_levels[DataPriority::FC_COMMAND] = LogLevel::DEBUG;
        module_levels[DataPriority::POINT_CLOUD] = LogLevel::DEBUG;
        module_levels[DataPriority::GRID_MAP] = LogLevel::DEBUG;
    }

    bool ShouldLog(LogLevel level, DataPriority priority = DataPriority::COUNT) {
        std::lock_guard<std::mutex> lock(mutex);
        LogLevel threshold = global_level;
        auto it = module_levels.find(priority);
        if (it != module_levels.end()) {
            // 模块级别取更严格的一个（数值更小）
            if (static_cast<int>(it->second) < static_cast<int>(threshold)) {
                threshold = it->second;
            }
        }
        return static_cast<int>(level) <= static_cast<int>(threshold);
    }

    void SetGlobalLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex);
        global_level = level;
    }

    void SetModuleLevel(DataPriority priority, LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex);
        module_levels[priority] = level;
    }
};

// 全局单例
inline LogConfig& GetLogConfig() {
    static LogConfig config;
    return config;
}

// 内部宏：不要直接使用
#define _LOG_IMPL(level_enum, priority, prefix) \
    do { \
        if (GetLogConfig().ShouldLog(level_enum, priority)) { \
            std::cout << prefix; \
        } \
    } while (0)

#define LOG_ERROR(msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::ERROR)) { std::cerr << "[ERR] " << msg << std::endl; } } while (0)

#define LOG_WARN(msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::WARN)) { std::cout << "[WARN] " << msg << std::endl; } } while (0)

#define LOG_INFO(msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::INFO)) { std::cout << "[INFO] " << msg << std::endl; } } while (0)

#define LOG_DEBUG(msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::DEBUG)) { std::cout << "[DEBUG] " << msg << std::endl; } } while (0)

#define LOG_VERBOSE(msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::VERBOSE)) { std::cout << "[VERB] " << msg << std::endl; } } while (0)

// 带模块优先级的日志
#define LOG_MODULE_ERROR(prio, msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::ERROR, prio)) { std::cerr << "[ERR][" << PriorityToName(prio) << "] " << msg << std::endl; } } while (0)

#define LOG_MODULE_WARN(prio, msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::WARN, prio)) { std::cout << "[WARN][" << PriorityToName(prio) << "] " << msg << std::endl; } } while (0)

#define LOG_MODULE_INFO(prio, msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::INFO, prio)) { std::cout << "[INFO][" << PriorityToName(prio) << "] " << msg << std::endl; } } while (0)

#define LOG_MODULE_DEBUG(prio, msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::DEBUG, prio)) { std::cout << "[DEBUG][" << PriorityToName(prio) << "] " << msg << std::endl; } } while (0)

#define LOG_MODULE_VERBOSE(prio, msg) \
    do { if (GetLogConfig().ShouldLog(LogLevel::VERBOSE, prio)) { std::cout << "[VERB][" << PriorityToName(prio) << "] " << msg << std::endl; } } while (0)

// 辅助函数：将 DataPriority 转为名称
inline const char* PriorityToName(DataPriority prio) {
    switch (prio) {
        case DataPriority::FC_COMMAND: return "FC";
        case DataPriority::VOICE: return "Voice";
        case DataPriority::VIDEO: return "Video";
        case DataPriority::POINT_CLOUD: return "PointCloud";
        case DataPriority::GRID_MAP: return "GridMap";
        default: return "?";
    }
}

#endif // LOG_H
