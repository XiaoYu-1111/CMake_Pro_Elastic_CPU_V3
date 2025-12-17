/**
 * @file    CMakePro_fdm_V3_3.cpp
 * @author  XIAO YU (1269466423@qq.com)
 * @brief   Acoustic Wave Propagation Simulation Engine (FDTD Method)
 * @version 3.3.0
 * @date    2025-12-16
 *
 * @copyright Copyright (c) 2025 [Xiao Yu]. All rights reserved.
 *
 * This software is licensed under the MIT License.
 * See the LICENSE file for more details.
 */
 // =================================================================================
#define NOMINMAX                 // 禁用Windows的min/max宏
#define STB_IMAGE_WRITE_IMPLEMENTATION // 启用写入实现
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "stb_image.h"
#include "stb_image_write.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <windows.h>
#include <vector>
#include <deque> // 双端列表
#include <cmath>
#include <iostream>
#include <algorithm>
#include <list>
#include <omp.h> 
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <thread>
#include <array> // 别忘了在文件顶部包含这个头文件

#include <filesystem> // 用于创建文件夹
namespace fs = std::filesystem;

// 跨平台文件操作
#include <string.h>
#ifdef _WIN32

#include <commdlg.h>
#else
// Linux headers
#include <stdio.h>
#include <stdlib.h>
#endif

#ifdef _WIN32
#include <windows.h> // Windows 下使用原生的 strcpy_s
#else
#include <string.h>
// Linux/Mac 下定义一个宏或内联函数来模拟 strcpy_s
// 参数含义: 目标 buffer, buffer 大小, 源字符串
inline int strcpy_s(char* dest, size_t size, const char* src) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0'; // 确保末尾也是 \0，防止溢出
    return 0;
}
#endif

// ==========================================
// 1. 核心参数与全局定义
// ==========================================
using Real = float;
const int INITIAL_WINDOW_WIDTH = 1000;
const int INITIAL_WINDOW_HEIGHT = 1000;
// const int GRID_W = 500;  <-- 删除 const
// const int GRID_H = 500;  <-- 删除 const
int GRID_W = 500;
int GRID_H = 500;
// const Real DX = 1.0f;    <-- 删除 const
Real DX = 1.0f;
// const Real DT = 0.0001f; <-- 删除 const
Real DT = 0.0001f;
// const int DAMP_WIDTH = 50; <-- 删除 const
int DAMP_WIDTH = 50;

int newW = GRID_W;
int newH = GRID_H;

// 其他保持不变...
const Real PI = 3.1415926535f;

// 全局控制开关
bool show_recorder_window = true;
bool show_recorders_window = false;
bool SwapInterval = true;
int stepsPerFrame = 1;
std::string exe_dir;
float wetness = 0.5;
//截图相关
bool triggerScreenshot = false;
// --- 截图提示相关 ---
std::string lastSavedPath = "";   // 保存最后一次截图的完整路径
double saveMsgStartTime = -10.0;  // 提示框开始显示的时间 (负数表示不显示)
const double SAVE_MSG_DURATION = 5.0; // 提示框显示几秒

// 全局交互变量
int arrayReceiverCount = 20;
bool isDraggingArray = false;
int dragStartX, dragStartY;

// --- 录制相关变量 ---
bool isRecording = false;          // 是否正在录制
int recordFrameCount = 0;          // 录制帧计数器
std::string currentRecordPath;     // 当前录制的保存文件夹路径
int recordInterval = 1;            // 录制间隔（1=每帧都存，2=隔一帧存...）

// 视口与坐标结构
struct ViewportInfo {
    int x, y, w, h;
    int screenX, screenY;
    float scaleX, scaleY;
};

// 工具模式枚举
enum class ToolMode {
    TRIGGER_SOURCE = 0, DRAW_HIGH_VEL = 1, DRAW_LOW_VEL = 2,
    DRAW_WALL = 3, ERASE = 4, PLACE_ARRAY = 5
};

// 渲染模式枚举
enum class RenderMode {
    LIQUID = 0, HEATMAP = 1, MODEL = 2, COOLWARM = 3, CUSTOM = 4, JET = 5,
    Black_White = 6, Frosted_Glass = 7, Thin_Film = 8, Porcelain = 9,
    Clay_Terracotta = 10, PBR_Gold = 11, Hologram = 12, Clay_wetness = 13,
    Coolwarm_Metallic = 14
};

enum class EngineType { ACOUSTIC, ELASTIC };
enum class ElasticVis { DIVERGENCE, CURL, ENERGY }; // 弹性波可视化模式

// ==========================================
// 2. Shader 工具函数
// ==========================================
std::string loadShaderFromFile(const std::string& filePath) {
    std::ifstream shaderFile;
    shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        shaderFile.open(filePath);
        std::stringstream shaderStream;
        shaderStream << shaderFile.rdbuf();
        shaderFile.close();
        return shaderStream.str();
    }
    catch (std::ifstream::failure& e) {
        std::cerr << "ERROR::SHADER::READ_FAIL: " << filePath << std::endl;
        return "";
    }
}

GLuint createShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint success; glGetShaderiv(s, GL_COMPILE_STATUS, &success);
    if (!success) { char l[512]; glGetShaderInfoLog(s, 512, NULL, l); std::cerr << l << std::endl; exit(-1); }
    return s;
}

inline Real sinc(Real x) { return (std::abs(x) < 1e-9) ? 1.0f : std::sin(x) / x; }

// ==========================================
// 3. 物理源与引擎
// ==========================================
// 雷克子波源
struct RickerSource {
    int cx, cy;
    Real t, t_peak, f_peak, amp;
    bool active;
    RickerSource(int x, int y, Real freq, Real amplitude)
        : cx(x), cy(y), f_peak(freq), amp(amplitude), t(0.0f), active(true) {
        t_peak = 1.0f / f_peak;
    }
    Real getValue() {
        Real x = PI * f_peak * (t - t_peak);
        return amp * (1.0f - 2.0f * x * x) * std::exp(-x * x);
    }
    void update(Real dt) {
        t += dt;
        if (t > 2.5f * t_peak) active = false;
    }
};

// 方波源
struct SquarePulseSource {
    int cx, cy; Real t, amp, duration; bool active;
    SquarePulseSource(int x, int y, Real amplitude, Real pulseDuration)
        : cx(x), cy(y), amp(amplitude), duration(pulseDuration), t(0.0f), active(true) {
    }
    Real getValue() { return (t < duration) ? amp : 0.0f; }
    void update(Real dt) { if (active) { t += dt; if (t >= duration) active = false; } }
};

// Ormsby 子波源
struct OrmsbySource {
    int cx, cy; Real t, t_offset, amp, f1, f2, f3, f4; bool active;
    OrmsbySource(int x, int y, Real f1, Real f2, Real f3, Real f4, Real amplitude)
        : cx(x), cy(y), f1(f1), f2(f2), f3(f3), f4(f4), amp(amplitude), t(0.0f), active(true) {
        t_offset = 0.2f;
    }
    Real getValue() {
        Real current_t = t - t_offset;
        Real term4 = f4 * f4 * sinc(PI * f4 * current_t) * sinc(PI * f4 * current_t);
        Real term3 = f3 * f3 * sinc(PI * f3 * current_t) * sinc(PI * f3 * current_t);
        Real term2 = f2 * f2 * sinc(PI * f2 * current_t) * sinc(PI * f2 * current_t);
        Real term1 = f1 * f1 * sinc(PI * f1 * current_t) * sinc(PI * f1 * current_t);
        Real alpha = (PI * PI) / ((f4 - f3) + (f2 - f1));
        return amp * alpha * ((term4 - term3) - (term2 - term1));
    }
    void update(Real dt) { t += dt; if (t > 2.0f * t_offset) active = false; }
};

// 声学引擎核心类
class AcousticEngine {
public:
    std::vector<Real> p, vx, vz;
    std::vector<Real> coef_v, coef_p, damp_coef;
    std::vector<Real> vp_data;
    std::vector<Real> gpu_field, gpu_model;
    std::list<RickerSource> sources;
    std::vector<SquarePulseSource> sources_2;
    std::vector<OrmsbySource> sources_OrmsbySource;

    // --- 【新增】弹性波变量 ---
    std::vector<Real> sxx, szz, sxz; // 应力张量分量
    std::vector<Real> vs_data;       // 剪切波速度模型
    std::vector<Real> rho_data;      // 密度模型 (需要单独存一下以便计算Lamé参数)

    // --- 状态控制 ---
    EngineType engineType = EngineType::ACOUSTIC; // 默认为声波
    ElasticVis elasticVisMode = ElasticVis::DIVERGENCE; // 默认看旋转场(S波)

    // 【新增】震源类型枚举
   // 0: 仅P波 (Explosive), 1: 仅S波 (Shear), 2: P+S同时 (Mixed)
    int sourceInjectionMode = 0;

    bool modelModified = true;
    bool exploded = false;
    bool paused = false;
    bool useLineSource = false;
    int boundaryMode = 0; // 0:Absorb, 1:Reflect, 2:Hide
    bool enableFreeSurface = false;
    int currentFreq = 100;

    //single recorder
    struct Recorder {
        int gx = 0;
        int gy = 0;
        std::deque<float> history; // 改用 deque
        int maxHistory = 1000;  // 类型改为 size_t 更规范

        void update(const std::vector<Real>& field, int width) {
            if (gx < 0 || gx >= width) return;

            // 增加对 gy 的边界检查，防止 gy 为负数或过大导致 idx 越界
            int idx = gy * width + gx;
            if (idx >= 0 && idx < (int)field.size()) {
                history.push_back(field[idx]);
                if (history.size() > maxHistory) {
                    history.pop_front(); // deque 的头部删除操作是 O(1) 的，极快
                }
            }
        }
    } recorder;

    // 检波器阵列
    struct Receiver {
        int gx, gy;
        std::vector<float> data;
        int writeHead = 0; // 当前写入位置的索引
        int capacity = 0;  // 最大历史长度

        Receiver(int x, int y, int maxLen) : gx(x), gy(y), capacity(maxLen) {
            data.resize(maxLen, 0.0f); // 预先分配内存并填零
            writeHead = 0;
        }

        // 辅助函数：重置
        void reset() {
            std::fill(data.begin(), data.end(), 0.0f);
            writeHead = 0;
        }
    };
    std::vector<Receiver> receivers;
    int maxHistoryLen = 1000;
    // ...
    void placeArray(int x1, int y1, int x2, int y2, int count) {
        receivers.clear();
        if (count < 1) count = 1;

        for (int i = 0; i < count; ++i) {
            // 1. 计算插值 t (0.0 到 1.0)
            float t = (count == 1) ? 0.0f : (float)i / (count - 1);

            // 2. 使用四舍五入计算坐标，比直接截断更平滑
            int cx = std::round(x1 + (x2 - x1) * t);
            int cy = std::round(y1 + (y2 - y1) * t);

            // 3. 使用 std::clamp 限制边界 (C++17)
            // 确保不放在最外圈的边界上（假设通常边界是墙壁或PML）
            cx = std::clamp(cx, 2, GRID_W - 3);
            cy = std::clamp(cy, 2, GRID_H - 3);

            receivers.emplace_back(cx, cy, maxHistoryLen);
        }
    }

    void updateReceivers() {
        for (auto& r : receivers) {
            // 边界安全检查 (可选，但在调试阶段很有用)
            if (r.gx < 0 || r.gx >= GRID_W || r.gy < 0 || r.gy >= GRID_H) continue;

            int idx = r.gy * GRID_W + r.gx;

            // 4. 环形缓冲区写入 (O(1) 复杂度)
            // 直接覆盖当前位置，然后指针后移
            r.data[r.writeHead] = p[idx];
            r.writeHead = (r.writeHead + 1) % r.data.size();
        }
    }

    void clearReceiversData() {
        for (auto& r : receivers) {
            r.reset();
        }
    }

    AcousticEngine() {
        size_t size = GRID_W * GRID_H;
        p.resize(size, 0); vx.resize(size, 0); vz.resize(size, 0);
        coef_v.resize(size); coef_p.resize(size); damp_coef.resize(size);
        vp_data.resize(size); gpu_field.resize(size); gpu_model.resize(size);

        // 【新增】初始化弹性波变量
        sxx.resize(size, 0); szz.resize(size, 0); sxz.resize(size, 0);
        vs_data.resize(size); rho_data.resize(size);

        initMaterial();
        initDamping();
        recorder.gx = GRID_W / 2; recorder.gy = GRID_H / 2; recorder.maxHistory = 1000;
    }

