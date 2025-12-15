#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <list>
#include <omp.h> 
#include <chrono>
#include <string>
#include <cstring>
#include <iomanip>

// --- ImGui Headers ---
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define NOMINMAX  // 取消windows.h中max和min宏定义
#include <windows.h>

// ==========================================
// 1. 核心参数 
// ==========================================
using Real = float;

const int INITIAL_WINDOW_WIDTH = 1200;
const int INITIAL_WINDOW_HEIGHT = 1200;
const int GRID_W = 600;
const int GRID_H = 600;
const Real DX = 2.0f;
const Real DT = 0.0001f;
const int DAMP_WIDTH = 50;
const Real PI = 3.1415926535f;


bool show_recorder_window = false;
bool show_recorders_window = false;
bool SwapInterval = true;

std::string exe_dir;

// 视口信息
struct ViewportInfo {
    int x, y, w, h;
    int screenX, screenY;
    float scaleX, scaleY;
};

// 1. 修改 ToolMode 枚举，添加 PLACE_ARRAY
enum class ToolMode {
    TRIGGER_SOURCE = 0,
    DRAW_HIGH_VEL = 1,
    DRAW_LOW_VEL = 2,
    DRAW_WALL = 3,
    ERASE = 4,
    PLACE_ARRAY = 5 // <--- 新增
};

// 2. 全局变量区域新增
int arrayReceiverCount = 20; // 阵列中检波器的数量
bool isDraggingArray = false;
int dragStartX, dragStartY;  // 拖拽起始点(网格坐标)

enum class RenderMode {
    LIQUID = 0,
    HEATMAP = 1,
    MODEL = 2,
    COOLWARM = 3,
    CUSTOM = 4
};

// ==========================================
// 2. Shader 
// ==========================================
const char* VS_CODE = R"(
#version 330 core
layout (location=0) in vec2 aPos;
layout (location=1) in vec2 aTex;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTex; 
    TexCoord.y = 1.0 - TexCoord.y;
}
)";

const char* FS_CODE = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D fieldTex;
uniform sampler2D modelTex;
uniform float gain;
uniform int mode; 
uniform vec2 resolution;
uniform vec2 gridSize;   
uniform int dampWidth;
uniform int boundaryMode; 
uniform int freeSurface;
uniform float time;

uniform vec2 mousePos;     
uniform float brushRadius; 
uniform int toolMode;      
uniform int isLineSource;  
uniform vec2 probePos; 

uniform vec3 colValPos;  
uniform vec3 colValNeg;  
uniform vec3 colValZero; 

float grid(vec2 uv, float spacing, float thick) {
    vec2 gridPos = uv * resolution;
    vec2 deriv = max(vec2(fwidth(gridPos.x), fwidth(gridPos.y)), vec2(0.001));
    vec2 range = abs(fract(gridPos / spacing - 0.5) - 0.5);
    vec2 line = 1.0 - smoothstep(0.0, deriv.x / spacing * thick * 2.0, range); 
    return max(line.x, line.y);
}

float techStripe(vec2 uv) {
    vec2 px = uv * resolution;
    float stripe = mod(px.x + px.y + time * 20.0, 30.0); 
    return step(stripe, 2.0); 
}

vec3 paletteHeat(float v) {
    vec3 colLow = vec3(0.0, 0.1, 0.4); 
    vec3 colMid = vec3(0.0, 0.0, 0.0); 
    vec3 colHigh = vec3(1.0, 0.4, 0.0); 
    float t = clamp(v, -1.0, 1.0);
    return mix(colMid, (t<0.0?colLow:colHigh), abs(t));
}

vec3 getCoolWarmColor(float t) {
    vec3 colorBlue = vec3(0.23, 0.29, 0.75); 
    vec3 colorWhite = vec3(0.95, 0.95, 0.95); 
    vec3 colorRed = vec3(0.70, 0.01, 0.15);   
    vec3 res = colorWhite;
    if (t < 0.0) res = mix(colorWhite, colorBlue, -t);
    else res = mix(colorWhite, colorRed, t);
    return res;
}

