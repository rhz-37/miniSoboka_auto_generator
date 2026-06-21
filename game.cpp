#include "game.h"
#include "create_map.h"
#include "solver.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <mmsystem.h>  // for MCI BGM
#include <ctime>
#include <algorithm>

// ============================================================
// 着色器源码
// ============================================================
static const char* VERTEX_SHADER_SRC = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uProj;
uniform mat4 uModel;
void main() {
    gl_Position = uProj * uModel * vec4(aPos, 0.0, 1.0);
}
)";

static const char* FRAGMENT_SHADER_SRC = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

// ============================================================
// 文字着色器
// ============================================================
// ============================================================
// GDI 辅助
// ============================================================
// (文字使用 Windows GDI 原生绘制，不再需要位图字体数据)


// ============================================================
// 着色器编译辅助
// ============================================================
static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "Shader compile error: " << infoLog << std::endl;
    }
    return shader;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(prog, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "Program link error: " << infoLog << std::endl;
    }
    return prog;
}

// ============================================================
// Construction / Destruction
// ============================================================
Game::Game()
    : window(nullptr)
    , winWidth(800), winHeight(600)
    , gameState(GameState::Menu)
    , currentDifficulty(Difficulty::Easy)
    , currentLevelIndex(0)
    , totalGoals(0), totalBoxes(0)
    , playerX(0), playerY(0)
    , moves(0), undoCount(0)
    , elapsedTime(0), gameStartTime(0)
    , menuSelection(0)
    , menuAnimTime(0)
    , recordListScroll(0)
    , replayIndex(0), replayTimer(0), replayStarted(false)
    , shaderProgram(0), vao(0), vbo(0)
{
}

Game::~Game() {
    // 停止 BGM
    mciSendString("stop bgm", NULL, 0, NULL);
    mciSendString("close bgm", NULL, 0, NULL);

    destroyOverlay();
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (shaderProgram) glDeleteProgram(shaderProgram);
    for (auto& p : fontCache) {
        if (p.second) DeleteObject(p.second);
    }
    fontCache.clear();
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

// ============================================================
// Init
// ============================================================
bool Game::init(int width, int height) {
    winWidth = width;
    winHeight = height;

    // 从 exe 所在目录往上找 resource/ 目录
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string rootDir(exePath);
    size_t pos = rootDir.find_last_of("\\/");
    if (pos != std::string::npos) rootDir = rootDir.substr(0, pos);
    // 向上搜索直到找到 resource 目录
    while (!rootDir.empty()) {
        std::string testPath = rootDir + "\\resource\\1.mp3";
        FILE* f = fopen(testPath.c_str(), "rb");
        if (f) { fclose(f); break; }
        size_t p = rootDir.find_last_of("\\/");
        if (p == std::string::npos || p < 2) { rootDir = ""; break; }
        rootDir = rootDir.substr(0, p);
    }
    if (rootDir.empty()) {
        // fallback: exe 所在目录
        pos = std::string(exePath).find_last_of("\\/");
        rootDir = std::string(exePath).substr(0, pos);
    }
    std::cout << "Root dir: " << rootDir << std::endl;

    // 启动 BGM
    {
        std::string mp3Path = rootDir + "\\resource\\1.mp3";
        std::cout << "BGM path: " << mp3Path << std::endl;

        // 检查文件是否存在
        FILE* f = fopen(mp3Path.c_str(), "rb");
        if (f) {
            fclose(f);
            // 前后空格都不可有，两次尝试（有/无 type）
            std::string cmd = "open \"" + mp3Path + "\" alias bgm";
            MCIERROR mciErr = mciSendStringA(cmd.c_str(), NULL, 0, NULL);
            if (mciErr) {
                // 无 type 失败则尝试带 type
                cmd = "open \"" + mp3Path + "\" type mpegvideo alias bgm";
                mciErr = mciSendStringA(cmd.c_str(), NULL, 0, NULL);
            }
            if (!mciErr) {
                mciErr = mciSendStringA("play bgm repeat", NULL, 0, NULL);
                if (!mciErr) std::cout << "BGM playing" << std::endl;
            }
            if (mciErr) {
                char buf[256];
                mciGetErrorStringA(mciErr, buf, 256);
                std::cerr << "MCI error: " << buf << std::endl;
            }
        } else {
            std::cerr << "BGM file not found at: " << mp3Path << std::endl;
        }
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, "Sokoban", glfwGetPrimaryMonitor(), nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // 设置窗口图标（sokoban.bmp — 缩放到 64×64）
    {
        std::string bmpPath = rootDir + "\\resource\\sokoban.bmp";
        std::cout << "Icon path: " << bmpPath << std::endl;
        HBITMAP hBmp = (HBITMAP)LoadImageA(NULL, bmpPath.c_str(), IMAGE_BITMAP,
            0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (hBmp) {
            BITMAP bm = {};
            GetObject(hBmp, sizeof(bm), &bm);
            int iw = bm.bmWidth, ih = bm.bmHeight;
            unsigned char* src = (unsigned char*)bm.bmBits;
            int srcBPP = bm.bmBitsPixel;
            int srcStride = ((iw * srcBPP + 31) / 32) * 4;

            // 缩放到 64×64
            const int ICON_SZ = 64;
            std::vector<unsigned char> pixels(ICON_SZ * ICON_SZ * 4);
            for (int dy = 0; dy < ICON_SZ; ++dy) {
                for (int dx = 0; dx < ICON_SZ; ++dx) {
                    int sx = dx * iw / ICON_SZ;
                    int sy = dy * ih / ICON_SZ;
                    if (sx >= iw) sx = iw - 1;
                    if (sy >= ih) sy = ih - 1;
                    int bytesPerPixel = srcBPP / 8;
                    int si = sy * srcStride + sx * bytesPerPixel;
                    int di = (dy * ICON_SZ + dx) * 4;
                    pixels[di + 0] = src[si + 2];  // R
                    pixels[di + 1] = src[si + 1];  // G
                    pixels[di + 2] = src[si + 0];  // B
                    pixels[di + 3] = (bytesPerPixel == 4) ? src[si + 3] : 255;  // A
                }
            }
            std::cout << "Icon set: " << iw << "x" << ih << " → 64x64" << std::endl;
            GLFWimage icon = { ICON_SZ, ICON_SZ, pixels.data() };
            glfwSetWindowIcon(window, 1, &icon);
            DeleteObject(hBmp);
        } else {
            std::cerr << "Icon file not found at: " << bmpPath << std::endl;
        }
    }

    glfwMakeContextCurrent(window);

    // 全屏窗口：获取实际分辨率
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    winWidth = fbW;
    winHeight = fbH;
    std::cout << "Fullscreen: " << fbW << "x" << fbH << std::endl;

    // Matrix Rain 在正确的窗口尺寸下初始化
    initRain();

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // 纯黑背景
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!initShaders()) return false;
    initQuad();
    initGDI();
    initShaders3D();
    initCube();

    projection = glm::ortho(0.0f, (float)fbW, (float)fbH, 0.0f, -1.0f, 1.0f);

    // Callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int sc, int action, int mods) {
        auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
        if (g) g->keyCallback(key, action, mods);
    });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int ww, int wh) {
        auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
        if (g) g->resizeCallback(ww, wh);
    });

    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int btn, int act, int mods) {
        double x, y; glfwGetCursorPos(w, &x, &y);
        auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
        if (g) g->mouseButtonCallback(btn, act, x, y);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* g = static_cast<Game*>(glfwGetWindowUserPointer(w));
        if (g) g->cursorPosCallback(x, y);
    });
    std::srand((unsigned)std::time(nullptr));

    return true;
}

// ============================================================
// Update
// ============================================================
void Game::update(float deltaTime) {
    updateRain(deltaTime);
    glfwPollEvents();

    if (gameState == GameState::Playing3D) {
        if (moveCooldown > 0) moveCooldown -= deltaTime;
        // 3D WASD movement（按相机朝向选主方向）
        auto getDir = [](float yaw) -> std::pair<int,int> {
            float fdx = sinf(yaw), fdy = cosf(yaw);
            if (fabsf(fdx) > fabsf(fdy))
                return {fdx > 0 ? 1 : -1, 0};
            else
                return {0, fdy > 0 ? 1 : -1};
        };
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            auto [dx, dy] = getDir(camYaw);
            movePlayer3D(dx, dy);
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            auto [dx, dy] = getDir(camYaw);
            movePlayer3D(-dx, -dy);
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            auto [dx, dy] = getDir(camYaw - 1.5708f);  // -90° = left
            movePlayer3D(dx, dy);
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            auto [dx, dy] = getDir(camYaw + 1.5708f);  // +90° = right
            movePlayer3D(dx, dy);
        }
        // Check win
        if (isWon()) {
            gameState = GameState::Won;
            updateTitle();
        }
    } else if (gameState == GameState::Playing) {
        if (!isWon()) {
            elapsedTime = glfwGetTime() - gameStartTime;
        }
    } else if (gameState == GameState::Menu) {
        menuAnimTime += deltaTime;
    } else if (gameState == GameState::Replaying) {
        if (replayIndex < (int)replayMoves.size()) {
            replayTimer += deltaTime;
            if (replayTimer >= REPLAY_INTERVAL) {
                replayTimer = 0;
                const auto& m = replayMoves[replayIndex++];
                if (!movePlayer(m.dx, m.dy)) {
                    // 移动失败（理论上重放不会失败，但有备无患）
                }
                // 重放时不计步
            }
        } else if (replayStarted) {
            // 重放完毕
            replayStarted = false;
        }
    }
}

