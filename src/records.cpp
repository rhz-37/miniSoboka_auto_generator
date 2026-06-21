#include "records.h"
#include <ctime>
#include <algorithm>
#include <direct.h>
#include <io.h>

// 记录文件存储路径：项目根目录下的 records/
std::string RecordManager::recordsPath() {
    // 先尝试从可执行文件所在目录查找，回退到当前目录
    return "records";
}

// 确保目录存在
static void ensureRecordsDir() {
    std::string dir = RecordManager::recordsPath();
    if (_access(dir.c_str(), 0) != 0) {
        _mkdir(dir.c_str());
    }
}

// 将记录序列化为一行文本
static std::string serializeRecord(const LevelRecord& rec) {
    std::ostringstream oss;
    oss << (int)rec.difficulty << "|"
        << rec.levelIndex << "|"
        << rec.totalGoals << "|"
        << rec.timeSeconds << "|"
        << rec.steps << "|"
        << rec.dateStr << "|"
        << (int)rec.levelData.size() << "|";
    // 关卡地图数据
    for (const auto& row : rec.levelData)
        oss << row << "|";
    // 操作序列：U/D/L/R
    for (const auto& m : rec.moves) {
        if (m.dx == 0 && m.dy == -1) oss << 'U';
        else if (m.dx == 0 && m.dy == 1) oss << 'D';
        else if (m.dx == -1 && m.dy == 0) oss << 'L';
        else if (m.dx == 1 && m.dy == 0) oss << 'R';
    }
    oss << "|END";
    return oss.str();
}

// 反序列化一行文本为记录
static LevelRecord deserializeRecord(const std::string& line) {
    LevelRecord rec;
    rec.difficulty = Difficulty::Easy;
    rec.levelIndex = 0;
    rec.totalGoals = 0;
    rec.timeSeconds = 0;
    rec.steps = 0;
    rec.dateStr = "";

    std::istringstream iss(line);
    std::string token;
    int field = 0;
    int levelDataRows = 0;
    std::string movesStr;

    while (std::getline(iss, token, '|')) {
        switch (field++) {
            case 0: rec.difficulty = (Difficulty)std::stoi(token); break;
            case 1: rec.levelIndex = std::stoi(token); break;
            case 2: rec.totalGoals = std::stoi(token); break;
            case 3: rec.timeSeconds = std::stod(token); break;
            case 4: rec.steps = std::stoi(token); break;
            case 5: rec.dateStr = token; break;
            case 6:
                // 新格式：levelDataRows 是数字；旧格式：该字段是 moves 字符串
                if (!token.empty() && token[0] >= '0' && token[0] <= '9') {
                    levelDataRows = std::stoi(token);
                } else {
                    movesStr = token;
                }
                break;
            default:
                if (levelDataRows > 0 && field - 7 < levelDataRows) {
                    rec.levelData.push_back(token);
                } else if (token != "END") {
                    movesStr = token;
                }
                break;
        }
    }

    // 解析操作序列
    for (char c : movesStr) {
        MoveRecord mr;
        mr.dx = 0; mr.dy = 0;
        switch (c) {
            case 'U': mr.dy = -1; break;
            case 'D': mr.dy = 1; break;
            case 'L': mr.dx = -1; break;
            case 'R': mr.dx = 1; break;
            default: continue;
        }
        if (mr.dx != 0 || mr.dy != 0) {
            rec.moves.push_back(mr);
        }
    }

    return rec;
}

bool RecordManager::saveRecord(const LevelRecord& rec) {
    ensureRecordsDir();

    std::string filename = recordsPath() + "/sokoban_records.txt";

    // 读取已有记录
    std::vector<LevelRecord> records;
    std::ifstream infile(filename);
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            if (line.empty()) continue;
            records.push_back(deserializeRecord(line));
        }
        infile.close();
    }

    // 检查是否有同关卡的更好记录
    bool hasBetter = false;
    for (auto& r : records) {
        if (r.difficulty == rec.difficulty && r.levelIndex == rec.levelIndex) {
            // 步数优先，步数相同用时更少
            if (r.steps < rec.steps || (r.steps == rec.steps && r.timeSeconds <= rec.timeSeconds)) {
                hasBetter = true;
                break;
            }
            // 替换为更好的记录
            r = rec;
            hasBetter = true;
            break;
        }
    }

    if (!hasBetter) {
        records.push_back(rec);
    }

    // 写回文件
    std::ofstream outfile(filename);
    if (!outfile.is_open()) return false;

    for (const auto& r : records) {
        outfile << serializeRecord(r) << "\n";
    }
    outfile.close();
    return true;
}

LevelRecord RecordManager::loadBest(Difficulty d, int levelIndex) {
    std::string filename = recordsPath() + "/sokoban_records.txt";
    std::ifstream infile(filename);
    if (!infile.is_open()) return LevelRecord();

    std::string line;
    LevelRecord best;
    best.steps = 999999;
    best.timeSeconds = 999999.0;

    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        LevelRecord rec = deserializeRecord(line);
        if (rec.difficulty == d && rec.levelIndex == levelIndex) {
            if (rec.steps < best.steps ||
                (rec.steps == best.steps && rec.timeSeconds < best.timeSeconds)) {
                best = rec;
            }
        }
    }
    infile.close();
    return best;
}

bool RecordManager::deleteRecord(int index) {
    ensureRecordsDir();
    std::string filename = recordsPath() + "/sokoban_records.txt";

    std::vector<LevelRecord> records;
    std::ifstream infile(filename);
    if (infile.is_open()) {
        std::string line;
        while (std::getline(infile, line)) {
            if (line.empty()) continue;
            records.push_back(deserializeRecord(line));
        }
        infile.close();
    }

    if (index < 0 || index >= (int)records.size()) return false;
    records.erase(records.begin() + index);

    std::ofstream outfile(filename);
    if (!outfile.is_open()) return false;
    for (const auto& r : records) {
        outfile << serializeRecord(r) << "\n";
    }
    outfile.close();
    return true;
}

bool RecordManager::hasRecord(Difficulty d, int levelIndex) {
    LevelRecord r = loadBest(d, levelIndex);
    return r.steps > 0 && r.steps < 999999;
}

std::vector<LevelRecord> RecordManager::listAll() {
    std::vector<LevelRecord> result;
    std::string filename = recordsPath() + "/sokoban_records.txt";
    std::ifstream infile(filename);
    if (!infile.is_open()) return result;

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;
        result.push_back(deserializeRecord(line));
    }
    infile.close();
    return result;
}