void main() {
    vec2 step = 1.0 / resolution;
    float p = texture(fieldTex, TexCoord).r;
    float vp = texture(modelTex, TexCoord).r;
    float v_norm = clamp((vp - 1500.0) / 3500.0, 0.0, 1.0);
    
    float p_l = texture(fieldTex, TexCoord - vec2(step.x*1.5, 0.0)).r;
    float p_r = texture(fieldTex, TexCoord + vec2(step.x*1.5, 0.0)).r;
    float p_u = texture(fieldTex, TexCoord - vec2(0.0, step.y*1.5)).r;
    float p_d = texture(fieldTex, TexCoord + vec2(0.0, step.y*1.5)).r;
    vec2 grad = vec2(p_r - p_l, p_d - p_u) * gain * 10.0; 
    float gradLen = length(grad);

    vec3 bgBase = vec3(0.12, 0.12, 0.14); 
    vec3 bgHigh = vec3(0.20, 0.20, 0.23); 
    vec3 bgLow  = vec3(0.08, 0.08, 0.10); 
    vec3 bgWall = vec3(0.50, 0.52, 0.55); 

    vec3 bg = bgBase;
    bool isWall = vp < 10.0;
    if (isWall) bg = bgWall; 
    else bg = mix(bgBase, bgHigh, v_norm);

    if (!isWall) {
        float minorGrid = grid(TexCoord, 50.0, 0.5);
        bg += vec3(minorGrid * 0.04);
    } else {
        float s = techStripe(TexCoord);
        bg = mix(bg, bg * 1.2, s * 0.2); 
    }
    
    vec3 color = bg;
    float visualGain = gain; 

    // === Mode 0: Liquid 3D ===
    if (mode == 0) { 
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.8)); 
        vec3 lightDir = normalize(vec3(-0.5, 0.5, 1.0)); 
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 40.0);
        float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
        float shadow = 1.0 - smoothstep(0.2, 2.0, gradLen); 
        float abb = gradLen * 0.005;
        float r_val = texture(fieldTex, TexCoord + vec2(abb, 0.0)).r;
        float b_val = texture(fieldTex, TexCoord - vec2(abb, 0.0)).r;
        vec3 waveBase = (p > 0.0) ? vec3(1.0, 0.7, 0.2) : vec3(0.1, 0.5, 1.0);         
        float waveMask = smoothstep(0.02, 0.2, abs(p * visualGain));
        vec3 liquidColor = bg * shadow;
        liquidColor = mix(liquidColor, liquidColor * 0.6, fresnel); 
        liquidColor += vec3(1.0) * spec * 1.2 * waveMask; 
        liquidColor = mix(liquidColor, waveBase, waveMask * 0.35); 
        float shock = smoothstep(0.5, 1.2, abs(p * visualGain));
        liquidColor += vec3(r_val*2.0, 0.0, b_val*2.0) * visualGain * shock * 1.5;
        color = liquidColor;
    }
    // === Mode 1: Heatmap ===
    else if (mode == 1) { 
        float val = tanh(p * visualGain); 
        vec3 wCol = paletteHeat(val);
        float opacity = smoothstep(0.02, 0.2, abs(val)); 
        color = mix(bg, wCol, opacity); 
    }
    // === Mode 2: Model ===
    else if (mode == 2) { 
        color = bg;
    }
    // === Mode 3: Coolwarm ===
    else if (mode == 3) {
        float val = clamp(p * visualGain * 1.5, -1.0, 1.0);
        vec3 cwColor = getCoolWarmColor(val);
        vec3 light = normalize(vec3(-1.0, 1.0, 0.5));
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 1.0)); 
        float shade = dot(normal, light);
        color = cwColor * (0.8 + 0.4 * shade); 
        float opacity = smoothstep(0.05, 0.2, abs(val));
        color = mix(bg, color, opacity);
    }
    // === Mode 4: Custom ===
    else if (mode == 4) {
        float val = clamp(p * visualGain * 2.0, -1.0, 1.0);
        vec3 c = colValZero;
        if (val > 0.0) c = mix(colValZero, colValPos, val);
        else c = mix(colValZero, colValNeg, -val);
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 1.0)); 
        float shade = dot(normal, normalize(vec3(-1.0, 1.0, 0.5)));
        color = c * (0.7 + 0.5 * shade);
        float opacity = smoothstep(0.02, 0.1, abs(val));
        color = mix(bg, color, opacity);
    }
    
    // --- 边界 & UI ---
    vec2 gridPos = TexCoord * gridSize; 
    float dw = float(dampWidth);
    float dx = min(gridPos.x, gridSize.x - gridPos.x);
    float dy = min(gridPos.y, gridSize.y - gridPos.y);
    float dist = min(dx, dy);

    if (boundaryMode == 0 && dist < dw) { 
        // [修复] 调亮边界：更浅的遮罩
        color = mix(color, vec3(0.2, 0.25, 0.3), 0.4); 
        
        // [修复] 条纹高亮
        float s = techStripe(TexCoord);
        color += vec3(0.1, 0.4, 0.5) * s * 0.5; // 加法混合，产生发光感
        
        // 边界线
        if (abs(dist - dw) < 1.0) color = mix(color, vec3(0.0, 0.9, 1.0), 0.9); 
    }
    
    if (freeSurface == 1 && gridPos.y < 3.0) color = vec3(1.2); 

    vec2 pxPos = TexCoord * resolution;
    vec2 probePx = probePos * resolution;
    float dProbe = distance(pxPos, probePx);
    float probeRing = smoothstep(1.5, 0.0, abs(dProbe - 8.0));
    float probeDot = smoothstep(3.0, 0.0, dProbe);
    color = mix(color, vec3(0.0, 1.0, 0.5), max(probeRing, probeDot * 0.8));

    if (mousePos.x >= 0.0) { 
        vec2 mousePx = mousePos * resolution;
        float dMouse = distance(pxPos, mousePx);
        vec3 cursorCol = vec3(1.0);
        if (toolMode == 0) cursorCol = vec3(1.0, 0.5, 0.0); 
        else if (toolMode == 1) cursorCol = vec3(1.0, 0.0, 0.0); 
        else if (toolMode == 2) cursorCol = vec3(0.0, 0.0, 1.0); 
        else if (toolMode == 3) cursorCol = vec3(0.8, 0.8, 0.8); 
        
        float ring = smoothstep(1.5, 0.0, abs(dMouse - brushRadius));
        color = mix(color, cursorCol, ring);
        
        if (toolMode == 0 && isLineSource == 1) {
            if (abs(pxPos.y - mousePx.y) < 1.0 && abs(pxPos.x - mousePx.x) < 200.0) {
                 color = mix(color, cursorCol, 0.6 * (1.0 - abs(pxPos.x - mousePx.x)/200.0));
            }
        }
        if (brushRadius < 10.0 && dMouse < 4.0) color = mix(color, cursorCol, 0.8);
    }

    FragColor = vec4(color, 1.0);
}
)";

