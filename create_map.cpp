#include "create_map.h"
#include <set>
#include <queue>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>
#include <cassert>

// ============================================================
// 死局检测（纯数据版本，与 Game 类解耦）
// ============================================================
using Pos = std::pair<int,int>;

static int mapWidth(const std::vector<std::string>& m) {
    if (m.empty()) return 0;
    int w = 0;
    for (auto& s : m) if ((int)s.size() > w) w = (int)s.size();
    return w;
}

static int mapHeight(const std::vector<std::string>& m) {
    return (int)m.size();
}

static bool isWall(int x, int y, const std::vector<std::string>& m) {
    if (y < 0 || y >= (int)m.size() || x < 0 || x >= (int)m[y].size()) return true;
    return m[y][x] == '#';
}

static bool isAnyBox(int x, int y, const std::vector<std::string>& m) {
    if (y < 0 || y >= (int)m.size() || x < 0 || x >= (int)m[y].size()) return false;
    char c = m[y][x];
    return c == '$' || c == '*';
}

static bool isGoalCell(int x, int y, const std::vector<std::string>& m) {
    if (y < 0 || y >= (int)m.size() || x < 0 || x >= (int)m[y].size()) return false;
    char c = m[y][x];
    return c == '.' || c == '+' || c == '*';
}

// 核心递归：箱子 (x,y) 是否死局
static bool boxDeadlocked(int x, int y, const std::vector<std::string>& m,
                           std::set<Pos>& frozen, std::set<Pos>& visited) {
    if (isGoalCell(x, y, m)) return false;
    if (visited.count({x, y})) return true;
    visited.insert({x, y});

    // 墙角检测（只算实墙 #）
    if (isWall(x, y-1, m) && isWall(x-1, y, m)) return true;
    if (isWall(x, y-1, m) && isWall(x+1, y, m)) return true;
    if (isWall(x, y+1, m) && isWall(x-1, y, m)) return true;
    if (isWall(x, y+1, m) && isWall(x+1, y, m)) return true;

    // — 贴边墙检测：贴地图边界的箱子只能平行移动 —
    {
        int mw = mapWidth(m), mh = mapHeight(m);
        // 左侧边界墙（x=0 是墙，箱子在 x=1）
        if (x == 1 && isWall(0, y, m)) {
            bool goalOnColumn = false;
            for (int gy = 0; gy < mh && !goalOnColumn; ++gy)
                if (isGoalCell(0, gy, m)) goalOnColumn = true;
            if (!goalOnColumn) return true;
        }
        // 右侧边界墙
        if (mw > 0 && x == mw - 2 && isWall(mw-1, y, m)) {
            bool goalOnColumn = false;
            for (int gy = 0; gy < mh && !goalOnColumn; ++gy)
                if (isGoalCell(mw-1, gy, m)) goalOnColumn = true;
            if (!goalOnColumn) return true;
        }
        // 顶部边界墙
        if (y == 1 && isWall(x, 0, m)) {
            bool goalOnRow = false;
            for (int gx = 0; gx < mw && !goalOnRow; ++gx)
                if (isGoalCell(gx, 0, m)) goalOnRow = true;
            if (!goalOnRow) return true;
        }
        // 底部边界墙
        if (mh > 0 && y == mh - 2 && isWall(x, mh-1, m)) {
            bool goalOnRow = false;
            for (int gx = 0; gx < mw && !goalOnRow; ++gx)
                if (isGoalCell(gx, mh-1, m)) goalOnRow = true;
            if (!goalOnRow) return true;
        }
    }

    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    for (auto& d : dirs) {
        int dx = d[0], dy = d[1];
        int toX = x+dx, toY = y+dy;
        int frX = x-dx, frY = y-dy;

        if (isWall(toX, toY, m)) continue;
        if (isAnyBox(toX, toY, m)) continue;
        if (isWall(frX, frY, m)) continue;

        if (isAnyBox(frX, frY, m)) {
            if (frozen.count({frX, frY})) continue;
            std::set<Pos> subFrozen = frozen;
            subFrozen.insert({x, y});
            std::set<Pos> subVisited;
            if (boxDeadlocked(frX, frY, m, subFrozen, subVisited)) continue;
        }

        // 检查相邻箱子
        bool allFree = true;
        for (auto& nd : dirs) {
            int nx = x + nd[0], ny = y + nd[1];
            if (ny < 0 || ny >= (int)m.size() || nx < 0 || nx >= (int)m[ny].size()) continue;
            if (!isAnyBox(nx, ny, m) || frozen.count({nx, ny})) continue;
            std::set<Pos> subFrozen = frozen;
            subFrozen.insert({x, y});
            std::set<Pos> subVisited;
            if (boxDeadlocked(nx, ny, m, subFrozen, subVisited)) {
                allFree = false;
                break;
            }
        }
        if (allFree) return false;
    }
    return true;
}

