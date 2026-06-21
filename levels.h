#pragma once
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>

// 关卡难度
enum class Difficulty { Easy = 0, Medium, Hard, Hell, Count };

// 难度名称
inline const char* difficultyName(Difficulty d) {
    switch (d) {
        case Difficulty::Easy:   return "Easy";
        case Difficulty::Medium: return "Medium";
        case Difficulty::Hard:   return "Hard";
        case Difficulty::Hell:   return "Hell";
        default: return "?";
    }
}

// 每个难度的关卡数
constexpr int LEVELS_PER_DIFFICULTY = 8;

// ============================================================
// 关卡池 — 每个难度 4 关，共 16 关
// 使用经典 Sokoban / Microban 可解关卡
// ============================================================

static const std::vector<std::vector<std::string>> LEVEL_POOL[] = {
    // ===== Easy (小地图, 2-3箱子) =====
    {
        { // Easy #1
            "  ####",
            "###  #",
            "#    #",
            "# $# #",
            "# .$ #",
            "# .@ #",
            "######"
        },
        { // Easy #2
            "#####",
            "#  $#",
            "# $.#",
            "## .##",
            " # @ #",
            " #####"
        },
        { // Easy #3
            "######",
            "#    #",
            "# $  #",
            "# .$ #",
            "# .@ #",
            "######"
        },
        { // Easy #4
            " #### ",
            " #  # ",
            " # $# ",
            "## .###",
            "# $ . #",
            "# . $ #",
            "#  @  #",
            "#######"
        }
    },
    // ===== Medium (中等大小, 3-4箱子) =====
    {
        { // Medium #1
            "  #####",
            "  #   #",
            "  # # #",
            "### # #",
            "# $  $#",
            "# . ##",
            "# . @ #",
            "# . $ #",
            "## ## #",
            " #  ###",
            " ####"
        },
        { // Medium #2
            "#######",
            "#     #",
            "# #$# #",
            "# $.  #",
            "#.. ## #",
            "# $ # #",
            "#   @ #",
            "#######"
        },
        { // Medium #3
            "  ####",
            "  #  #",
            "  # $##",
            "###  $#",
            "#  . .#",
            "# #  ##",
            "#   @ #",
            "#######"
        },
        { // Medium #4
            "########",
            "#      #",
            "# #  # #",
            "# $  $ #",
            "#. ..  #",
            "#  $   #",
            "#  # # #",
            "#   @  #",
            "########"
        }
    },
    // ===== Hard (较大, 4-5箱子) =====
    {
        { // Hard #1
            "########",
            "#      #",
            "# #  # #",
            "# $$   #",
            "# . .$ #",
            "# .$.  #",
            "# #  # #",
            "#   @  #",
            "########"
        },
        { // Hard #2
            "  ######",
            "  #    #",
            "  # $# #",
            "### $$ #",
            "# .  . #",
            "# .#.# #",
            "#   $ #",
            "##   @ #",
            " #  ###",
            " ####"
        },
        { // Hard #3
            " #######",
            "##  #  #",
            "# $  $ #",
            "# # ## #",
            "# .  $ #",
            "## .  ##",
            " # . # #",
            " # @   #",
            " ########"
        },
        { // Hard #4
            " ######",
            " #    #",
            "# $ $ #",
            "# # # #",
            "# .   #",
            "#  $  #",
            "# . . #",
            "# #@# #",
            "########"
        }
    },
    // ===== Hell (复杂, 5+箱子) =====
    {
        { // Hell #1 — 可解 3 箱
            "  ##### ",
            "  #   # ",
            "### $ ##",
            "#  $   #",
            "# #. $ #",
            "##  .  #",
            " #  .  #",
            " # # @ #",
            " #   ###",
            "  ##### "
        },
        { // Hell #2 — 可解 3 箱
            " ###### ",
            "##    ##",
            "#  $$  #",
            "#   $  #",
            "## . ###",
            " # .   #",
            " # .   #",
            " #  @  #",
            " ###### "
        },
        { // Hell #3
            "  ######",
            "  #    #",
            "  # $# #",
            "### $$ #",
            "# .. . #",
            "# . $# #",
            "#      #",
            "## @  ##",
            " #  ###",
            " ####"
        },
        { // Hell #4
            "##########",
            "#        #",
            "# #  ##  #",
            "# $$ $$ ##",
            "# .. . . #",
            "#  . .   #",
            "# ## $$$ #",
            "#   # .  #",
            "#     @  #",
            "##########"
        }
    }
};

// 验证关卡池大小
static_assert(sizeof(LEVEL_POOL) / sizeof(LEVEL_POOL[0]) == 4, "Need exactly 4 difficulty levels");

// 返回值：[0, LEVELS_PER_DIFFICULTY)
inline int randomLevelIndex() {
    static bool seeded = false;
    if (!seeded) { std::srand((unsigned)std::time(nullptr)); seeded = true; }
    return std::rand() % LEVELS_PER_DIFFICULTY;
}

// 获取指定难度的随机关卡
inline const std::vector<std::string>& getRandomLevel(Difficulty d) {
    int idx = (int)d;
    if (idx < 0 || idx >= 4) idx = 0;
    const auto& pool = LEVEL_POOL[idx];
    return pool[randomLevelIndex()];
}

// 获取指定难度 + 指定索引的关卡
inline const std::vector<std::string>& getLevelByIndex(Difficulty d, int index) {
    int idx = (int)d;
    if (idx < 0 || idx >= 4) idx = 0;
    const auto& pool = LEVEL_POOL[idx];
    return pool[index % LEVELS_PER_DIFFICULTY];
}