GLuint createShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint success; glGetShaderiv(s, GL_COMPILE_STATUS, &success);
    if (!success) { char l[512]; glGetShaderInfoLog(s, 512, NULL, l); std::cerr << l << std::endl; exit(-1); }
    return s;
}

// 物理引擎核心
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

class AcousticEngine {
public:
    std::vector<Real> p, vx, vz;
    std::vector<Real> coef_v, coef_p, damp_coef;
    std::vector<Real> vp_data;
    std::vector<Real> gpu_field, gpu_model;
    std::list<RickerSource> sources;

    bool modelModified = true;
    bool exploded = false;
    bool paused = false;
    bool useLineSource = false;
    int boundaryMode = 0;
    bool enableFreeSurface = false;

    Real currentFreq = 35.0f;

    struct Recorder {
        int gx, gy;
        std::vector<float> history;
        int maxHistory = 1200;
        void update(const std::vector<Real>& field, int width) {
            if (gx < 0 || gx >= width) return;
            int idx = gy * width + gx;
            if (idx >= 0 && idx < field.size()) {
                float val = field[idx];
                history.push_back(val);
                if (history.size() > maxHistory) history.erase(history.begin());
            }
        }
    } recorder;

    struct Receiver {
        int gx, gy;          // 网格坐标
        std::vector<float> data; // 波形历史

        Receiver(int x, int y, int capacity) : gx(x), gy(y) {
            data.reserve(capacity);
        }
    };

    // --- 新增: 检波器阵列 ---
    std::vector<Receiver> receivers;
    int maxHistoryLen = 1000;

    // 用于创建阵列的工具函数
    void placeArray(int x1, int y1, int x2, int y2, int count) {
        receivers.clear(); // 清除旧的
        if (count < 1) count = 1;
        for (int i = 0; i < count; ++i) {
            float t = (count == 1) ? 0.0f : (float)i / (count - 1);
            int cx = (int)(x1 + (x2 - x1) * t);
            int cy = (int)(y1 + (y2 - y1) * t);

            // 边界检查
            cx = std::max(2, std::min(GRID_W - 3, cx));
            cy = std::max(2, std::min(GRID_H - 3, cy));

            receivers.emplace_back(cx, cy, maxHistoryLen);
        }
    }

    // 更新所有检波器数据
    void updateReceivers() {
        for (auto& r : receivers) {
            int idx = r.gy * GRID_W + r.gx;
            float val = p[idx];
            r.data.push_back(val);
            if (r.data.size() > maxHistoryLen) {
                r.data.erase(r.data.begin()); // 保持滚动
            }
        }
    }

    void clearReceiversData() {
        for (auto& r : receivers) r.data.clear();
    }

    

    AcousticEngine() {
        size_t size = GRID_W * GRID_H;
        p.resize(size, 0); vx.resize(size, 0); vz.resize(size, 0);
        coef_v.resize(size); coef_p.resize(size); damp_coef.resize(size);
        vp_data.resize(size);
        gpu_field.resize(size); gpu_model.resize(size);

        initMaterial();
        initDamping();

        recorder.gx = GRID_W / 2;
        recorder.gy = GRID_H / 2;
        recorder.maxHistory = 1000;
    }