    void initMaterial() {
#pragma omp parallel for
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                setParams(y * GRID_W + x, 2000.0f, 2000.0f);
            }
        }
        modelModified = true;
    }

    // --- 修改 setParams 以支持 Vs ---
    void setParams(int idx, Real Vp, Real Rho) {
        vp_data[idx] = Vp;
        rho_data[idx] = Rho;

        // 自动设置 Vs (剪切波速度)
        // 液体/气体 (Vp < 1550) Vs = 0
        // 固体 Vs ≈ Vp / 1.732 (泊松体)
        Real Vs = 0.0f;
        if (Vp > 500.0f) {
            Vs = Vp / 1.732f;
        }
        vs_data[idx] = Vs;

        // 声波方程系数
        if (Vp < 10.0f) { coef_v[idx] = 0.0f; coef_p[idx] = 0.0f; }
        else {
            coef_v[idx] = (DT / DX) * (1.0f / Rho);
            coef_p[idx] = (DT / DX) * (Rho * Vp * Vp);
        }
    }

    void setParams(AcousticEngine& engine, int idx, Real Vp, Real Rho) {
        vp_data[idx] = Vp;
        rho_data[idx] = Rho;

        // 自动设置 Vs (剪切波速度)
        // 液体/气体 (Vp < 1550) Vs = 0
        // 固体 Vs ≈ Vp / 1.732 (泊松体)
        Real Vs = 0.0f;
        if (Vp > 500.0f) {
            Vs = Vp / 1.732f;
        }
        vs_data[idx] = Vs;

        // 声波方程系数
        if (Vp < 10.0f) { coef_v[idx] = 0.0f; coef_p[idx] = 0.0f; }
        else {
            coef_v[idx] = (DT / DX) * (1.0f / Rho);
            coef_p[idx] = (DT / DX) * (Rho * Vp * Vp);
        }
    }
    // --- 【新增】动态调整网格大小 ---
    void resizeSimulation(int newW, int newH) {
        // 安全检查：阻尼层不能覆盖整个屏幕
        if (DAMP_WIDTH * 2 >= newW) DAMP_WIDTH = newW / 2 - 2;
        if (DAMP_WIDTH * 2 >= newH) DAMP_WIDTH = newH / 2 - 2;

        // 1. 更新全局尺寸
        GRID_W = newW;
        GRID_H = newH;
        size_t size = GRID_W * GRID_H;

        // 2. 重新分配所有核心向量的大小
        // 注意：调整大小时，旧数据的空间位置会乱（因为行宽变了），
        // 所以直接 resize 并清零是最安全的做法，相当于重置模拟。

        // 声波/通用变量
        p.assign(size, 0.0f);
        vx.assign(size, 0.0f);
        vz.assign(size, 0.0f);
        coef_v.resize(size);
        coef_p.resize(size);
        damp_coef.resize(size);
        vp_data.resize(size);
        gpu_field.resize(size);
        gpu_model.resize(size);

        // 弹性波变量
        sxx.assign(size, 0.0f);
        szz.assign(size, 0.0f);
        sxz.assign(size, 0.0f);
        vs_data.resize(size);
        rho_data.resize(size);

        // 3. 重置波源、接收器和状态
        sources.clear();
        receivers.clear();
        recorder.history.clear();
        // 重置记录点到中心
        recorder.gx = GRID_W / 2;
        recorder.gy = GRID_H / 2;

        exploded = false;

        // 4. 重新初始化物理参数 (材质、阻尼、系数)
        initMaterial();
        initDamping();

        std::cout << ">>> Grid Resized to: " << GRID_W << " x " << GRID_H << std::endl;
    }

    void initDamping() {
        const Real DAMP_STRENGTH = 0.025f;
#pragma omp parallel for
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                int idx = y * GRID_W + x;
                Real damping = 1.0f;

                if (boundaryMode == 0) { // Absorbing Mode
                    // 计算到左右边界的距离
                    int dx = std::min(x, GRID_W - 1 - x);

                    // 【关键修改】计算到上下边界的距离
                    int dy;
                    if (enableFreeSurface) {
                        // 如果开启自由表面，只计算到底部的距离（顶部不设防）
                        dy = GRID_H - 1 - y;
                    }
                    else {
                        // 否则，顶部和底部都要计算
                        dy = std::min(y, GRID_H - 1 - y);
                    }

                    // 取最小距离
                    int min_dist = std::min(dx, dy);

                    // 如果在阻尼层范围内，计算衰减系数
                    if (min_dist < DAMP_WIDTH) {
                        Real r = (Real)(DAMP_WIDTH - min_dist) / DAMP_WIDTH;
                        damping = std::exp(-DAMP_STRENGTH * r * r * 10.0f);
                    }
                }
                damp_coef[idx] = damping;
            }
        }
    }

    void toggleBoundary() { boundaryMode = (boundaryMode == 0) ? 1 : 0;; initDamping(); }

    void toggleFreeSurface() {
        enableFreeSurface = !enableFreeSurface;
        initDamping(); // 【关键修改】状态改变后，必须重新生成阻尼层
    }

    void hide_Boundary_damp() { boundaryMode = 2; }

    void addSource(int x, int y) {
        if (exploded) return;
        float amp = 40000.0f;
        if (useLineSource) {
            int startY = (boundaryMode == 0) ? DAMP_WIDTH + 5 : 5;
            int endY = (boundaryMode == 0) ? GRID_H - DAMP_WIDTH - 5 : GRID_H - 5;
            for (int iy = startY; iy < endY; ++iy) if (iy % 2 == 0) sources.emplace_back(x, iy, currentFreq, amp * 0.4f);
        }
        else {
            sources.emplace_back(x, y, currentFreq, amp);
        }
    }

    // --- 修改 clearWavefield 清空新增变量 ---
    void clearWavefield() {
        std::fill(p.begin(), p.end(), 0.0f);
        std::fill(vx.begin(), vx.end(), 0.0f);
        std::fill(vz.begin(), vz.end(), 0.0f);
        // 【新增】
        std::fill(sxx.begin(), sxx.end(), 0.0f);
        std::fill(szz.begin(), szz.end(), 0.0f);
        std::fill(sxz.begin(), sxz.end(), 0.0f);

        sources.clear(); exploded = false;
        clearReceiversData();
    }
    // --- 核心步进分发 ---
    void step() {
        if (engineType == EngineType::ACOUSTIC) {
            stepAcoustic();
        }
        else {
            stepElastic();
        }
    }

    // 原来的 step() 改名为 stepAcoustic()，内容不变
    void stepAcoustic() {
        // ... (把原来 step 函数的内容原封不动放这里) ...
        // 注意：原来 step 里的逻辑是针对 p, vx, vz 的
        if (exploded) return;
        Real* __restrict d_p = p.data();
        Real* __restrict d_vx = vx.data();
        Real* __restrict d_vz = vz.data();
        const Real* __restrict d_cv = coef_v.data();
        const Real* __restrict d_cp = coef_p.data();
        const Real* __restrict d_damp = damp_coef.data();
        const Real C1 = 9.0f / 8.0f;
        const Real C2 = -1.0f / 24.0f;

        // 1. Update V
#pragma omp parallel for
        for (int y = 2; y < GRID_H - 2; ++y) {
            int row = y * GRID_W;
            int row_u1 = (y - 1) * GRID_W; int row_d1 = (y + 1) * GRID_W;
            int row_u2 = (y - 2) * GRID_W; int row_d2 = (y + 2) * GRID_W;
            for (int x = 2; x < GRID_W - 2; ++x) {
                int i = row + x;
                if (d_cv[i] == 0.0f) continue;
                Real dP_dx = C1 * (d_p[i + 1] - d_p[i]) + C2 * (d_p[i + 2] - d_p[i - 1]);
                Real dP_dz = C1 * (d_p[row_d1 + x] - d_p[i]) + C2 * (d_p[row_d2 + x] - d_p[row_u1 + x]);
                d_vx[i] -= d_cv[i] * dP_dx; d_vz[i] -= d_cv[i] * dP_dz;
                Real d = d_damp[i]; d_vx[i] *= d; d_vz[i] *= d;
            }
        }
        bool local_exploded = false;
        // 2. Update P
#pragma omp parallel for reduction(|:local_exploded)
        for (int y = 2; y < GRID_H - 2; ++y) {
            int row = y * GRID_W;
            int row_u1 = (y - 1) * GRID_W; int row_d1 = (y + 1) * GRID_W;
            int row_u2 = (y - 2) * GRID_W;
            for (int x = 2; x < GRID_W - 2; ++x) {
                int i = row + x;
                if (d_cp[i] == 0.0f) continue;
                Real dVx_dx = C1 * (d_vx[i] - d_vx[i - 1]) + C2 * (d_vx[i + 1] - d_vx[i - 2]);
                Real dVz_dy = C1 * (d_vz[i] - d_vz[row_u1 + x]) + C2 * (d_vz[row_d1 + x] - d_vz[row_u2 + x]);
                d_p[i] -= d_cp[i] * (dVx_dx + dVz_dy);
                d_p[i] *= d_damp[i];
                if (std::abs(d_p[i]) > 1e10) local_exploded = true;
            }
        }
        if (local_exploded) exploded = true;

        applySourcesAndBoundary(d_p); // 抽取的公共逻辑
    }

    // --- 【新增】弹性波步进逻辑 ---
    void stepElastic() {
        if (exploded) return;

        // 获取指针以加速访问
        Real* d_vx = vx.data(); Real* d_vz = vz.data();
        Real* d_sxx = sxx.data(); Real* d_szz = szz.data(); Real* d_sxz = sxz.data();
        const Real* d_vp = vp_data.data();
        const Real* d_vs = vs_data.data();
        const Real* d_rho = rho_data.data();
        const Real* d_damp = damp_coef.data();

        // 4阶差分系数
        const Real C1 = 9.0f / 8.0f;
        const Real C2 = -1.0f / 24.0f;
        const Real dt_dx = DT / DX;

        // 1. 更新速度场 V (利用应力求导)
#pragma omp parallel for
        for (int y = 2; y < GRID_H - 2; ++y) {
            int row = y * GRID_W;
            for (int x = 2; x < GRID_W - 2; ++x) {
                int i = row + x;
                if (d_rho[i] <= 0.0f) continue;

                Real buoyancy = dt_dx / d_rho[i]; // 浮力项 (1/rho)

                // dSxx/dx + dSxz/dz
                Real dSxx_dx = C1 * (d_sxx[i] - d_sxx[i - 1]) + C2 * (d_sxx[i + 1] - d_sxx[i - 2]);
                Real dSxz_dz = C1 * (d_sxz[i] - d_sxz[i - GRID_W]) + C2 * (d_sxz[i + GRID_W] - d_sxz[i - 2 * GRID_W]);

                // dSxz/dx + dSzz/dz
                Real dSxz_dx = C1 * (d_sxz[i + 1] - d_sxz[i]) + C2 * (d_sxz[i + 2] - d_sxz[i - 1]);
                Real dSzz_dz = C1 * (d_szz[i + GRID_W] - d_szz[i]) + C2 * (d_szz[i + 2 * GRID_W] - d_szz[i - GRID_W]);

                d_vx[i] += buoyancy * (dSxx_dx + dSxz_dz);
                d_vz[i] += buoyancy * (dSxz_dx + dSzz_dz);

                // 应用阻尼
                Real d = d_damp[i];
                d_vx[i] *= d; d_vz[i] *= d;
            }
        }

        bool local_exploded = false;

        // 2. 更新应力场 Stress (利用速度求导)
        // Lamé 参数: mu = rho * Vs^2, lambda = rho * (Vp^2 - 2*Vs^2)
#pragma omp parallel for reduction(|:local_exploded)
        for (int y = 2; y < GRID_H - 2; ++y) {
            int row = y * GRID_W;
            for (int x = 2; x < GRID_W - 2; ++x) {
                int i = row + x;
                if (d_rho[i] <= 0.0f) continue;

                Real r = d_rho[i];
                Real vp2 = d_vp[i] * d_vp[i];
                Real vs2 = d_vs[i] * d_vs[i];
                Real mu = r * vs2;
                Real lambda = r * (vp2 - 2.0f * vs2);

                // dVx/dx
                Real dVx_dx = C1 * (d_vx[i + 1] - d_vx[i]) + C2 * (d_vx[i + 2] - d_vx[i - 1]);
                // dVz/dz
                Real dVz_dz = C1 * (d_vz[i] - d_vz[i - GRID_W]) + C2 * (d_vz[i + GRID_W] - d_vz[i - 2 * GRID_W]);
                // dVx/dz
                Real dVx_dz = C1 * (d_vx[i + GRID_W] - d_vx[i]) + C2 * (d_vx[i + 2 * GRID_W] - d_vx[i - GRID_W]);
                // dVz/dx
                Real dVz_dx = C1 * (d_vz[i] - d_vz[i - 1]) + C2 * (d_vz[i + 1] - d_vz[i - 2]);

                d_sxx[i] += dt_dx * ((lambda + 2.0f * mu) * dVx_dx + lambda * dVz_dz);
                d_szz[i] += dt_dx * (lambda * dVx_dx + (lambda + 2.0f * mu) * dVz_dz);
                d_sxz[i] += dt_dx * (mu * (dVx_dz + dVz_dx));

                Real d = d_damp[i];
                d_sxx[i] *= d; d_szz[i] *= d; d_sxz[i] *= d;

                if (std::abs(d_sxx[i]) > 1e12) local_exploded = true;
            }
        }
        if (local_exploded) exploded = true;

        // 3. 处理波源和边界
        if (enableFreeSurface) {
#pragma omp parallel for
            for (int x = 0; x < GRID_W; ++x) {
                d_sxx[x] = 0; d_szz[x] = 0; d_sxz[x] = 0;
                // 顶部几层强制清零以模拟自由表面反射
                d_sxx[x + GRID_W] = 0; d_szz[x + GRID_W] = 0; d_sxz[x + GRID_W] = 0;
            }
        }
        // 波源注入：根据设定向不同的应力分量注入能量
        //for (auto& s : sources) {
        //    if (s.cx > 2 && s.cx < GRID_W - 2 && s.cy > 2 && s.cy < GRID_H - 2) {
        //        int idx = s.cy * GRID_W + s.cx;
        //        Real val = s.getValue();

        //        // 注入 P 波成分 (爆炸/膨胀)
        //        // 作用于正应力 Sxx 和 Szz
        //        if (sourceInjectionMode == 0 || sourceInjectionMode == 2) {
        //            d_sxx[idx] += val;
        //            d_szz[idx] += val;
        //        }

        //        // 注入 S 波成分 (剪切/旋转)
        //        // 作用于切应力 Sxz
        //        if (sourceInjectionMode == 1 || sourceInjectionMode == 2) {
        //            d_sxz[idx] += val;
        //        }
        //    }
        //    s.update(DT);
        //}
        // 波源注入：根据设定向不同的应力分量注入能量
        for (auto& s : sources) {
            if (s.cx > 2 && s.cx < GRID_W - 2 && s.cy > 2 && s.cy < GRID_H - 2) {
                int idx = s.cy * GRID_W + s.cx;
                Real val = s.getValue();

                // Mode 0: 纯 P 波 (爆炸源, 各向同性圆形)
                if (sourceInjectionMode == 0) {
                    d_sxx[idx] += val;
                    d_szz[idx] += val;
                }
                // Mode 1: 纯 S 波 (剪切源, S波最强在水平/垂直轴)
                else if (sourceInjectionMode == 1) {
                    d_sxz[idx] += val;
                }
                // Mode 2: 【定向混合源】 (P波垂直最大，S波旋转45度)
                else if (sourceInjectionMode == 2) {
                    // 策略：为了让P波在垂直方向最强，我们主要注入 Szz
                    // 为了修正S波角度，我们减少或取消 Sxz 的注入，依靠 Szz 的天然剪切分量

                    // 1. 垂直主分量 (产生垂直方向最强的P波)
                    d_szz[idx] += val * 1.0f;

                    // 2. 水平分量 (设得很小或为0)
                    // 如果设为0，P波会变成完美的 "8" 字形(偶极子)，垂直最强，水平无波。
                    // 如果设为 0.2 左右，P波会变成椭圆形，垂直强，水平弱。
                    d_sxx[idx] += val * 0.1f;

                    // 3. 切应力分量 (设为 0)
                    // 为什么？因为单纯的 Szz 注入本身就会在 45度 方向产生最强的 S波！
                    // 这正好修正了你说的 45度 偏差。
                    // 如果你还加上 d_sxz，S波角度又会被拉回到 0/90度。
                    // d_sxz[idx] += 0; 
                }
            }
            s.update(DT);
        }
        sources.remove_if([](const RickerSource& s) { return !s.active; });

        // 4. 【可视化映射】 (修改版：大幅提升可视化增益)
#pragma omp parallel for
        for (int y = 1; y < GRID_H - 1; ++y) {
            int row = y * GRID_W;
            int row_up = (y - 1) * GRID_W;
            int row_dn = (y + 1) * GRID_W;

            for (int x = 1; x < GRID_W - 1; ++x) {
                int i = row + x;

                // 计算速度场导数
                Real dVx_dx = (d_vx[i + 1] - d_vx[i - 1]) * 0.5f;
                Real dVz_dz = (d_vz[row_dn + x] - d_vz[row_up + x]) * 0.5f;
                Real dVx_dz = (d_vx[row_dn + x] - d_vx[row_up + x]) * 0.5f;
                Real dVz_dx = (d_vz[i + 1] - d_vz[i - 1]) * 0.5f;

                // --- 物理量级修正系数 ---
                // 散度/旋度是无量纲或速度梯度的，数值极小。
                // 乘以一个巨大的系数 (模拟体积模量 rho * vp^2)，使其数值范围与声压模式(P)接近。
                // 这样切换引擎时，画面亮度不会跳变。
                const Real VIS_SCALE = 4000000.0f;

                if (elasticVisMode == ElasticVis::DIVERGENCE) {
                    // 纯 P 波
                    p[i] = (dVx_dx + dVz_dz) * VIS_SCALE;
                }
                else if (elasticVisMode == ElasticVis::CURL) {
                    // 纯 S 波
                    p[i] = (dVx_dz - dVz_dx) * VIS_SCALE;
                }
                else { // ENERGY
                    // 能量模式也需要提升系数
                    p[i] = std::sqrt(d_vx[i] * d_vx[i] + d_vz[i] * d_vz[i]) * 5000000.0f;
                }
            }
        }
    }

    // 辅助函数：声波的源和边界处理(从原step中提取)
    void applySourcesAndBoundary(Real * d_p) {
        if (enableFreeSurface) {
#pragma omp parallel for
            for (int x = 0; x < GRID_W; ++x) { d_p[x] = 0; d_p[x + GRID_W] = 0; d_p[x + 2 * GRID_W] = 0; }
        }
        for (auto& s : sources) {
            if (s.cx > 2 && s.cx < GRID_W - 2 && s.cy > 2 && s.cy < GRID_H - 2) {
                int idx = s.cy * GRID_W + s.cx;
                if (coef_p[idx] > 0.0f) d_p[idx] += s.getValue();
            }
            s.update(DT);
        }
        sources.remove_if([](const RickerSource& s) { return !s.active; });
    }

    void modifyMaterial(int cx, int cy, int radius, int type) {
        int r2 = radius * radius;
        int x0 = std::max(0, cx - radius), x1 = std::min(GRID_W - 1, cx + radius);
        int y0 = std::max(0, cy - radius), y1 = std::min(GRID_H - 1, cy + radius);
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2) {
                    int idx = y * GRID_W + x;
                    if (type == 1) setParams(idx, 4500.0f, 2800.0f);
                    else if (type == 2) setParams(idx, 1500.0f, 1000.0f);
                    else if (type == 3) setParams(idx, 0.0f, 10000.0f);
                    else { Real factor = (Real)y / GRID_H; setParams(idx, 2000.0f, 2000.0f); }
                }
            }
        }
        modelModified = true;
    }

    void prepareGPUData() {
        if (modelModified) {
#pragma omp parallel for
            for (int i = 0; i < GRID_W * GRID_H; ++i) gpu_model[i] = vp_data[i];
            //for (int i = 0; i < GRID_W * GRID_H; ++i) std::memcpy(gpu_model.data(), vp_data.data(), vp_data.size() * sizeof(Real));
        }
        std::memcpy(gpu_field.data(), p.data(), p.size() * sizeof(Real));
    }

    // --- 场景加载 ---
    void loadScenario(int type) {
        initMaterial(); clearWavefield();
        if (type == 0) { // Earth Model (Acoustic: Velocity, Density)
            int cx = GRID_W / 2;
            int cy = GRID_H / 2;
            // 1. 预计算半径平方 (优化性能)
            float maxRadius = (GRID_W < GRID_H ? GRID_W : GRID_H) / 2.0f * 0.8f;
            float r_core = maxRadius * 0.54f;
            float r_mantle = maxRadius * 0.92f;

            float r_core_sq = r_core * r_core;
            float r_mantle_sq = r_mantle * r_mantle;
            float maxRadius_sq = maxRadius * maxRadius;

            for (int y = 0; y < GRID_H; ++y) {
                for (int x = 0; x < GRID_W; ++x) {
                    float dx = (float)(x - cx);
                    float dy = (float)(y - cy);
                    float distSq = dx * dx + dy * dy;
                    int idx = y * GRID_W + x;
                    // 参数格式: setParams(index, Velocity, Density)
                    // 速度保持在稳定范围 (3.0 ~ 6.0)
                    /*层级(Layer)	真实 Vp(km / s)	模拟 Vp(建议)	真实密度(g / cm³)	模拟密度(建议)	物理现象预期
                    地壳(Crust)	~6.0	3.0	~2.7	1.0 (基准)波速慢，波长短
                    地幔(Mantle)	~11.0 (平均)6.0 (最快)~4.5	1.5	波速最快，波前拉得最开
                    外地核(Core)	~8.0	4.0 (变慢)~11.0	4.0 (极重)强反射 + 折射聚焦*/
                    if (distSq < r_core_sq) {
                        // 【地核 Core】: 液态铁
                        // 速度: 4.0 (比地幔慢，产生折射聚焦)
                        // 密度: 3.0 (极高，这是产生反射的关键)
                        setParams(idx, 4000.0f, 4000.0f);
                    }
                    else if (distSq < r_mantle_sq) {
                        // 【地幔 Mantle】: 硅酸盐岩石
                        // 速度: 6.0 (最快)
                        // 密度: 1.5 (中等)
                        setParams(idx, 6000.0f, 1500.0f);
                    }
                    else if (distSq < maxRadius_sq) {
                        // 【地壳 Crust】: 表面岩石
                        // 速度: 3.0 (较慢)
                        // 密度: 1.0 (基准)
                        setParams(idx, 3000.0f, 1000.0f);
                    }
                    // 外部: 默认 (通常 initMaterial 会设为阻尼层或空气)
                    else {
						setParams(idx, 314.0f, 10000.0f);//设置为不可传播区域
                    }
                }
            }
            modelModified = true;
            // 震源位置：放在地幔中
            // 既然是声波模拟，即使放在地核也能传出来，但放在地幔能看到波撞击地核界面的反射，效果更好。
            int sourceDist = (int)((r_core + r_mantle) / 2.0f);
            int dis = 100;
            addSource(cx, cy - dis);
            //addSource(cx, cy);
        }
        else if (type == 1) { // Double Slit
            for (int y = 0; y < GRID_H; ++y)
                if (abs(y - GRID_H / 2) > 30 && abs(y - GRID_H / 2) < 180) modifyMaterial(GRID_W / 2, y, 5, 3);
            addSource(GRID_W / 4, GRID_H / 2);
        }
        else if (type == 2) { // Crystal
            for (int y = 100; y < GRID_H - 100; y += 40) for (int x = 200; x < GRID_W - 100; x += 40) modifyMaterial(x, y, 6, 3);
            useLineSource = true; addSource(50, GRID_H / 2);
        }
        else if (type == 3) { // Waveguide
#pragma omp parallel for
            for (int y = 0; y < GRID_H; ++y) for (int x = 0; x < GRID_W; ++x) {
                if (abs(y - GRID_H / 2) < 40) setParams(y * GRID_W + x, 1500.f, 1000.f);
                else setParams(y * GRID_W + x, 2500.f, 2000.f);
            }
            modelModified = true; addSource(50, GRID_H / 2);
        }
    }

    void setMaterialForGrid(AcousticEngine& engine, float lowSpeed, float highSpeed, float density = 1.0f, int scatterSize = 5.0) {
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                if (rand() % 100 < density * 100) {
                    bool isHighSpeed = rand() % 2 == 0;
                    for (int dy = 0; dy < scatterSize && y + dy < GRID_H; ++dy) {
                        for (int dx = 0; dx < scatterSize && x + dx < GRID_W; ++dx) {
                            int index = (y + dy) * GRID_W + (x + dx);
                            float speed = isHighSpeed ? highSpeed : lowSpeed;
                            engine.setParams(index, speed, speed * 1.2f);
                        }
                    }
                }
                else {
                    engine.setParams(y * GRID_W + x, 2000.f, 2000.f);
                }
            }
        }
    }

    void loadScatteringScenario(AcousticEngine& engine, float scatterDensity = 0.1f) {
        engine.initMaterial();
        setMaterialForGrid(engine, 1500.f, 4500.f, scatterDensity, 1);
        engine.modelModified = true;
    }

    void loadLayeredScenario(AcousticEngine& engine) {
        engine.initMaterial(); engine.clearWavefield();
        int layerCount = 3; float layerThickness = GRID_H / layerCount;
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                if (y < layerThickness) engine.setParams(y * GRID_W + x, 1500.f, 1000.f);
                else if (y < 2 * layerThickness) engine.setParams(y * GRID_W + x, 2500.f, 1500.f);
                else engine.setParams(y * GRID_W + x, 4500.f, 2800.f);
            }
        }
        engine.modelModified = true; engine.addSource(GRID_W / 4, GRID_H / 2);
    }

    void loadCurvedScenario_Sine(AcousticEngine& engine) {
        engine.initMaterial(); engine.clearWavefield();
        float amplitude1 = 30.0f, frequency1 = 2.0f, verticalOffset1 = GRID_H / 3.0f;
        float amplitude2 = 20.0f, frequency2 = 1.5f, verticalOffset2 = 2.0f * GRID_H / 3.0f;
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                float nx = (float)x / GRID_W * frequency1 * 2.0 * PI;
                float b1 = amplitude1 * sin(nx) + verticalOffset1;
                nx = (float)x / GRID_W * frequency2 * 2.0 * PI;
                float b2 = amplitude2 * sin(nx) + verticalOffset2;
                if (y < b1) engine.setParams(y * GRID_W + x, 314.f, 100.f);
                else if (y < b2) engine.setParams(y * GRID_W + x, 2500.f, 1500.f);
                else engine.setParams(y * GRID_W + x, 4500.f, 2800.f);
            }
        }
        engine.modelModified = true; engine.addSource(GRID_W / 2, GRID_H / 4);
    }

    void loadRefractionScenario(AcousticEngine& engine, int layerCount = 3, float minSpeed = 1500.0f, float maxSpeed = 4500.0f) {
        engine.initMaterial(); engine.clearWavefield();
        float layerThickness = GRID_H / layerCount;
        float layerSpeedStep = (maxSpeed - minSpeed) / layerCount;
        for (int i = 0; i < layerCount; ++i) {
            float layerSpeed = minSpeed + i * layerSpeedStep;
            for (int y = i * layerThickness; y < (i + 1) * layerThickness && y < GRID_H; ++y) {
                for (int x = 0; x < GRID_W; ++x) {
                    engine.setParams(y * GRID_W + x, layerSpeed, layerSpeed * 1.2f);
                }
            }
        }
        engine.modelModified = true; engine.addSource(GRID_W / 4, GRID_H / 2);
    }

    void loadTwoLayerData(AcousticEngine& engine) {
        // 1. 初始化与清空
        engine.initMaterial();
        engine.clearWavefield();

        // 2. 定义分界面深度 (深度 0.7 处)
        int interfaceDepth = (int)(GRID_H * 0.7f);

        // 3. 遍历网格设置参数
#pragma omp parallel for
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                int idx = y * GRID_W + x;

                if (y < interfaceDepth) {
                    // 【第一层】: 深度 < 0.7
                    // 纵波 Vp = 1000, 密度 rho = 1500
                    engine.setParams(idx, 1000.0f, 1500.0f);
                }
                else {
                    // 【第二层】: 深度 >= 0.7
                    // 纵波 Vp = 1200, 密度 rho = 2000
                    engine.setParams(idx, 1200.0f, 2000.0f);
                }
            }
        }

        // 4. 标记模型已修改，以便更新 GPU 纹理
        engine.modelModified = true;

        // 5. 设置震源位置
        // x = 500, z = 300 (在2D网格中 z 通常对应 y 轴)
        engine.addSource(250, 150);
    }

    };