// ============================================================
// Render — Main Dispatcher
// ============================================================
void Game::render() {
    if (gameState == GameState::Playing3D) {
        // 3D 模式：直接渲染 3D 场景 + UI 覆盖
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        render3D();
        glDisable(GL_DEPTH_TEST);
        clearButtons();
        addButton("Undo", 20, winHeight - 42, 80, 34, 14, [this](){
            // Simple undo for 3D
            if (!movesHistory.empty()) {
                movesHistory.pop_back();
                resetLevel();
                --moves;
            }
        });
        addButton("Menu", 110, winHeight - 42, 80, 34, 14, [this](){ gameState = GameState::Menu; });
        drawButtons(-1);
        clearMemDC();
        updateOverlayTexture();
        drawOverlay(1.0f);
        glfwSwapBuffers(window);
        return;
    }
    glClear(GL_COLOR_BUFFER_BIT);

    // ── Pass 1: Matrix Rain 文字（半透明 overlay）──
    clearMemDC();
    renderRainGDI();
    updateOverlayTexture();
    drawOverlay(0.35f);  // 35% 透明度

    // ── Pass 2: 关卡地图（OpenGL 渲染）──
    clearMemDC();
    switch (gameState) {
        case GameState::Menu:      /* no tiles */ break;
        case GameState::Playing:
        case GameState::Won:
        case GameState::Replaying: renderLevel(); break;
        case GameState::Playing3D:  /* rendered in Pass 3 */ break;
        default: break;
    }

    // ── Pass 3: UI 覆盖层（GDI 按钮/文字）──
    clearButtons();
    switch (gameState) {
        case GameState::Menu:      renderMenu(); break;
        case GameState::Playing:   renderPlaying(); break;
        case GameState::Won:       renderPlaying(); renderWon(); break;
        case GameState::Records:   renderRecords(); break;
        case GameState::Replaying: renderReplaying(); break;
    }
    checkButtonClicks();
    int kbdSel = (gameState == GameState::Menu) ? menuSelection : -1;
    drawButtons(kbdSel);

    updateOverlayTexture();
    drawOverlay(1.0f);

    glfwSwapBuffers(window);
}

// ============================================================
// Mouse Callbacks
// ============================================================
void Game::mouseButtonCallback(int button, int action, double x, double y) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        prevMouseDown = mouseDown;
        mouseDown = (action == GLFW_PRESS);
        mouseX = x;
        mouseY = y;
    }
}

void Game::cursorPosCallback(double x, double y) {
    if (gameState == GameState::Playing3D && mouseDown) {
        double dx = x - mouseX;
        double dy = y - mouseY;
        camYaw += (float)dx * 0.001f;
        camPitch += (float)dy * 0.001f;
        if (camPitch > 1.4f) camPitch = 1.4f;
        if (camPitch < -1.4f) camPitch = -1.4f;
    }
    mouseX = x;
    mouseY = y;
}

// ============================================================
// Key Callback
// ============================================================
void Game::keyCallback(int key, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    switch (gameState) {
    case GameState::Menu:
        if (key == GLFW_KEY_UP || key == GLFW_KEY_W) {
            menuSelection = (menuSelection - 1 + 5) % 5;
        } else if (key == GLFW_KEY_DOWN || key == GLFW_KEY_S) {
            menuSelection = (menuSelection + 1) % 5;
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_SPACE) {
            if (menuSelection == 4) {
                // 查看记录
                allRecords = RecordManager::listAll();
                recordListScroll = 0;
                gameState = GameState::Records;
            } else {
                currentDifficulty = (Difficulty)menuSelection;
                startGame(currentDifficulty, std::rand() % LEVELS_PER_DIFFICULTY);
            }
        } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) {
            int idx = key - GLFW_KEY_1;
            if (idx == 4) {
                allRecords = RecordManager::listAll();
                recordListScroll = 0;
                gameState = GameState::Records;
            } else {
                currentDifficulty = (Difficulty)idx;
                startGame(currentDifficulty, std::rand() % LEVELS_PER_DIFFICULTY);
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, true);
        }
        break;

    case GameState::Playing:
        if (isWon()) {
            // 通关后按键进入下一关或返回菜单
            if (key == GLFW_KEY_N || key == GLFW_KEY_ENTER) {
                // 下一关（同一难度随机）
                startGame(currentDifficulty, std::rand() % LEVELS_PER_DIFFICULTY);
            } else if (key == GLFW_KEY_R) {
                startGame(currentDifficulty, currentLevelIndex);
            } else if (key == GLFW_KEY_M || key == GLFW_KEY_ESCAPE) {
                gameState = GameState::Menu;
            }
            return;
        }

        // 方向
        {
            int dx = 0, dy = 0;
            if (key == GLFW_KEY_UP    || key == GLFW_KEY_W) dy = -1;
            if (key == GLFW_KEY_DOWN  || key == GLFW_KEY_S) dy =  1;
            if (key == GLFW_KEY_LEFT  || key == GLFW_KEY_A) dx = -1;
            if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_D) dx =  1;
            if ((dx != 0 || dy != 0) && movePlayer(dx, dy)) {
                ++moves;
                if (isWon()) {
                    // 通关：保存记录
                    LevelRecord rec;
                    rec.difficulty = currentDifficulty;
                    rec.levelIndex = currentLevelIndex;
                    rec.totalGoals = totalGoals;
                    rec.timeSeconds = elapsedTime;
                    rec.steps = moves;
                    std::time_t t = std::time(nullptr);
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
                    rec.dateStr = buf;
                    rec.moves = movesHistory;
                    // 填充所有行到 mapWidth（防止最后一行被截断）
                    rec.levelData = map;
                    for (auto& row : rec.levelData)
                        while ((int)row.size() < mapWidth) row += ' ';
                    RecordManager::saveRecord(rec);
                    gameState = GameState::Won;
                    updateTitle();
                }
            }
        }

        if (key == GLFW_KEY_Z && (mods & GLFW_MOD_CONTROL)) {
            undoMove();
        } else if (key == GLFW_KEY_Z) {
            undoMove();
        } else if (key == GLFW_KEY_R) {
            resetLevel();
        } else if (key == GLFW_KEY_M || key == GLFW_KEY_ESCAPE) {
            // 退出时保存操作记录
            {
                LevelRecord rec;
                rec.difficulty = currentDifficulty;
                rec.levelIndex = currentLevelIndex;
                rec.totalGoals = totalGoals;
                rec.timeSeconds = elapsedTime;
                rec.steps = moves;
                std::time_t t = std::time(nullptr);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
                rec.dateStr = buf;
                rec.moves = movesHistory;
                rec.levelData = map;
                for (auto& row : rec.levelData)
                    while ((int)row.size() < mapWidth) row += ' ';
                RecordManager::saveRecord(rec);
            }
            gameState = GameState::Menu;
        }
        break;

    case GameState::Won:
        if (key == GLFW_KEY_N || key == GLFW_KEY_ENTER || key == GLFW_KEY_SPACE) {
            startGame(currentDifficulty, std::rand() % LEVELS_PER_DIFFICULTY);
        } else if (key == GLFW_KEY_R) {
            startGame(currentDifficulty, currentLevelIndex);
        } else if (key == GLFW_KEY_M || key == GLFW_KEY_ESCAPE) {
            gameState = GameState::Menu;
        }
        break;

    case GameState::Records:
        if (key == GLFW_KEY_UP || key == GLFW_KEY_W) {
            recordListScroll = std::max(0, recordListScroll - 1);
        } else if (key == GLFW_KEY_DOWN || key == GLFW_KEY_S) {
            recordListScroll = std::min((int)allRecords.size() - 1, recordListScroll + 1);
        } else if (key == GLFW_KEY_R && !allRecords.empty()) {
            int idx = recordListScroll;
            if (idx >= 0 && idx < (int)allRecords.size()) {
                startReplay(allRecords[idx]);
            }
        } else if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) && !allRecords.empty()) {
            RecordManager::deleteRecord(recordListScroll);
            allRecords = RecordManager::listAll();
            if (recordListScroll >= (int)allRecords.size())
                recordListScroll = std::max(0, (int)allRecords.size() - 1);
        } else if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_M) {
            gameState = GameState::Menu;
        }
        break;

    case GameState::Playing3D:
        if (key == GLFW_KEY_Z) {
            if (!undoStack.empty()) {
                auto ss = undoStack.back(); undoStack.pop_back();
                map = ss.map; playerX = ss.playerX; playerY = ss.playerY;
                if (!movesHistory.empty()) movesHistory.pop_back();
                if (moves > 0) --moves;
            }
        } else if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_M) {
            gameState = GameState::Menu;
        }
        break;

    case GameState::Replaying:
        if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_M) {
            gameState = GameState::Menu;
        } else if (key == GLFW_KEY_SPACE || key == GLFW_KEY_ENTER) {
            gameState = GameState::Menu;
        }
        break;
    }
}

