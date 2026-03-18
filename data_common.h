/**
 * data_common.h - 多数据类型传输公共定义
 * 
 * 定义飞控、点云、栅格地图三种数据类型的:
 * - FEC参数（符号大小、冗余度）
 * - 数据包格式
 * - 超时配置
 */

#ifndef DATA_COMMON_H
#define DATA_COMMON_H

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

namespace DataTransmit {

// ============================================================================
// 数据类型枚举
// ============================================================================
enum class DataType : uint8_t {
    UNKNOWN = 0,
    VIDEO = 1,       // 视频流 (端口9001)
    FC_CONTROL = 2,  // 飞控指令 (端口9000)
    POINT_CLOUD = 3, // 点云数据 (端口9002)
    GRID_MAP = 4     // 栅格地图 (端口9003)
};

// ============================================================================
// FEC 参数配置（按系统架构文档要求）
// ============================================================================
struct FECParams {
    uint16_t symbol_size;      // 符号大小（字节）
    float redundancy_ratio;    // 冗余比例（0.0-1.0）
    uint32_t timeout_ms;       // 解码超时（毫秒）
    uint32_t send_interval_us; // 发送间隔（微秒）
    
    // 飞控指令: 最高可靠性，50%冗余，50ms超时
    static FECParams FCParams() {
        return {512, 0.5f, 50, 0};  // 无发送间隔，实时传输
    }
    
    // 点云数据: 大带宽，10%冗余
    static FECParams PointCloudParams() {
        return {1024, 0.1f, 500, 10000};  // 10ms间隔，1024B符号（与视频相同）
    }
    
    // 栅格地图: 中高可靠性，20%冗余
    static FECParams GridMapParams() {
        return {1024, 0.2f, 200, 5000};  // 5ms间隔，1024B符号
    }
};

// ============================================================================
// 飞控指令数据格式
// ============================================================================
struct FCControlHeader {
    uint64_t timestamp;        // 发送时间戳（微秒）
    uint32_t seq;              // 指令序号
    uint32_t cmd_len;          // 指令内容长度
    uint8_t priority;          // 优先级（0-255，越高越优先）
    uint8_t reserved[3];       // 保留字节
    
    static constexpr uint8_t PRIORITY_HIGH = 255;
    static constexpr uint8_t PRIORITY_NORMAL = 128;
    static constexpr uint8_t PRIORITY_LOW = 0;
};

// 飞控指令包：Header + 指令字符串
struct FCControlPacket {
    FCControlHeader header;
    std::string command;       // 指令内容（如 "TAKEOFF", "LAND", "MOVE 1.0 2.0 3.0"）
};

// ============================================================================
// 点云数据格式 (livox_ros_driver2/CustomMsg)
// ============================================================================
struct PointCloudPoint {
    uint32_t offset_time;      // 相对于帧起始的时间偏移（纳秒）
    float x;                   // X坐标（米）
    float y;                   // Y坐标（米）
    float z;                   // Z坐标（米）
    uint8_t reflectivity;      // 反射强度
    uint8_t tag;               // 标签
    uint8_t line;              // 线号
    uint8_t reserved;          // 保留字节（对齐）
};

struct PointCloudHeader {
    uint64_t timestamp;        // 帧时间戳（微秒）
    uint64_t timebase;         // 基准时间
    uint32_t point_num;        // 点数
    uint32_t seq;              // 帧序号
    uint8_t lidar_id;          // LiDAR设备ID
    uint8_t rsvd[3];           // 保留字节
};

// 点云帧数据
struct PointCloudFrame {
    PointCloudHeader header;
    std::vector<PointCloudPoint> points;
};

// ============================================================================
// 栅格地图数据格式
// ============================================================================
struct GridMapHeader {
    uint64_t timestamp;        // 时间戳
    uint32_t seq;              // 帧序号
    uint32_t width;            // 栅格宽度（单元格数）
    uint32_t height;           // 栅格高度（单元格数）
    double resolution;         // 分辨率（米/单元格）
    double origin_x;           // 原点X坐标
    double origin_y;           // 原点Y坐标
    double origin_z;           // 原点Z坐标
};

// 栅格值定义
enum class GridCellValue : uint8_t {
    FREE = 0,      // 空闲
    OCCUPIED = 1,  // 占据
    UNKNOWN = 2    // 未知
};

// 栅格地图帧
struct GridMapFrame {
    GridMapHeader header;
    std::vector<uint8_t> data;  // width * height 大小的数组，值为 0/1/2
};

// ============================================================================
// 通用数据包头部（所有数据类型共享）
// ============================================================================
struct DataPacketHeader {
    uint8_t magic[2];          // 魔数 {'R', 'Q'}
    uint8_t version;           // 协议版本
    DataType data_type;        // 数据类型
    uint32_t payload_size;     // 载荷大小
    uint64_t timestamp;        // 时间戳
    
    static constexpr uint8_t MAGIC_0 = 'R';
    static constexpr uint8_t MAGIC_1 = 'Q';
    static constexpr uint8_t VERSION = 1;
};

// ============================================================================
// 工具函数
// ============================================================================

// 获取当前时间戳（微秒）
inline uint64_t GetCurrentTimestampUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

// 数据类型转字符串
inline const char* DataTypeToString(DataType type) {
    switch (type) {
        case DataType::VIDEO: return "VIDEO";
        case DataType::FC_CONTROL: return "FC_CONTROL";
        case DataType::POINT_CLOUD: return "POINT_CLOUD";
        case DataType::GRID_MAP: return "GRID_MAP";
        default: return "UNKNOWN";
    }
}

// 获取数据类型默认端口
inline int GetDefaultPort(DataType type) {
    switch (type) {
        case DataType::FC_CONTROL: return 9000;
        case DataType::VIDEO: return 9001;
        case DataType::POINT_CLOUD: return 9002;
        case DataType::GRID_MAP: return 9003;
        default: return 0;
    }
}

// 获取数据类型输入目录
inline std::string GetInputDir(DataType type) {
    switch (type) {
        case DataType::VIDEO: return "data/videos/";
        case DataType::FC_CONTROL: return "data/fc/";
        case DataType::POINT_CLOUD: return "data/pointcloud/";
        case DataType::GRID_MAP: return "data/gridmap/";
        default: return "data/";
    }
}

// 获取数据类型输出目录
inline std::string GetOutputDir(DataType type) {
    switch (type) {
        case DataType::VIDEO: return "output/videos/";
        case DataType::FC_CONTROL: return "output/fc/";
        case DataType::POINT_CLOUD: return "output/pointcloud/";
        case DataType::GRID_MAP: return "output/gridmap/";
        default: return "output/";
    }
}

} // namespace DataTransmit

#endif // DATA_COMMON_H
