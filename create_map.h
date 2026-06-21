#pragma once
#include <vector>
#include <string>
#include "levels.h"

// 生成的关卡数据
struct GeneratedLevel {
    std::vector<std::string> map;
    int playerX = 0, playerY = 0;
    int width = 0, height = 0;
    bool valid = false;
};

// 生成指定难度的关卡（levelIdx ≥ 1 时随机生成）
GeneratedLevel generateLevel(Difficulty diff, int levelIdx);

// 死局检测（Game 类也需要调用，所以放在 header 中）
bool mapHasDeadlock(const std::vector<std::string>& m);

// 会话级种子，确保本次运行内生成的关卡不变
void seedGenerator(unsigned int seed);