// ============================================================
// Resize
// ============================================================
void Game::resizeCallback(int width, int height) {
    winWidth = width;
    winHeight = height;
    glViewport(0, 0, width, height);
    projection = glm::ortho(0.0f, (float)width, (float)height, 0.0f, -1.0f, 1.0f);

    // 重建内存位图
    destroyOverlay();
    HDC screenDC = GetDC(hwnd);
    memDC = CreateCompatibleDC(screenDC);
    memBmpW = width;
    memBmpH = height;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    memBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void**)&memBits, NULL, 0);
    if (memBmp) {
        SelectObject(memDC, memBmp);
        memset(memBits, 0, (size_t)width * height * 4);
    }
    ReleaseDC(hwnd, screenDC);
    initOverlay();
    overlayDirty = true;
}

// ============================================================
// Game Flow
// ============================================================
void Game::startGame(Difficulty d, int lvlIdx, int depth) {
    currentDifficulty = d;
    currentLevelIndex = lvlIdx;

    if (lvlIdx >= 1) {
        // 随机生成关卡（lvlIdx ≥ 1 时）
        GeneratedLevel gen = generateLevel(d, lvlIdx);
        if (gen.valid) {
            map = gen.map;
            mapWidth = gen.width;
            mapHeight = gen.height;
            playerX = gen.playerX;
            playerY = gen.playerY;
        } else {
            // 生成失败时 fallback 到硬编码
            const auto& rows = getLevelByIndex(d, 0);
            map.clear();
            mapWidth = 0;
            for (const auto& row : rows)
                if ((int)row.size() > mapWidth) mapWidth = (int)row.size();
            for (const auto& row : rows) {
                std::string s = row;
                while ((int)s.size() < mapWidth) s += ' ';
                map.push_back(s);
            }
            mapHeight = (int)map.size();
            playerX = playerY = -1;
            for (int y = 0; y < mapHeight; ++y)
                for (int x = 0; x < mapWidth; ++x)
                    if (map[y][x] == '@' || map[y][x] == '+')
                        { playerX = x; playerY = y; }
        }
    } else {
        // lvlIdx == 0: 使用硬编码关卡
        const auto& rows = getLevelByIndex(d, lvlIdx);
        map.clear();
        mapWidth = 0;
        for (const auto& row : rows)
            if ((int)row.size() > mapWidth) mapWidth = (int)row.size();
        for (const auto& row : rows) {
            std::string s = row;
            while ((int)s.size() < mapWidth) s += ' ';
            map.push_back(s);
        }
        mapHeight = (int)map.size();
        playerX = playerY = -1;
        for (int y = 0; y < mapHeight; ++y)
            for (int x = 0; x < mapWidth; ++x)
                if (map[y][x] == '@' || map[y][x] == '+')
                    { playerX = x; playerY = y; }
    }

    moves = 0;
    undoCount = 0;
    elapsedTime = 0;
    gameStartTime = glfwGetTime();
    undoStack.clear();
    movesHistory.clear();
    gameState = GameState::Playing;
    countGoalsAndBoxes();

    // 关卡验证：三层死局检测 + 开局遍历
    // depth 保护：最多尝试 LEVELS_PER_DIFFICULTY 次，全失败则强制通过
    if (depth < LEVELS_PER_DIFFICULTY && (isUnwinnable() || !hasValidFirstPush())) {
        startGame(d, (lvlIdx + 1) % LEVELS_PER_DIFFICULTY, depth + 1);
        return;
    }

    // 不再在进入时保存记录，改为退出/通关时保存（含操作序列）

    updateTitle();
}

void Game::resetLevel() {
    const auto& rows = getLevelByIndex(currentDifficulty, currentLevelIndex);
    for (int y = 0; y < mapHeight && y < (int)rows.size(); ++y) {
        std::string s = rows[y];
        while ((int)s.size() < mapWidth) s += ' ';
        map[y] = s;
    }
    for (int y = 0; y < mapHeight; ++y)
        for (int x = 0; x < mapWidth; ++x)
            if (map[y][x] == '@' || map[y][x] == '+')
                { playerX = x; playerY = y; }

    moves = 0;
    undoCount = 0;
    elapsedTime = 0;
    gameStartTime = glfwGetTime();
    undoStack.clear();
    movesHistory.clear();
    countGoalsAndBoxes();
    updateTitle();
}

bool Game::movePlayer(int dx, int dy) {
    int nx = playerX + dx;
    int ny = playerY + dy;
    if (nx < 0 || nx >= mapWidth || ny < 0 || ny >= mapHeight) return false;

    char target = map[ny][nx];

    // 墙
    if (target == '#') return false;

    // —— 保存快照 + 操作方向（撤回 / 重放用） ——
    if (!isWon()) {
        undoStack.push_back({map, playerX, playerY});
        if ((int)undoStack.size() > MAX_UNDO)
            undoStack.erase(undoStack.begin());
        movesHistory.push_back({dx, dy});
        if ((int)movesHistory.size() > MAX_UNDO * 2)
            movesHistory.erase(movesHistory.begin());
    }

    // —— 箱子 ——
    if (target == '$' || target == '*') {
        int bx = nx + dx;
        int by = ny + dy;
        if (bx < 0 || bx >= mapWidth || by < 0 || by >= mapHeight) return false;
        char beyond = map[by][bx];
        if (beyond == '#' || beyond == '$' || beyond == '*') return false;

        char oldPlayer = map[playerY][playerX];
        map[playerY][playerX] = (oldPlayer == '+') ? '.' : ' ';
        map[by][bx] = (beyond == '.') ? '*' : '$';
        bool wasOnGoal = (target == '*');
        map[ny][nx] = wasOnGoal ? '.' : ' ';
        char targetTile = map[ny][nx];
        map[ny][nx] = (targetTile == '.') ? '+' : '@';
        playerX = nx;
        playerY = ny;
        return true;
    }

    // —— 空地 / 目标 ——
    {
        char oldPlayer = map[playerY][playerX];
        map[playerY][playerX] = (oldPlayer == '+') ? '.' : ' ';
        playerX = nx;
        playerY = ny;
        map[playerY][playerX] = (target == '.') ? '+' : '@';
    }
    return true;
}

void Game::undoMove() {
    if (undoStack.empty()) return;
    auto snap = undoStack.back();
    undoStack.pop_back();
    map = snap.map;
    playerX = snap.playerX;
    playerY = snap.playerY;
    moves = std::max(0, moves - 1);
    ++undoCount;
    if (!movesHistory.empty()) movesHistory.pop_back();
    updateTitle();
}

// ============================================================
// Deadlock Detection
// ============================================================
bool Game::isGoal(int x, int y) const {
    if (y < 0 || y >= (int)map.size() || x < 0 || x >= (int)map[y].size()) return false;
    char c = map[y][x];
    return c == '.' || c == '+' || c == '*';
}

bool Game::isBoxAt(int x, int y) const {
    if (y < 0 || y >= (int)map.size() || x < 0 || x >= (int)map[y].size()) return false;
    char c = map[y][x];
    return c == '$' || c == '*';
}

bool Game::isCornerDeadlock(int x, int y) const {
    // 在目标点上的箱子永远不会死局
    if (isGoal(x, y)) return false;

    // 检查四面的墙
    bool upWall   = (y <= 0)                  || map[y-1][x] == '#';
    bool downWall = (y >= (int)map.size()-1)  || map[y+1][x] == '#';
    bool leftWall = (x <= 0)                  || map[y][x-1] == '#';
    bool rightWall= (x >= (int)map[y].size()-1)|| map[y][x+1] == '#';

    // 角落死局：垂直方向有一个被堵 + 水平方向有一个被堵
    return (upWall || downWall) && (leftWall || rightWall);
}

// ============================================================
// Level Validation — 委托 create_map 完成
// ============================================================
using Pos = std::pair<int,int>;

bool Game::isUnwinnable() const {
    return mapHasDeadlock(map);
}

bool Game::hasValidFirstPush() const {
    std::vector<Pos> boxes;
    for (int y = 0; y < mapHeight; ++y)
        for (int x = 0; x < mapWidth; ++x)
            if (map[y][x] == '$' || map[y][x] == '*')
                boxes.push_back({x, y});

    std::set<Pos> reachable;
    std::queue<Pos> q;
    reachable.insert({playerX, playerY});
    q.push({playerX, playerY});
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        for (auto& d : dirs) {
            int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || nx >= mapWidth || ny < 0 || ny >= mapHeight) continue;
            if (reachable.find({nx, ny}) != reachable.end()) continue;
            char nc = map[ny][nx];
            if (nc == ' ' || nc == '.') {
                reachable.insert({nx, ny});
                q.push({nx, ny});
            }
        }
    }

    for (auto& [bx, by] : boxes) {
        for (auto& d : dirs) {
            int px = bx - d[0], py = by - d[1];
            int tx = bx + d[0], ty = by + d[1];
            if (reachable.find({px, py}) == reachable.end()) continue;
            if (tx < 0 || tx >= mapWidth || ty < 0 || ty >= mapHeight) continue;
            char tc = map[ty][tx];
            if (tc == '#' || tc == '$' || tc == '*') continue;

            std::vector<std::string> simMap = map;
            simMap[by][bx] = (simMap[by][bx] == '*') ? '.' : ' ';
            simMap[ty][tx] = (tc == '.') ? '*' : '$';
            if (!mapHasDeadlock(simMap)) return true;
        }
    }
    return false;
}

