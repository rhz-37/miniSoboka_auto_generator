cmake_minimum_required(VERSION 3.16)
project(Sokoban VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- GLAD (编译 glad.c 作为静态库) ---
add_library(glad STATIC glad/glad.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR})

# --- GLFW (预编译静态库) ---
add_library(glfw3 STATIC IMPORTED)
set_target_properties(glfw3 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/GLFW/glfw3.lib"
)
target_include_directories(glfw3 INTERFACE ${CMAKE_SOURCE_DIR})

# --- GLM (header-only) ---
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_SOURCE_DIR})

# --- OpenGL (CMake 内置) ---
find_package(OpenGL REQUIRED)

# --- 可执行文件 ---
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/game.cpp
    src/records.cpp
    src/create_map.cpp
    src/solver.cpp
    src/icon_resource.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glad
    glfw3
    glm
    winmm
)

# MSVC 配置
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    # 构建后复制 resource 到输出目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resource"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resource"
        COMMENT "Copying resources..."
    )
endif()

# VC++ 运行时 DLL（构建后复制）
set(VS_REDIST "C:/Program Files/Microsoft Visual Studio/18/Insiders/VC/Redist/MSVC/14.50.35710/x64/Microsoft.VC145.CRT")
if(EXISTS "${VS_REDIST}/vcruntime140.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VS_REDIST}/msvcp140.dll"
            "${VS_REDIST}/vcruntime140.dll"
            "${VS_REDIST}/vcruntime140_1.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
        COMMENT "Copying VC++ runtime DLLs..."
    )
endif()