// ==========================================
// 4. 全局状态
// ==========================================
AcousticEngine engine;
ToolMode currentTool = ToolMode::TRIGGER_SOURCE;
RenderMode currentRenderMode = RenderMode::Clay_wetness;
float displayGain = 0.0001f;
float brushRadiusPx = 20.0f;
bool showUI = true;
bool singleStepTrigger = false;
int paletteID = 0;

// 颜色定义
float colPos[3] = { 1.0f, 0.4f, 0.0f };
float colNeg[3] = { 0.0f, 0.2f, 0.8f };
float colZero[3] = { 0.9f, 0.9f, 0.9f };
float colClayBase[3] = { 0.72f, 0.35f, 0.22f };
float colClayDark[3] = { 0.45f, 0.20f, 0.12f };
float colmossColor[3] = { 0.3f, 0.5f, 0.1f };

float colAbsorbColor[3] = {1.0f, 0.6f, 0.2f };//vec3(1.0, 0.5, 0.0)
std::array<float, 3> colBaseColor = { 0.12f, 0.12f, 0.14f };
int scale_fac = 2;
std::array<float, 3> colHighColor = { 0.12f* scale_fac, 0.12f* scale_fac, 0.14f* scale_fac };

std::array<float, 3> colGridColor = { 0.1f, 0.1f, 0.1f };

float cursorX = 0.0f, cursorY = 0.0f;

// ==========================================
// 5. 交互与输入处理
// ==========================================
ViewportInfo getViewport(int winW, int winH) {
    ViewportInfo vp;
    float targetAspect = (float)GRID_W / GRID_H;
    float windowAspect = (float)winW / winH;
    if (windowAspect > targetAspect) {
        vp.h = winH; vp.w = (int)(winH * targetAspect); vp.x = (winW - vp.w) / 2; vp.y = 0;
    }
    else {
        vp.w = winW; vp.h = (int)(winW / targetAspect); vp.x = 0; vp.y = (winH - vp.h) / 2;
    }
    vp.screenX = vp.x; vp.screenY = (winH - (vp.y + vp.h));
    vp.scaleX = (float)vp.w / GRID_W; vp.scaleY = (float)vp.h / GRID_H;
    return vp;
}