    void initMaterial() {
#pragma omp parallel for
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                int idx = y * GRID_W + x;
                Real Vp = 2000.0f;
                Real Rho = 2000.0f;
                setParams(idx, Vp, Rho);
            }
        }
        modelModified = true;
    }

    void setParams(int idx, Real Vp, Real Rho) {
        vp_data[idx] = Vp;
        if (Vp < 10.0f) { coef_v[idx] = 0.0f; coef_p[idx] = 0.0f; }
        else { coef_v[idx] = DT * (1.0f / Rho); coef_p[idx] = DT * (Rho * Vp * Vp); }
    }

    void initDamping() {
        const Real DAMP_STRENGTH = 0.025f;
#pragma omp parallel for
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                int idx = y * GRID_W + x;
                Real damping = 1.0f;
                if (boundaryMode == 0) { // Absorbing
                    int dx = std::min(x, GRID_W - 1 - x);
                    int dy = std::min(y, GRID_H - 1 - y);
                    int min_dist = std::min(dx, dy);
                    if (min_dist < DAMP_WIDTH) {
                        Real r = (Real)(DAMP_WIDTH - min_dist) / DAMP_WIDTH;
                        damping = std::exp(-DAMP_STRENGTH * r * r * 10.0f);
                    }
                }
                damp_coef[idx] = damping;
            }
        }
    }

    void toggleBoundary() { boundaryMode = !boundaryMode; initDamping(); }
    void toggleFreeSurface() { enableFreeSurface = !enableFreeSurface; }

    void addSource(int x, int y) {
        if (exploded) return;
        float amp = 40000.0f;
        if (useLineSource) {
            int startY = (boundaryMode == 0) ? DAMP_WIDTH + 5 : 5;
            int startX = (boundaryMode == 0) ? DAMP_WIDTH + 5 : 5;
            int endY = (boundaryMode == 0) ? GRID_H - DAMP_WIDTH - 5 : GRID_H - 5;
            int endX = (boundaryMode == 0) ? GRID_H - DAMP_WIDTH - 5 : GRID_H - 5;
			//horizontal line source
            for (int iy = startY; iy < endY; ++iy) if (iy % 2 == 0) sources.emplace_back(x, iy, currentFreq, amp * 0.4f);
			//vectical line source
            //for (int ix = startX; ix < endX; ++ix) if (ix % 2 == 0) sources.emplace_back(ix,y, currentFreq, amp * 0.4f);
        }
        else {
            sources.emplace_back(x, y, currentFreq, amp);
        }
    }

    // 修改: clearWavefield 中也要清空接收器数据
    void clearWavefield() {
        std::fill(p.begin(), p.end(), 0.0f);
        std::fill(vx.begin(), vx.end(), 0.0f);
        std::fill(vz.begin(), vz.end(), 0.0f);
        sources.clear(); exploded = false;
        clearReceiversData(); // 新增
    }

    // [修复] 步进函数：移除内部的 paused 检查，由外部循环控制
    void step() {
        if (exploded) return; // 只保留爆炸检查

        Real* __restrict d_p = p.data();
        Real* __restrict d_vx = vx.data();
        Real* __restrict d_vz = vz.data();
        const Real* __restrict d_cv = coef_v.data();
        const Real* __restrict d_cp = coef_p.data();
        const Real* __restrict d_damp = damp_coef.data();

        // 4th Order Coeffs
        const Real C1 = 9.0f / 8.0f;
        const Real C2 = -1.0f / 24.0f;

        // 1. Update V
#pragma omp parallel for
        for (int y = 2; y < GRID_H - 2; ++y) {
            int row = y * GRID_W;
            int row_u1 = (y - 1) * GRID_W;
            int row_d1 = (y + 1) * GRID_W;
            int row_u2 = (y - 2) * GRID_W;
            int row_d2 = (y + 2) * GRID_W;

            for (int x = 2; x < GRID_W - 2; ++x) {
                int i = row + x;
                Real cv = d_cv[i];
                if (cv == 0.0f) continue;
                Real dP_dx = C1 * (d_p[i + 1] - d_p[i]) + C2 * (d_p[i + 2] - d_p[i - 1]);
                Real dP_dz = C1 * (d_p[row_d1 + x] - d_p[i]) + C2 * (d_p[row_d2 + x] - d_p[row_u1 + x]);
                d_vx[i] -= cv * dP_dx;
                d_vz[i] -= cv * dP_dz;
                Real d = d_damp[i];
                d_vx[i] *= d; d_vz[i] *= d;
            }
        }

        bool local_exploded = false;

        // 2. Update P
#pragma omp parallel for reduction(|:local_exploded)
        for (int y = 2; y < GRID_H - 2; ++y) {
            int row = y * GRID_W;
            int row_u1 = (y - 1) * GRID_W;
            int row_d1 = (y + 1) * GRID_W;
            int row_u2 = (y - 2) * GRID_W;

            for (int x = 2; x < GRID_W - 2; ++x) {
                int i = row + x;
                Real cp = d_cp[i];
                if (cp == 0.0f) continue;
                Real dVx_dx = C1 * (d_vx[i] - d_vx[i - 1]) + C2 * (d_vx[i + 1] - d_vx[i - 2]);
                Real dVz_dy = C1 * (d_vz[i] - d_vz[row_u1 + x]) + C2 * (d_vz[row_d1 + x] - d_vz[row_u2 + x]);
                d_p[i] -= cp * (dVx_dx + dVz_dy);
                d_p[i] *= d_damp[i];
                if (std::abs(d_p[i]) > 1e10) local_exploded = true;
            }
        }
        if (local_exploded) exploded = true;

        if (enableFreeSurface) {
#pragma omp parallel for
            for (int x = 0; x < GRID_W; ++x) {
                d_p[x] = 0; d_p[x + GRID_W] = 0; d_p[x + 2 * GRID_W] = 0;
            }
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
        }
        std::memcpy(gpu_field.data(), p.data(), p.size() * sizeof(Real));
    }

    void loadScenario(int type) {
        initMaterial(); clearWavefield();
        if (type == 0) { // Double Slit
            for (int y = 0; y < GRID_H; ++y) {
                if (abs(y - GRID_H / 2) > 30 && abs(y - GRID_H / 2) < 180) modifyMaterial(GRID_W / 2, y, 5, 3);
            }
            addSource(GRID_W / 4, GRID_H / 2);
        }
        else if (type == 1) { // Lens
            int cx = GRID_W / 2, cy = GRID_H / 2;
            for (int y = 0; y < GRID_H; ++y) for (int x = 0; x < GRID_W; ++x) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < 6400) setParams(y * GRID_W + x, 1000.f, 1000.f);
            }
            modelModified = true;
            addSource(GRID_W / 5, GRID_H / 2);
        }
        else if (type == 2) { // Crystal
            for (int y = 100; y < GRID_H - 100; y += 40) for (int x = 200; x < GRID_W - 100; x += 40) modifyMaterial(x, y, 6, 3);
            useLineSource = true; addSource(50, GRID_H / 2);
        }
        else if (type == 3) { // Waveguide
#pragma omp parallel for
            for (int y = 0; y < GRID_H; ++y) for (int x = 0; x < GRID_W; ++x) {
                if (abs(y - GRID_H / 2) < 40) setParams(y * GRID_W + x, 1500.f, 1000.f);
                else setParams(y * GRID_W + x, 3000.f, 2000.f);
            }
            modelModified = true; addSource(50, GRID_H / 2);
        }
    }
};