// 检查是否有任何包点无法被推入（四周距离墙 < 2 格）
static bool hasInaccessibleGoal(const std::vector<std::string>& m) {
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    int mw = mapWidth(m), mh = mapHeight(m);

    for (int y = 0; y < mh; ++y)
        for (int x = 0; x < (int)m[y].size(); ++x)
            if (m[y][x] == '.' || m[y][x] == '+') {  // 未被占用的目标点
                bool canReach = false;
                for (auto& d : dirs) {
                    int c1x = x + d[0], c1y = y + d[1];  // 箱子位置
                    int c2x = x + 2*d[0], c2y = y + 2*d[1];  // 推手位置

                    if (c1y < 0 || c1y >= mh || c1x < 0 || c1x >= (int)m[c1y].size()) continue;
                    if (m[c1y][c1x] == '#') continue;  // cell1 是墙 → 箱子进不来

                    if (c2y < 0 || c2y >= mh || c2x < 0 || c2x >= (int)m[c2y].size()) continue;
                    if (m[c2y][c2x] == '#') continue;  // cell2 是墙 → 推手站不下

                    canReach = true;
                    break;
                }
                if (!canReach) return true;
            }
    return false;
}

// 检查箱子所有首次推动是否都导致卡死
static bool boxHasNoValidFirstPush(int x, int y, const std::vector<std::string>& m) {
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    for (auto& d : dirs) {
        int toX = x+d[0], toY = y+d[1];
        int frX = x-d[0], frY = y-d[1];
        // 推手位置和去向都必须为空
        if (isWall(toX, toY, m)) continue;
        if (isAnyBox(toX, toY, m)) continue;
        if (isWall(frX, frY, m)) continue;
        if (isAnyBox(frX, frY, m)) continue;
        // 模拟推箱
        std::vector<std::string> sim = m;
        sim[y][x] = ' ';
        sim[toY][toX] = (sim[toY][toX] == '.') ? '*' : '$';
        // 检查推走后箱子是否死局
        std::set<Pos> frozen, visited;
        if (!boxDeadlocked(toX, toY, sim, frozen, visited))
            return false;  // 至少有一个有效首次推动
    }
    return true;  // 所有首次推动都死局
}

bool mapHasDeadlock(const std::vector<std::string>& m) {
    // 1) 包点无法被推入
    if (hasInaccessibleGoal(m)) return true;

    // 2) 每个箱子检测死局
    for (int y = 0; y < (int)m.size(); ++y)
        for (int x = 0; x < (int)m[y].size(); ++x)
            if (m[y][x] == '$') {
                std::set<Pos> frozen, visited;
                if (boxDeadlocked(x, y, m, frozen, visited)) return true;
                // 所有首次推动都死局 → 箱子永远无法离开出生位置
                if (boxHasNoValidFirstPush(x, y, m)) return true;
            }
    return false;
}

// Layer 3: 检查是否有至少一个有效开局操作
static bool hasValidFirstPush(const std::vector<std::string>& map,
                               int playerX, int playerY) {
    int mw = mapWidth(map), mh = mapHeight(map);

    // 收集箱子
    std::vector<Pos> boxes;
    for (int y = 0; y < mh; ++y)
        for (int x = 0; x < (int)map[y].size(); ++x)
            if (map[y][x] == '$')
                boxes.push_back({x, y});

    // BFS
    std::set<Pos> reachable;
    std::queue<Pos> q;
    reachable.insert({playerX, playerY});
    q.push({playerX, playerY});
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        for (auto& d : dirs) {
            int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || nx >= mw || ny < 0 || ny >= mh) continue;
            if (reachable.count({nx, ny})) continue;
            char nc = map[ny][nx];
            if (nc == ' ' || nc == '.') {
                reachable.insert({nx, ny});
                q.push({nx, ny});
            }
        }
    }

    // 枚举开局推箱
    for (auto& [bx, by] : boxes) {
        for (auto& d : dirs) {
            int px = bx - d[0], py = by - d[1];
            int tx = bx + d[0], ty = by + d[1];

            if (reachable.count({px, py}) == 0) continue;
            if (tx < 0 || tx >= mw || ty < 0 || ty >= mh) continue;
            char tc = map[ty][tx];
            if (tc == '#' || tc == '$' || tc == '*') continue;

            // 模拟推箱
            std::vector<std::string> sim = map;
            sim[by][bx] = ' ';
            sim[ty][tx] = (tc == '.') ? '*' : '$';

            if (!mapHasDeadlock(sim)) return true;
        }
    }
    return false;
}