// ============================================================
// Win / Count
// ============================================================
bool Game::isWon() const {
    // 所有目标点必须被箱子覆盖才算胜利（玩家站在目标点上不算）
    for (const auto& row : map)
        for (char c : row)
            if (c == '.' || c == '+') return false; // '.'=空目标 '+'=玩家站目标
    return totalGoals > 0;
}

void Game::countGoalsAndBoxes() {
    totalGoals = 0;
    totalBoxes = 0;
    for (const auto& row : map) {
        for (char c : row) {
            if (c == '.' || c == '+' || c == '*') ++totalGoals;
            if (c == '$' || c == '*') ++totalBoxes;
        }
    }
}

// ============================================================
// 旋转关卡
// ============================================================
void Game::rotateMap(bool clockwise) {
    if (map.empty()) return;
    int oldH = mapHeight, oldW = mapWidth;
    int newH = oldW, newW = oldH;

    std::vector<std::string> newMap(newH, std::string(newW, ' '));

    for (int y = 0; y < oldH; ++y) {
        for (int x = 0; x < oldW; ++x) {
            char c = map[y][x];
            int nx, ny;
            if (clockwise) {
                nx = y;          // x' = y
                ny = newH - 1 - x; // y' = oldW - 1 - x (在新高度中)
            } else {
                nx = oldH - 1 - y; // x' = oldH - 1 - y
                ny = x;           // y' = x (在新宽度中)
            }
            // 箱子/目标/玩家特殊字符处理
            if (c == '$' || c == '.' || c == '*' || c == '+') {
                newMap[ny][nx] = (c == '$') ? '$' : (c == '*') ? '*' : '.';
            } else if (c == '@') {
                newMap[ny][nx] = ' ';  // 玩家不复制，稍后重新放置
            } else {
                newMap[ny][nx] = c;
            }
        }
    }

    // 玩家位置旋转
    int newPx, newPy;
    if (clockwise) {
        newPx = playerY;
        newPy = newH - 1 - playerX;
    } else {
        newPx = oldH - 1 - playerY;
        newPy = playerX;
    }

    // 确保新位置可通行
    if (newPy < 0 || newPy >= newH || newPx < 0 || newPx >= newW || newMap[newPy][newPx] != ' ') {
        newPy = 1; newPx = 1;
        for (int y = 1; y < newH-1 && newMap[newPy][newPx] != ' '; ++y)
            for (int x = 1; x < newW-1 && newMap[newPy][newPx] != ' '; ++x)
                { newPy = y; newPx = x; }
    }
    newMap[newPy][newPx] = '@';

    map = std::move(newMap);
    mapHeight = newH;
    mapWidth = newW;
    playerX = newPx;
    playerY = newPy;
    countGoalsAndBoxes();
    movesHistory.clear();
    undoCount = 0;
}

// ============================================================
// Replay
// ============================================================
void Game::startReplay(const LevelRecord& rec) {
    // 使用保存的关卡地图数据，确保重放的是同一关卡
    if (!rec.levelData.empty()) {
        map = rec.levelData;
        mapWidth = 0;
        for (const auto& row : map)
            if ((int)row.size() > mapWidth) mapWidth = (int)row.size();
        for (auto& row : map)
            while ((int)row.size() < mapWidth) row += ' ';
        mapHeight = (int)map.size();
        playerX = playerY = -1;
        for (int y = 0; y < mapHeight; ++y)
            for (int x = 0; x < mapWidth; ++x)
                if (map[y][x] == '@' || map[y][x] == '+')
                    { playerX = x; playerY = y; }
        currentDifficulty = rec.difficulty;
        currentLevelIndex = rec.levelIndex;
        moves = 0;
        undoCount = 0;
        elapsedTime = 0;
        movesHistory.clear();
        totalGoals = rec.totalGoals;
    } else {
        // 旧记录没有地图数据，fallback 到 startGame
        startGame(rec.difficulty, rec.levelIndex);
    }
    replayMoves = rec.moves;
    replayIndex = 0;
    replayTimer = 0;
    replayStarted = true;
    gameState = GameState::Replaying;
}

// ============================================================
// Title
// ============================================================
void Game::updateTitle() {
    if (gameState == GameState::Playing) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Sokoban - %s #%d  Steps:%d  Time:%.1fs  Undo:%d",
            difficultyName(currentDifficulty), currentLevelIndex + 1,
            moves, elapsedTime, undoCount);
        glfwSetWindowTitle(window, buf);
    } else if (gameState == GameState::Won) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "★ LEVEL CLEAR! Steps:%d  Time:%.1fs",
            moves, elapsedTime);
        glfwSetWindowTitle(window, buf);
    } else {
        glfwSetWindowTitle(window, "Sokoban");
    }
}

// ============================================================
// Shaders / Quad
// ============================================================
bool Game::initShaders() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER_SRC);
    if (!vs || !fs) return false;
    shaderProgram = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return shaderProgram != 0;
}

// ============================================================
// 3D 渲染
// ============================================================
static const char* VERTEX_3D_SRC = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* FRAGMENT_3D_SRC = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

static GLuint compileShader3D(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
        std::cerr << "3D shader error: " << log << std::endl;
        return 0;
    }
    return s;
}

static GLuint linkProgram3D(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(p, 512, NULL, log);
        std::cerr << "3D link error: " << log << std::endl;
        return 0;
    }
    return p;
}