// ==========================================
// 4. 全局状态
// ==========================================
AcousticEngine engine;
ToolMode currentTool = ToolMode::TRIGGER_SOURCE;
RenderMode currentRenderMode = RenderMode::COOLWARM;
float displayGain = 0.0005f;
float brushRadiusPx = 20.0f;
bool showUI = true;
bool singleStepTrigger = false;

// Custom Colors
float colPos[3] = { 1.0f, 0.4f, 0.0f }; // Warm
float colNeg[3] = { 0.0f, 0.2f, 0.8f }; // Cool
float colZero[3] = { 0.9f, 0.9f, 0.9f }; // White

// UI State
float cursorX = 0.0f, cursorY = 0.0f; // Normalized (0-1)

// ==========================================
// 5. 视口与坐标映射核心逻辑
// ==========================================
ViewportInfo getViewport(int winW, int winH) {
    ViewportInfo vp;
    float targetAspect = (float)GRID_W / GRID_H;
    float windowAspect = (float)winW / winH;

    if (windowAspect > targetAspect) {
        vp.h = winH;
        vp.w = (int)(winH * targetAspect);
        vp.x = (winW - vp.w) / 2;
        vp.y = 0;
    }
    else {
        vp.w = winW;
        vp.h = (int)(winW / targetAspect);
        vp.x = 0;
        vp.y = (winH - vp.h) / 2;
    }

    vp.screenX = vp.x;
    vp.screenY = (winH - (vp.y + vp.h));
    vp.scaleX = (float)vp.w / GRID_W;
    vp.scaleY = (float)vp.h / GRID_H;
    return vp;
}

void process_input(GLFWwindow* window) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        cursorX = -1.0f;
        isDraggingArray = false; // 如果鼠标移到UI上，取消拖拽
        return;
    }

    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    ViewportInfo vp = getViewport(winW, winH);

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    float relX = (float)mx - vp.screenX;
    float relY = (float)my - vp.screenY;

    float normX = relX / vp.w;
    float normY = relY / vp.h;

    cursorX = normX;
    cursorY = normY;

    int gx = (int)(normX * GRID_W);
    int gy = (int)(normY * GRID_H);


    bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool leftRelease = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE;

    if (gx > 2 && gx < GRID_W - 2 && gy > 2 && gy < GRID_H - 2) {
        // --- 逻辑分支 ---
        if (currentTool == ToolMode::PLACE_ARRAY) {
            // 阵列放置逻辑：按下开始，释放结束
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!isDraggingArray) {
                    isDraggingArray = true;
                    dragStartX = gx;
                    dragStartY = gy;
                }
            }
        }
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            engine.recorder.gx = gx;
            engine.recorder.gy = gy;
            engine.recorder.history.clear();
        }
        else if (leftDown) {
            // 原有的绘画/触发逻辑
            if (currentTool == ToolMode::TRIGGER_SOURCE) {
                static auto lastClickTime = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count() > 100) {
                    engine.addSource(gx, gy);
                    lastClickTime = now;
                }
            }
            else {
                // 绘画逻辑...
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
    else {
        cursorX = -1.0f;
    }

    // 处理拖拽释放 (可能在网格内也可能在网格外释放)
    if (isDraggingArray && leftRelease) {
        isDraggingArray = false;
        // 创建阵列
        engine.placeArray(dragStartX, dragStartY, gx, gy, arrayReceiverCount);
    }
}