// ============================================================
// 关卡生成
// ============================================================
static int sessionSeed = 0;
static bool seeded = false;

void seedGenerator(unsigned int seed) {
    sessionSeed = (int)seed;
    std::srand(seed);
    seeded = true;
}

// 随机打乱辅助
static void shuffleVector(std::vector<Pos>& v) {
    static std::mt19937 rng(std::rand());
    std::shuffle(v.begin(), v.end(), rng);
}

// 获取一个随机位置（只在 open 格中）
static Pos randomOpenCell(const std::vector<std::string>& m) {
    int w = mapWidth(m), h = mapHeight(m);
    std::vector<Pos> opens;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < (int)m[y].size(); ++x)
            if (m[y][x] == ' ')
                opens.push_back({x, y});
    if (opens.empty()) return {-1, -1};
    return opens[std::rand() % opens.size()];
}

// 一个位置有多少开放邻居
static int openNeighbors(int x, int y, const std::vector<std::string>& m) {
    int cnt = 0;
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    for (auto& d : dirs) {
        int nx = x + d[0], ny = y + d[1];
        if (ny >= 0 && ny < (int)m.size() && nx >= 0 && nx < (int)m[ny].size() && m[ny][nx] != '#')
            ++cnt;
    }
    return cnt;
}

GeneratedLevel generateLevel(Difficulty diff, int levelIdx) {
    if (!seeded) {
        seedGenerator((unsigned)std::time(nullptr));
    }

    // 每个 levelIdx 有固定的附加种子，保证同次运行内一致
    int levelSeed = (int)diff * 100 + levelIdx * 7 + sessionSeed;
    std::srand(levelSeed);

    // 难度参数
    int roomW, roomH, boxCount, wallDensity;
    int hellTemplate = 0;  // 仅 Hell 使用
    switch (diff) {
        case Difficulty::Easy:
            roomW = 7; roomH = 6; boxCount = 2 + std::rand() % 2; wallDensity = 5; break;
        case Difficulty::Medium:
            roomW = 8; roomH = 7; boxCount = 3; wallDensity = 10; break;
        case Difficulty::Hard:
            roomW = 10; roomH = 8; boxCount = 3 + std::rand() % 2; wallDensity = 12; break;
        case Difficulty::Hell:
            roomW = 14; roomH = 11; boxCount = 6 + std::rand() % 2; wallDensity = 0;
            hellTemplate = std::rand() % 4;  // 4 种模板随机选
            break;
        default:
            roomW = 7; roomH = 6; boxCount = 2; wallDensity = 5;
    }

    const int MAX_ATTEMPTS = 100;

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        // 创建带边框的空房间
        std::vector<std::string> map(roomH, std::string(roomW, ' '));
        for (int x = 0; x < roomW; ++x) { map[0][x] = '#'; map[roomH-1][x] = '#'; }
        for (int y = 0; y < roomH; ++y) { map[y][0] = '#'; map[y][roomW-1] = '#'; }

        if (diff == Difficulty::Hell) {
            // Hell 难度：4 种局部模板，每次随机选一种
            switch (hellTemplate) {

            // ── 模板 0：棋盘格 + 隔断 ──
            case 0: {
                // 棋盘式墙柱
                for (int wy = 2; wy < roomH - 2; wy += 2) {
                    int offset = (wy % 4 == 2) ? 3 : 4;
                    for (int wx = offset; wx < roomW - 2; wx += 3) {
                        map[wy][wx] = '#';
                        if (wx+1 < roomW-1) map[wy][wx+1] = '#';
                        if (wy+1 < roomH-1) map[wy+1][wx] = '#';
                        if (wx+1 < roomW-1 && wy+1 < roomH-1) map[wy+1][wx+1] = '#';
                    }
                }
                // 水平隔断
                int dy = 3 + std::rand() % 4;
                int gap = 3 + std::rand() % (roomW - 6);
                for (int dx = 2; dx < roomW - 2; ++dx)
                    if (dx != gap && dx != gap + 1) map[dy][dx] = '#';
                break;
            }

            // ── 模板 1：内室 + 回廊 ──
            case 1: {
                // 内室（中央带缺口）
                for (int y = 3; y <= roomH - 5; ++y)
                    for (int x = 3; x <= roomW - 5; ++x)
                        map[y][x] = '#';
                // 在 4 个方向各开一个 2 格缺口
                int topGap = 4 + std::rand() % (roomW - 8);
                int botGap = 4 + std::rand() % (roomW - 8);
                int leftGap = 4 + std::rand() % (roomH - 8);
                int rightGap = 4 + std::rand() % (roomH - 8);
                for (int g = 0; g < 2; ++g) {
                    if (topGap + g < roomW-3) map[3][topGap + g] = ' ';
                    if (botGap + g < roomW-3) map[roomH-5][botGap + g] = ' ';
                    if (leftGap + g < roomH-3) map[leftGap + g][3] = ' ';
                    if (rightGap + g < roomH-3) map[rightGap + g][roomW-5] = ' ';
                }
                // 内室里加一个 2x2 柱
                int cx = 5 + std::rand() % (roomW - 10);
                int cy = 5 + std::rand() % (roomH - 8);
                map[cy][cx] = '#'; map[cy][cx+1] = '#';
                map[cy+1][cx] = '#'; map[cy+1][cx+1] = '#';
                break;
            }

            // ── 模板 2：镜像分区 ──
            case 2: {
                // 左右镜像分区
                int midX = roomW / 2;
                // 中间隔墙
                for (int y = 2; y < roomH - 2; ++y)
                    map[y][midX] = '#';
                // 开 2 个缺口
                int gap1 = 2 + std::rand() % (roomH - 5);
                int gap2 = 2 + std::rand() % (roomH - 5);
                while (std::abs(gap1 - gap2) < 3) gap2 = 2 + std::rand() % (roomH - 5);
                map[gap1][midX] = ' ';
                map[gap2][midX] = ' ';
                // 每侧随机加 2 对阻挡柱
                for (int half = 0; half < 2; ++half) {
                    int hx = (half == 0) ? 2 + std::rand() % (midX - 3) : midX + 2 + std::rand() % (roomW - midX - 3);
                    int hy = 2 + std::rand() % (roomH - 4);
                    map[hy][hx] = '#'; if (hy+1 < roomH-1) map[hy+1][hx] = '#';
                    hx = (half == 0) ? 2 + std::rand() % (midX - 3) : midX + 2 + std::rand() % (roomW - midX - 3);
                    hy = 2 + std::rand() % (roomH - 4);
                    map[hy][hx] = '#'; if (hy+1 < roomH-1) map[hy+1][hx] = '#';
                }
                break;
            }

            // ── 模板 3：L/T 型走廊 ──
            case 3: {
                // 多条短墙段形成走廊
                int numSegs = 5 + std::rand() % 3;
                for (int s = 0; s < numSegs; ++s) {
                    int sx = 2 + std::rand() % (roomW - 4);
                    int sy = 2 + std::rand() % (roomH - 4);
                    int len = 2 + std::rand() % 3;
                    bool horiz = std::rand() % 2 == 0;
                    for (int i = 0; i < len; ++i) {
                        if (horiz && sx + i < roomW - 1) {
                            map[sy][sx + i] = '#';
                            if (sy+1 < roomH-1) map[sy+1][sx + i] = '#';
                        } else if (!horiz && sy + i < roomH - 1) {
                            map[sy + i][sx] = '#';
                            if (sx+1 < roomW-1) map[sy + i][sx+1] = '#';
                        }
                    }
                }
                break;
            }
            }
        } else {
            // 其他难度：随机撒墙
            int wallsToAdd = (roomW - 2) * (roomH - 2) * wallDensity / 100;
            for (int w = 0; w < wallsToAdd; ++w) {
                int wx = 1 + std::rand() % (roomW - 2);
                int wy = 1 + std::rand() % (roomH - 2);
                if (map[wy][wx] != '#') map[wy][wx] = '#';
            }
        }

        // 确保有足够的开放空间
        std::vector<Pos> opens;
        for (int y = 1; y < roomH-1; ++y)
            for (int x = 1; x < roomW-1; ++x)
                if (map[y][x] == ' ') opens.push_back({x, y});

        if ((int)opens.size() < boxCount * 4 + 4) { // 空间不足
            // 随机移除内墙直到空间足够
            int maxRemove = (roomW - 2) * (roomH - 2) / 4;
            for (int r = 0; r < maxRemove && (int)opens.size() < boxCount * 4 + 4; ++r) {
                int wx = 1 + std::rand() % (roomW - 2);
                int wy = 1 + std::rand() % (roomH - 2);
                if (map[wy][wx] == '#') {
                    map[wy][wx] = ' ';
                    opens.push_back({wx, wy});
                }
            }
        }

        // 放置目标点
        std::vector<Pos> goals;
        int goalSearchRadius = (diff == Difficulty::Hell) ? 3 : 0;  // Hell 用更大搜索(后文用)
        for (int b = 0; b < boxCount; ++b) {
            Pos p = randomOpenCell(map);
            if (p.first < 0) break;
            // 部分目标点放在靠墙位置（增加难度）
            if (diff == Difficulty::Hell && std::rand() % 3 == 0) {
                // 找一条边旁边的位置
                std::vector<Pos> edgeCells;
                for (int y = 2; y < roomH - 2; ++y) {
                    for (int x = 2; x < roomW - 2; ++x) {
                        if (map[y][x] != ' ') continue;
                        if ((map[y-1][x] == '#' || map[y+1][x] == '#' ||
                             map[y][x-1] == '#' || map[y][x+1] == '#') &&
                            openNeighbors(x, y, map) >= 2) {
                            edgeCells.push_back({x, y});
                        }
                    }
                }
                if (!edgeCells.empty()) {
                    shuffleVector(edgeCells);
                    p = edgeCells[0];
                }
            }
            if (p.first < 0) break;
            map[p.second][p.first] = '.';
            goals.push_back(p);
        }
        if ((int)goals.size() < boxCount) continue;

        // 放置箱子（在目标附近，避开角落）
        std::vector<Pos> boxes;
        bool placementOk = true;
        int boxSearchRadius = (diff == Difficulty::Hell) ? 4 : 2;
        for (int b = 0; b < boxCount; ++b) {
            const Pos& goal = goals[b];
            std::vector<Pos> candidates;
            for (int dy = -boxSearchRadius; dy <= boxSearchRadius; ++dy) {
                for (int dx = -boxSearchRadius; dx <= boxSearchRadius; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int dist = std::abs(dx) + std::abs(dy);
                    int cx = goal.first + dx, cy = goal.second + dy;
                    if (cx < 1 || cx >= roomW-1 || cy < 1 || cy >= roomH-1) continue;
                    if (map[cy][cx] != ' ') continue;
                    // Hell 优先远距离（更难）
                    if (diff == Difficulty::Hell && dist < 2) continue;
                    // 不能是角落位置（2面墙）
                    if (isWall(cx, cy-1, map) && isWall(cx-1, cy, map)) continue;
                    if (isWall(cx, cy-1, map) && isWall(cx+1, cy, map)) continue;
                    if (isWall(cx, cy+1, map) && isWall(cx-1, cy, map)) continue;
                    if (isWall(cx, cy+1, map) && isWall(cx+1, cy, map)) continue;
                    if (openNeighbors(cx, cy, map) < 2) continue;
                    candidates.push_back({cx, cy});
                }
            }
            if (candidates.empty()) { placementOk = false; break; }
            shuffleVector(candidates);
            Pos boxPos = candidates[0];
            bool taken = false;
            for (auto& eb : boxes) if (eb == boxPos) { taken = true; break; }
            if (taken) { placementOk = false; break; }
            map[boxPos.second][boxPos.first] = '$';
            boxes.push_back(boxPos);
        }
        if (!placementOk) continue;

        // 放置玩家（在一个开放区域）
        std::vector<Pos> playerCandidates;
        for (int y = 2; y < roomH-2; ++y)
            for (int x = 2; x < roomW-2; ++x)
                if (map[y][x] == ' ' && openNeighbors(x, y, map) >= 2)
                    playerCandidates.push_back({x, y});
        if (playerCandidates.empty()) continue;
        shuffleVector(playerCandidates);
        int pIdx = 0;
        for (; pIdx < (int)playerCandidates.size(); ++pIdx) {
            auto [px, py] = playerCandidates[pIdx];
            // 玩家不能在目标上
            if (map[py][px] != ' ') continue;
            map[py][px] = '@';
            playerCandidates[0] = {px, py};
            break;
        }
        if (pIdx >= (int)playerCandidates.size()) continue;

        // 验证
        if (!mapHasDeadlock(map) && hasValidFirstPush(map, playerCandidates[0].first, playerCandidates[0].second)) {
            GeneratedLevel result;
            result.map = map;
            result.playerX = playerCandidates[0].first;
            result.playerY = playerCandidates[0].second;
            result.width = roomW;
            result.height = roomH;
            result.valid = true;
            return result;
        }
    }

    // 生成失败，返回空
    GeneratedLevel result;
    result.valid = false;
    return result;
}

// mapHasDeadlock 的前向声明已在 create_map.h 中