bool Game::initShaders3D() {
    GLuint vs = compileShader3D(GL_VERTEX_SHADER, VERTEX_3D_SRC);
    GLuint fs = compileShader3D(GL_FRAGMENT_SHADER, FRAGMENT_3D_SRC);
    if (!vs || !fs) return false;
    shader3D = linkProgram3D(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    return shader3D != 0;
}

void Game::initCube() {
    float verts[] = {
        // back
        -0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f, 0.5f,-0.5f,
         0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f,-0.5f,-0.5f,
        // front
        -0.5f,-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,-0.5f, 0.5f,
        -0.5f,-0.5f, 0.5f,-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        // left
        -0.5f, 0.5f, 0.5f,-0.5f,-0.5f,-0.5f,-0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f,-0.5f,-0.5f, 0.5f, 0.5f,-0.5f,-0.5f, 0.5f,
        // right
         0.5f, 0.5f, 0.5f, 0.5f, 0.5f,-0.5f, 0.5f,-0.5f,-0.5f,
         0.5f,-0.5f,-0.5f, 0.5f,-0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        // bottom
        -0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f, 0.5f,
         0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f,-0.5f,
        // top
        -0.5f, 0.5f,-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,-0.5f,
        -0.5f, 0.5f,-0.5f,-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
    };
    glGenVertexArrays(1, &cubeVao);
    glGenBuffers(1, &cubeVbo);
    glBindVertexArray(cubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // 轮廓线（12 条边 × 2 顶点）
    float edges[] = {
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,
         0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
         0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f,
        -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,
        -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
         0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
         0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
         0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
         0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
    };
    glGenVertexArrays(1, &edgeVao);
    glGenBuffers(1, &edgeVbo);
    glBindVertexArray(edgeVao);
    glBindBuffer(GL_ARRAY_BUFFER, edgeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(edges), edges, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Game::initQuad() {
    float verts[] = {0,0, 1,0, 1,1, 0,1};
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Game::initGDI() {
    hwnd = glfwGetWin32Window(window);

    // 创建内存 DC 和 DIB 位图（文字绘制到此处，然后上传为纹理）
    HDC screenDC = GetDC(hwnd);
    memDC = CreateCompatibleDC(screenDC);
    memBmpW = winWidth;
    memBmpH = winHeight;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = winWidth;
    bmi.bmiHeader.biHeight = -winHeight; // 正=下到上，负=上到下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    memBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void**)&memBits, NULL, 0);
    if (memBmp) {
        SelectObject(memDC, memBmp);
        // 初始化为全透明
        memset(memBits, 0, (size_t)winWidth * winHeight * 4);
    }
    ReleaseDC(hwnd, screenDC);

    // 初始化 OpenGL 覆盖层
    initOverlay();
}

// ============================================================
// Overlay: GDI 位图 → OpenGL 纹理 → 全屏四边形
// ============================================================
static const char* OVERLAY_VERTEX_SRC = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 uProj;
out vec2 vTexCoord;
void main() {
    vTexCoord = aTexCoord;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";

static const char* OVERLAY_FRAGMENT_SRC = R"(
#version 330 core
uniform sampler2D uTex;
uniform float uAlpha = 1.0;
in vec2 vTexCoord;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vTexCoord);
    float bright = max(c.r, max(c.g, c.b));
    if (bright < 0.005) discard;
    // 亮度决定透明度：暗=半透明（雨滴），亮=不透明（按钮文字）
    float alpha = min(1.0, bright * 3.0);
    FragColor = vec4(c.rgb, alpha * uAlpha);
}
)";

bool Game::initOverlayShader() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, OVERLAY_VERTEX_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, OVERLAY_FRAGMENT_SRC);
    if (!vs || !fs) return false;
    overlayShader = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return overlayShader != 0;
}

void Game::initOverlay() {
    initOverlayShader();

    // 全屏四边形 (两个三角形: 6 顶点 × 4 分量 xy+uv)
    float verts[] = {
        0,          0,           0, 1, // BL
        (float)winWidth, 0,           1, 1, // BR
        0,           (float)winHeight, 0, 0, // TL
        (float)winWidth, (float)winHeight, 1, 0  // TR
    };
    float vertsFull[] = {
        0, 0, 0, 0,
        (float)winWidth, 0, 1, 0,
        0, (float)winHeight, 0, 1,
        (float)winWidth, (float)winHeight, 1, 1
    };
    // 使用六个顶点（两个三角形）
    float triVerts[] = {
        0, 0, 0, 0,
        (float)winWidth, 0, 1, 0,
        0, (float)winHeight, 0, 1,
        0, (float)winHeight, 0, 1,
        (float)winWidth, 0, 1, 0,
        (float)winWidth, (float)winHeight, 1, 1
    };

    glGenVertexArrays(1, &overlayVao);
    glGenBuffers(1, &overlayVbo);
    glBindVertexArray(overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triVerts), triVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // 纹理
    glGenTextures(1, &overlayTex);
    glBindTexture(GL_TEXTURE_2D, overlayTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, winWidth, winHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    overlayDirty = true;
}

void Game::destroyOverlay() {
    if (overlayVao) glDeleteVertexArrays(1, &overlayVao);
    if (overlayVbo) glDeleteBuffers(1, &overlayVbo);
    if (overlayTex) glDeleteTextures(1, &overlayTex);
    if (overlayShader) glDeleteProgram(overlayShader);
    if (memBmp) DeleteObject(memBmp);
    if (memDC) DeleteDC(memDC);
    overlayVao = overlayVbo = overlayTex = overlayShader = 0;
    memBmp = nullptr; memDC = nullptr; memBits = nullptr;
}

void Game::updateOverlayTexture() {
    if (!overlayDirty || !overlayTex || !memBits) return;
    glBindTexture(GL_TEXTURE_2D, overlayTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, winWidth, winHeight, GL_BGRA, GL_UNSIGNED_BYTE, memBits);
    overlayDirty = false;
}

void Game::drawOverlay(float alpha) {
    if (!overlayShader || !overlayTex) return;
    glUseProgram(overlayShader);
    glUniformMatrix4fv(glGetUniformLocation(overlayShader, "uProj"), 1, GL_FALSE, &projection[0][0]);
    glUniform1f(glGetUniformLocation(overlayShader, "uAlpha"), alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayTex);
    glUniform1i(glGetUniformLocation(overlayShader, "uTex"), 0);
    glBindVertexArray(overlayVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Game::clearMemDC() {
    if (memBits) {
        memset(memBits, 0, (size_t)winWidth * winHeight * 4);
        overlayDirty = true;
    }
}

// ============================================================
// Matrix Rain
// ============================================================
void Game::initRain() {
    const int DROP_COUNT = 120;
    rainDrops.clear();
    rainDrops.reserve(DROP_COUNT);
    for (int i = 0; i < DROP_COUNT; ++i) {
        RainDrop d;
        d.x = (float)(std::rand() % (winWidth + 200)) - 100.0f;
        d.y = -((float)(std::rand() % 800));  // 从更远处落下
        d.speed = 120.0f + (float)(std::rand() % 150);  // 120-270 px/s
        d.size = 1.0f + (float)(std::rand() % 20) / 10.0f;
        d.brightness = 0.3f + (float)(std::rand() % 70) / 100.0f;
        rainDrops.push_back(d);
    }
}

// ============================================================
// Matrix Rain
// ============================================================
void Game::updateRain(float dt) {
    for (auto& d : rainDrops) {
        d.y += d.speed * dt;
        if (d.y > (float)winHeight + 20.0f * d.size) {
            d.x = (float)(std::rand() % (winWidth + 40)) - 20.0f;
            d.y = -(float)(std::rand() % 200) - 20.0f * d.size;
            d.speed = 30.0f + (float)(std::rand() % 80);
            d.size = 1.0f + (float)(std::rand() % 20) / 10.0f;
            d.brightness = 0.3f + (float)(std::rand() % 70) / 100.0f;
        }
    }
}

void Game::renderRainGDI() {
    if (!memDC) return;
    const char* word = "sokoban";
    int wordLen = 7;

    SetBkMode(memDC, TRANSPARENT);

    for (const auto& d : rainDrops) {
        for (int i = 0; i < wordLen; ++i) {
            int charSize = (int)(12.0f * d.size);
            int cx = (int)d.x;
            int cy = (int)(d.y + i * charSize * 1.2f);
            if (cy < -charSize * 2 || cy > winHeight + charSize * 2) continue;

            // 亮度渐变：顶部暗、底部亮
            float fade = 0.3f + 0.7f * (float)i / (float)wordLen;
            int green = (int)(200.0f * d.brightness * fade);
            if (green < 30) green = 30;
            if (green > 255) green = 255;

            HFONT hFont = getOrCreateFont(charSize);
            HFONT old = (HFONT)SelectObject(memDC, hFont);
            SetTextColor(memDC, RGB(0, green, 0));

            char ch[2] = { word[i], 0 };
            TextOutA(memDC, cx, cy, ch, 1);

            SelectObject(memDC, old);
        }
    }
    overlayDirty = true;
}

// ============================================================
// Quad Drawing
// ============================================================
void Game::renderQuad(const glm::mat4& model, const glm::vec3& color) {
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uProj"), 1, GL_FALSE, &projection[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uModel"), 1, GL_FALSE, &model[0][0]);
    glUniform3fv(glGetUniformLocation(shaderProgram, "uColor"), 1, &color[0]);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);
}

// ============================================================
// Tile Colors
// ============================================================
glm::vec3 Game::tileColor(Tile t) const {
    switch (t) {
        case Tile::Wall:        return glm::vec3(0.85f, 0.85f, 0.85f);
        case Tile::Floor:       return glm::vec3(0.08f, 0.08f, 0.10f);
        case Tile::Goal:        return glm::vec3(0.85f, 0.10f, 0.25f);
        case Tile::Box:         return glm::vec3(0.15f, 0.65f, 0.20f);
        case Tile::BoxOnGoal:   return glm::vec3(0.25f, 0.80f, 0.30f);
        case Tile::Player:      return glm::vec3(0.30f, 0.60f, 0.95f);
        case Tile::PlayerOnGoal:return glm::vec3(0.20f, 0.50f, 0.90f);
        default:                return glm::vec3(0,0,0);
    }
}

// ============================================================
// Font Rendering
// ============================================================
// ============================================================
// GDI 文字渲染
// ============================================================
COLORREF Game::vec3ToColor(const glm::vec3& c) {
    return RGB((BYTE)(c.x * 255), (BYTE)(c.y * 255), (BYTE)(c.z * 255));
}

int Game::sizeToPoints(float size) {
    if (size >= 6.5f) return 32;
    if (size >= 5.0f) return 24;
    if (size >= 4.0f) return 20;
    if (size >= 3.5f) return 17;
    if (size >= 3.0f) return 15;
    if (size >= 2.5f) return 13;
    return 11;
}

HFONT Game::getOrCreateFont(int pointSize) {
    auto it = fontCache.find(pointSize);
    if (it != fontCache.end()) return it->second;
    HDC dc = GetDC(hwnd);
    int height = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(hwnd, dc);
    HFONT f = CreateFontA(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    fontCache[pointSize] = f;
    return f;
}

int Game::getTextWidthGDI(const std::string& text, int fontSize) const {
    if (text.empty() || !memDC) return 0;
    HDC dc = memDC;
    HFONT old = (HFONT)SelectObject(dc, const_cast<Game*>(this)->getOrCreateFont(fontSize));
    SIZE sz;
    GetTextExtentPoint32A(dc, text.c_str(), (int)text.size(), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

int Game::getTextHeightGDI(int fontSize) const {
    if (!memDC) return 0;
    HDC dc = memDC;
    HFONT old = (HFONT)SelectObject(dc, const_cast<Game*>(this)->getOrCreateFont(fontSize));
    TEXTMETRICA tm;
    if (!GetTextMetricsA(dc, &tm)) { SelectObject(dc, old); return fontSize; }
    SelectObject(dc, old);
    return tm.tmHeight;
}

void Game::renderTextGDI(int x, int y, int fontSize, const std::string& text, COLORREF color) {
    if (text.empty() || !memDC) return;
    HFONT old = (HFONT)SelectObject(memDC, getOrCreateFont(fontSize));
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, color);
    TextOutA(memDC, x, y, text.c_str(), (int)text.size());
    SelectObject(memDC, old);
    overlayDirty = true;
}

void Game::renderTextCenteredGDI(int cx, int y, int fontSize, const std::string& text, COLORREF color) {
    int tw = getTextWidthGDI(text, fontSize);
    renderTextGDI(cx - tw / 2, y, fontSize, text, color);
}

// ============================================================
// GDI 文字包装器（保留旧签名，调用方不变）
// ============================================================
void Game::renderText(float x, float y, float size, const char* text, const glm::vec3& color) {
    renderTextGDI((int)x, (int)y, sizeToPoints(size), text, vec3ToColor(color));
}

void Game::renderText(float x, float y, float size, const std::string& text, const glm::vec3& color) {
    renderText(x, y, size, text.c_str(), color);
}

void Game::renderTextCentered(float cx, float y, float size, const char* text, const glm::vec3& color) {
    renderTextCenteredGDI((int)cx, (int)y, sizeToPoints(size), text, vec3ToColor(color));
}

float Game::getTextWidth(const char* text, float size) const {
    return (float)const_cast<Game*>(this)->getTextWidthGDI(text ? text : "", sizeToPoints(size));
}

// ============================================================
// Button System
// ============================================================
void Game::clearButtons() { buttons.clear(); }

void Game::addButton(const std::string& label, int x, int y, int w, int h, int fontSize, std::function<void()> onClick, bool hasBorder) {
    buttons.push_back({label, x, y, w, h, fontSize, onClick, hasBorder});
}

void Game::checkButtonClicks() {
    if (!mouseDown && prevMouseDown) {
        for (auto& btn : buttons) {
            int mx = (int)mouseX, my = (int)mouseY;
            if (mx >= btn.x && mx <= btn.x + btn.w && my >= btn.y && my <= btn.y + btn.h) {
                // 菜单/记录页点击音效
                if (gameState == GameState::Menu || gameState == GameState::Records) {
                    Beep(880, 15);
                }
                if (btn.onClick) btn.onClick();
                break;
            }
        }
    }
    prevMouseDown = mouseDown;
}

void Game::drawButton(const Button& btn, bool hovered, bool keyboardSelected) {
    if (!memDC) return;
    bool active = hovered || keyboardSelected;

    // 无边框按钮 → 透明模式（默认透明，活跃时高亮）
    if (!btn.hasBorder) {
        if (active) {
            COLORREF hlColor = keyboardSelected ? RGB(0, 120, 60) : RGB(0, 60, 30);
            HBRUSH hlBrush = CreateSolidBrush(hlColor);
            RECT r = { btn.x, btn.y, btn.x + btn.w, btn.y + btn.h };
            FillRect(memDC, &r, hlBrush);
            DeleteObject(hlBrush);
        }
        COLORREF tCol = keyboardSelected ? RGB(255, 255, 200)
                      : hovered        ? RGB(150, 255, 150)
                      :                  RGB(80, 200, 100);
        renderTextCenteredGDI(btn.x + btn.w / 2,
            btn.y + (btn.h - getTextHeightGDI(btn.fontSize)) / 2,
            btn.fontSize, btn.label, tCol);
        overlayDirty = true;
        return;
    }

    // 有边框按钮 → 传统样式
    bool active2 = hovered || keyboardSelected;
    COLORREF bgColor = active2 ? RGB(40, 55, 80) : RGB(20, 25, 35);
    COLORREF borderColor = RGB(100, 150, 220);
    COLORREF textColor = keyboardSelected ? RGB(255, 255, 200) : RGB(200, 200, 210);
    HBRUSH bgBrush = CreateSolidBrush(bgColor);
    HPEN borderPen = CreatePen(PS_SOLID, keyboardSelected ? 2 : 1, borderColor);
    RECT r = { btn.x, btn.y, btn.x + btn.w, btn.y + btn.h };
    FillRect(memDC, &r, bgBrush);
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, btn.x, btn.y, btn.x + btn.w, btn.y + btn.h);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(bgBrush);
    DeleteObject(borderPen);
    overlayDirty = true;

    int tw = getTextWidthGDI(btn.label, btn.fontSize);
    int th = getTextHeightGDI(btn.fontSize);
    int tx = btn.x + (btn.w - tw) / 2;
    int ty = btn.y + (btn.h - th) / 2;
    renderTextGDI(tx, ty, btn.fontSize, btn.label, textColor);
}

void Game::drawButtons(int keyboardSelectedIndex) {
    int i = 0;
    for (auto& btn : buttons) {
        int mx = (int)mouseX, my = (int)mouseY;
        bool hovered = (mx >= btn.x && mx <= btn.x + btn.w && my >= btn.y && my <= btn.y + btn.h);
        bool kbdSel = (keyboardSelectedIndex >= 0 && i == keyboardSelectedIndex);
        drawButton(btn, hovered, kbdSel);
        ++i;
    }
}

// ============================================================
// Menu Rendering
// ============================================================
// ============================================================
// 3D 渲染
// ============================================================
void Game::render3D() {
    float asp = (float)winWidth / (float)winHeight;
    glm::mat4 proj = glm::perspective(glm::radians(70.0f), asp, 0.1f, 100.0f);

    // 相机位置：玩家网格坐标转为世界坐标
    float cx = playerX + 0.5f, cy = 0.65f, cz = playerY + 0.5f;
    glm::vec3 camPos(cx, cy, cz);
    glm::vec3 fwd(std::sin(camYaw) * std::cos(camPitch),
                  std::sin(camPitch),
                  std::cos(camYaw) * std::cos(camPitch));
    glm::vec3 center = camPos + fwd;
    glm::mat4 view = glm::lookAt(camPos, center, glm::vec3(0, 1, 0));

    glUseProgram(shader3D);
    GLint uMVPLoc = glGetUniformLocation(shader3D, "uMVP");
    GLint uColorLoc = glGetUniformLocation(shader3D, "uColor");

    // 地板（灰色网格）
    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < (int)map[y].size(); ++x) {
            char c = map[y][x];
            glm::vec3 floorCol = (c == '.' || c == '+' || c == '*')
                ? glm::vec3(0.8f, 0.1f, 0.1f)
                : glm::vec3(0.55f, 0.55f, 0.55f);
            glm::mat4 m = glm::translate(glm::mat4(1), glm::vec3((float)x + 0.5f, 0.0f, (float)y + 0.5f));
            m = glm::scale(m, glm::vec3(0.88f, 0.04f, 0.88f));
            glm::mat4 mvp = proj * view * m;
            glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, &mvp[0][0]);
            glUniform3fv(uColorLoc, 1, &floorCol[0]);
            glBindVertexArray(cubeVao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    // 墙壁（白色有轮廓+光照）
    glm::vec3 wallCol(0.85f, 0.85f, 0.85f);
    glm::vec3 edgeCol(0.15f, 0.15f, 0.15f);
    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < (int)map[y].size(); ++x) {
            if (map[y][x] != '#') continue;
            glm::mat4 m = glm::translate(glm::mat4(1), glm::vec3((float)x + 0.5f, 0.5f, (float)y + 0.5f));
            glm::mat4 mScale = glm::scale(m, glm::vec3(0.85f, 0.95f, 0.85f));
            glm::mat4 mvp = proj * view * mScale;
            glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, &mvp[0][0]);
            glUniform3fv(uColorLoc, 1, &wallCol[0]);
            glBindVertexArray(cubeVao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            // 黑色轮廓线
            glm::mat4 mvpE = proj * view * glm::scale(m, glm::vec3(0.85f, 0.95f, 0.85f));
            glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, &mvpE[0][0]);
            glUniform3fv(uColorLoc, 1, &edgeCol[0]);
            glBindVertexArray(edgeVao);
            glDrawArrays(GL_LINES, 0, 24);
        }
    }

    // 箱子（绿色实心方块，带浅色轮廓）
    glm::vec3 boxCol(0.15f, 0.70f, 0.20f);
    glm::vec3 boxEdge(0.10f, 0.50f, 0.15f);
    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < (int)map[y].size(); ++x) {
            if (map[y][x] != '$' && map[y][x] != '*') continue;
            // 箱底标记（深绿地面，指示格子）
            glm::mat4 mm = glm::translate(glm::mat4(1), glm::vec3((float)x + 0.5f, 0.005f, (float)y + 0.5f));
            mm = glm::scale(mm, glm::vec3(0.7f, 0.005f, 0.7f));
            glm::mat4 mvp2 = proj * view * mm;
            glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, &mvp2[0][0]);
            glUniform3fv(uColorLoc, 1, &boxEdge[0]);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            // 箱体（0.9 缩放，近乎满格）
            glm::mat4 m = glm::translate(glm::mat4(1), glm::vec3((float)x + 0.5f, 0.45f, (float)y + 0.5f));
            m = glm::scale(m, glm::vec3(0.9f, 0.9f, 0.9f));
            glm::mat4 mvp = proj * view * m;
            glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, &mvp[0][0]);
            glUniform3fv(uColorLoc, 1, &boxCol[0]);
            glBindVertexArray(cubeVao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
    }

    glBindVertexArray(0);
}

