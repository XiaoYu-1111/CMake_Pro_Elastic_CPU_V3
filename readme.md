这是一份基于你提供的代码生成的详细 **Markdown** 文档。你可以将其保存为 `README.md` 或 `Manual.md`，用于项目说明或文档记录。

---

# Acoustic Wave Pro (Ultimate) - 实时声波模拟器

**Acoustic Wave Pro** 是一个高性能的二维声波传播模拟器。它基于 **有限差分时域法 (FDTD)**，利用 **OpenMP** 进行 CPU 多线程加速，并使用 **OpenGL** 和 **ImGui** 实现实时可视化与交互。

该程序旨在模拟声波在不同介质中的传播、反射、折射、衍射以及干涉现象，适用于物理教学、地震勘探模拟及波动声学研究。

---

## 🌟 主要功能 (Features)

### 1. 核心物理引擎
*   **高精度计算**：采用 **4阶空间差分** 和 **2阶时间差分** 的 FDTD 算法，相比传统 2阶/2阶 方法具有更低的数值色散。
*   **多线程加速**：利用 OpenMP 并行计算，充分利用多核 CPU 性能。
*   **复杂边界条件**：
    *   **PML/海绵吸收边界**：模拟无限大区域，消除边界反射。
    *   **自由表面 (Free Surface)**：模拟空气/水界面。
    *   **刚性边界**：全反射边界。

### 2. 交互与可视化
*   **多种渲染模式**：
    *   `Liquid` (液体光泽)
    *   `Heatmap` (热力图)
    *   `Coolwarm` (冷暖色调，地震学常用)
    *   `Clay/Terracotta` (陶土风格)
    *   `Refraction` (类似玻璃/水银的折射效果)
*   **实时交互工具**：
    *   鼠标左键生成波源。
    *   画笔绘制高/低速介质、墙体。
    *   橡皮擦工具。
*   **地震观测系统**：
    *   **单点示波器**：实时显示特定点的波形历史。
    *   **阵列记录 (Seismic Array)**：支持拖拽放置检波器阵列，生成地震记录剖面 (Seismic Profile)。

### 3. 预设场景
*   **双缝干涉 (Double Slit)**
*   **声透镜 (Acoustic Lens)**
*   **晶体散射 (Crystal)**
*   **波导 (Waveguide)**
*   **随机散射介质 (Random Scattering)**
*   **多层地质模型 (Multi-Layered Model)**

---

## 🛠️ 依赖环境 (Prerequisites)

本项目基于 C++ 开发，依赖以下库：