void process_input(GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        cursorX = -1.0f; isDraggingArray = false; return;
    }

    int winW, winH; glfwGetWindowSize(window, &winW, &winH);
    ViewportInfo vp = getViewport(winW, winH);
    double mx, my; glfwGetCursorPos(window, &mx, &my);

    float relX = (float)mx - vp.screenX;
    float relY = (float)my - vp.screenY;
    float normX = relX / vp.w; float normY = relY / vp.h;
    cursorX = normX; cursorY = normY;
    int gx = (int)(normX * GRID_W); int gy = (int)(normY * GRID_H);

    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool leftRelease = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE;

    if (gx > 2 && gx < GRID_W - 2 && gy > 2 && gy < GRID_H - 2) {
        if (currentTool == ToolMode::PLACE_ARRAY) {
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS && !isDraggingArray) {
                isDraggingArray = true; dragStartX = gx; dragStartY = gy;
            }
        }
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            engine.recorder.gx = gx; engine.recorder.gy = gy; engine.recorder.history.clear();
        }
        else if (leftDown) {
            if (currentTool == ToolMode::TRIGGER_SOURCE) {
                static auto lastClickTime = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count() > 50) {
                    engine.addSource(gx, gy); lastClickTime = now;
                }
            }
            else {
                int r = (int)(brushRadiusPx / vp.scaleX);
                int type = 1;
                if (currentTool == ToolMode::DRAW_HIGH_VEL) type = 1;
                else if (currentTool == ToolMode::DRAW_LOW_VEL) type = 2;
                else if (currentTool == ToolMode::DRAW_WALL) type = 3;
                else if (currentTool == ToolMode::ERASE) type = 4;
                engine.modifyMaterial(gx, gy, r, type);
            }
        }
    }
    else { cursorX = -1.0f; }

    if (isDraggingArray && leftRelease) {
        isDraggingArray = false;
        engine.placeArray(dragStartX, dragStartY, gx, gy, arrayReceiverCount);
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_1) currentTool = ToolMode::TRIGGER_SOURCE;
        if (key == GLFW_KEY_2) currentTool = ToolMode::DRAW_HIGH_VEL;
        if (key == GLFW_KEY_3) currentTool = ToolMode::DRAW_LOW_VEL;
        if (key == GLFW_KEY_5) currentTool = ToolMode::DRAW_WALL;
        if (key == GLFW_KEY_4) currentTool = ToolMode::ERASE;
        if (key == GLFW_KEY_6) currentTool = ToolMode::PLACE_ARRAY, show_recorders_window=true;

        if (key == GLFW_KEY_7) engine.loadScenario(0);
        if (key == GLFW_KEY_8) engine.loadScenario(1);
        if (key == GLFW_KEY_9) engine.loadScenario(2);
        if (key == GLFW_KEY_0) engine.loadScenario(3);

        if (key == GLFW_KEY_Q) currentRenderMode = RenderMode::Clay_wetness;
        if (key == GLFW_KEY_W) currentRenderMode = RenderMode::HEATMAP;
        if (key == GLFW_KEY_E) currentRenderMode = RenderMode::MODEL;
        if (key == GLFW_KEY_A) currentRenderMode = RenderMode::COOLWARM;
        if (key == GLFW_KEY_D) currentRenderMode = RenderMode::Thin_Film;
        if (key == GLFW_KEY_T) {currentRenderMode = static_cast<RenderMode>((static_cast<int>(currentRenderMode) + 1) % 15);};

        if (key == GLFW_KEY_F) engine.toggleFreeSurface();
        if (key == GLFW_KEY_B) engine.toggleBoundary();
        if (key == GLFW_KEY_C) engine.clearWavefield();
        if (key == GLFW_KEY_SPACE) engine.paused = !engine.paused;
        if (key == GLFW_KEY_S) { engine.paused = true; singleStepTrigger = true; }
        if (key == GLFW_KEY_L) engine.useLineSource = !engine.useLineSource;
        if (key == GLFW_KEY_P) triggerScreenshot=1;
        

        if (key == GLFW_KEY_LEFT_BRACKET) engine.currentFreq = std::max(5.0f, engine.currentFreq - 2.0f);
        if (key == GLFW_KEY_RIGHT_BRACKET) engine.currentFreq = std::min(100.0f, engine.currentFreq + 2.0f);
        if (key == GLFW_KEY_BACKSPACE) { engine.initMaterial(); engine.clearWavefield(); }
        if (key == GLFW_KEY_UP) displayGain *= 1.2f;
        if (key == GLFW_KEY_DOWN) displayGain /= 1.2f;
    }
}

void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f; style.FrameRounding = 6.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.92f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.60f, 0.50f, 1.00f);
}

std::string get_exe_dir() {
    char buffer[MAX_PATH]; GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
}

void switchToEnglishInput() {
    ActivateKeyboardLayout((HKL)0x0409, KLF_ACTIVATE);
}
void switchToChineseInput() {
    // 0x0804 代表中文（简体，中国）
    ActivateKeyboardLayout((HKL)0x0804, KLF_ACTIVATE);
}

// 保存截图函数
std::string saveWavefieldToPNG(int x, int y, int width, int height) {
    std::vector<unsigned char> pixels(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // 翻转图像
    int rowSize = width * 3;
    std::vector<unsigned char> flippedRow(rowSize);
    for (int i = 0; i < height / 2; ++i) {
        unsigned char* rowTop = pixels.data() + i * rowSize;
        unsigned char* rowBottom = pixels.data() + (height - 1 - i) * rowSize;
        memcpy(flippedRow.data(), rowTop, rowSize);
        memcpy(rowTop, rowBottom, rowSize);
        memcpy(rowBottom, flippedRow.data(), rowSize);
    }

    // 生成文件名
    std::time_t now = std::time(nullptr);
    char filename[64];
    std::strftime(filename, sizeof(filename), "Snapshot_%Y%m%d_%H%M%S.png", std::localtime(&now));

    // 写入 PNG
    if (stbi_write_png(filename, width, height, 3, pixels.data(), width * 3)) {
        return std::string(filename); // 【修改】成功返回文件名
    }
    return ""; // 【修改】失败返回空
}
// 重载：保存到指定路径 (用于录制)
void saveFrameToPath(int x, int y, int width, int height, const std::string& filepath) {
    std::vector<unsigned char> pixels(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // 垂直翻转图像 (OpenGL坐标系原点在左下角，图片在左上角)
    int rowSize = width * 3;
    std::vector<unsigned char> flippedRow(rowSize);
    for (int i = 0; i < height / 2; ++i) {
        unsigned char* rowTop = pixels.data() + i * rowSize;
        unsigned char* rowBottom = pixels.data() + (height - 1 - i) * rowSize;
        memcpy(flippedRow.data(), rowTop, rowSize);
        memcpy(rowTop, rowBottom, rowSize);
        memcpy(rowBottom, flippedRow.data(), rowSize);
    }

    // 写入 PNG
    stbi_write_png(filepath.c_str(), width, height, 3, pixels.data(), width * 3);
}

void startRecording() {
    // 1. 生成基于时间的文件夹名，例如 "Recordings/Session_20231215_103000"
    std::time_t now = std::time(nullptr);
    char dirName[64];
    std::strftime(dirName, sizeof(dirName), "Recordings/Rec_%Y%m%d_%H%M%S", std::localtime(&now));

    currentRecordPath = dirName;

    // 2. 创建文件夹
    try {
        if (!fs::exists("Recordings")) fs::create_directory("Recordings");
        fs::create_directory(currentRecordPath);
        std::cout << ">>> Recording Started. Saving to: " << currentRecordPath << std::endl;

        isRecording = true;
        recordFrameCount = 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to create directory: " << e.what() << std::endl;
        isRecording = false;
    }
}

//# 在图片文件夹内运行
//ffmpeg - framerate 60 - i frame_ % 05d.png - vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" - c:v av1_nvenc - pix_fmt yuv420p - cq 20 output_av1.mp4
void stopRecording() {
    isRecording = false;
    std::cout << ">>> Recording Stopped. Total frames: " << recordFrameCount << std::endl;
    // 这里可以提示用户如何使用 FFmpeg 合成
}
// 放在 main 函数内部，或者作为独立函数
void resizeGLTextures(GLuint fieldTex, GLuint modelTex, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, fieldTex);
    // 重新分配显存，data 传 NULL 即可
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, NULL);

    glBindTexture(GL_TEXTURE_2D, modelTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, NULL);
}

// 辅助函数：绘制简单的状态浮窗（右上角）
void DrawStatusOverlay(bool* p_open, int gridW, int gridH, float cursorX, float cursorY, const AcousticEngine& engine) {
    const float DISTANCE = 10.0f;
    static int corner = 1; // 0=左上, 1=右上, 2=左下, 3=右下
    ImGuiIO& io = ImGui::GetIO();

    if (corner != -1) {
        ImVec2 window_pos = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE + 20.0f); // +20避开菜单栏
        ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    }

    ImGui::SetNextWindowBgAlpha(0.35f); // 半透明背景
    if (ImGui::Begin("StatusOverlay", p_open, (corner != -1 ? ImGuiWindowFlags_NoMove : 0) | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Text("Grid: %dx%d", gridW, gridH);

        // 坐标转换显示
        int gx = (int)(cursorX * gridW);
        int gy = (int)(cursorY * gridH);
        if (gx >= 0 && gx < gridW && gy >= 0 && gy < gridH)
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Pos: [%d, %d]", gx, gy);
        else
            ImGui::Text("Pos: [---, ---]");

        ImGui::Separator();
        ImGui::Text("Status: %s", engine.paused ? "PAUSED" : "RUNNING");
        if (engine.exploded) ImGui::TextColored(ImVec4(1, 0, 0, 1), "EXPLOSION DETECTED!");

        ImGui::Text("Sources: %d", (int)engine.sources.size());

        if (ImGui::IsMousePosValid())
            ImGui::Text("Mouse: (%.1f, %.1f)", io.MousePos.x, io.MousePos.y);
    }
    ImGui::End();
}

void Init_Imgui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    exe_dir = get_exe_dir();
    std::string font_path = "../resources/font/consola.ttf";
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 14.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    io.FontGlobalScale = 1.5f;

    SetupImGuiStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

}

// 定义一个 Getter 函数，用来从 deque 中取值
float ValueGetter(void* data, int idx) {
    const auto& dequeRef = *(const std::deque<float>*)data;
    return dequeRef[idx];
}

// ---------------------------------------------------------
// Helper: Seismic Color Mappings
// ---------------------------------------------------------

// Mode 2: Red-White-Blue (Seismic Polarity)
static ImU32 GetSeismicColor(float val, float maxVal) {
    float t = val / maxVal;
    if (t > 1.0f) t = 1.0f;
    if (t < -1.0f) t = -1.0f;

    // Background neutral color (matches deep UI background)
    ImVec4 colCenter(0.4f, 0.4f, 0.45f, 1.0f);
    ImVec4 colPos(1.0f, 0.2f, 0.2f, 1.0f);  // Positive: Soft Red
    ImVec4 colNeg(0.2f, 0.6f, 1.0f, 1.0f);  // Negative: Soft Blue

    ImVec4 res;
    if (t > 0) {
        res.x = colCenter.x + (colPos.x - colCenter.x) * t;
        res.y = colCenter.y + (colPos.y - colCenter.y) * t;
        res.z = colCenter.z + (colPos.z - colCenter.z) * t;
        res.w = 1.0f;
    }
    else {
        t = -t;
        res.x = colCenter.x + (colNeg.x - colCenter.x) * t;
        res.y = colCenter.y + (colNeg.y - colCenter.y) * t;
        res.z = colCenter.z + (colNeg.z - colCenter.z) * t;
        res.w = 1.0f;
    }
    return ImGui::ColorConvertFloat4ToU32(res);
}

// Mode 3: Rainbow/Jet (Energy)
static ImU32 GetRainbowColor(float val, float maxVal) {
    float t = std::abs(val) / maxVal;
    if (t > 1.0f) t = 1.0f;
    // Map 0.0->1.0 to Hue 0.66(Blue)->0.0(Red)
    return ImColor::HSV((1.0f - t) * 0.66f, 0.85f, 0.9f);
}

void Manual_print() {
    // 控制台操作手册
    std::cout << "=== Acoustic Wave Pro (Console Version) ===\n";
    std::cout << "-------------------------------------------\n";
    std::cout << " [MODES]\n";
    std::cout << "   Q: Liquid\n";
    std::cout << "   W: Heatmap\n";
    std::cout << "   E: Model\n";
    std::cout << "   A: Coolwarm (NEW!)\n";
    std::cout << "-------------------------------------------\n";
    std::cout << " [SCENARIOS]\n";
    std::cout << "   6: Double Slit\n";
    std::cout << "   7: Acoustic Lens\n";
    std::cout << "   8: Crystal\n";
    std::cout << "   9: Waveguide\n";
    std::cout << "-------------------------------------------\n";
    std::cout << " [TOOLS]\n";
    std::cout << "   1: Source (Left Click)\n";
    std::cout << "   2: High Vel\n";
    std::cout << "   3: Low Vel\n";
    std::cout << "   5: Wall\n";
    std::cout << "   4: Eraser\n";
    std::cout << "-------------------------------------------\n";
    std::cout << " [CONTROLS]\n";
    std::cout << "   L: Line/Point Source\n";
    std::cout << "   F: Free Surface\n";
    std::cout << "   C: Clear\n";
    std::cout << "   Space: Pause\n";
    std::cout << "   [ / ]: Freq\n";
    std::cout << "   Up/Down: Gain\n";
    std::cout << "-------------------------------------------\n";

}
//自动配置openmp
void InitOpenMPRobust(int target) {
    int maxProcs = omp_get_num_procs();

    // 逻辑：如果核心足够多，就用 target；否则就全用上
    if (maxProcs >= target) {
        omp_set_num_threads(target);
    }
    else {
        // 这里的改进是：如果只有 6 核，就用 6 核，而不是降级到 2 核
        omp_set_num_threads(maxProcs);
    }

    // 再次检查，防止 maxProcs 返回异常值（极少情况）
    if (omp_get_max_threads() < 1) omp_set_num_threads(1);

    std::cout << ">>> Auto-configured OpenMP: " << omp_get_max_threads()
        << " threads (Hardware: " << maxProcs << ")" << std::endl;
}

