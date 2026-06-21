#pragma once
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <queue>
#include <unordered_map>
#include <functional>
#include "levels.h"
#include "records.h"

enum class Tile : char {
    Floor = ' ', Wall = '#', Box = '$',
    Goal = '.', Player = '@',
    BoxOnGoal = '*', PlayerOnGoal = '+'
};

enum class GameState { Menu, Playing, Won, Records, Replaying, Playing3D };

struct MapSnapshot {
    std::vector<std::string> map;
    int playerX, playerY;
};

class Game {
public:
    Game();
    ~Game();
    bool init(int width, int height);
    void update(float deltaTime);
    void render();
    void keyCallback(int key, int action, int mods);
    void resizeCallback(int width, int height);
    void mouseButtonCallback(int button, int action, double x, double y);
    void cursorPosCallback(double x, double y);
    bool shouldClose() const { return glfwWindowShouldClose(window); }
    GLFWwindow* getWindow() const { return window; }
private:
    GLFWwindow* window;
    int winWidth = 800, winHeight = 600;
    GameState gameState = GameState::Menu;
    Difficulty currentDifficulty = Difficulty::Easy;
    int currentLevelIndex = 0, totalGoals = 0, totalBoxes = 0;
    std::vector<std::string> map;
    int mapWidth = 0, mapHeight = 0, playerX = 0, playerY = 0;
    std::vector<MoveRecord> movesHistory, replayMoves;
    int replayIndex = 0;
    double replayTimer = 0;
    bool replayStarted = false;
    static constexpr double REPLAY_INTERVAL = 0.12;
    int moves = 0, undoCount = 0;
    double elapsedTime = 0, gameStartTime = 0;
    std::vector<MapSnapshot> undoStack;
    static constexpr int MAX_UNDO = 256;
    int menuSelection = 0;
    float menuAnimTime = 0;
    int recordListScroll = 0;
    std::vector<LevelRecord> allRecords;

    // GDI 内存位图（文字渲染到内存，一次上传为纹理覆盖层）
    HWND hwnd = nullptr;
    std::unordered_map<int, HFONT> fontCache;
    HFONT getOrCreateFont(int pointSize);
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    uint32_t* memBits = nullptr;
    int memBmpW = 0, memBmpH = 0;

    // OpenGL 覆盖层纹理 & 着色器
    GLuint overlayTex = 0;
    GLuint overlayVao = 0, overlayVbo = 0;
    GLuint overlayShader = 0;
    bool overlayDirty = true;

    void initOverlay();
    void destroyOverlay();
    void updateOverlayTexture();
    void drawOverlay(float alpha = 1.0f);
    void clearMemDC();

    // Matrix Rain
    struct RainDrop {
        float x, y;
        float speed;
        float size;     // 缩放倍数 1.0~3.0
        float brightness; // 亮度 0.3~1.0
    };
    std::vector<RainDrop> rainDrops;
    void initRain();
    void updateRain(float dt);
    void renderRainGDI();

    // Buttons & Mouse
    struct Button {
        std::string label; int x, y, w, h;
        int fontSize; std::function<void()> onClick; bool hasBorder;
    };
    std::vector<Button> buttons;
    double mouseX = 0, mouseY = 0;
    bool mouseDown = false, prevMouseDown = false;

    // OpenGL
    GLuint shaderProgram = 0, vao = 0, vbo = 0;
    glm::mat4 projection;

    bool initShaders();
    bool initOverlayShader();
    void initQuad();
    void initGDI();
    void startGame(Difficulty d, int lvlIdx, int depth = 0);
    void resetLevel();
    bool movePlayer(int dx, int dy);
    void undoMove();
    void startReplay(const LevelRecord& rec);
    void rotateMap(bool clockwise);
    void countGoalsAndBoxes();
    bool isWon() const;
    bool isGoal(int x, int y) const;
    bool isBoxAt(int x, int y) const;
    bool isCornerDeadlock(int x, int y) const;
    bool isUnwinnable() const;
    bool hasValidFirstPush() const;
    // 3D 模式
    GLuint shader3D = 0, cubeVao = 0, cubeVbo = 0, edgeVao = 0, edgeVbo = 0;
    float camYaw = 0, camPitch = -0.3f;
    float moveCooldown = 0;
    static constexpr float MOVE_COOLDOWN_3D = 0.25f;
    bool initShaders3D();
    void initCube();
    void render3D();
    void movePlayer3D(int dx, int dy);

    void updateTitle();

    void clearButtons();
    void addButton(const std::string& label, int x, int y, int w, int h, int fontSize, std::function<void()> onClick, bool hasBorder = true);
    void checkButtonClicks();
    void drawButtons(int keyboardSelectedIndex = -1);
    void drawButton(const Button& btn, bool hovered, bool keyboardSelected = false);

    void renderMenu();
    void renderPlaying();
    void renderWon();
    void renderRecords();
    void renderReplaying();
    void renderLevel();
    void renderTile(int x, int y, Tile tile);
    void renderQuad(const glm::mat4& model, const glm::vec3& color);

    // old-signature wrappers -> GDI
    void renderText(float x, float y, float size, const char* text, const glm::vec3& color);
    void renderText(float x, float y, float size, const std::string& text, const glm::vec3& color);
    void renderTextCentered(float cx, float y, float size, const char* text, const glm::vec3& color);
    float getTextWidth(const char* text, float size) const;

    // GDI impl
    void renderTextGDI(int x, int y, int fontSize, const std::string& text, COLORREF color);
    void renderTextCenteredGDI(int cx, int y, int fontSize, const std::string& text, COLORREF color);
    int getTextWidthGDI(const std::string& text, int fontSize) const;
    int getTextHeightGDI(int fontSize) const;
    static int sizeToPoints(float size);
    static COLORREF vec3ToColor(const glm::vec3& c);

    float getTilePixels() const;
    glm::vec2 getLevelOffset() const;
    glm::vec3 tileColor(Tile t) const;
};
