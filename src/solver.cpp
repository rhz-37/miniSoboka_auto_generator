#include "solver.h"
#include "create_map.h"
#include <set>
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <cstring>
#include <cassert>

using Pos = std::pair<int, int>;

// 状态编码：用于 visited 去重
// boxPositions sorted + playerPos = 64-bit hash
struct SokState {
    std::vector<Pos> boxes;   // 排序后的箱子位置
    int px, py;               // 玩家位置
    int prevState;            // 父状态索引（回退路径用）
    MoveRecord move;          // 到达此状态的推动操作

    bool operator==(const SokState& o) const {
        if (px != o.px || py != o.py) return false;
        if (boxes.size() != o.boxes.size()) return false;
        for (size_t i = 0; i < boxes.size(); ++i)
            if (boxes[i] != o.boxes[i]) return false;
        return true;
    }
};

struct SokHash {
    size_t operator()(const SokState& s) const {
        size_t h = (size_t)s.px ^ ((size_t)s.py << 12);
        for (auto& b : s.boxes)
            h ^= ((size_t)b.first << 3) ^ ((size_t)b.second << 17);
        return h;
    }
};

// 地图辅助函数
static int getW(const std::vector<std::string>& m) {
    int w = 0;
    for (auto& s : m) if ((int)s.size() > w) w = (int)s.size();
    return w;
}

static bool isGoal(char c) {
    return c == '.' || c == '+' || c == '*';
}

static bool isWall(char c) {
    return c == '#';
}

// 安全获取地图字符（带边界和行长检查）
static char safeChar(const std::vector<std::string>& map, int x, int y) {
    int h = (int)map.size();
    if (y < 0 || y >= h) return '#';
    if (x < 0 || x >= (int)map[y].size()) return '#';
    return map[y][x];
}

// 计算玩家可达位置（BFS）
static std::set<Pos> playerReachable(const std::vector<std::string>& map, int px, int py) {
    int w = getW(map), h = (int)map.size();
    std::set<Pos> visited;
    std::queue<Pos> q;
    visited.insert({px, py});
    q.push({px, py});
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        for (auto& d : dirs) {
            int nx = cx + d[0], ny = cy + d[1];
            if (visited.count({nx, ny})) continue;
            char c = safeChar(map, nx, ny);
            if (c == '#' || c == '$' || c == '*') continue;
            visited.insert({nx, ny});
            q.push({nx, ny});
        }
    }
    return visited;
}

// 构建含箱子位置的地图（所有行补齐到等宽）
static std::vector<std::string> buildMap(const std::vector<std::string>& base,
                                          const std::vector<Pos>& boxes, int px, int py) {
    std::vector<std::string> m = base;
    int h = (int)m.size(), w = getW(m);
    for (int y = 0; y < h; ++y) while ((int)m[y].size() < w) m[y] += ' ';
    for (auto& [bx, by] : boxes) {
        if (by < 0 || by >= h) continue;
        char c = m[by][bx];
        if (c == '.' || c == '+') m[by][bx] = '*';
        else if (c != '#') m[by][bx] = '$';
    }
    // Place player
    if (py >= 0 && py < h && px >= 0 && px < w) {
        char pc = m[py][px];
        if (pc == '.' || pc == '*') m[py][px] = '+';
        else if (pc != '#') m[py][px] = '@';
    }
    return m;
}

bool solveSokoban(const std::vector<std::string>& map, int playerX, int playerY,
                  std::vector<MoveRecord>& solution, int maxStates) {
    // 解析初始状态
    int w = getW(map), h = (int)map.size();
    std::set<Pos> goals;
    std::vector<Pos> initBoxes;
    int px = playerX, py = playerY;

    // 基础地图（不包含箱子和玩家）
    std::vector<std::string> base = map;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < (int)map[y].size(); ++x) {
            char c = map[y][x];
            if (c == '$' || c == '*') {
                initBoxes.push_back({x, y});
                if (base[y][x] == '*') goals.insert({x, y});
            }
            if (c == '.') goals.insert({x, y});
            if (c == '+' || c == '@') { px = x; py = y; }
                    // 清理基础地图中的箱子/玩家标记
            if (c == '$' || c == '@') base[y][x] = ' ';
            if (c == '+' || c == '*') { base[y][x] = '.'; }
        }
        // 补齐行宽
        while ((int)base[y].size() < w) base[y] += ' ';
    }

    if (initBoxes.empty()) return false;
    // 箱数过多时跳过求解（内存/时间限制）
    if ((int)initBoxes.size() > 4) return false;
    std::sort(initBoxes.begin(), initBoxes.end());

    // BFS
    std::vector<SokState> states;
    std::unordered_set<SokState, SokHash> visited;
    std::queue<int> q;  // state indices

    SokState start;
    start.boxes = initBoxes;
    start.px = px;
    start.py = py;
    start.prevState = -1;
    start.move = {0,0};

    states.push_back(start);
    visited.insert(start);
    q.push(0);

    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    MoveRecord dirMoves[4] = {{0,-1},{0,1},{-1,0},{1,0}};

    int checked = 0;

    while (!q.empty() && (int)states.size() < maxStates) {
        int idx = q.front(); q.pop();
        const auto& state = states[idx];
        checked++;

        // 检查是否胜利
        bool won = true;
        for (auto& [bx, by] : state.boxes) {
            if (!goals.count({bx, by})) { won = false; break; }
        }
        if (won) {
            // 回溯路径
            solution.clear();
            int cur = idx;
            while (cur > 0) {
                solution.push_back(states[cur].move);
                cur = states[cur].prevState;
            }
            std::reverse(solution.begin(), solution.end());
            return true;
        }

        // 计算玩家可达区域
        std::vector<std::string> curMap = buildMap(base, state.boxes, state.px, state.py);
        auto reachable = playerReachable(curMap, state.px, state.py);

        // 枚举每个箱子
        for (size_t bi = 0; bi < state.boxes.size(); ++bi) {
            auto [bx, by] = state.boxes[bi];
            for (int d = 0; d < 4; ++d) {
                int dx = dirs[d][0], dy = dirs[d][1];
                int tox = bx + dx, toy = by + dy;    // 箱子去向
                int frx = bx - dx, fry = by - dy;     // 推手位置

                // 去向不能是墙或箱子
                char tc = safeChar(curMap, tox, toy);
                if (tc == '#' || tc == '$' || tc == '*') continue;

                // 推手位置必须在可达区域内且可通行
                if (!reachable.count({frx, fry})) continue;

                // 模拟推箱
                SokState next;
                next.boxes = state.boxes;
                next.boxes[bi] = {tox, toy};  // 移动箱子
                std::sort(next.boxes.begin(), next.boxes.end());
                next.px = bx;   // 玩家站到箱子原来的位置
                next.py = by;
                next.prevState = idx;
                next.move = dirMoves[d];

                // visited 检查
                if (visited.count(next)) continue;

                // 死局检测（用移动后的箱子位置构建地图）
                std::vector<std::string> simMap = buildMap(base, next.boxes, bx, by);
                if (mapHasDeadlock(simMap)) continue;

                visited.insert(next);
                int newIdx = (int)states.size();
                states.push_back(next);
                q.push(newIdx);
            }
        }
    }

    return false;  // 未找到解
}