// 将此代码放在你的 ImGui 渲染循环中 (例如 RenderUI() 函数内)
void RenderThreadControl(){
    // 定义一个控制窗口显示的布尔值
    // 如果是在函数内部，记得加 static；如果是全局变量则不需要
    static bool showThreadConfig = false;
    // ==========================================================
    // 1. 触发部分：放在你的主界面、菜单栏或工具栏中
    // ==========================================================
    // 可以在这里点击打开窗口
    if (ImGui::Button("CPU Settings", ImVec2(-1, 40))) {
        showThreadConfig = true;
    }
    // ==========================================================
    // 2. 弹窗逻辑部分：每一帧都要调用
    // ==========================================================
    if (showThreadConfig)
    {
        // 注意这里传入了 &showThreadConfig
        // 这会在窗口右上角创建一个 "X" 关闭按钮，点击后自动将 showThreadConfig 设为 false
        ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver); // 设置个默认大小
        if (ImGui::Begin("Thread Configuration", &showThreadConfig))
        {
            // --- 初始化状态 (只执行一次) ---
            static bool isInitialized = false;
            static int currentThreads = 1;
            static int maxProcs = 1;

            if (!isInitialized) {
                maxProcs = omp_get_num_procs();
                currentThreads = omp_get_max_threads(); // 获取当前 OpenMP 设置的值
                isInitialized = true;
            }

            ImGui::Text("Detected Hardware Cores: %d", maxProcs);
            ImGui::Separator();

            // --- 滑动条 ---
            // 拖动结束或数值改变时更新 OpenMP
            if (ImGui::SliderInt("Threads", &currentThreads, 1, maxProcs)) {
                omp_set_num_threads(currentThreads);
            }

            // --- 快捷按钮 ---
            if (ImGui::Button("Min (1)")) {
                currentThreads = 1;
                omp_set_num_threads(currentThreads);
            }
            ImGui::SameLine();
            if (ImGui::Button("Half")) {
                currentThreads = maxProcs / 2;
                if (currentThreads < 1) currentThreads = 1;
                omp_set_num_threads(currentThreads);
            }
            ImGui::SameLine();
            if (ImGui::Button("Max")) {
                currentThreads = maxProcs;
                omp_set_num_threads(currentThreads);
            }

            // --- 动态可视化进度条 ---
            ImGui::Spacing();
            ImGui::Text("Active Utilization:");

            float fraction = (float)currentThreads / (float)maxProcs;

            // 颜色逻辑：低负载(绿) -> 高负载(红)
            ImVec4 progressColor = ImVec4(fraction, 1.0f - fraction, 0.2f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, progressColor);

            char overlay[32];
            sprintf(overlay, "%d Threads", currentThreads);
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);

            // (可选)提示信息
                if (currentThreads == maxProcs) {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Warning: Full load may affect UI responsiveness.");
                }

            ImGui::PopStyleColor();

            // --- 底部关闭按钮 (可选，为了方便用户) ---
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(-1, 0))) { // -1 表示宽度填满
                showThreadConfig = false;
            }
        }
        ImGui::End();
    }
}

//导出模型数据
void exportModelData(const AcousticEngine& engine, const std::string& vpFilename, const std::string& rhoFilename) {
    std::cout << "[IO] Starting model export..." << std::endl;

    // 1. 打开文件流
    std::ofstream fileVp(vpFilename);
    std::ofstream fileRho(rhoFilename);

    if (!fileVp.is_open() || !fileRho.is_open()) {
        std::cerr << "[IO] Error: Failed to create output files." << std::endl;
        return;
    }

    // 2. 设置高精度，防止数据截断导致模拟误差
    // 使用 fixed 或 scientific 均可，这里保留 4 位小数
    fileVp << std::fixed << std::setprecision(4);
    fileRho << std::fixed << std::setprecision(4);

    // 3. 遍历数据并写入
    // 注意：数据在内存中是一维的 (idx = y * W + x)，但在文件中我们要写成二维格式
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            int idx = y * GRID_W + x;

            // 获取数据 (假设 engine.vp_data 和 engine.rho_data 是 public 的 std::vector<float>)
            // 如果是 private，请提供 getVp(idx) 接口
            float vp = engine.vp_data[idx];
            float rho = engine.rho_data[idx];

            // 写入数值
            fileVp << vp;
            fileRho << rho;

            // 如果不是这一行的最后一个数，加空格分隔
            if (x < GRID_W - 1) {
                fileVp << " ";
                fileRho << " ";
            }
        }
        // 一行结束，写入换行符
        fileVp << "\n";
        fileRho << "\n";
    }

    // 4. 关闭文件
    fileVp.close();
    fileRho.close();

    std::cout << "[IO] Export success!" << std::endl;
    std::cout << "     -> " << vpFilename << std::endl;
    std::cout << "     -> " << rhoFilename << std::endl;
}
// 辅助：读取文件确定网格尺寸
bool GetMatrixSize(const std::string& path, int& outW, int& outH) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    int rows = 0;
    int cols = 0;

    // 读取第一行计算列数
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        float val;
        while (ss >> val) cols++;
        rows++;
    }
    // 读取剩余行
    while (std::getline(file, line)) {
        if (!line.empty()) rows++;
    }
    outW = cols;
    outH = rows;
    return (outW > 0 && outH > 0);
}

void uploadModelToGPU(const AcousticEngine& engine, GLuint modelTex, int width, int height) {
    // 1. 准备上传缓冲区 (RGBA格式: R=Vp, G=Rho, B=未使用, A=未使用)
    // 大小 = 宽 * 高 * 4个通道
    std::vector<float> textureBuffer(width * height * 4);

#pragma omp parallel for
    for (int i = 0; i < width * height; ++i) {
        // 获取 CPU 数据
        float vp = engine.vp_data[i];
        float rho = engine.rho_data[i];

        // 填充到纹理缓冲区
        // 假设 Shader 里: model.r = Vp, model.g = Rho
        textureBuffer[i * 4 + 0] = vp;       // R通道
        textureBuffer[i * 4 + 1] = rho;      // G通道
        textureBuffer[i * 4 + 2] = 0.0f;     // B (padding)
        textureBuffer[i * 4 + 3] = 0.0f;     // A (padding)
    }

    // 2. 上传到 GPU
    glBindTexture(GL_TEXTURE_2D, modelTex);

    // 注意：这里我们使用刚刚填充好的 textureBuffer.data()
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, textureBuffer.data());

    glBindTexture(GL_TEXTURE_2D, 0); // 解绑

    std::cout << "[GPU] Model texture updated successfully." << std::endl;
}
//加载模型数据
bool loadModelFromData(AcousticEngine& engine,
    const std::string& vpFilename,
    const std::string& rhoFilename,
    GLuint& fieldTex, GLuint& modelTex) // 需要传入纹理ID以便重置
{
    
    // 1. 预读取尺寸
    int w1, h1, w2, h2;
    if (!GetMatrixSize(vpFilename, w1, h1) || !GetMatrixSize(rhoFilename, w2, h2)) {
        std::cerr << "[IO] Error: Could not read file dimensions." << std::endl;
        return false;
    }

    if (w1 != w2 || h1 != h2) {
        std::cerr << "[IO] Error: Mismatch dimensions! Vp:" << w1 << "x" << h1 << ", Rho:" << w2 << "x" << h2 << std::endl;
        return false;
    }

    // 2. 调整引擎大小 (Resize)
    std::cout << "[IO] Resizing simulation to " << w1 << "x" << h1 << "..." << std::endl;

    // 更新全局网格变量 (假设你有 GRID_W 等全局变量)
    extern int GRID_W, GRID_H; // 声明引用全局变量
    GRID_W = w1;
    GRID_H = h1;

    engine.resizeSimulation(GRID_W, GRID_H);
    resizeGLTextures(fieldTex, modelTex, GRID_W, GRID_H); // 调整显存纹理

    // 3. 读取数据
    std::ifstream fileVp(vpFilename);
    std::ifstream fileRho(rhoFilename);

    float tempVp, tempRho;
    int idx = 0;

    // 确保清空波场
    // 1. 初始化与清空
    engine.initMaterial();
    engine.clearWavefield();

    while (fileVp >> tempVp && fileRho >> tempRho) {
        if (idx >= GRID_W * GRID_H) break;
        //engine.vp_data[idx] = tempVp;
        //engine.rho_data[idx] = tempRho;
        engine.setParams(engine, idx, tempVp, tempRho);

        idx++;
    }
    // ===============================================
    // 【关键修复】将读取到的数据上传到显卡
    // ===============================================
    uploadModelToGPU(engine, modelTex, GRID_W, GRID_H);

    // 标记更新
    newW = GRID_W;
    newH = GRID_H;
    engine.modelModified = true;


    // 建议：重置一下震源位置，防止震源跑出新网格的范围
    engine.addSource(GRID_W / 2,GRID_H / 2);

    std::cout << "[IO] Model loaded successfully!" << std::endl;
    return true;
}

bool OpenWindowsFileDialog(char* buffer, int bufferSize) {
    OPENFILENAMEA ofn; // 使用 A 版本对应 char*
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL; // 如果有 glfw 的 HWND 可以填入，填 NULL 也没问题
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = bufferSize;
    // 过滤器：只显示 txt 文件，或者所有文件
    ofn.lpstrFilter = "Model Data (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    // 关键：OFN_NOCHANGEDIR 防止改变程序当前目录，导致资源加载失败
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    // 如果用户点击了“打开”，返回 true
    return GetOpenFileNameA(&ofn);
}


// 统一的接口函数,适用于win/linux
bool OpenSystemFileDialog(char* buffer, int bufferSize) {
    // 清空缓冲区
    memset(buffer, 0, bufferSize);

#ifdef _WIN32
    // ================= Windows 实现 =================
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = bufferSize;
    ofn.lpstrFilter = "Model Data (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    return GetOpenFileNameA(&ofn);

#else
    // ================= Linux 实现 (Zenity) =================
    // 注意：确保 Linux 系统安装了 zenity
    const char* cmd = "zenity --file-selection --file-filter='*.txt' --title='Select Model Data'";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        std::cerr << "[Error] Zenity not found. Install it via package manager." << std::endl;
        return false;
    }

    bool success = false;
    if (fgets(buffer, bufferSize, pipe) != NULL) {
        // 去掉末尾的换行符
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        success = (strlen(buffer) > 0);
    }

    pclose(pipe);
    return success;
#endif
}

void RenderNativeLoadPanel(AcousticEngine& engine, GLuint& fieldTex, GLuint& modelTex)
{
    static bool showWindow = false;

    // 用于存储路径的缓冲区
    // static 保证下一帧数据还在
    static char vpPath[1024] = "";
    static char rhoPath[1024] = "";

    // 状态提示
    static std::string statusMsg = "";
    if (ImGui::Button("Import Model (Win32)",ImVec2(-1, 40))) {
        showWindow = true;
    }

    if (showWindow) {
        ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Import from Local", &showWindow)) {

            ImGui::Text("Load Vp and Density files (.txt)");
            ImGui::Separator();
            ImGui::Spacing();
            // ---------------------------------------------------------
            // 1. 选择纵波文件 (Vp)
            // ---------------------------------------------------------
            ImGui::Text("Vp File Path:");
            // 显示当前路径，如果太长可能会被截断
            ImGui::InputText("##vp", vpPath, sizeof(vpPath), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();

            // 按钮逻辑：调用封装好的 Windows API
            if (ImGui::Button("Browse##1")) {
                // 创建一个临时 buffer 或者是直接清空 vpPath
                // 这里我们直接传入 vpPath，如果用户取消选择，内容不会变（取决于实现，最安全是先清空）
                char tempBuffer[1024] = "";
                if (OpenSystemFileDialog(tempBuffer, 1024)) {
                    // 如果选中了文件，更新路径
                    strcpy_s(vpPath, tempBuffer);
                }
            }
            ImGui::Spacing();

            // ---------------------------------------------------------
            // 2. 选择密度文件 (Rho)
            // ---------------------------------------------------------
            ImGui::Text("Density File Path:");
            ImGui::InputText("##rho", rhoPath, sizeof(rhoPath), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();

            if (ImGui::Button("Browse##2")) {
                char tempBuffer[1024] = "";
                if (OpenSystemFileDialog(tempBuffer, 1024)) {
                    strcpy_s(rhoPath, tempBuffer);
                }
            }

            ImGui::Separator();
            ImGui::Spacing();

            // ---------------------------------------------------------
            // 3. 确认加载
            // ---------------------------------------------------------
            // 检查两个路径是否都不为空
            bool isReady = (strlen(vpPath) > 0) && (strlen(rhoPath) > 0);

            if (!isReady) ImGui::BeginDisabled();

            if (ImGui::Button("LOAD MODEL", ImVec2(-1, 50))) {
                // 调用之前的核心加载函数
                // 此时 vpPath 和 rhoPath 已经包含了完整的 Windows 路径
                if (loadModelFromData(engine, vpPath, rhoPath, fieldTex, modelTex)) {
                    statusMsg = "Success: Model Loaded!";
                    showWindow = false; // 关闭窗口
                }
                else {
                    statusMsg = "Error: Failed to load files.";
                }
            }

            if (!isReady) ImGui::EndDisabled();

            if (!statusMsg.empty()) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", statusMsg.c_str());
            }

            ImGui::End();
        }
    }
}