// 键盘回调
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_1) currentTool = ToolMode::TRIGGER_SOURCE;
        if (key == GLFW_KEY_2) currentTool = ToolMode::DRAW_HIGH_VEL;
        if (key == GLFW_KEY_3) currentTool = ToolMode::DRAW_LOW_VEL;
        if (key == GLFW_KEY_5) currentTool = ToolMode::DRAW_WALL;
        if (key == GLFW_KEY_4) currentTool = ToolMode::ERASE;
        if (key == GLFW_KEY_6) currentTool = ToolMode::PLACE_ARRAY; // 新增

        
        if (key == GLFW_KEY_7) engine.loadScenario(1);
        if (key == GLFW_KEY_8) engine.loadScenario(2);
        if (key == GLFW_KEY_9) engine.loadScenario(3);
        if (key == GLFW_KEY_0) engine.loadScenario(0);

        if (key == GLFW_KEY_Q) currentRenderMode = RenderMode::LIQUID;
        if (key == GLFW_KEY_W) currentRenderMode = RenderMode::HEATMAP;
        if (key == GLFW_KEY_E) currentRenderMode = RenderMode::MODEL;
        if (key == GLFW_KEY_A) currentRenderMode = RenderMode::COOLWARM;
        if (key == GLFW_KEY_D) currentRenderMode = RenderMode::CUSTOM;

        if (key == GLFW_KEY_F) engine.toggleFreeSurface();
        if (key == GLFW_KEY_B) engine.toggleBoundary();
        if (key == GLFW_KEY_C) engine.clearWavefield();

        if (key == GLFW_KEY_SPACE) engine.paused = !engine.paused;

        // [修复] S键步进逻辑
        if (key == GLFW_KEY_S) {
            engine.paused = true; // 强制暂停
            singleStepTrigger = true; // 触发一帧计算
        }

        if (key == GLFW_KEY_L) engine.useLineSource = !engine.useLineSource;

        if (key == GLFW_KEY_LEFT_BRACKET) engine.currentFreq = std::max(5.0f, engine.currentFreq - 2.0f);
        if (key == GLFW_KEY_RIGHT_BRACKET) engine.currentFreq = std::min(100.0f, engine.currentFreq + 2.0f);

        if (key == GLFW_KEY_BACKSPACE) { engine.initMaterial(); engine.clearWavefield(); }
        if (key == GLFW_KEY_UP) displayGain *= 1.2f;
        if (key == GLFW_KEY_DOWN) displayGain /= 1.2f;
    }
}

void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // 圆角
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;

    // 间距
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(10, 10);

    // 颜色主题：深空灰 + 霓虹青
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.92f); // 半透明背景
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.35f, 0.50f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 0.80f, 1.00f); // Neon Cyan
    colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.80f, 0.60f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.60f, 0.50f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 0.60f, 1.00f);
}