*   **操作系统**: Windows (由于代码中包含了 `<windows.h>`)
*   **编译器**: 支持 C++17 和 OpenMP 的编译器 (如 MSVC, MinGW64)
*   **OpenGL 库**:
    *   [GLEW](http://glew.sourceforge.net/)
    *   [GLFW3](https://www.glfw.org/)
*   **UI 库**: [Dear ImGui](https://github.com/ocornut/imgui) (已集成在代码逻辑中)
*   **图像加载**: `stb_image.h` (单头文件库)

---

## 🎮 操作手册 (User Guide)

### ⌨️ 键盘快捷键 (Keyboard Controls)

| 按键 | 功能描述 |
| :--- | :--- |
| **工具选择** | |
| `1` | **波源触发模式** (Source Trigger) |
| `2` | **绘制高速介质** (Draw High Vel) |
| `3` | **绘制低速介质** (Draw Low Vel) |
| `4` | **橡皮擦** (Eraser) |
| `5` | **绘制墙体** (Draw Wall) |
| `6` | **放置检波器阵列** (Place Array) |
| **场景加载** | |
| `7` | 声透镜场景 |
| `8` | 晶体散射场景 |
| `9` | 波导场景 |
| `0` | 双缝干涉场景 |
| **渲染模式** | |
| `Q` | Liquid (液体模式) |
| `W` | Black & White (黑白模式) |
| `E` | Model (仅显示速度模型) |
| `A` | Coolwarm (冷暖色) |
| `D` | Custom (自定义颜色) |
| **模拟控制** | |
| `Space` | **暂停 / 继续** |
| `S` | **单步步进** (暂停时使用) |
| `C` | **清除波场** (Clear Wavefield) |
| `BackSpace`| 重置所有 (模型和波场) |
| **参数调整** | |
| `F` | 切换自由表面 (Free Surface) |
| `B` | 切换边界吸收/反射 (Boundary) |
| `L` | 切换点声源 / 线声源 |
| `[` / `]` | 降低 / 升高 声源频率 |
| `Up` / `Down` | 增加 / 减小 显示增益 (曝光度) |

### 🖱️ 鼠标操作 (Mouse Controls)

*   **左键 (Left Click)**:
    *   **触发模式下**: 点击生成雷克子波 (Ricker Wavelet)。
    *   **绘图模式下**: 绘制对应的介质结构。
    *   **阵列模式下**: 按下并拖拽以放置一排检波器。
*   **右键 (Right Click)**:
    *   移动单点示波器的探针位置。
*   **UI 操作**:
    *   左侧面板可调整频率、波速、画笔大小等。
    *   右侧/底部面板观察波形记录。

---

## 🏗️ 代码结构说明 (Code Structure)

### 1. `AcousticEngine` 类 (物理核心)
这是模拟器的大脑，负责所有的物理计算。
*   **核心数组**:
    *   `p`: 声压场 (Pressure)。
    *   `vx`, `vz`: x 和 z 方向的质点振动速度。
    *   `coef_v`, `coef_p`: 基于介质密度和速度预计算的更新系数。
*   **`step()` 函数**:
    *   执行 FDTD 迭代。
    *   先更新速度场 ($v_x, v_z$)，再更新声压场 ($p$)。
    *   应用 CPML 吸收边界条件。
*   **`prepareGPUData()`**:
    *   将计算结果从 CPU 内存拷贝到 GPU 纹理内存，供 Shader 渲染。

### 2. `main` 函数与 UI 循环
*   **初始化**: 设置 GLFW 窗口、GLEW 上下文、ImGui 样式。
*   **主循环**:
    1.  处理输入 (`process_input`).
    2.  物理步进 (如果未暂停，调用 `engine.step()`).
    3.  数据上传 (纹理更新).
    4.  **OpenGL 渲染**: 绘制全屏矩形，运行 Fragment Shader 进行场的可视化。
    5.  **ImGui 渲染**: 绘制控制面板、示波器窗口和地震记录剖面。

### 3. 着色器 (Shaders)
*   **Vertex Shader**: 传递全屏四边形的坐标。
*   **Fragment Shader**:
    *   读取 `fieldTex` (波场) 和 `modelTex` (模型)。
    *   根据选择的 `RenderMode` (如热力图、光照折射效果) 计算像素颜色。
    *   实现“黏土”、“水面”等高级视觉效果。

---

## 📐 物理模型 (Physics Model)

模拟器求解的是各向同性介质中的声波方程（一阶速度-应力方程组形式）：

$$
\begin{cases}
\rho \frac{\partial v_x}{\partial t} = -\frac{\partial p}{\partial x} \\
\rho \frac{\partial v_z}{\partial t} = -\frac{\partial p}{\partial z} \\
\frac{1}{\rho c^2} \frac{\partial p}{\partial t} = -(\frac{\partial v_x}{\partial x} + \frac{\partial v_z}{\partial z}) + S
\end{cases}
$$

其中：
*   $p$: 声压
*   $v_x, v_z$: 质点速度
*   $\rho$: 介质密度
*   $c$: 声速
*   $S$: 声源项

离散化采用了 **交错网格 (Staggered Grid)** 技术，压力节点和速度节点在空间上交错分布，以提高数值稳定性。

---

## 📝 编译指南 (Build Instructions)

### Visual Studio
1.  创建一个新的 C++ 控制台项目。
2.  将整理好的 `.cpp` 代码复制到源文件中。
3.  通过 NuGet 或手动下载安装 `GLEW` 和 `GLFW`。
4.  在项目属性中：
    *   **C/C++ -> 常规 -> 附加包含目录**: 添加 GLEW, GLFW, ImGui 的头文件路径。
    *   **链接器 -> 常规 -> 附加库目录**: 添加 .lib 文件路径。
    *   **链接器 -> 输入 -> 附加依赖项**: 添加 `opengl32.lib`, `glew32.lib`, `glfw3.lib`。
    *   **C/C++ -> 语言 -> OpenMP 支持**: 设置为 **是 (/openmp)**。
5.  将 ImGui 的源文件 (`imgui.cpp`, `imgui_draw.cpp`, etc.) 添加到项目中。
6.  编译运行。

---

## ⚠️ 注意事项

*   **数值频散**: 如果频率设置过高或网格速度过低，可能会产生数值频散（波前出现波纹）。请确保满足 $ \lambda_{min} > 10 \cdot dx $。
*   **数值爆炸**: 如果波速设置过高导致 CFL 条件 ($v \cdot dt / dx < 1/\sqrt{2}$) 被破坏，模拟会发生数值爆炸。程序内置了检测机制，如果发生爆炸会显示警告并停止模拟。
*   **性能**: 网格大小为 500x500。在现代多核 CPU 上，开启 OpenMP 后应能轻松达到 60FPS 以上。

---

**Created with Acoustic Wave Pro Engine**