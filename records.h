#pragma once
#include "levels.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

// 一次移动的记录
struct MoveRecord {
    int dx, dy;
};

// 通关记录
struct LevelRecord {
    Difficulty difficulty;
    int levelIndex;        // 该难度下的关卡索引
    int totalGoals;        // 总目标数
    double timeSeconds;
    int steps;
    std::string dateStr;
    std::vector<MoveRecord> moves;  // 完整操作序列（用于重放）
    std::vector<std::string> levelData;  // 关卡地图数据（用于准确重放）
};

// 记录管理器
class RecordManager {
public:
    static std::string recordsPath();

    // 保存一条新记录（如果比已有记录更好才保存）
    static bool saveRecord(const LevelRecord& rec);

    // 加载指定关卡的所有记录
    static LevelRecord loadBest(Difficulty d, int levelIndex);

    // 检查是否有关卡记录
    static bool hasRecord(Difficulty d, int levelIndex);

    // 列出所有记录
    static std::vector<LevelRecord> listAll();

    // 删除指定索引的记录
    static bool deleteRecord(int index);
};