void Render_Imgui(GLFWwindow* window, int frames,  GLuint fieldTex, GLuint modelTex) {

    // 渲染流程
    int winW, winH; glfwGetWindowSize(window, &winW, &winH);
    ViewportInfo vp = getViewport(winW, winH);
    ImGuiIO& io = ImGui::GetIO();
    // ==========================================
    //  [新增] 截图逻辑
    //  放在 ImGui 渲染之前，只截取波场画面，不带 UI
    // ==========================================
    // 在 glDrawArrays 之后，ImGui 渲染之前
    if (triggerScreenshot) {
        std::string fName = saveWavefieldToPNG(vp.x, vp.y, vp.w, vp.h);
        if (!fName.empty()) {
            // 获取绝对路径 (Windows API)
            char absPath[MAX_PATH];
            if (_fullpath(absPath, fName.c_str(), MAX_PATH) != NULL) {
                lastSavedPath = std::string(absPath);
            }
            else {
                lastSavedPath = fName; // 如果获取绝对路径失败，就显示文件名
            }

            // 记录当前时间，触发提示框
            saveMsgStartTime = glfwGetTime();
        }
        triggerScreenshot = false;
    }
    // ==========================================
    //  [录制逻辑]
    // ==========================================
    if (isRecording) {
        // 每隔 recordInterval 帧保存一次
        if (frames % recordInterval == 0) {
            // 格式化文件名: frame_00000.png
            std::stringstream ss;
            ss << currentRecordPath << "/frame_"
                << std::setw(5) << std::setfill('0') << recordFrameCount
                << ".png";

            // 保存
            saveFrameToPath(vp.x, vp.y, vp.w, vp.h, ss.str());
            recordFrameCount++;
        }
    }
    // --- ImGui 绘制开始 ---
    glViewport(0, 0, winW, winH);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 窗口状态控制 (static 记忆状态)
    static bool show_control_panel = true;
    static bool show_overlay = true;
    static bool showSuccessPopup = false; // <--- 这里定义缺少的变量
    // show_recorder_window 和 show_recorders_window 是全局变量

    if (showUI) {
        // ============================================================
        // 1. 顶部主菜单栏 (仅用于窗口管理) 
        // ============================================================
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Program")) {
                if (ImGui::MenuItem("Exit", "Alt+F4")) glfwSetWindowShouldClose(window, true);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Main Controls", NULL, &show_control_panel);
                ImGui::MenuItem("Status Overlay", NULL, &show_overlay);
                ImGui::MenuItem("Single Detector", NULL, &show_recorder_window);
                ImGui::MenuItem("Detector Array", NULL, &show_recorders_window);
                ImGui::EndMenu();
            }

            // 右侧 FPS 显示
            float rightAlign = ImGui::GetWindowWidth() - 100;
            ImGui::SameLine(rightAlign);
            ImGui::TextDisabled("%.0f FPS", io.Framerate);
            ImGui::EndMainMenuBar();
        }
        // ============================================================
        // 2. 主控制面板 (Tab 布局)
        // ============================================================
        if (show_control_panel) {
            ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(320, 750), ImGuiCond_FirstUseEver);

            ImGui::Begin("Wave Control Panel", &show_control_panel);

            // --- 顶部通用控制区 ---
            float btnW = (ImGui::GetContentRegionAvail().x - 10) / 2;
            if (ImGui::Button(engine.paused ? " RESUME (Space) " : " PAUSE (Space) ", ImVec2(btnW, 35))) engine.paused = !engine.paused;
            ImGui::SameLine();
            if (ImGui::Button(" CLEAR (C) ", ImVec2(btnW, 35))) engine.clearWavefield();

            if (ImGui::Button(" STEP FRAME (S) ", ImVec2(-1, 25))) { engine.paused = true; singleStepTrigger = true; }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            // --- Tab 栏 ---
            if (ImGui::BeginTabBar("CtrlTabs")) {
                // --------------------------------------------------------
                // Tab 1: 工具与录制 (Tools & IO)
                // --------------------------------------------------------
                if (ImGui::BeginTabItem(" Tools ")) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Interaction Tools");

                    const char* tools[] = { "Source (1)", "High Vel (2)", "Low Vel (3)", "Wall (5)", "Eraser (4)", "Array Placer (6)" };
                    for (int i = 0; i < 6; i++) {
                        if (ImGui::RadioButton(tools[i], (int)currentTool == i)) {
                            currentTool = (ToolMode)i;
                            if (currentTool == ToolMode::PLACE_ARRAY) show_recorders_window = true;
                        }
                        // 每两个放一行，节省空间
                        if (i % 2 == 0) ImGui::SameLine(160);
                    }
                    // 补一个换行，防止最后的 SameLine 导致布局错乱
                    ImGui::NewLine();
                    ImGui::Separator();
                    ImGui::Text("Properties");
                    ImGui::SliderFloat("Brush Size", &brushRadiusPx, 5.0f, 100.0f);
                    ImGui::Checkbox("Line Source (L)", &engine.useLineSource);
                    if (currentTool == ToolMode::PLACE_ARRAY) {
                        ImGui::SliderInt("Array Count", &arrayReceiverCount, 1, 100);
                    }

                    ImGui::Spacing(); ImGui::Separator();
                    ImGui::TextDisabled("Capture & Recording");

                    // 截图
                    if (ImGui::Button(" SNAPSHOT (P) ", ImVec2(-1, 30))) { triggerScreenshot = true; }

                    // 录制
                    ImGui::Spacing();
                    if (!isRecording) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button(" START RECORDING ", ImVec2(-1, 30))) startRecording();
                        ImGui::PopStyleColor();
                    }
                    else {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Recording... %d frames", recordFrameCount);
                        if (ImGui::Button(" STOP RECORDING ", ImVec2(-1, 30))) stopRecording();
                    }
                    ImGui::SliderInt("Interval", &recordInterval, 1, 10, "Every %d Frm");
                    ImGui::Separator();
                    ImGui::Checkbox("SwapInterval", &SwapInterval);
                    ImGui::EndTabItem();
                }
                // --------------------------------------------------------
                // Tab 2: 设置 (Settings - 物理/网格)
                // --------------------------------------------------------
                if (ImGui::BeginTabItem(" Settings ")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Physics Engine");
                    // 引擎切换
                    int engIdx = (int)engine.engineType;
                    if (ImGui::Combo("Mode", &engIdx, "Acoustic (P-Wave)\0Elastic (P+S Wave)\0")) {
                        engine.engineType = (EngineType)engIdx;
                        engine.initMaterial(); engine.clearWavefield();
                    }

                    // 弹性波特定选项
                    if (engine.engineType == EngineType::ELASTIC) {
                        ImGui::Indent();
                        ImGui::Text("Visualization:");
                        int evMode = (int)engine.elasticVisMode;
                        ImGui::RadioButton("Div(P)", &evMode, 0); ImGui::SameLine();
                        ImGui::RadioButton("Curl(S)", &evMode, 1); ImGui::SameLine();
                        ImGui::RadioButton("Energy", &evMode, 2);
                        engine.elasticVisMode = (ElasticVis)evMode;

                        ImGui::Text("Source Injection:");
                        ImGui::Combo("Type", &engine.sourceInjectionMode, "Explosive (P)\0Shear (S)\0Mixed (P+S)\0");
                        ImGui::Unindent();
                    }

                    ImGui::Separator();
                    ImGui::Text("Simulation Parameters");
                    ImGui::SliderInt("Freq (Hz)", &engine.currentFreq, 5, 300);
                    ImGui::SliderInt("Sim Speed/stepsPerFrame", &stepsPerFrame, 1, 50);
                    ImGui::Separator();
                    ImGui::Checkbox("SwapInterval", &SwapInterval);

                    ImGui::Separator();
                    ImGui::Text("Grid Resize");
                    //            // --- 【新增】网格与参数动态修改 ---
                    /*static int newW = GRID_W;
                    static int newH = GRID_H;*/

                    /*newW = GRID_W;
                    newH = GRID_H;*/
                    static float newDT = DT;
                    static float newDX = DX;
                    static int newDamp = DAMP_WIDTH;

                    // 使用 InputInt 输入框，Slider 可能会在拖动时频繁触发
                    ImGui::InputInt("Grid Width", &newW, 10, 10000);
                    ImGui::InputInt("Grid Height", &newH, 10, 10000);
                    ImGui::InputFloat("Time Step (dt)", &newDT, 0.00001f, 0.01f, "%.6f");
                    ImGui::InputFloat("Grid Step (dx)", &newDX, 0.1f, 100.0f);
                    ImGui::InputInt("Damp Width", &newDamp);

                    // 限制最小尺寸，防止崩溃
                    if (newW < 100) newW = 100;
                    if (newH < 100) newH = 100;

                    if (ImGui::Button(" APPLY RESIZE & RESET ", ImVec2(-1, 40))) {
                        // 1. 更新全局参数
                        //GRID_W=newW;
                        //GRID_H=newH;
                        DT = newDT;
                        DX = newDX;
                        DAMP_WIDTH = newDamp;
                        // 2. 调整引擎内存
                        engine.resizeSimulation(newW, newH);

                        // 3. 调整 OpenGL 纹理大小 (使用之前定义的 fieldTex 和 modelTex ID)
                        resizeGLTextures(fieldTex, modelTex, GRID_W, GRID_H);

                        // 4. 强制标记模型更新，以便将新材质上传到 GPU
                        engine.modelModified = true;
                    }
                    

                    RenderThreadControl();

                    ImGui::EndTabItem();
                }
                // --------------------------------------------------------
                // Tab 3: 场景 (Scenarios - 恢复丢失的参数控制)
                // --------------------------------------------------------
                if (ImGui::BeginTabItem(" Scenarios ")) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Preset Scenes");

                    if (ImGui::Button("Earth Model", ImVec2(-1, 0))) engine.loadScenario(0);
                    if (ImGui::Button("Double Slit", ImVec2(-1, 0))) engine.loadScenario(1);
                    if (ImGui::Button("Waveguide", ImVec2(-1, 0))) engine.loadScenario(3);
                    if (ImGui::Button("Crystal", ImVec2(-1, 0))) engine.loadScenario(2);
                    if (ImGui::Button("Curved Surface", ImVec2(-1, 0))) engine.loadCurvedScenario_Sine(engine);
                    if (ImGui::Button("Two Layers", ImVec2(-1, 0))) engine.loadTwoLayerData(engine);

                    ImGui::Separator();

                    // --- 恢复：散射场景控制 ---
                    ImGui::Text("Random Scattering");
                    static float scatterDens = 0.5f;
                    if (ImGui::Button("Load Scatter", ImVec2(-1, 0))) engine.loadScatteringScenario(engine, scatterDens);
                    ImGui::SliderFloat("Density", &scatterDens, 0.0f, 1.0f);

                    ImGui::Separator();

                    // --- 恢复：多层折射场景控制 ---
                    ImGui::Text("Multi-Layer Model");
                    static int layerCount = 3;
                    static float minSpeed = 1500.0f;
                    static float maxSpeed = 4500.0f;

                    if (ImGui::Button("Load Layers", ImVec2(-1, 0))) engine.loadRefractionScenario(engine, layerCount, minSpeed, maxSpeed);
                    ImGui::SliderInt("Count", &layerCount, 1, 10);
                    ImGui::SliderFloat("Min Vel", &minSpeed, 1000.0f, 3000.0f);
                    ImGui::SliderFloat("Max Vel", &maxSpeed, 3000.0f, 6000.0f);

                    ///////////
                    // 1. 定义状态变量 (如果之前没定义)
                    static bool showSettingsWindow = false;
                    static bool showSuccessPopup = false; // <--- 这里定义缺少的变量

                    if (ImGui::Button("System Settings & Export", ImVec2(-1, 0))) {
                        showSettingsWindow = true;
                    }

                    if (showSettingsWindow) {
                        ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);

                        // 注意：传递 &showSettingsWindow 允许点击右上角 X 关闭
                        if (ImGui::Begin("System Configuration", &showSettingsWindow)) {

                            // ... (这里是你之前的线程设置代码) ...

                            ImGui::Separator();
                            ImGui::Text("Data Export");

                            // 导出按钮
                            if (ImGui::Button("Save Vp & Rho to Disk", ImVec2(-1.0f, 40.0f))) {

                                // 1. 执行导出 (调用上一条回答中的函数)
                                exportModelData(engine, "model_vp.txt", "model_density.txt");

                                // 2. 【关键】触发弹窗标志
                                showSuccessPopup = true;
                            }

                            ImGui::End(); // 结束设置窗口
                        }
                    }
                    // 1. 检查标志位，如果为真，告诉 ImGui 打开弹窗
                    if (showSuccessPopup) {
                        ImGui::OpenPopup("Export Completed"); // ID 必须与下面的 BeginPopupModal 一致
                        showSuccessPopup = false; // 重置标志，防止每帧都重复 Open
                    }

                    // 2. 设置弹窗居中显示
                    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

                    // 3. 渲染模态弹窗 (Modal 表示背景不可点击)
                    if (ImGui::BeginPopupModal("Export Completed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

                        // 弹窗内容
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Success!"); // 绿色文字
                        ImGui::Separator();
                        ImGui::Text("Model data has been saved to:");
                        ImGui::TextDisabled("./model_vp.txt");
                        ImGui::TextDisabled("./model_density.txt");
                        ImGui::Spacing();

                        // 确认按钮
                        // 让按钮居中一点（简单的居中计算）
                        ImGui::SetCursorPosX(winW * 0.5f);
                        if (ImGui::Button("OK", ImVec2(120, 0))) {
                            ImGui::CloseCurrentPopup(); // 点击后关闭弹窗
                        }

                        ImGui::EndPopup();
                    }

                    RenderNativeLoadPanel( engine, fieldTex,  modelTex);

                    ImGui::EndTabItem();
                    }
                    // --------------------------------------------------------
                    // Tab 4: 视觉 (Visuals)
                    // --------------------------------------------------------
                    if (ImGui::BeginTabItem(" Visuals ")) {
                        ImGui::Spacing();
                        const char* modes[] = { "Liquid", "Heatmap", "Model", "COOLWARM", "CUSTOM", "Jet", "Black-White",
                            "Frosted Glass", "Thin-Film", "Porcelain","Clay_Terracotta",
                            "PBR_Gold", "Hologram", "Clay_wetness", "Coolwarm_Metallic" };
                        int modeIdx = (int)currentRenderMode;
                        if (ImGui::Combo("Render Mode", &modeIdx, modes, IM_ARRAYSIZE(modes))) currentRenderMode = (RenderMode)modeIdx;

                        ImGui::SliderFloat("Exposure", &displayGain, 0.0001f, 0.005f, "%.5f");
                        if (ImGui::Button("Reset Exposure")) displayGain = 0.001f;

                        if (currentRenderMode == RenderMode::CUSTOM) {
                            ImGui::Spacing(); ImGui::Text("Custom Palette");
                            ImGui::ColorEdit3("Positive", colPos); ImGui::ColorEdit3("Negative", colNeg); ImGui::ColorEdit3("Zero", colZero);
                        }
                        if (currentRenderMode == RenderMode::Clay_wetness) {
                            ImGui::Spacing(); ImGui::Text("Custom Clay Palette");
                            ImGui::ColorEdit3("ClayBase", colClayBase); ImGui::ColorEdit3("ClayDark", colClayDark);
                            ImGui::ColorEdit3("mossColor", colmossColor); ImGui::SliderFloat("Clay_wetness", &wetness, 0.1f, 1.0f, "%.5f");
                        }
                        ImGui::Spacing(); ImGui::Text("Base groundColor.");
                        ImGui::ColorEdit3("colBaseColor", colBaseColor.data());
                        ImGui::ColorEdit3("colHighColor", colHighColor.data());
                        ImGui::ColorEdit3("colGridColor", colGridColor.data());

                        ImGui::ColorEdit3("AbsorbColor", colAbsorbColor);

                        if (ImGui::Button(" Reset_Color ", ImVec2(btnW, 35)))
                        {
                            colBaseColor = { 0.12f, 0.12f, 0.14f };
                            colHighColor = { 0.12f * scale_fac, 0.12f * scale_fac, 0.14f * scale_fac };
                            colGridColor = { 0.1f, 0.1f, 0.1f };
                        };

                        ImGui::Separator();
                        ImGui::Text("Boundaries");
                        bool bFree = engine.enableFreeSurface;
                        if (ImGui::Checkbox("Free Surface (F)", &bFree)) { engine.toggleFreeSurface(); engine.toggleBoundary(); }

                        bool bAbsorb = (engine.boundaryMode == 0);
                        if (ImGui::Checkbox("Show Damp Layer", &bAbsorb)) engine.toggleBoundary();

                        bool bHide = (engine.boundaryMode == 2);
                        if (ImGui::Checkbox("Hide All", &bHide)) engine.hide_Boundary_damp();

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
                ImGui::End();
            }
            // ============================================================
            // 3. 右上角状态浮窗 (Overlay) - 保持不变
            // ============================================================
            if (show_overlay) {
                DrawStatusOverlay(&show_overlay, GRID_W, GRID_H, cursorX, cursorY, engine);
            }
            // ============================================================
            // 4. 其他窗口 (示波器、阵列记录、截图提示) - 保持原有逻辑
            // ============================================================
            //单点检波器
            if (show_recorder_window) {
                ImGui::SetNextWindowPos(ImVec2(10, 800), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_FirstUseEver);
                ImGui::Begin("Oscilloscope", &show_recorder_window);
                ImGui::SliderInt("History Len", &engine.recorder.maxHistory, 100, 2000);
                if (engine.recorder.history.size() > 0) {
                    //ImGui::PlotLines("##W", engine.recorder.history.data(), (int)engine.recorder.history.size(), 0, NULL, -5000.0f, 5000.0f, ImVec2(-1, 140));
                    ImGui::PlotLines(
                        "##W",
                        ValueGetter,
                        (void*)&engine.recorder.history, // 传入 deque 的指针作为 user_data
                        (int)engine.recorder.history.size(),
                        0,
                        NULL,
                        -5000.0f,
                        5000.0f,
                        ImVec2(-1, 140)
                    );
                
                }
                ImGui::Text("Probe: (%d, %d)", engine.recorder.gx, engine.recorder.gy);
                ImGui::End();
            }
            //检波器序列
            if (show_recorders_window) {
                ImGui::SetNextWindowPos(ImVec2(10, 1100), ImGuiCond_Once);
                ImGui::SetNextWindowSize(ImVec2(520, 500), ImGuiCond_Once);
                ImGui::Begin("Seismic Array Record", &show_recorders_window);
                if (engine.receivers.empty()) {
                    ImGui::Text("No receivers active."); ImGui::Text("Select 'Array Tool', Click & Drag on the map to place receivers.");
                }
                /*else {
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
                    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
                    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 25, 255));
                    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(100, 100, 100, 255));
                    static float waveGain = 1.0f;
                    ImGui::SliderFloat("Gain", &waveGain, 0.1f, 10.0f);
                    float rowHeight = canvas_size.y / engine.receivers.size();
                    float timeStepWidth = canvas_size.x / engine.maxHistoryLen;
                    if (rowHeight < 5.0f) rowHeight = 5.0f;
                    ImGui::InvisibleButton("canvas", canvas_size);

                    for (size_t i = 0; i < engine.receivers.size(); ++i) {
                        const auto& r = engine.receivers[i];
                        if (r.data.empty()) continue;
                        float basePathY = canvas_pos.y + (i + 0.5f) * rowHeight;
                        if (basePathY > canvas_pos.y + canvas_size.y) break;
                        ImColor waveColor = ImColor(0.0f, 1.0f, 0.6f);
                        for (size_t t = 0; t < r.data.size() - 1; ++t) {
                            float x1 = canvas_pos.x + t * timeStepWidth;
                            float y1 = basePathY - r.data[t] * waveGain * 0.05f;
                            float x2 = canvas_pos.x + (t + 1) * timeStepWidth;
                            float y2 = basePathY - r.data[t + 1] * waveGain * 0.05f;
                            if (x1 < canvas_pos.x + canvas_size.x) draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), waveColor, 1.0f);
                        }
                        draw_list->AddLine(ImVec2(canvas_pos.x, basePathY), ImVec2(canvas_pos.x + canvas_size.x, basePathY), IM_COL32(50, 50, 60, 100));
                        char label[16]; sprintf(label, "R%d", (int)i);
                        draw_list->AddText(ImVec2(canvas_pos.x + 2, basePathY - 7), IM_COL32(150, 150, 150, 200), label);
                    }
                }*/
                //vertical
                else {
                    // --- 1. 顶部控件区域 ---
                    // 先画滑块，让它占据顶部的空间
                    static float waveGain = 0.05f;
                    ImGui::SetNextItemWidth(200.0f); // 设置滑块宽度，不要太长
                    ImGui::SliderFloat("Gain", &waveGain, 0.01f, 1.0f);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adjust the amplitude of the seismic traces");
                    ImGui::SameLine();
                    float btnW = (ImGui::GetContentRegionAvail().x - 20) / 2;
                    if (ImGui::Button(" Reset_Gain ", ImVec2(btnW, 35))) { waveGain = 0.05f; };
                    ImGui::Separator(); // 画一条分割线

                    // 1. 定义变量 (使用 static 确保它在函数退出后保存值，或者把它放到你的类成员里)
                    static int show_type = 1;
                    // 2. 添加标题或说明 (可选)
                    ImGui::Spacing();
                    ImGui::Text("Visualization Style:");

                    // 3. 绘制整数滑条
                    // 参数说明: 标签, 变量指针, 最小值, 最大值, 显示格式
                    // 这里假设你有两种模式：0 (普通线), 1 (热力图)
                    ImGui::SliderInt("Mode", &show_type, 0, 1, "Type: %d");
                    // 4. 添加鼠标悬停提示 (强烈建议添加，否则用户不知道 0 和 1 代表什么)
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Mode 0: Wiggle Trace (Black Line)\n"
                            "Mode 1: Heatmap (Red-Blue Color)"
                        );
                    }
                    ImGui::Separator(); // 再画一条分割线，区分下面的绘图区域

                    // --- 2. 画布区域计算 ---
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 canvas_pos = ImGui::GetCursorScreenPos(); // 获取滑块下方的起始位置
                    ImVec2 canvas_size = ImGui::GetContentRegionAvail(); // 获取剩余的全部空间

                    // 限制一下画布最小高度，防止出错
                    if (canvas_size.y < 50.0f) canvas_size.y = 50.0f;

                    // --- 3. 绘制背景和网格 ---
                    // 3.1 背景色
                    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 25, 255));

                    // 3.2 限制绘图区域 (Clip Rect)
                    // 这一步非常重要！确保波形太强时不会画到滑块上面去
                    draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);

                    // 3.3 绘制水平网格线 (时间深度网格)
                    int gridRows = 10; // 将Y轴分为10份
                    for (int r = 1; r < gridRows; ++r) {
                        float y = canvas_pos.y + (canvas_size.y / gridRows) * r;
                        // 画细淡的灰色横线
                        draw_list->AddLine(
                            ImVec2(canvas_pos.x, y),
                            ImVec2(canvas_pos.x + canvas_size.x, y),
                            IM_COL32(60, 60, 70, 80)
                        );
                    }
                    // --- 4. 核心波形绘制逻辑 ---

                    // 定义左右边距 (Padding)
                    float paddingX = 20.0f;

                    // 计算可用宽度
                    float usableWidth = canvas_size.x - (paddingX * 2.0f);

                    // 计算每一道的宽度
                    float colWidth = usableWidth / engine.receivers.size();
                    float timeStepHeight = canvas_size.y / engine.maxHistoryLen;

                    // 占位按钮 (为了让 ImGui 知道这里有东西，支持鼠标滚轮等)
                    ImGui::InvisibleButton("canvas", canvas_size);

                    for (size_t i = 0; i < engine.receivers.size(); ++i) {
                        const auto& r = engine.receivers[i];
                        if (r.data.empty()) continue;

                        // 计算当前道的基准 X 坐标
                        // 公式：画布左边 + 左边距 + (第i个 * 宽度) + 半个宽度(居中)
                        float basePathX = canvas_pos.x + paddingX + (i * colWidth) + (colWidth * 0.5f);

                        ImColor waveColor = ImColor(0.0f, 1.0f, 0.6f);
                        ImColor centerLineColor = IM_COL32(50, 50, 60, 150);

                        // 绘制垂直基准线 (道中心线)
                        draw_list->AddLine(
                            ImVec2(basePathX, canvas_pos.y),
                            ImVec2(basePathX, canvas_pos.y + canvas_size.y),
                            centerLineColor
                        );
                        // 计算起始索引：最旧的数据在 writeHead 处 (因为它马上要被覆盖)
                        // 注意：如果数据还没填满，逻辑稍微不同，但这里假设已经填满或无所谓

                        int startIdx = r.writeHead;
                        int size = (int)r.data.size();
                        //基础版本
                        if (show_type == 0) {
                            for (int i = 0; i < size - 1; ++i) {
                                // 计算真实的物理索引 (环形映射)
                                int idx1 = (startIdx + i) % size;
                                int idx2 = (startIdx + i + 1) % size;

                                // t 代表时间步 (用于画Y轴位置)
                                float t = (float)i;

                                float y1 = canvas_pos.y + t * timeStepHeight;
                                float y2 = canvas_pos.y + (t + 1) * timeStepHeight;

                                // 取数据
                                float val1 = r.data[idx1] * waveGain * 0.15f;
                                float val2 = r.data[idx2] * waveGain * 0.15f;

                                // 简单的钳制，防止画太远
                                float maxDeviation = colWidth * 2.0f; // 允许波形稍微重叠，但不允许飞太远
                                if (val1 > maxDeviation) val1 = maxDeviation;
                                if (val1 < -maxDeviation) val1 = -maxDeviation;
                                if (val2 > maxDeviation) val2 = maxDeviation;
                                if (val2 < -maxDeviation) val2 = -maxDeviation;

                                float x1 = basePathX + val1;
                                float x2 = basePathX + val2;

                                if (y1 < canvas_pos.y + canvas_size.y) {
                                    draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), waveColor, 1.5f); // 线条稍微加粗一点点 (1.5f)
                                }
                            }
                        }

                        else if (show_type == 1) {
                            // --- 修复版：宽过渡带 平滑色标 ---
                            auto GetSmoothColor = [](float val, float maxVal) -> ImU32 {
                                // 1. 归一化
                                float t = val / maxVal;

                                // 2. 移除之前的 pow(0.7) 增强，改回线性或者更平缓的曲线
                                // 钳制范围 [-1, 1]
                                if (t > 1.0f) t = 1.0f;
                                if (t < -1.0f) t = -1.0f;

                                // 3. 定义颜色节点
                                // 既然背景是深色(20,20,25)，中心颜色设为深灰色，能让线条一直可见，且过渡更柔和
                                ImVec4 colCenter(0.4f, 0.4f, 0.45f, 1.0f); // 中性灰 (静默)
                                ImVec4 colPos(1.0f, 0.2f, 0.2f, 1.0f);  // 正值: 柔和红
                                ImVec4 colNeg(0.2f, 0.6f, 1.0f, 1.0f);  // 负值: 柔和蓝
                                ImVec4 result;
                                if (t > 0) {
                                    // 正值区间：从 灰 -> 红
                                    // 简单的线性插值 (Linear Interpolation) 保证过渡最宽
                                    result.x = colCenter.x + (colPos.x - colCenter.x) * t;
                                    result.y = colCenter.y + (colPos.y - colCenter.y) * t;
                                    result.z = colCenter.z + (colPos.z - colCenter.z) * t;
                                    result.w = 1.0f;
                                }
                                else {
                                    // 负值区间：从 灰 -> 蓝
                                    t = -t; // 取绝对值
                                    result.x = colCenter.x + (colNeg.x - colCenter.x) * t;
                                    result.y = colCenter.y + (colNeg.y - colCenter.y) * t;
                                    result.z = colCenter.z + (colNeg.z - colCenter.z) * t;
                                    result.w = 1.0f;
                                }

                                return ImGui::ColorConvertFloat4ToU32(result);
                                };

                            // --- 关键修改：扩大动态范围 ---
                            // 之前是 5.0f，现在改为 20.0f 或更大。
                            // 值越大，颜色从灰色变成纯红/纯蓝所需的振幅就越大，过渡带就越宽。
                            float colorMaxAmp = 20.0f / waveGain;

                            for (int i = 0; i < size - 1; ++i) {
                                // ... (索引计算保持不变) ...
                                int idx1 = (startIdx + i) % size;
                                int idx2 = (startIdx + i + 1) % size;
                                float t = (float)i;

                                float y1 = canvas_pos.y + t * timeStepHeight;
                                float y2 = canvas_pos.y + (t + 1) * timeStepHeight;

                                // ... (振幅钳制保持不变) ...
                                float val1 = r.data[idx1] * waveGain * 0.15f;
                                float val2 = r.data[idx2] * waveGain * 0.15f;

                                // ... (x1, x2 计算保持不变) ...
                                float x1 = basePathX + val1;
                                float x2 = basePathX + val2;

                                if (y1 < canvas_pos.y + canvas_size.y) {
                                    // 使用线段中点的值来计算颜色，这样渐变比取最大值更细腻
                                    float avgV = (r.data[idx1] + r.data[idx2]) * 0.5f;

                                    ImU32 segColor = GetSmoothColor(avgV, colorMaxAmp);
                                    draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), segColor, 2.0f);
                                }
                            }
                        }

                        // 绘制标签 (放在顶部)
                        char label[16]; sprintf(label, "%d", (int)i);
                        // 简单居中文字
                        float textWidth = ImGui::CalcTextSize(label).x;
                        draw_list->AddText(ImVec2(basePathX - textWidth * 0.5f, canvas_pos.y + 5), IM_COL32(200, 200, 200, 255), label);
                    }
                    // 绘制外边框
                    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(100, 100, 100, 255));
                    // 结束裁剪
                    draw_list->PopClipRect();
                }
                ImGui::End();

                float scaleX = (float)vp.w / GRID_W;
                float scaleY = (float)vp.h / GRID_H;
                float screenOriginX = vp.screenX;
                float screenOriginY = vp.screenY;
                // 注意：process_input 里 vp.screenY 是反的(相对于底部的)，但ImGui绘图是相对于顶部的
                // 正确的 ImGui Y = 窗口高度 - (glY + glH) + (GRID_H - gy)*scaleY ... 有点复杂
                // 最简单的方法：利用TexCoord映射。
                // OpenGL渲染时: TexCoord (0,0) 在左下角(物理内存开头)，但Shader里做了 TexCoord.y = 1.0 - TexCoord.y 翻转。
                // 我们的 grid (0,0) 是内存开头 (top-left in standard image processing, but bottom-left in physics usually).
                // 让我们看代码: int idx = y * GRID_W + x; 
                // process_input 中: relY = my - vp.screenY; normY = relY / vp.h;
                // 所以 grid (0,0) 对应屏幕顶部。

                // 修正 ImGui 绘图的 Origin
                ImVec2 glScreenPos(vp.x, vp.y); // 这是 OpenGL Viewport 左上角在窗口中的位置

                ImDrawList* fg_list = ImGui::GetForegroundDrawList();

                // 定义样式参数
                const float geoSize = 12.0f; // 检波器图标大小
                const ImU32 colCable = IM_COL32(200, 200, 200, 180); // 线缆颜色
                const ImU32 colGeoFill = IM_COL32(255, 140, 0, 255); // 检波器填充 (深橙色)
                const ImU32 colGeoBorder = IM_COL32(0, 0, 0, 255);   // 检波器描边 (黑色)
                const ImU32 colPreview = IM_COL32(255, 255, 0, 150); // 预览颜色

                // 1. 绘制已放置的检波器 (倒三角形 + 线缆)
                if (!engine.receivers.empty()) {
                    // A. 先画线缆 (连接所有检波器)
                    // 为了性能，我们不画几百条线段，而是简单地画一条贯穿首尾的线（假设是直线阵列）
                    // 如果阵列是弯曲的，则需要循环画线。这里假设是直线工具生成的：
                    if (engine.receivers.size() > 1) {
                        const auto& rFirst = engine.receivers.front();
                        const auto& rLast = engine.receivers.back();
                        ImVec2 p1(glScreenPos.x + rFirst.gx * scaleX, glScreenPos.y + rFirst.gy * scaleY);
                        ImVec2 p2(glScreenPos.x + rLast.gx * scaleX, glScreenPos.y + rLast.gy * scaleY);
                        fg_list->AddLine(p1, p2, colCable, 2.0f);
                    }

                    // B. 再画检波器图标 (倒三角)
                    for (size_t i = 0; i < engine.receivers.size(); ++i) {
                        const auto& r = engine.receivers[i];
                        float px = glScreenPos.x + r.gx * scaleX;
                        float py = glScreenPos.y + r.gy * scaleY;

                        // 倒三角形顶点计算
                        ImVec2 pTopLeft(px - geoSize * 0.7f, py - geoSize * 0.7f);
                        ImVec2 pTopRight(px + geoSize * 0.7f, py - geoSize * 0.7f);
                        ImVec2 pBottom(px, py + geoSize);

                        fg_list->AddTriangleFilled(pTopLeft, pTopRight, pBottom, colGeoFill);
                        fg_list->AddTriangle(pTopLeft, pTopRight, pBottom, colGeoBorder);

                        // 可选：绘制首尾编号
                        if (i == 0 || i == engine.receivers.size() - 1) {
                            char idText[8]; sprintf(idText, "%d", (int)i);
                            fg_list->AddText(ImVec2(px + 8, py - 8), IM_COL32(255, 255, 255, 200), idText);
                        }
                    }
                }

                // 2. 绘制拖拽过程中的预览 (虚影)
                if (isDraggingArray && currentTool == ToolMode::PLACE_ARRAY) {
                    float startPx = glScreenPos.x + dragStartX * scaleX;
                    float startPy = glScreenPos.y + dragStartY * scaleY;
                    float endPx = glScreenPos.x + cursorX * GRID_W * scaleX;
                    float endPy = glScreenPos.y + cursorY * GRID_H * scaleY;

                    // 绘制预览中轴线
                    fg_list->AddLine(ImVec2(startPx, startPy), ImVec2(endPx, endPy), colPreview, 1.0f);

                    // 显示总数标签
                    char countLabel[32];
                    sprintf(countLabel, "Count: %d", arrayReceiverCount);
                    fg_list->AddText(ImVec2(endPx + 10, endPy + 10), IM_COL32(255, 255, 0, 255), countLabel);

                    // 绘制预览点
                    int count = arrayReceiverCount;
                    for (int i = 0; i < count; ++i) {
                        float t = (count == 1) ? 0.0f : (float)i / (count - 1);
                        float tx = startPx + (endPx - startPx) * t;
                        float ty = startPy + (endPy - startPy) * t;

                        // 预览用简单的圆形或半透明三角
                        fg_list->AddCircleFilled(ImVec2(tx, ty), 3.0f, colPreview);
                    }


                    // 绘制起点和终点的十字准星，辅助对齐
                    fg_list->AddLine(ImVec2(startPx - 5, startPy), ImVec2(startPx + 5, startPy), colPreview);
                    fg_list->AddLine(ImVec2(startPx, startPy - 5), ImVec2(startPx, startPy + 5), colPreview);
                    fg_list->AddLine(ImVec2(endPx - 5, endPy), ImVec2(endPx + 5, endPy), colPreview);
                    fg_list->AddLine(ImVec2(endPx, endPy - 5), ImVec2(endPx, endPy + 5), colPreview);
                }
                // ==========================================
                // 新增: 3. 绘制波场刻度尺 (Rulers)
                // ==========================================
                {
                    const int rulerStep = 50;       // 刻度间隔
                    const float tickLen = 6.0f;     // 刻度线长度
                    const float textOffset = 15.0f; // 文字偏移
                    const ImU32 colRuler = IM_COL32(220, 220, 220, 180);

                    // --- A. 顶部 X 轴刻度 (移至网格 Y=50 处) ---
                    // 计算网格 Y=50 对应的屏幕垂直坐标
                    float topRulerY = glScreenPos.y + 50.0f * scaleY;

                    for (int x = 0; x <= GRID_W; x += rulerStep) {
                        float px = glScreenPos.x + x * scaleX;
                        float py = topRulerY;

                        // 1. 绘制刻度短线 (向上伸出)
                        fg_list->AddLine(ImVec2(px, py), ImVec2(px, py - tickLen), colRuler, 1.5f);

                        // 2. 绘制数字 (显示在 Y=50 线条的上方)
                        char num[16]; sprintf(num, "%d", x);
                        float txtW = ImGui::CalcTextSize(num).x;
                        fg_list->AddText(ImVec2(px - txtW * 0.5f, py - tickLen - textOffset), colRuler, num);
                    }

                    // 绘制一条横贯的基准线 (在 Y=50 处)
                    fg_list->AddLine(ImVec2(glScreenPos.x, topRulerY), ImVec2(glScreenPos.x + vp.w, topRulerY), colRuler);


                    // --- B. 左侧 Y 轴刻度 (保持在最左侧) ---
                    for (int y = 0; y <= GRID_H; y += rulerStep) {
                        float px = glScreenPos.x;
                        float py = glScreenPos.y + y * scaleY;

                        // 1. 绘制刻度短线
                        fg_list->AddLine(ImVec2(px, py), ImVec2(px - tickLen, py), colRuler, 1.5f);

                        // 2. 绘制数字
                        char num[16]; sprintf(num, "%d", y);
                        float txtW = ImGui::CalcTextSize(num).x;
                        float txtH = ImGui::CalcTextSize(num).y;
                        fg_list->AddText(ImVec2(px - tickLen - txtW - 4.0f, py - txtH * 0.5f), colRuler, num);
                    }

                    // --- C. 绘制左边框线 (顶边框已经由上面的基准线代替了) ---
                    fg_list->AddLine(glScreenPos, ImVec2(glScreenPos.x, glScreenPos.y + vp.h), colRuler);
                }
            }
            // 截图提示框
            if (glfwGetTime() - saveMsgStartTime < SAVE_MSG_DURATION) {
                ImGui::SetNextWindowSizeConstraints(ImVec2(500.0f, 0.0f), ImVec2(500.0f, 1000.0f));
                ImGui::SetNextWindowPos(ImVec2(winW * 0.5f, 100.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
                ImGui::SetNextWindowBgAlpha(0.8f);
                if (ImGui::Begin("SaveToast", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[SUCCESS]"); ImGui::SameLine(); ImGui::Text("Screenshot Saved!");
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Path:"); ImGui::SameLine(); ImGui::TextWrapped("%s", lastSavedPath.c_str());
                }
                ImGui::End();
            }
        }
    }
    
// ==========================================
// 6. 主程序入口
// ==========================================
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, "Acoustic Wave Pro (Ultimate)", NULL, NULL);
    if (!window) return -1;

    GLFWimage images[1];
    std::string image_icon = "../resources/images/icon_main.png";
    images[0].pixels = stbi_load(image_icon.c_str(), &images[0].width, &images[0].height, 0, 4);
    if (images[0].pixels) { glfwSetWindowIcon(window, 1, images); stbi_image_free(images[0].pixels); }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    glewInit();
    glfwSwapInterval(0);
    switchToEnglishInput();
    //------------------------------imgui初始化----------------------------------
    Init_Imgui(window);
    ImGuiIO& io = ImGui::GetIO();
    glfwSetKeyCallback(window, key_callback);
    // GL 初始化
    float verts[] = { -1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1 };
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO); glBindVertexArray(VAO);
    glGenBuffers(1, &VBO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);

    std::string vsSourceString = loadShaderFromFile("../resources/shader/vs_code.vert");
    std::string fsSourceString = loadShaderFromFile("../resources/shader/fs_code.frag");
    GLuint vs = createShader(vsSourceString.c_str(), GL_VERTEX_SHADER);
    GLuint fs = createShader(fsSourceString.c_str(), GL_FRAGMENT_SHADER);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLuint fieldTex, modelTex;
    glGenTextures(1, &fieldTex); glBindTexture(GL_TEXTURE_2D, fieldTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, GRID_W, GRID_H, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &modelTex); glBindTexture(GL_TEXTURE_2D, modelTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, GRID_W, GRID_H, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    engine.prepareGPUData();
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, modelTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_W, GRID_H, GL_RED, GL_FLOAT, engine.gpu_model.data());

    //手册输出
    Manual_print();
    //初始化线程数
    InitOpenMPRobust(2);

    auto lastT = std::chrono::high_resolution_clock::now();
    int frames = 0;
    //main loop
    while (!glfwWindowShouldClose(window)) {
        glfwSwapInterval(SwapInterval);
        glfwPollEvents();
        process_input(window);

        // 物理模拟
        bool performStep = !engine.paused;
        if (singleStepTrigger) { performStep = true; singleStepTrigger = false; }
        if (performStep && !engine.exploded) {
            for (int i = 0; i < stepsPerFrame; ++i) engine.step();
            engine.updateReceivers();
            engine.recorder.update(engine.p, GRID_W);
            // 数据上传,
            engine.prepareGPUData();
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fieldTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_W, GRID_H, GL_RED, GL_FLOAT, engine.gpu_field.data());

        }
        if (engine.modelModified) {
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, modelTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_W, GRID_H, GL_RED, GL_FLOAT, engine.gpu_model.data());
            engine.modelModified = false;
        }

        // 渲染流程
        int winW, winH; glfwGetWindowSize(window, &winW, &winH);
        ViewportInfo vp = getViewport(winW, winH);

        glViewport(0, 0, winW, winH);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(vp.x, vp.y, vp.w, vp.h);

        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "bgTex"), 2);
        glUniform1f(glGetUniformLocation(prog, "gain"), displayGain);
        glUniform1i(glGetUniformLocation(prog, "mode"), (int)currentRenderMode);
        glUniform2f(glGetUniformLocation(prog, "resolution"), (float)vp.w, (float)vp.h);
        glUniform2f(glGetUniformLocation(prog, "gridSize"), (float)GRID_W, (float)GRID_H);
        glUniform1i(glGetUniformLocation(prog, "dampWidth"), DAMP_WIDTH);
        glUniform1i(glGetUniformLocation(prog, "boundaryMode"), engine.boundaryMode);
        glUniform1i(glGetUniformLocation(prog, "freeSurface"), engine.enableFreeSurface ? 1 : 0);
        glUniform1f(glGetUniformLocation(prog, "time"), (float)glfwGetTime());
        glUniform1i(glGetUniformLocation(prog, "fieldTex"), 0);
        glUniform1i(glGetUniformLocation(prog, "modelTex"), 1);

        glUniform2f(glGetUniformLocation(prog, "mousePos"), cursorX, cursorY);
        float scaledBrush = brushRadiusPx * ((float)vp.w / INITIAL_WINDOW_WIDTH);
        glUniform1f(glGetUniformLocation(prog, "brushRadius"), scaledBrush);
        glUniform1i(glGetUniformLocation(prog, "toolMode"), (int)currentTool);
        glUniform1i(glGetUniformLocation(prog, "isLineSource"), engine.useLineSource ? 1 : 0);
        glUniform2f(glGetUniformLocation(prog, "probePos"), (float)engine.recorder.gx / GRID_W, (float)engine.recorder.gy / GRID_H);

        glUniform3f(glGetUniformLocation(prog, "colValPos"), colPos[0], colPos[1], colPos[2]);
        glUniform3f(glGetUniformLocation(prog, "colValNeg"), colNeg[0], colNeg[1], colNeg[2]);
        glUniform3f(glGetUniformLocation(prog, "colValZero"), colZero[0], colZero[1], colZero[2]);
        glUniform1f(glGetUniformLocation(prog, "wetness"), wetness);
        glUniform3f(glGetUniformLocation(prog, "colClayBase"), colClayBase[0], colClayBase[1], colClayBase[2]);
        glUniform3f(glGetUniformLocation(prog, "colClayDark"), colClayDark[0], colClayDark[1], colClayDark[2]);
        glUniform3f(glGetUniformLocation(prog, "colmossColor"), colmossColor[0], colmossColor[1], colmossColor[2]);
        
        glUniform3f(glGetUniformLocation(prog, "colAbsorbColor"), colAbsorbColor[0], colAbsorbColor[1], colAbsorbColor[2]);

        glUniform3f(glGetUniformLocation(prog, "colBaseColor"), colBaseColor[0], colBaseColor[1], colBaseColor[2]);
        glUniform3f(glGetUniformLocation(prog, "colHighColor"), colHighColor[0], colHighColor[1], colHighColor[2]);
        glUniform3f(glGetUniformLocation(prog, "colGridColor"), colGridColor[0], colGridColor[1], colGridColor[2]);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        //------------------------------imgui_render----------------------------------
        //Render_gui绘制步骤
        Render_Imgui(window, frames,fieldTex, modelTex);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        frames++;
        auto now = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<double>(now - lastT).count() >= 1.0) {
            std::string t = "Wave Pro | FPS: " + std::to_string(frames);
            glfwSetWindowTitle(window, t.c_str());
            frames = 0; lastT = now;
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    switchToChineseInput();

    return 0;
 }