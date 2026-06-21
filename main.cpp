#include "game.h"
#include "create_map.h"
#include <iostream>
#include <ctime>

int main() {
    // 初始化关卡生成器种子
    seedGenerator((unsigned)std::time(nullptr));

    Game game;
    if (!game.init(800, 600)) {
        std::cerr << "Failed to initialize game." << std::endl;
        return -1;
    }

    std::cout << "=== Sokoban ===" << std::endl;
    std::cout << "Menu: Arrow Up/Down + Enter, or 1-5 key" << std::endl;
    std::cout << "Game: Arrow/WASD move, Z=Undo, R=Restart, M=Menu" << std::endl;
    std::cout << "Records: R=Replay selected, ESC=Back" << std::endl;

    double lastTime = glfwGetTime();
    while (!game.shouldClose()) {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;
        game.update(deltaTime);
        game.render();
    }
    return 0;
}
