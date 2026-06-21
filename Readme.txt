Sokoban

一、CMake 项目结构*
CMakeLists.txt 定义三个静态库：glad(GLenum加载)、glfw3(窗口/输入)、glm(矩阵数学)，外加 winmm(MCI音乐)和OpenGL。
主目标 Sokoban.exe 链接 src/ 下六个源文件：main.cpp(入口)、game.cpp(引擎核心)、records.cpp(记录IO)、create_map.cpp(关卡生成+死局检测)、solver.cpp(最优解BFS)及icon_resource.rc(EXE图标嵌入)。

二、窗口绘制与交互*
采用双缓冲架构：OpenGL 渲染游戏tile，GDI 在独立内存DC绘制按钮和文字，通过 32-bit RGBA DIB section上传为纹理，经自定义着色器混合输出。
按钮绑定坐标矩形，鼠标点击和键盘选择共享同一按钮列表，支持悬停高亮和键盘选中边框。
背景动画为 Matrix 雨：120个"sokoban"字符串以120-270px/s垂直下落，GDI绿色文字分两层绘制(35%透明背景雨 + 100%不透明UI)。

三、游戏逻辑**
玩家以 WASD/方向键在网格上移动，遇到箱子($)且后方为空时推动。
moveHistory 记录每次推动方向用于重放和Undo(小于等于256)。
checkWin() 遍历所有目标点(./+)是否被箱子覆盖。
rotateMap() 将整个地图矩阵转置 90度，箱子/玩家同步旋转。
3D模式下采用透视投影+第一人称相机(camYaw/camPitch由鼠标拖拽控制)，WASD按视角朝向选择主方向移动，0.25s冷却防止误触。

四、关卡生成与死局检测***
每个难度保留第一关硬编码，其余关卡即时随机生成。
Easy/Medium/Hard用随机撒墙
Hell用4种局部模板(棋盘格柱/内室回廊/镜像分区/LT走廊，已人工验证可解)。
生成后通过四层死局检测：(1)包点可达性——目标点四周需有≥2格连续空位供推手+箱子；(2)贴边墙约束——边界旁箱子若无同边目标点则死局；(3)递归箱簇检测——箱子之间互相阻塞可递归解开则非死局；(4)首次推动验证——BFS枚举所有可到达的首步，全部死局则重生成。

五、记录存储*
LevelRecord 以管道符分隔一行一条存入 records/sokoban_records.txt。
字段：难度(int)、关卡索引(int)、用时(double)、步数、日期、地图行数、地图数据(n行字符串)……
saveRecord() 读取现有文件，同关卡步数更优则替换否则追加。deleteRecord(index) 删除第index行后写回。

六、图形渲染***
2D模式下正交投影，每种tile颜色由 tileColor(index) 预定义，带内边框区分tile类型。
3D模式下透视投影 + 每面4顶点的cube(6xGL_TRIANGLE_STRIP=24顶点/36三角形)，墙壁白色加GL_LINES黑色轮廓(12边x2顶=24顶点)，箱子绿色底部贴地(y=0.3)，地板灰色0.88缩小留间隙形成网格。
着色器按像素亮度计算alpha实现文字和按钮渐变透明度。

参考文献和资料：
https://vtzyz.xetlk.com/s/3sMRgn
https://www.bilibili.com/video/BV1Aw411Q73M?vd_source=920c4762e2344135d539749dd0df5208
https://github.com/jnowakow/SobokanSolver
https://github.com/nicls67/LevelCreator2
https://github.com/HolyMichael/CG1
《计算机图形学编程》——V.Scott Gordon   John Clevenger (著)	魏广程 沈瞳（译）