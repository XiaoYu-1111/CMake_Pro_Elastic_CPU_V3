
---

# 🌊 Acoustic Wave Pro (Ultimate Edition)

**高性能实时二维声波与弹性波模拟仿真平台**

![License](https://img.shields.io/badge/license-MIT-blue.svg) ![Language](https://img.shields.io/badge/language-C%2B%2B17-orange.svg) ![Platform](https://img.shields.io/badge/platform-Windows-blue.svg) ![Renderer](https://img.shields.io/badge/renderer-OpenGL%203.3-green.svg)

**Acoustic Wave Pro** 是一款基于物理的实时波动模拟软件。它利用**有限差分时域法 (FDTD)** 和 **OpenMP** 并行计算技术，在普通 PC 上实现了高精度的声学与弹性波传播模拟。

本项目不仅是一个科学计算工具，更结合了**电影级 Shader 渲染**技术，将枯燥的波场数据转化为极具视觉冲击力的流体、光影与艺术效果。

---

## ✨ 核心特性 (Key Features)

### 1. 双物理引擎 (Dual Physics Engines)
*   **声学引擎 (Acoustic Engine)**: 模拟标量压力场 ($P$)。适用于流体、气体介质中的声波传播演示。
*   **弹性波引擎 (Elastic Wave Engine)**: **[NEW]** 模拟矢量位移场和应力张量 ($\sigma_{xx}, \sigma_{zz}, \sigma_{xz}$)。
    *   支持 **纵波 (P-wave)** 与 **横波 (S-wave)** 的共存与转换。
    *   实现了基于 **Helmholtz 分解** 的实时波场分离技术（散度显示 P 波，旋度显示 S 波）。
    *   支持多种震源机制：爆炸源 (P)、剪切源 (S) 及混合震源。

### 2. 交互式建模与仿真
*   **实时绘图**: 使用鼠标绘制高速岩石、低速透镜体、反射墙体或吸波材料。
*   **自由表面 (Free Surface)**: 模拟空气/水界面的全反射现象（相位翻转）。
*   **复杂边界**: 内置 CPML 类海绵吸收边界，有效消除人工边界反射。
*   **震源控制**: 支持雷克子波 (Ricker)、方波脉冲等，可调节频率与位置。

### 3. 专业地震观测系统
*   **单点示波器**: 实时监测任意网格点的波形历史。
*   **检波器阵列 (Seismic Array)**: **[NEW]** 支持拖拽放置线性检波器阵列，模拟野外地震勘探。
*   **地震记录剖面**: 实时生成标准的竖向地震记录图 (Seismic Section)，支持波形填充与增益调整。

### 4. 电影级可视化 (Advanced Visualization)
摒弃传统的科学伪彩色，引入基于物理的渲染 (PBR) 思路：
*   **影纹法 (Schlieren)**: 模拟光学实验，可视化波前的密度梯度，呈现极具科技感的边缘光。
*   **珠光薄膜 (Pearlescent)**: 模拟纳米级薄膜干涉，呈现丝绸般的高级质感。
*   **水墨风格 (Ink Wash)**: 结合纸张纹理与轮廓描边，呈现中国传统水墨画风格。
*   **能量岩浆 (Magma)**: 基于黑体辐射色谱，展示波的能量流动。
*   **其他模式**: 磨砂玻璃、液态金属、陶瓷釉面等 15+ 种渲染模式。

---

## 🔬 物理原理 (Physical Principles)

### 1. 数值方法：FDTD
本项目采用 **交错网格有限差分 (Staggered-Grid Finite Difference)** 算法。
*   **空间**: 4 阶精度差分 (4th-order in space)。
*   **时间**: 2 阶精度差分 (2nd-order in time)。
相比传统中心差分，该方案显著减少了数值频散（Grid Dispersion），使得波形在长距离传播后依然保持清晰。

### 2. 声波方程 (Acoustic)
求解一阶速度-应力方程组：
$$
\begin{cases}
\frac{\partial v_x}{\partial t} = -\frac{1}{\rho} \frac{\partial p}{\partial x} \\
\frac{\partial v_z}{\partial t} = -\frac{1}{\rho} \frac{\partial p}{\partial z} \\
\frac{\partial p}{\partial t} = -\rho c^2 (\frac{\partial v_x}{\partial x} + \frac{\partial v_z}{\partial z}) + S
\end{cases}
$$

### 3. 弹性波方程 (Elastic)
引入剪切模量 $\mu$ 和拉梅常数 $\lambda$，求解完整的弹性动力学方程：
$$
\begin{cases}
\rho \frac{\partial v_i}{\partial t} = \frac{\partial \sigma_{ij}}{\partial x_j} + f_i \\
\frac{\partial \sigma_{ij}}{\partial t} = \lambda \delta_{ij} (\nabla \cdot \mathbf{v}) + \mu (\frac{\partial v_i}{\partial x_j} + \frac{\partial v_j}{\partial x_i})
\end{cases}
$$
在弹性介质中，波分为快波（P波）和慢波（S波）。当 P 波撞击界面时，部分能量会转换为 S 波（波型转换），本程序可完美复现这一物理现象。

### 4. 波场分离技术 (Wavefield Separation)
为了独立观察 P 波和 S 波，程序利用矢量微积分进行实时分离：
*   **P 波 (纵波)**: 对应速度场的**散度 (Divergence)**。
    $$ P \propto \nabla \cdot \mathbf{V} = \frac{\partial V_x}{\partial x} + \frac{\partial V_z}{\partial z} $$
*   **S 波 (横波)**: 对应速度场的**旋度 (Curl)**。
    $$ S \propto (\nabla \times \mathbf{V})_y = \frac{\partial V_x}{\partial z} - \frac{\partial V_z}{\partial x} $$

---

## 🎮 操作指南 (User Manual)

### 快捷键 (Shortcuts)

| 按键 | 功能 |
| :--- | :--- |
| **Space** | 暂停 / 继续模拟 |
| **S** | 单步步进 (暂停时使用) |
| **C** | 清空波场 |
| **P** | **截图 (Snapshot)**，保存至程序目录 |
| **F** | 切换自由表面 (Free Surface) |
| **B** | 切换吸收边界 (Absorbing Boundary) |
| **L** | 切换 点声源 / 线声源 |
| **1 - 6** | 切换工具 (1:震源, 2:岩石, 4:橡皮, 6:阵列) |
| **[/]** | 调整震源频率 |
| **Up/Down** | 调整显示曝光度 |

### 界面功能
*   **Settings Panel**: 切换物理引擎 (Acoustic/Elastic)、选择震源类型 (Explosive/Shear/Mixed)、调整材质参数。
*   **Visuals Panel**: 切换渲染模式 (Mode)、自定义颜色映射。
*   **Seismic Record**: 当使用 "Array Tool (6)" 并在地图上拖拽放置检波器后，该窗口将显示实时的地震记录剖面。

---

## 🛠️ 编译与构建 (Build)

### 依赖库
*   **C++17 Compiler** (MSVC 推荐)
*   **OpenGL 3.3+**
*   **GLFW3** (窗口管理)
*   **GLEW** (OpenGL 扩展加载)
*   **OpenMP** (并行计算，通常编译器自带)
*   **Dear ImGui** (UI 库，源码已集成)
*   **stb_image / stb_image_write** (图像处理)

### Visual Studio 配置
1.  新建 C++ 控制台项目。
2.  链接 `opengl32.lib`, `glew32.lib`, `glfw3.lib`。
3.  在项目属性 -> C/C++ -> 语言 中启用 **OpenMP 支持**。
4.  将 ImGui 源码加入项目。
5.  编译运行 (Release 模式推荐以获得最高帧率)。

---

## 🌟 优势与应用前景

### 1. 教学演示 (Education)
*   **直观性**: 相比枯燥的公式，实时可视化的波场能让学生瞬间理解“波的干涉”、“惠更斯原理”、“波型转换”等抽象概念。
*   **互动性**: 学生可以亲手“画”出透镜或障碍物，观察波的行为，极大地提高了学习兴趣。

### 2. 地球物理研究 (Geophysics)
*   **正演模拟**: 可作为轻量级的地震正演工具，用于验证地质模型的地震响应。
*   **观测系统设计**: 利用阵列工具，可以模拟不同检波器排列对信号接收的影响。

### 3. 跨界艺术创作 (Generative Art)
*   利用流体动力学和波动方程生成的复杂图案，结合高级 Shader，可用于生成动态壁纸、VJ 素材或数字艺术作品。

---

## 📷 截图展示 (Gallery)

*(此处可放置保存的 Snapshot_xxx.png 图片)*

*   **Mode 6: Color Schlieren (影纹法)** - 展示波前的精细结构。
*   **Mode 8: Pearlescent (珠光)** - 展示具有物理厚度的薄膜干涉效果。
*   **Seismic Section** - 展示垂直地震剖面记录。

---

**Author**: [Yu/Project Team]
**Version**: 2.0 Ultimate
**Date**: 2025