//获取exe文件所在目录
std::string get_exe_dir() {
    char buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, "Acoustic Wave Pro (Ultimate)", NULL, NULL);
    if (!window) return -1;

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    glewInit();
    glfwSwapInterval(0);

    // --- ImGui Setup ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    //获取编译后文件所在目录
    exe_dir = get_exe_dir();
    std::string font_path = exe_dir + "/../images" + "/font/consola.ttf";
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 14.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

    // [重要] 字体放大
    io.FontGlobalScale = 1.5f;

    SetupImGuiStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 恢复键盘回调
    glfwSetKeyCallback(window, key_callback);

    float verts[] = { -1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1 };
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO); glBindVertexArray(VAO);
    glGenBuffers(1, &VBO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);

    GLuint vs = createShader(VS_CODE, GL_VERTEX_SHADER);
    GLuint fs = createShader(FS_CODE, GL_FRAGMENT_SHADER);


    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLuint fieldTex, modelTex;
    glGenTextures(1, &fieldTex); glBindTexture(GL_TEXTURE_2D, fieldTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, GRID_W, GRID_H, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &modelTex); glBindTexture(GL_TEXTURE_2D, modelTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, GRID_W, GRID_H, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    engine.prepareGPUData();
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, modelTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_W, GRID_H, GL_RED, GL_FLOAT, engine.gpu_model.data());

    int stepsPerFrame = 1;
    auto lastT = std::chrono::high_resolution_clock::now();
    int frames = 0;

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

    std::cout << "Detected CPU Threads: " << omp_get_max_threads() << std::endl;
    // 使用默认最大核心数
    // omp_set_num_threads(16); 

    std::cout << ">>> OpenMP Enabled." << std::endl;

    while (!glfwWindowShouldClose(window)) {
        glfwSwapInterval(SwapInterval);//设置垂直同步,0表示关闭垂直同步,1表示开启垂直同步
        glfwPollEvents();
        process_input(window);

        // Physics Logic
        bool performStep = !engine.paused;
        if (singleStepTrigger) {
            performStep = true;
            singleStepTrigger = false;
        }

        if (performStep && !engine.exploded) {
            for (int i = 0; i < stepsPerFrame; ++i) engine.step();
            // --- 更新所有接收器 ---
            engine.updateReceivers();
            engine.recorder.update(engine.p, GRID_W);
        }

        // Upload
        engine.prepareGPUData();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fieldTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_W, GRID_H, GL_RED, GL_FLOAT, engine.gpu_field.data());

        if (engine.modelModified) {
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, modelTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_W, GRID_H, GL_RED, GL_FLOAT, engine.gpu_model.data());
            engine.modelModified = false;
        }

        // --- Render Pass ---
        int winW, winH; glfwGetWindowSize(window, &winW, &winH);
        ViewportInfo vp = getViewport(winW, winH);

        glViewport(0, 0, winW, winH);
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(vp.x, vp.y, vp.w, vp.h);

        glUseProgram(prog);
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

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // --- ImGui Draw ---
        glViewport(0, 0, winW, winH);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (showUI) {
            // [左侧控制面板]
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
            ImGui::SetNextWindowSize(ImVec2(360, 850), ImGuiCond_Once);

            ImGui::Begin("Wave Control", NULL);

            // 状态栏
            ImGui::Text("FPS: %.1f | Grid: %dx%d", io.Framerate, GRID_W, GRID_H);
            if (engine.exploded) ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "STATUS: NUMERICAL EXPLOSION!");
            else ImGui::TextColored(ImVec4(0, 1, 0.8f, 1), "Status: %s", engine.paused ? "PAUSED" : "RUNNING");

            // 播放控制
            float btnW = (ImGui::GetContentRegionAvail().x - 20) / 3;
            if (ImGui::Button(engine.paused ? " RESUME (Space) " : "  PAUSE (Space) ", ImVec2(btnW, 35))) engine.paused = !engine.paused;
            ImGui::SameLine();
            if (ImGui::Button(" STEP (S) ", ImVec2(btnW, 35))) { engine.paused = true; singleStepTrigger = true; }
            ImGui::SameLine();
            if (ImGui::Button(" CLEAR (C) ", ImVec2(btnW, 35))) engine.clearWavefield();

            ImGui::Spacing();
            ImGui::Separator();

            if (ImGui::BeginTabBar("Tabs")) {
                if (ImGui::BeginTabItem(" Tools ")) {
                    ImGui::Spacing();
                    const char* tools[] = { "Source Trigger (1)", "Draw Rock (2)", "Draw Lens (3)", "Draw Wall (5)", "Eraser (4)","pro array (6)" };
                    for (int i = 0; i < 6; i++) {
                        if (ImGui::RadioButton(tools[i], (int)currentTool == i)) currentTool = (ToolMode)i;
                        ImGui::Spacing();
                    }

                    ImGui::Separator();
                    ImGui::RadioButton("Array Tool par", (int)currentTool == 5); // 新增
                    if (currentTool == ToolMode::PLACE_ARRAY) {
                        ImGui::Indent();
                        ImGui::SliderInt("Count", &arrayReceiverCount, 1, 100);
                        ImGui::Unindent();
                    }
                    ImGui::Separator();
                    ImGui::Checkbox("Line Source (L)", &engine.useLineSource);
                    ImGui::Checkbox("Show recorder window", &show_recorder_window);
                    ImGui::Checkbox("Show recorders window", &show_recorders_window);
                    ImGui::Checkbox("SwapInterval", &SwapInterval);
                    ImGui::Separator();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(" Settings ")) {
                    ImGui::Spacing();
                    ImGui::Text("Simulation");
                    ImGui::SliderFloat("Frequency", &engine.currentFreq, 5.0f, 300.0f);
                    ImGui::SliderInt("Speed", &stepsPerFrame, 1, 50);

                    ImGui::Spacing();
                    ImGui::Text("Interaction");
                    ImGui::SliderFloat("Brush Size", &brushRadiusPx, 5.0f, 100.0f);

                    ImGui::Spacing();
                    ImGui::Text("Boundaries");
                    bool bFree = engine.enableFreeSurface;
                    if (ImGui::Checkbox("Free Surface (F)", &bFree)) engine.toggleFreeSurface();
                    bool bReflect = (engine.boundaryMode == 1);
                    if (ImGui::Checkbox("Reflective Box (B)", &bReflect)) engine.toggleBoundary();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(" Visuals ")) {
                    ImGui::Spacing();
                    ImGui::Text("Render Mode");
                    const char* modes[] = { "Liquid (Q)", "Heatmap (W)", "Model (E)", "Coolwarm (A)", "Custom (D)" };
                    int modeIdx = (int)currentRenderMode;
                    if (ImGui::Combo("##Mode", &modeIdx, modes, IM_ARRAYSIZE(modes))) currentRenderMode = (RenderMode)modeIdx;

                    ImGui::Spacing();
                    ImGui::SliderFloat("Exposure", &displayGain, 0.0001f, 0.005f, "%.5f");

                    if (currentRenderMode == RenderMode::CUSTOM) {
                        ImGui::Spacing();
                        ImGui::Text("Custom Palette");
                        ImGui::ColorEdit3("Positive", colPos);
                        ImGui::ColorEdit3("Negative", colNeg);
                        ImGui::ColorEdit3("Zero", colZero);
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Separator();
            ImGui::Text("Scenarios (6-9)");
            if (ImGui::Button("Double Slit", ImVec2(100, 0))) engine.loadScenario(0); ImGui::SameLine();
            if (ImGui::Button("Acoustic Lens", ImVec2(110, 0))) engine.loadScenario(1);
            if (ImGui::Button("Crystal", ImVec2(100, 0))) engine.loadScenario(2); ImGui::SameLine();
            if (ImGui::Button("Waveguide", ImVec2(110, 0))) engine.loadScenario(3);

            if (ImGui::Button("RESET SIMULATION", ImVec2(-1, 35))) {
                engine.initMaterial(); engine.clearWavefield();
            }

            ImGui::End();


            if (show_recorder_window) {
                // Oscilloscope
                ImGui::SetNextWindowPos(ImVec2(400, (float)winH - 280), ImGuiCond_Once);
                ImGui::SetNextWindowSize(ImVec2((float)winW - 420, 250), ImGuiCond_Once);
                ImGui::Begin("Oscilloscope", &show_recorder_window);

                ImGui::SliderInt("History Len", &engine.recorder.maxHistory, 100, 2000);
                if (engine.recorder.history.size() > 0) {
                    ImGui::PlotLines("##W", engine.recorder.history.data(),
                        (int)engine.recorder.history.size(), 0, NULL,
                        -5000.0f, 5000.0f, ImVec2(-1, 160));
                }
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Probe Position: (%d, %d) - Right Click to Move", engine.recorder.gx, engine.recorder.gy);
                ImGui::End();
            }
            // ==========================================
            // 新增: 地震记录窗口 (Seismic Record)
            // ==========================================

            if (show_recorders_window)
            {
                ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_Once);
                ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Once);
                ImGui::Begin("Seismic Array Record", &show_recorders_window);

                if (engine.receivers.empty()) {
                    ImGui::Text("No receivers active.");
                    ImGui::Text("Select 'Array Tool', Click & Drag on the map to place receivers.");
                }
                else {
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
                    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

                    // 绘制背景
                    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 25, 255));
                    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(100, 100, 100, 255));

                    // 参数
                    static float waveGain = 1.0f;
                    ImGui::SliderFloat("Gain", &waveGain, 0.1f, 10.0f);

                    // 开始绘图
                    // 布局：X轴为时间 (History), Y轴为检波器索引 (叠加快照)
                    // 或者是地震学常用布局：X轴为检波器距离, Y轴为时间 (Waterfall)
                    // 这里我们采用：垂直堆叠 (Vertical Stack) 
                    //    行 = 检波器
                    //    列 = 时间

                    float rowHeight = canvas_size.y / engine.receivers.size();
                    float timeStepWidth = canvas_size.x / engine.maxHistoryLen;

                    // 限制行高，避免太挤
                    if (rowHeight < 5.0f) rowHeight = 5.0f;

                    ImGui::InvisibleButton("canvas", canvas_size); // 占位，允许在上面画图

                    for (size_t i = 0; i < engine.receivers.size(); ++i) {
                        const auto& r = engine.receivers[i];
                        if (r.data.empty()) continue;

                        float basePathY = canvas_pos.y + (i + 0.5f) * rowHeight;

                        // 裁剪：如果超出屏幕下方就不画了
                        if (basePathY > canvas_pos.y + canvas_size.y) break;

                        ImColor waveColor = ImColor(0.0f, 1.0f, 0.6f);

                        // 构建顶点
                        for (size_t t = 0; t < r.data.size() - 1; ++t) {
                            float x1 = canvas_pos.x + t * timeStepWidth;
                            float y1 = basePathY - r.data[t] * waveGain * 0.05f; // 缩放振幅
                            float x2 = canvas_pos.x + (t + 1) * timeStepWidth;
                            float y2 = basePathY - r.data[t + 1] * waveGain * 0.05f;

                            // 简单的裁剪
                            if (x1 < canvas_pos.x + canvas_size.x) {
                                draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), waveColor, 1.0f);
                            }
                        }

                        // 绘制基线/分割线
                        draw_list->AddLine(ImVec2(canvas_pos.x, basePathY), ImVec2(canvas_pos.x + canvas_size.x, basePathY), IM_COL32(50, 50, 60, 100));

                        // 绘制左侧标签
                        char label[16]; sprintf(label, "R%d", (int)i);
                        draw_list->AddText(ImVec2(canvas_pos.x + 2, basePathY - 7), IM_COL32(150, 150, 150, 200), label);
                    }
                }
                ImGui::End();

                // ==========================================
                // 新增: 在主视图 Overlay 绘制检波器位置和拖拽线
                // ==========================================
                // 我们需要将网格坐标转换为屏幕像素坐标
                // 使用之前的 ViewportInfo vp 计算
                // vp 已经在 main 循环中计算过了

                // 为了安全起见，这里重新计算视口位置 (确保和OpenGL渲染对齐)
                // 注意：ImGui是全屏窗口覆盖，所以坐标是相对于屏幕左上角的
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

                // ... (前文代码: ImDrawList* fg_list = ...)

        // 定义样式参数
                const float geoSize = 6.0f; // 检波器图标大小
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
            }
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
        return 0;
    }
}