/**
 * generate_sample_data.cpp - 生成点云和栅格地图样例数据
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include "data_common.h"

using namespace DataTransmit;

// ============================================================================
// 生成点云PCD文件
// ============================================================================
void GeneratePointCloudPCD(const std::string& filepath, uint32_t num_points = 5000) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "无法创建文件: " << filepath << std::endl;
        return;
    }
    
    // PCD文件头
    file << "# .PCD v0.7 - Point Cloud Data file format" << std::endl;
    file << "VERSION 0.7" << std::endl;
    file << "FIELDS x y z intensity" << std::endl;
    file << "SIZE 4 4 4 1" << std::endl;
    file << "TYPE F F F U" << std::endl;
    file << "COUNT 1 1 1 1" << std::endl;
    file << "WIDTH " << num_points << std::endl;
    file << "HEIGHT 1" << std::endl;
    file << "VIEWPOINT 0 0 0 1 0 0 0" << std::endl;
    file << "POINTS " << num_points << std::endl;
    file << "DATA ascii" << std::endl;
    
    // 随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // 生成一个房间场景的点云
    // 地板
    std::uniform_real_distribution<float> floor_x(-5.0f, 5.0f);
    std::uniform_real_distribution<float> floor_z(-5.0f, 5.0f);
    for (uint32_t i = 0; i < num_points / 4; ++i) {
        float x = floor_x(gen);
        float z = floor_z(gen);
        float y = 0.0f;  // 地板高度
        uint8_t intensity = static_cast<uint8_t>(100 + (gen() % 50));
        file << x << " " << y << " " << z << " " << (int)intensity << std::endl;
    }
    
    // 两面墙
    std::uniform_real_distribution<float> wall_y(0.0f, 3.0f);
    for (uint32_t i = 0; i < num_points / 4; ++i) {
        float x = -5.0f;  // 左墙
        float y = wall_y(gen);
        float z = floor_z(gen);
        uint8_t intensity = static_cast<uint8_t>(150 + (gen() % 50));
        file << x << " " << y << " " << z << " " << (int)intensity << std::endl;
    }
    for (uint32_t i = 0; i < num_points / 4; ++i) {
        float x = floor_x(gen);
        float y = wall_y(gen);
        float z = -5.0f;  // 后墙
        uint8_t intensity = static_cast<uint8_t>(150 + (gen() % 50));
        file << x << " " << y << " " << z << " " << (int)intensity << std::endl;
    }
    
    // 随机障碍物（模拟几个箱子和圆柱）
    std::uniform_real_distribution<float> box_x(-3.0f, 3.0f);
    std::uniform_real_distribution<float> box_z(-3.0f, 3.0f);
    std::uniform_real_distribution<float> box_y(0.0f, 1.5f);
    for (uint32_t i = 0; i < num_points / 4; ++i) {
        // 几个聚类点模拟障碍物
        float cx = box_x(gen);
        float cz = box_z(gen);
        float cy = box_y(gen);
        
        // 在该位置附近生成一组点
        std::normal_distribution<float> noise(0.0f, 0.1f);
        float x = cx + noise(gen);
        float y = cy + noise(gen);
        float z = cz + noise(gen);
        uint8_t intensity = static_cast<uint8_t>(200 + (gen() % 55));
        file << x << " " << y << " " << z << " " << (int)intensity << std::endl;
    }
    
    file.close();
    std::cout << "生成点云文件: " << filepath << " (" << num_points << " 点)" << std::endl;
}

// ============================================================================
// 生成栅格地图文件（二进制格式）
// ============================================================================
void GenerateGridMap(const std::string& filepath, uint32_t width = 200, uint32_t height = 200) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法创建文件: " << filepath << std::endl;
        return;
    }
    
    // 栅格地图头部
    GridMapHeader header;
    header.timestamp = GetCurrentTimestampUs();
    header.seq = 0;
    header.width = width;
    header.height = height;
    header.resolution = 0.05;  // 5cm分辨率
    header.origin_x = -5.0;    // 原点偏移
    header.origin_y = -5.0;
    header.origin_z = 0.0;
    
    // 写入头部
    file.write(reinterpret_cast<const char*>(&header), sizeof(GridMapHeader));
    
    // 生成栅格数据
    std::vector<uint8_t> data(width * height);
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // 创建一个有房间结构的地图
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = y * width + x;
            
            // 地图坐标（米）
            double mx = (x * header.resolution) + header.origin_x;
            double my = (y * header.resolution) + header.origin_y;
            
            // 外围墙壁（占据）
            if (x < 10 || x >= width - 10 || y < 10 || y >= height - 10) {
                data[idx] = static_cast<uint8_t>(GridCellValue::OCCUPIED);
            }
            // 内部障碍物（几个矩形和圆形）
            else if ((mx > -2.0 && mx < -1.0 && my > -2.0 && my < 0.0) ||  // 左下矩形
                     (mx > 1.0 && mx < 2.5 && my > 1.0 && my < 2.5) ||    // 右上矩形
                     (std::sqrt(mx*mx + my*my) < 0.8)) {                   // 中心圆形
                data[idx] = static_cast<uint8_t>(GridCellValue::OCCUPIED);
            }
            // 一些随机噪声（未知区域）
            else if (gen() % 100 < 3) {
                data[idx] = static_cast<uint8_t>(GridCellValue::UNKNOWN);
            }
            // 其余为空闲
            else {
                data[idx] = static_cast<uint8_t>(GridCellValue::FREE);
            }
        }
    }
    
    // 写入数据
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    
    // 统计
    size_t free_count = 0, occupied_count = 0, unknown_count = 0;
    for (auto cell : data) {
        switch (static_cast<GridCellValue>(cell)) {
            case GridCellValue::FREE: free_count++; break;
            case GridCellValue::OCCUPIED: occupied_count++; break;
            case GridCellValue::UNKNOWN: unknown_count++; break;
        }
    }
    
    std::cout << "生成栅格地图: " << filepath << std::endl;
    std::cout << "  尺寸: " << width << "x" << height << " (" << width*header.resolution 
              << "m x " << height*header.resolution << "m)" << std::endl;
    std::cout << "  分辨率: " << header.resolution << "m/单元格" << std::endl;
    std::cout << "  空闲: " << free_count << " (" << (100.0*free_count/data.size()) << "%)" << std::endl;
    std::cout << "  占据: " << occupied_count << " (" << (100.0*occupied_count/data.size()) << "%)" << std::endl;
    std::cout << "  未知: " << unknown_count << " (" << (100.0*unknown_count/data.size()) << "%)" << std::endl;
}

// ============================================================================
// 生成文本格式的栅格地图（便于查看）
// ============================================================================
void GenerateGridMapText(const std::string& filepath, uint32_t width = 100, uint32_t height = 100) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "无法创建文件: " << filepath << std::endl;
        return;
    }
    
    file << "# Grid Map - Text Format" << std::endl;
    file << "# Width: " << width << ", Height: " << height << std::endl;
    file << "# 0=FREE(.), 1=OCCUPIED(#), 2=UNKNOWN(?)" << std::endl;
    file << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            // 外围墙壁
            if (x < 5 || x >= width - 5 || y < 5 || y >= height - 5) {
                file << "#";
            }
            // 内部障碍物
            else if ((x > 20 && x < 30 && y > 20 && y < 40) ||  // 矩形1
                     (x > 60 && x < 75 && y > 50 && y < 65) ||  // 矩形2
                     ((x-50)*(x-50) + (y-50)*(y-50) < 100)) {   // 圆形
                file << "#";
            }
            else if (gen() % 100 < 5) {
                file << "?";
            }
            else {
                file << ".";
            }
        }
        file << std::endl;
    }
    
    file.close();
    std::cout << "生成栅格地图文本: " << filepath << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "   样例数据生成工具" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 生成点云数据
    std::cout << std::endl << "[1] 生成点云数据..." << std::endl;
    GeneratePointCloudPCD("data/pointcloud/room_5k.pcd", 5000);
    GeneratePointCloudPCD("data/pointcloud/outdoor_10k.pcd", 10000);
    
    // 生成栅格地图数据
    std::cout << std::endl << "[2] 生成栅格地图数据..." << std::endl;
    GenerateGridMap("data/gridmap/office_200x200.grid", 200, 200);
    GenerateGridMap("data/gridmap/warehouse_300x300.grid", 300, 300);
    
    // 生成文本格式（便于查看）
    std::cout << std::endl << "[3] 生成栅格地图文本预览..." << std::endl;
    GenerateGridMapText("data/gridmap/office_preview.txt", 100, 100);
    
    std::cout << std::endl << "========================================" << std::endl;
    std::cout << "样例数据生成完成！" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