void Game::movePlayer3D(int dx, int dy) {
    if (moveCooldown > 0) return;
    int nx = playerX + dx, ny = playerY + dy;
    if (nx < 0 || nx >= mapWidth || ny < 0 || ny >= mapHeight) return;
    char c = map[ny][nx];
    if (c == '#') return;
    if (c == '$' || c == '*') {
        int bx = nx + dx, by = ny + dy;
        if (bx < 0 || bx >= mapWidth || by < 0 || by >= mapHeight) return;
        char bc = map[by][bx];
        if (bc == '#' || bc == '$' || bc == '*') return;
        // Push box
        map[by][bx] = (map[by][bx] == '.') ? '*' : '$';
        map[ny][nx] = (map[ny][nx] == '*') ? '.' : ' ';
        movesHistory.push_back({dx, dy});
        ++moves;
        // Record undo
        MapSnapshot ss;
        ss.map = map; ss.playerX = playerX; ss.playerY = playerY;
        undoStack.push_back(ss);
        if ((int)undoStack.size() > MAX_UNDO) undoStack.erase(undoStack.begin());
    }
    playerX = nx; playerY = ny;
    moveCooldown = MOVE_COOLDOWN_3D;
}

void Game::renderMenu() {
    float w = (float)winWidth;
    float h = (float)winHeight;

    // 装饰线条
    {
        glm::mat4 line = glm::translate(glm::mat4(1), glm::vec3(w * 0.1f, h * 0.27f, 0));
        line = glm::scale(line, glm::vec3(w * 0.8f, 2.0f, 1));
        renderQuad(line, glm::vec3(0.3f, 0.5f, 0.8f));
    }

    // 标题（粗大渐变）
    float titleY = h * 0.08f;
    {
        // 标题颜色随时间缓慢变化
        float hue = menuAnimTime * 0.05f;
        float r = 0.3f + 0.2f * (float)std::sin((double)(hue * 1.3f));
        float g = 0.5f + 0.3f * (float)std::sin((double)(hue * 1.7f + 1.0f));
        float b = 0.7f + 0.2f * (float)std::sin((double)(hue * 2.1f + 2.0f));
        renderTextCentered(w / 2, titleY, 12.0f, "SOKOBAN", glm::vec3(r, g, b));
    }

    // 可点击按钮（透明模式）
    float bX = (w - 300) / 2.0f, bS = h * 0.33f, bG = 60.0f;
    addButton("  Easy",   (int)bX, (int)bS, 300, 50, 22, [this](){ menuSelection = 0; startGame(Difficulty::Easy, randomLevelIndex()); }, false);
    addButton("Medium",  (int)bX, (int)(bS + bG), 300, 50, 22, [this](){ menuSelection = 1; startGame(Difficulty::Medium, randomLevelIndex()); }, false);
    addButton("  Hard",  (int)bX, (int)(bS + 2*bG), 300, 50, 22, [this](){ menuSelection = 2; startGame(Difficulty::Hard, randomLevelIndex()); }, false);
    addButton("  Hell",  (int)bX, (int)(bS + 3*bG), 300, 50, 22, [this](){ menuSelection = 3; startGame(Difficulty::Hell, randomLevelIndex()); }, false);
    addButton("Records", (int)bX, (int)(bS + 4*bG), 220, 44, 18, [this](){ menuSelection = 4; gameState = GameState::Records; allRecords = RecordManager::listAll(); });
    addButton("  3D!", (int)bX + 230, (int)(bS + 4*bG), 80, 44, 16, [this](){
        startGame(Difficulty::Easy, 0);
        camYaw = 0; camPitch = -0.3f;
        gameState = GameState::Playing3D;
    }, false);
}

