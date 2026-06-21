#pragma once
#include <vector>
#include <string>
#include "records.h"

// Sokoban 求解器
// 使用 BFS + 死局剪枝，找出最短可行解
// 对 ≤5 箱关卡有效，大关卡可能耗时较长

// 求解：输入地图 + 玩家位置，输出操作序列（U/D/L/R）
// 返回 true 表示找到解
bool solveSokoban(const std::vector<std::string>& map, int playerX, int playerY,
                  std::vector<MoveRecord>& solution, int maxStates = 30000);