// ============================================================
// Playing Rendering
// ============================================================
void Game::renderPlaying() {
    // 信息栏背景
    {
        glm::mat4 infoBg = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0));
        infoBg = glm::scale(infoBg, glm::vec3((float)winWidth, 28.0f, 1));
        renderQuad(infoBg, glm::vec3(0.05f, 0.05f, 0.07f));
    }

    // 信息栏文字
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s #%d | Steps: %d | Time: %.1f", 
        difficultyName(currentDifficulty), currentLevelIndex + 1, moves, elapsedTime);
    renderText(8, 6, 2.5f, buf, glm::vec3(0.7f, 0.7f, 0.8f));

    // 关卡名称
    char lvlName[32];
    std::snprintf(lvlName, sizeof(lvlName), "Level %d/%d", currentLevelIndex + 1, LEVELS_PER_DIFFICULTY);
    renderText((float)winWidth - getTextWidth(lvlName, 2.5f) - 8, 6, 2.5f, lvlName, glm::vec3(0.5f, 0.5f, 0.6f));

    // 绘制关卡
    renderLevel();

    // 底部按钮
    int ay = winHeight - 48, ah = 34;
    addButton("CCW", 20, ay, 52, ah, 13, [this](){ rotateMap(false); });
    addButton("CW", 78, ay, 52, ah, 13, [this](){ rotateMap(true); });
    addButton("Undo", winWidth - 360, ay, 80, ah, 14, [this](){ undoMove(); });
    addButton("Restart", winWidth - 170, ay, 90, ah, 14, [this](){ resetLevel(); });
    addButton("Undo", winWidth - 70, ay, 60, ah, 14, [this](){ undoMove(); });
}

// ============================================================
// Won Overlay
// ============================================================
void Game::renderWon() {
    float w = (float)winWidth;
    float h = (float)winHeight;

    // 半透明遮罩
    glm::mat4 overlay = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0));
    overlay = glm::scale(overlay, glm::vec3(w, h, 1));
    renderQuad(overlay, glm::vec3(0, 0, 0));

    // 面板背景
    float pw = 400, ph = 240;
    float px = (w - pw) / 2.0f, py = (h - ph) / 2.0f;
    {
        glm::mat4 panel = glm::translate(glm::mat4(1), glm::vec3(px, py, 0));
        panel = glm::scale(panel, glm::vec3(pw, ph, 1));
        renderQuad(panel, glm::vec3(0.12f, 0.12f, 0.16f));
    }
    {
        glm::mat4 border = glm::translate(glm::mat4(1), glm::vec3(px + 3, py + 3, 0));
        border = glm::scale(border, glm::vec3(pw - 6, ph - 6, 1));
        renderQuad(border, glm::vec3(0.2f, 0.5f, 0.25f));
    }
    {
        glm::mat4 inner = glm::translate(glm::mat4(1), glm::vec3(px + 5, py + 5, 0));
        inner = glm::scale(inner, glm::vec3(pw - 10, ph - 10, 1));
        renderQuad(inner, glm::vec3(0.1f, 0.1f, 0.14f));
    }

    // 标题
    renderTextCentered(w / 2.0f, py + 18, 5.0f, "LEVEL CLEAR!", glm::vec3(0.3f, 0.9f, 0.4f));

    // 统计信息
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Difficulty: %s", difficultyName(currentDifficulty));
    renderTextCentered(w / 2.0f, py + 58, 3.0f, buf, glm::vec3(0.8f, 0.8f, 0.9f));

    std::snprintf(buf, sizeof(buf), "Steps: %d    Time: %.1fs", moves, elapsedTime);
    renderTextCentered(w / 2.0f, py + 88, 3.0f, buf, glm::vec3(0.9f, 0.9f, 0.7f));

    // 按钮（均匀分布）
    int cwx = (int)(w / 2);
    int btnY = (int)(py + 135);
    int btnW = 100, btnH = 38, btnGap = 24;
    int totalW = 3 * btnW + 2 * btnGap;
    int btnStart = cwx - totalW / 2;
    addButton("Next", btnStart, btnY, btnW, btnH, 15, [this](){
        int next = (currentLevelIndex + 1) % LEVELS_PER_DIFFICULTY;
        startGame(currentDifficulty, next);
    });
    addButton("Replay", btnStart + btnW + btnGap, btnY, btnW, btnH, 15, [this](){ resetLevel(); });
    addButton("Menu", btnStart + 2*(btnW + btnGap), btnY, btnW, btnH, 15, [this](){ gameState = GameState::Menu; });
}

// ============================================================
// Records Rendering
// ============================================================
void Game::renderRecords() {
    float w = (float)winWidth;
    float h = (float)winHeight;

    // 每次进入刷新记录列表
    allRecords = RecordManager::listAll();

    // 背景
    {
        glm::mat4 bg = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0));
        bg = glm::scale(bg, glm::vec3(w, h, 1));
        renderQuad(bg, glm::vec3(0.08f, 0.08f, 0.12f));
    }

    // 标题
    renderTextCentered(w / 2.0f, 20, 5.0f, "BEST RECORDS", glm::vec3(0.3f, 0.6f, 0.9f));

    if (allRecords.empty()) {
        renderTextCentered(w / 2.0f, h / 2.0f - 20, 3.5f, "No records yet!", glm::vec3(0.5f, 0.5f, 0.6f));
        renderTextCentered(w / 2.0f, h / 2.0f + 10, 3.0f, "Complete a level to save your record",
            glm::vec3(0.4f, 0.4f, 0.5f));
    } else {
        // 表头
        float colX[] = {20, 180, 320, 420, 520};
        renderText(colX[0], 70, 2.5f, "Difficulty", glm::vec3(0.5f, 0.5f, 0.6f));
        renderText(colX[1], 70, 2.5f, "Level", glm::vec3(0.5f, 0.5f, 0.6f));
        renderText(colX[2], 70, 2.5f, "Steps", glm::vec3(0.5f, 0.5f, 0.6f));
        renderText(colX[3], 70, 2.5f, "Time", glm::vec3(0.5f, 0.5f, 0.6f));
        renderText(colX[4], 70, 2.5f, "Date", glm::vec3(0.5f, 0.5f, 0.6f));

        // 列表行：统一行高
        float rowH = 28.0f;
        float rowStartY = 100.0f;
        int rowCount = std::min((int)allRecords.size(), (int)((h - rowStartY - 60) / rowH));
        for (int i = 0; i < rowCount; ++i) {
            float ry = rowStartY + i * rowH;
            const auto& rec = allRecords[i];
            bool sel = (i == recordListScroll);
            // 鼠标悬停高亮
            bool hover = (mouseX >= 10 && mouseX <= w - 10 &&
                          mouseY >= ry - 2 && mouseY < ry + rowH);

            if (sel || hover) {
                glm::vec3 hlCol = sel ? glm::vec3(0.15f, 0.2f, 0.3f) : glm::vec3(0.12f, 0.15f, 0.22f);
                glm::mat4 hl = glm::translate(glm::mat4(1), glm::vec3(10, ry - 2, 0));
                hl = glm::scale(hl, glm::vec3(w - 100, rowH - 4, 1));
                renderQuad(hl, hlCol);
            }

            glm::vec3 textCol = sel ? glm::vec3(1, 1, 0.8f) : glm::vec3(0.7f, 0.75f, 0.8f);
            char buf[32];
            renderText(colX[0], ry, 2.5f, difficultyName(rec.difficulty), textCol);
            std::snprintf(buf, sizeof(buf), "#%d", rec.levelIndex + 1);
            renderText(colX[1], ry, 2.5f, buf, textCol);
            std::snprintf(buf, sizeof(buf), "%d", rec.steps);
            renderText(colX[2], ry, 2.5f, buf, textCol);
            std::snprintf(buf, sizeof(buf), "%.1fs", rec.timeSeconds);
            renderText(colX[3], ry, 2.5f, buf, textCol);
            renderText(colX[4], ry, 2.5f, rec.dateStr.c_str(), glm::vec3(0.5f, 0.5f, 0.6f));
        }

        // 处理记录列表的鼠标点击
        if (prevMouseDown && !mouseDown) {
            for (int i = 0; i < rowCount; ++i) {
                float ry = rowStartY + i * rowH;
                if (mouseX >= 10 && mouseX <= w - 10 &&
                    mouseY >= ry - 2 && mouseY < ry + rowH) {
                    replayIndex = 0; replayTimer = 0; replayStarted = false;
                    startReplay(allRecords[i]);
                    break;
                }
            }
        }
    }

    // 底部按钮（增大、间距拉开）
    int by = (int)h - 42, bh = 34;
    if (!allRecords.empty()) {
        addButton("Replay (R)", 30, by, 130, bh, 14, [this](){
            replayIndex = 0; replayTimer = 0; replayStarted = false;
            startReplay(allRecords[recordListScroll]);
        });
        addButton("Del", 180, by, 70, bh, 14, [this](){
            RecordManager::deleteRecord(recordListScroll);
            allRecords = RecordManager::listAll();
            if (recordListScroll >= (int)allRecords.size())
                recordListScroll = std::max(0, (int)allRecords.size() - 1);
        });
    }
    addButton("Back (ESC/M)", (int)w - 160, by, 140, bh, 14, [this](){ gameState = GameState::Menu; });
}  // <-- closing brace for renderRecords

// ============================================================
// Replaying Rendering
// ============================================================
void Game::renderReplaying() {
    // 只渲染关卡地图和信息栏，不添加 Playing 按钮
    {
        glm::mat4 infoBg = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0));
        infoBg = glm::scale(infoBg, glm::vec3((float)winWidth, 28.0f, 1));
        renderQuad(infoBg, glm::vec3(0.05f, 0.05f, 0.07f));
    }
    renderLevel();

    float w = (float)winWidth;
    float h = (float)winHeight;

    // 重放顶部提示条
    {
        glm::mat4 bar = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0));
        bar = glm::scale(bar, glm::vec3(w, 28.0f, 1));
        renderQuad(bar, glm::vec3(0.02f, 0.3f, 0.1f));
    }

    char buf[64];
    int total = (int)replayMoves.size();
    std::snprintf(buf, sizeof(buf), "REPLAY: %d / %d", replayIndex, total);
    renderTextCentered(w / 2.0f, 5, 2.5f, buf, glm::vec3(0.3f, 0.9f, 0.5f));

    // 右侧竖排按钮（加大尺寸）
    int bx = (int)w - 150, bw = 135, bh = 38, bg = 6;
    int byStart = 36;

    addButton("CCW", bx, byStart, bw, bh, 15, [this](){ rotateMap(false); });
    addButton("CW", bx, byStart + (bh+bg), bw, bh, 15, [this](){ rotateMap(true); });
    addButton("Replay", bx, byStart + 2*(bh+bg), bw, bh, 15, [this](){
        replayIndex = 0;
        replayTimer = 0;
        replayStarted = true;
    });
    addButton("pos_solu", bx, byStart + 3*(bh+bg), bw, bh, 14, [this](){
        std::vector<MoveRecord> bestMoves;
        try {
            bool solved = solveSokoban(map, playerX, playerY, bestMoves);
            if (solved && !bestMoves.empty()) {
                replayMoves = bestMoves;
                replayIndex = 0;
                replayTimer = 0;
                replayStarted = true;
                std::cout << "Solution: " << bestMoves.size() << " pushes" << std::endl;
            } else {
                std::cout << "Solver failed" << std::endl;
            }
        } catch (...) {
            std::cerr << "Solver exception" << std::endl;
        }
    });
    addButton("Play Live", bx, byStart + 4*(bh+bg), bw, bh, 14, [this](){
        gameState = GameState::Playing;
        moves = replayIndex;
        movesHistory.clear();
        for (int i = 0; i < replayIndex && i < (int)replayMoves.size(); ++i)
            movesHistory.push_back(replayMoves[i]);
        elapsedTime = 0;
        updateTitle();
    });
    addButton("Menu", bx, byStart + 5*(bh+bg), bw, bh, 15, [this](){
        gameState = GameState::Menu;
    });

    // 重放进度条（底部居中）
    if (total > 0) {
        float prog = (float)replayIndex / (float)total;
        float barW = w * 0.5f;
        float barH = 6.0f;
        float barX = (w - barW) * 0.35f;  // 避开右侧按钮
        float barY = (float)h - 30.0f;

        glm::mat4 bg = glm::translate(glm::mat4(1), glm::vec3(barX, barY, 0));
        bg = glm::scale(bg, glm::vec3(barW, barH, 1));
        renderQuad(bg, glm::vec3(0.15f, 0.15f, 0.2f));

        if (prog > 0.01f) {
            glm::mat4 fg = glm::translate(glm::mat4(1), glm::vec3(barX, barY, 0));
            fg = glm::scale(fg, glm::vec3(barW * prog, barH, 1));
            renderQuad(fg, glm::vec3(0.1f, 0.7f, 0.3f));
        }
    }
}

// ============================================================
// Level Rendering
// ============================================================
float Game::getTilePixels() const {
    if (mapWidth == 0 || mapHeight == 0) return 32.0f;
    int availH = winHeight - 28 - 120; // 顶部信息栏 + 底部按钮区域
    if (availH < 32) availH = 32;
    return (float)std::min(winWidth / mapWidth, availH / mapHeight);
}

glm::vec2 Game::getLevelOffset() const {
    float tp = getTilePixels();
    float lw = tp * mapWidth;
    float lh = tp * mapHeight + 28;
    return glm::vec2((winWidth - lw) / 2.0f, 28.0f + ((winHeight - 28) - (lh - 28)) / 2.0f);
}

void Game::renderTile(int x, int y, Tile tile) {
    auto offset = getLevelOffset();
    float tp = getTilePixels();

    glm::mat4 model = glm::translate(glm::mat4(1.0f),
        glm::vec3(offset.x + x * tp, offset.y + y * tp, 0.0f));
    model = glm::scale(model, glm::vec3(tp, tp, 1.0f));

    float inset = 0.04f;
    glm::mat4 insetModel = glm::translate(model, glm::vec3(inset, inset, 0.0f));
    insetModel = glm::scale(insetModel, glm::vec3(1.0f - 2.0f * inset, 1.0f - 2.0f * inset, 1.0f));

    switch (tile) {
        case Tile::Wall:
        case Tile::Floor:
        case Tile::Goal:
            renderQuad(insetModel, tileColor(tile));
            break;
        case Tile::Box:
            renderQuad(model, glm::vec3(0.5f, 0.5f, 0.5f)); // 浅灰边框
            renderQuad(insetModel, tileColor(tile));
            break;
        case Tile::BoxOnGoal:
            renderQuad(model, glm::vec3(0.2f, 0.6f, 0.2f)); // 绿框
            renderQuad(insetModel, tileColor(tile));
            break;
        case Tile::Player:
        case Tile::PlayerOnGoal:
            renderQuad(model, glm::vec3(0.1f, 0.35f, 0.8f)); // 蓝框
            {
                glm::mat4 inner = glm::translate(model, glm::vec3(0.12f, 0.12f, 0.0f));
                inner = glm::scale(inner, glm::vec3(0.76f, 0.76f, 1.0f));
                renderQuad(inner, tileColor(tile));
            }
            break;
        default:
            break;
    }
}

void Game::renderLevel() {
    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < mapWidth; ++x) {
            Tile tile = static_cast<Tile>(map[y][x]);
            if (tile == Tile::Floor) continue;
            renderTile(x, y, tile);
        }
    }
}
