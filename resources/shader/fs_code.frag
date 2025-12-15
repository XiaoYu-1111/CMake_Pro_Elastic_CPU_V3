#version 330 core

//======================================================================
//  SHADER INPUTS & OUTPUTS
//======================================================================

out vec4 FragColor; // The final color of the pixel.
in vec2 TexCoord;  // The texture coordinate from the Vertex Shader.

//======================================================================
//  UNIFORMS (Parameters from CPU)
//======================================================================

// --- Core Data Textures ---
uniform sampler2D fieldTex;      // Texture with the wave simulation data (pressure 'p').
uniform sampler2D modelTex;      // Texture with the physical properties of the medium (wave speed 'vp').
uniform sampler2D backgroundTex; // An optional background texture.

// --- Scene & Simulation Parameters ---
uniform vec2 resolution;   // The resolution of the viewport.
uniform vec2 gridSize;     // The dimensions of the simulation grid.
uniform float gain;        // A multiplier to visually amplify wave effects.
uniform float time;        // A global time variable for animations.
uniform int dampWidth;     // The width of the absorbing boundary region.
uniform int boundaryMode;  // How the boundary is visualized.
uniform int freeSurface;   // A flag for a free surface boundary condition.

// --- User Interaction ---
uniform vec2 mousePos;     // The current mouse position.
uniform float brushRadius; // The radius of the user's interaction tool.
uniform int toolMode;      // The selected tool (e.g., source, wall).
uniform int isLineSource;  // Flag to indicate if the source is a line.
uniform vec2 probePos;     // The position of a measurement probe.

// --- Material & Color Controls ---
uniform int mode;          // The currently selected rendering mode (0-13).
uniform float wetness;     // A parameter for Mode 13 to control clay "wetness".
uniform vec3 colValPos;    // Custom color for positive values (Mode 4).
uniform vec3 colValNeg;    // Custom color for negative values (Mode 4).
uniform vec3 colValZero;   // Custom color for zero values (Mode 4).
uniform vec3 colClayBase;  // Base color for clay material (Mode 13).
uniform vec3 colClayDark;  // Shadow color for clay material (Mode 13).
uniform vec3 colmossColor; // Color for moss on clay (Mode 13).

uniform vec3 colAbsorbColor;  //vec3(1.0, 0.5, 0.0)

uniform vec3 colBaseColor;//(0.12, 0.12, 0.14)
uniform vec3 colHighColor;//vec3(0.20, 0.20, 0.23) * 4.0
uniform vec3 colGridColor;//vec3(0.) * 4.0
//======================================================================
//  HELPER FUNCTIONS
//======================================================================

// Generates a procedural grid pattern.
float grid(vec2 uv, float spacing, float thick) {
    vec2 gridPos = uv * resolution;
    vec2 deriv = max(vec2(fwidth(gridPos.x), fwidth(gridPos.y)), vec2(0.001));
    vec2 range = abs(fract(gridPos / spacing - 0.5) - 0.5);
    vec2 line = 1.0 - smoothstep(0.0, deriv.x / spacing * thick * 2.0, range);
    return max(line.x, line.y);
}

// Generates an animated, diagonal stripe pattern.
float techStripe(vec2 uv) {
    vec2 px = uv * resolution;
    float stripe = mod(px.x + px.y + time * 20.0, 20.0);
    return step(stripe, 1.0);
}

// Generates a color from a blue-black-red/yellow heatmap palette.
vec3 paletteHeat(float v) {
    vec3 colLow = vec3(0.0, 0.1, 0.4);
    vec3 colMid = vec3(0.0, 0.0, 0.0);
    vec3 colHigh = vec3(1.0, 0.4, 0.0);
    float t = clamp(v, -1.0, 1.0);
    return mix(colMid, (t < 0.0 ? colLow : colHigh), abs(t));
}

// Generates a color from a blue-white-red "coolwarm" palette.
vec3 getCoolWarmColor(float t) {
    vec3 colorBlue = vec3(0.23, 0.29, 0.75);
    vec3 colorWhite = vec3(0.95, 0.95, 0.95);
    vec3 colorRed = vec3(0.70, 0.01, 0.15);
    vec3 res = colorWhite;
    if (t < 0.0) res = mix(colorWhite, colorBlue, -t);
    else res = mix(colorWhite, colorRed, t);
    return res;
}

// Generates a color from the "Jet" colormap (blue-cyan-green-yellow-red).
vec3 jetColor(float value) {
    value = clamp(value, 0.0f, 1.0f);
    float r = clamp(1.5f - abs(value * 4.0f - 3.0f), 0.0f, 1.0f);
    float g = clamp(1.5f - abs(value * 4.0f - 2.0f), 0.0f, 1.0f);
    float b = clamp(1.5f - abs(value * 4.0f - 1.0f), 0.0f, 1.0f);
    return vec3(r, g, b);
}

// Creates a simple procedural environment map (sky/ground) for reflections.
vec3 getEnvironmentColor(vec3 reflectDir) {
    vec3 skyColor = vec3(0.6, 0.75, 0.9);
    vec3 groundColor = vec3(0.4, 0.3, 0.2);
    float mixFactor = smoothstep(-0.2, 0.2, reflectDir.y);
    return mix(groundColor, skyColor, mixFactor);
}

// 简单的随机/哈希函数
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}
// 在你的 main() 函数之前，定义这个辅助函数
vec3 calculateBackground(vec2 uv) {
    float m = texture(modelTex, uv).r;
    vec3 modelColor = vec3(0.12, 0.12, 0.14) + m * 0.00005; // 你的基础背景色逻辑

    // 如果你有网格，也在这里画。例如:
    // float grid = ...; // 你的网格计算逻辑
    // modelColor += grid * 0.1;

    return modelColor;
}

//======================================================================
//  MAIN RENDER FUNCTION
//======================================================================
void main() {
    // --- STEP 1: DATA INITIALIZATION ---
    // Read simulation values and calculate the gradient of the wave field.
    // The gradient is used to approximate the surface normal for lighting.
    vec2 step_resolution = 1.0 / resolution;
    float p = texture(fieldTex, TexCoord).r;
    float vp = texture(modelTex, TexCoord).r;
    float v_norm = clamp((vp - 500.0) / 4500.0, 0.0, 1.0);

    float p_l = texture(fieldTex, TexCoord - vec2(step_resolution.x * 1.5, 0.0)).r;
    float p_r = texture(fieldTex, TexCoord + vec2(step_resolution.x * 1.5, 0.0)).r;
    float p_u = texture(fieldTex, TexCoord - vec2(0.0, step_resolution.y * 1.5)).r;
    float p_d = texture(fieldTex, TexCoord + vec2(0.0, step_resolution.y * 1.5)).r;
    vec2 grad = vec2(p_r - p_l, p_d - p_u) * gain * 10.0;
    float gradLen = length(grad);

    // --- step_resolution 2: BACKGROUND SETUP ---
    // Determine the base color of the pixel based on the physical model (vp).
    vec3 bgBase = colBaseColor;
    vec3 bgHigh = colHighColor;
    vec3 bgWall = vec3(0.50, 0.52, 0.55);
    bool isWall = vp < 10.0;

    vec3 bg = bgBase;
    if (isWall) bg = bgWall;
    else bg = mix(bgBase, bgHigh, v_norm);

    // Add procedural details (grid, stripes) to the background.
    if (!isWall) {
        float minorGrid = grid(TexCoord, 100.0, 0.5);
        // --- 修改开始 ---
    // 格式：vec3(R, G, B) * minorGrid * 亮度系数
    // 例子 A：赛博朋克青色 (Cyberpunk Cyan)vec3(0.0, 1.0, 1.0)
        bg += colGridColor * minorGrid * 1.0;//修改网格颜色
    }
    else {
        float s = techStripe(TexCoord);
        bg = mix(bg, bg * 1.2, s * 0.2);
    }

    // --- STEP 3: RENDER MODE SELECTION ---
    // This is the core logic that determines the visual style.
    vec3 color = bg;
    float visualGain = gain;

    // === Mode 0: Liquid 3D (Clear Water / Glass Effect) ===
    if (mode == 0) {
        // 1. 法线计算 (Surface Normal)
        // Z值从0.8改为4.0，模拟液体的表面张力，使波面更圆润光滑，减少噪点
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 4.0));

        // 2. 光照设置 (Lighting Setup)
        vec3 lightDir = normalize(vec3(-0.5, 0.5, 1.0)); // 主光
        vec3 viewDir = vec3(0.0, 0.0, 1.0);              // 视线
        vec3 halfDir = normalize(lightDir + viewDir);    // 半程向量

        // 3. 基础波形数据
        float val = p * visualGain;       // 波的强度
        float absVal = abs(val);          // 波的绝对高度

        // 4. 通透感核心：背景光折射 (Refraction/Caustics)
        // 根据表面法线扭曲背景亮度。波峰聚焦光线(变亮)，波谷发散光线(变暗)
        float lightFocus = dot(normal, lightDir);
        // 模拟类似焦散的效果：光线穿过波浪打在底色上
        vec3 baseLayer = bg * (0.8 + 0.4 * lightFocus);

        // 5. 材质颜色 (Tint) - 类似于水或玻璃的本体色
        // 浅处透明，深处偏蓝/青色
        vec3 deepColor = vec3(0.0, 0.4, 0.8);   // 深水蓝
        vec3 shallowColor = vec3(0.6, 0.9, 1.0); // 浅水青

        // 根据波的高度混合颜色。正压(波峰)偏亮青，负压(波谷)偏深蓝
        vec3 liquidTint = (p > 0.0) ? shallowColor : deepColor;

        // Beer's Law 模拟：波越厚(振幅越大)，颜色越浓，透明度越低
        // smoothstep 控制透明度范围，保留微小波动的完全透明感
        float opacity = smoothstep(0.05, 0.8, absVal);

        // 6. 高光与反射 (Specular & Fresnel)
        // 提高高光指数(80.0)，让反光点更小更锐利，像湿润的表面
        float spec = pow(max(dot(normal, halfDir), 0.0), 80.0);

        // 菲涅尔效应：视线与水面夹角越小(边缘)，反射越强
        float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);

        // 7. 最终合成 (Compositing)
        // 第一层：背景透过液体 (Tinted Background)
        color = mix(baseLayer, baseLayer * liquidTint, opacity * 0.6);

        // 第二层：叠加菲涅尔边缘发光 (增加体积感)
        color += shallowColor * fresnel * 0.5 * opacity;

        // 第三层：叠加高光 (锐利的白点，表现湿润感)
        color += vec3(1.0) * spec * 0.9;

        // 8. (可选) 色散微调 - 模拟强烈波动时的光线色散
        if (absVal > 0.5) {
            color += vec3(0.1, 0.0, -0.1) * fresnel * absVal; // 边缘轻微红蓝偏移
        }
    }
    // === Mode 1: Energy Intensity Map (Magma Style) ===
    else if (mode == 1) {
        // 1. 获取能量强度 (Magnitude)
        // 使用绝对值，因为无论是波峰还是波谷都代表能量。
        // visualGain 需要适当调大，或者在这里乘个系数，因为能量图容易偏暗
        float val = abs(p * visualGain * 1.5);

        // 2. 非线性映射 (Tone Mapping)
        // 使用指数衰减映射将 [0, inf] 映射到 [0, 1]，同时保留低能量细节
        // 类似于 HDR 渲染中的 Tone Mapping
        float energy = 1.0 - exp(-val * 2.5);

        // 3. 高级配色 (Magma / Black Body Radiation 风格)
        // 这种配色方案在科学可视化中非常流行，既美观又具有感官上的线性亮度变化

        vec3 c0 = vec3(0.02, 0.02, 0.05); // 背景：极深的蓝黑色（虚空）
        vec3 c1 = vec3(0.25, 0.05, 0.35); // 低能：深紫罗兰色
        vec3 c2 = vec3(0.70, 0.15, 0.40); // 中低：树莓红/洋红色
        vec3 c3 = vec3(0.95, 0.50, 0.15); // 中高：炽热橙色
        vec3 c4 = vec3(1.00, 0.95, 0.80); // 高能：耀眼白炽色

        vec3 energyColor;

        // 多级混合插值 (Multi-stop Gradient)
        if (energy < 0.25) {
            // 0.0 - 0.25: 黑 -> 紫
            float t = smoothstep(0.0, 0.25, energy);
            energyColor = mix(c0, c1, t);
        }
        else if (energy < 0.5) {
            // 0.25 - 0.5: 紫 -> 红
            float t = smoothstep(0.25, 0.5, energy);
            energyColor = mix(c1, c2, t);
        }
        else if (energy < 0.75) {
            // 0.5 - 0.75: 红 -> 橙
            float t = smoothstep(0.5, 0.75, energy);
            energyColor = mix(c2, c3, t);
        }
        else {
            // 0.75 - 1.0: 橙 -> 白
            float t = smoothstep(0.75, 1.0, energy);
            energyColor = mix(c3, c4, t);
        }

        // 4. 透明度与发光感
        // 低能量区域透明，露出背景；高能量区域不透明且明亮
        float opacity = smoothstep(0.01, 0.15, energy);

        // 5. 合成 (修改部分：让墙体也动起来)
        // 【修复】先定义 modelVal，从模型纹理读取速度值
        float modelVal = texture(modelTex, TexCoord).r;
        if (isWall) {
            // 让墙体不是死板的背景色
            // 而是基于“速度模型”显示一种半透明的深色结构
            float wallDensity = smoothstep(2000.0, 4500.0, modelVal);
            vec3 wallColor = vec3(0.1, 0.12, 0.15) * (0.5 + 0.5 * wallDensity);

            // 【核心特效】能量护盾效果
            // 当波打在墙上时 (energy > 0)，叠加一层蓝紫色的边缘光
            vec3 shieldGlow = vec3(0.4, 0.6, 1.0) * energy * 0.8;

            color = wallColor + shieldGlow;
        }
        else {
            // 将能量颜色叠加在背景上
            color = mix(bg, energyColor, opacity);
            //color = energyColor;
        }
    }
    // === Mode 2: Geo-Scientific Model + Wave Overlay ===
    else if (mode == 2) {
        // 1. 获取速度场数据
        float mv = texture(modelTex, TexCoord).r;

        // 2. 基础底图：地质模型着色
        vec3 baseColor;

        // 特殊处理：墙体/真空 (速度 < 100)
        if (mv < 100.0) {
            // 墙体使用深色工程斜纹
            float stripe = step(0.5, fract((gl_FragCoord.x - gl_FragCoord.y) * 0.05));
            vec3 wallDark = vec3(0.15, 0.16, 0.18);
            vec3 wallLight = vec3(0.20, 0.21, 0.23);
            baseColor = mix(wallDark, wallLight, stripe);
        }
        else {
            // 常规介质：地质色谱
            float t = clamp((mv - 500.0) / 5000.0, 0.0, 1.0);

            vec3 cWater = vec3(0.0, 0.35, 0.7);    // 水 (深蓝)
            vec3 cSediment = vec3(0.3, 0.7, 0.5);  // 沉积层 (青绿)
            vec3 cRock = vec3(0.9, 0.8, 0.3);      // 岩石 (黄)
            vec3 cHard = vec3(0.8, 0.3, 0.2);      // 硬岩 (红)

            if (t < 0.33) baseColor = mix(cWater, cSediment, t * 3.0);
            else if (t < 0.66) baseColor = mix(cSediment, cRock, (t - 0.33) * 3.0);
            else baseColor = mix(cRock, cHard, (t - 0.66) * 3.0);

            // 添加等值线 (让结构更清晰)
            float contours = 0.5 + 0.5 * sin(mv * 0.05);
            float lineStrength = smoothstep(0.95, 1.0, contours) * 0.2;
            baseColor = mix(baseColor, vec3(1.0), lineStrength);

            // 边缘高亮
            float d1 = texture(modelTex, TexCoord + vec2(1.0 / resolution.x, 0.0)).r;
            float d2 = texture(modelTex, TexCoord + vec2(0.0, 1.0 / resolution.y)).r;
            float edge = length(vec2(mv - d1, mv - d2));
            float edgeFactor = smoothstep(10.0, 100.0, edge);
            baseColor = mix(baseColor, vec3(0.0), edgeFactor * 0.5); // 黑色描边
        }

        // 3. 波场叠加 (Wave Overlay)
        // 获取声压值
        float val = p * visualGain;

        // 计算波的可见度 (Mask)
        float waveMask = smoothstep(0.02, 0.5, abs(val));

        // 定义波的颜色：
        // 正压(波峰) -> 白色高亮
        // 负压(波谷) -> 黑色阴影
        // 这种黑白配色在彩色地图上对比度最高
        vec3 waveColor = (val > 0.0) ? vec3(1.0) : vec3(0.0);

        // 4. 最终合成
        // 混合逻辑：底图 + (波颜色 * 透明度)
        // 0.6 是最大不透明度，保证波浪不会完全遮挡住底下的地质结构
        color = mix(baseColor, waveColor, waveMask * 0.6);
        }
    // === Mode 3: Coolwarm Colormap ===
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
    // === Mode 3: Metallic Coolwarm Relief ===
    else if (mode == 14) {
        // 1. 数据准备
        float val = clamp(p * visualGain * 1.5, -1.0, 1.0);
        float absVal = abs(val);

        // 2. 增强立体感的法线
        // Z值从1.0改为0.5，让波浪的起伏更“陡峭”，浮雕感更强
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.5));

        // 3. 光照设置
        vec3 lightDir = normalize(vec3(-0.8, 0.8, 1.0));
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 halfDir = normalize(lightDir + viewDir);

        // 4. 获取 Coolwarm 基础色
        // (假设 getCoolWarmColor 函数已存在)
        vec3 baseColor = getCoolWarmColor(val);

        // 5. 光照计算
        // 漫反射 (Diffuse Shading)
        float diff = max(dot(normal, lightDir), 0.0);

        // 镜面高光 (Specular Highlight) -> 模拟金属光泽
        // 指数 32.0 产生较宽的高光，像拉丝金属
        float spec = pow(max(dot(normal, halfDir), 0.0), 32.0);

        // 动态阴影 (Dynamic Shadow)
        // 梯度越大 (波越陡)，阴影越深
        float shadow = 1.0 - smoothstep(0.5, 3.0, gradLen);

        // 6. 合成
        // a. 基础色 + 漫反射 (给颜色加上明暗)
        vec3 finalColor = baseColor * (0.5 + 0.5 * diff);

        // b. 叠加高光 (只在高亮处叠加纯白色)
        // 乘以 absVal，确保只有波纹起伏的地方才有高光，平坦处没有
        finalColor += vec3(1.0) * spec * 0.7 * absVal;

        // c. 应用阴影
        finalColor *= shadow;

        // 7. 透明度混合
        // 锐化透明度过渡，让波纹边缘更清晰
        float opacity = smoothstep(0.02, 0.1, absVal);

        if (isWall) {
            // 让墙体显示为深灰色金属，并能反射波的光芒
            float wallShade = max(dot(vec3(0, 0, 1), lightDir), 0.0);
            color = vec3(0.15) * (0.5 + 0.5 * wallShade) + baseColor * spec * 0.5;
        }
        else {
            color = mix(bg, finalColor, opacity);
        }
    }
    // === Mode 4: Custom 3-Color Map ===
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
    // === Mode 5: Jet Colormap ===
    else if (mode == 5) {
        float val = clamp(p * visualGain, -1.0f, 1.0f);
        color = bg;
        float opacity = smoothstep(0.02, 0.2, abs(val));
        color = mix(bg, jetColor(val), opacity);
    }
    // === Mode 6: Black & White Ink Wash / Sketch Style (White Background Version) ===
    else if (mode == 6) {
        // 1. 数据准备
        float val = p * visualGain;
        float absVal = abs(val);

        // 2. 纸张背景 (Paper Texture)
        float grain = random(TexCoord * 1.5) * 0.05;

        // 【核心修改】将背景强制设置为白色，并减去纹理，而不是使用 bg
        //   之前: vec3 paperColor = bg + grain;
        vec3 paperColor = vec3(1.0) - grain; // vec3(1.0) 就是纯白色

        // --- 【参数调节区】 ---
        float highlight_intensity = 0.7;
        float ink_wash_intensity = 0.65;
        float contour_line_strength = 0.7;
        // ----------------------

        // 3. 水墨渲染 (Ink Wash)
        float highlight = smoothstep(0.0, 0.8, val) * highlight_intensity;
        float inkWash = smoothstep(0.0, 0.8, -val) * ink_wash_intensity;

        vec3 inkColor = paperColor - inkWash;
        inkColor += highlight;

        // 4. 轮廓线描边 (Contour Lines)
        float zero_crossing_line = (1.0 - smoothstep(0.0, 0.1, absVal)) * smoothstep(0.2, 1.0, gradLen);

        float d_grad_x = dFdx(gradLen);
        float d_grad_y = dFdy(gradLen);
        float edge = sqrt(d_grad_x * d_grad_x + d_grad_y * d_grad_y);
        float edge_line = smoothstep(0.05, 0.2, edge);

        // 5. 合成
        vec3 finalColor = inkColor;
        finalColor = mix(finalColor, vec3(0.0), zero_crossing_line * contour_line_strength);
        finalColor = mix(finalColor, vec3(0.0), edge_line * (contour_line_strength - 0.2));

        float opacity = smoothstep(0.01, 0.05, absVal + gradLen * 0.1);

        if (isWall) {
            float stripe = step(0.5, fract((gl_FragCoord.x - gl_FragCoord.y) * 0.05)) +
                step(0.5, fract((gl_FragCoord.x + gl_FragCoord.y) * 0.05));
            color = paperColor - stripe * 0.1;
        }
        else {
            color = mix(paperColor, finalColor, opacity);
        }
    }
    // === Mode 7: Frosted Glass / Icy Relief Effect (Pure GLSL Version) ===
    else if (mode == 7) {
        // --- 【参数调节区】 ---
        float blur_radius = 0.003;
        float blur_samples = 8.0;
        float glossiness = 256.0;
        float bump_strength = 0.6;
        // ----------------------

        vec3 clearBg = calculateBackground(TexCoord);

        // 2. 背景模糊 (Simulating Frosted Surface)
        vec3 blurredBg = vec3(0.0);
        for (float i = 0.0; i < blur_samples; i++) {
            float angle = i * 2.3999632297;
            float radius = sqrt(i / blur_samples);
            vec2 offset = vec2(cos(angle), sin(angle)) * radius * blur_radius;
            blurredBg += calculateBackground(TexCoord + offset);
        }
        blurredBg /= blur_samples;

        // ... 后续代码完全一样 ...
        // 3. 法线, 4.光照, 5.光照计算, 6.合成
        vec3 normal = normalize(vec3(-grad.x * bump_strength, -grad.y * bump_strength, 0.2));
        vec3 lightDir = normalize(vec3(-0.6, 0.6, 1.0));
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), glossiness);
        float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
        vec3 finalColor = blurredBg;
        finalColor += vec3(0.6, 0.9, 1.0) * fresnel * 0.6;
        finalColor += vec3(1.0) * spec * 0.8;

        // 7. 透明度
        float opacity = smoothstep(0.0, 0.05, gradLen + abs(p) * 0.1);

        if (isWall) {
            color = vec3(0.1);
        }
        else {
            color = mix(clearBg, finalColor, opacity);
        }
    }
    // === Mode 8: Pearlescent Thin-Film (High-End Material) ===
    else if (mode == 8) {
        // 1. 物理参数
        float film_thickness = 3.0; // 薄膜厚度感
        float smoothness = 0.95;    // 表面光滑度

        // 2. 准备数据
        // 使用较平滑的法线 (Z=2.0)，营造丝绸般的起伏，而不是尖锐的波浪
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 2.0));

        // 视线向量 & 光照向量
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 lightDir = normalize(vec3(-1.0, 1.0, 1.5)); // 左上角柔和光源
        vec3 halfDir = normalize(lightDir + viewDir);

        // 3. 计算薄膜干涉色 (Thin-Film Interference)
        // 这是高级质感的来源：颜色不是固定的，而是根据视角(NdotV)发生变化
        // 模拟珍珠/油膜的色彩偏移：金 -> 紫 -> 青
        float NdotV = clamp(dot(normal, viewDir), 0.0, 1.0);

        // 基于余弦的调色板 (Cosine Palette) 生成彩虹干涉
        // 这里的参数经过微调，产生高雅的香槟金到淡紫色的过渡
        vec3 iridescence = vec3(0.5) + 0.5 * cos(6.28318 * (vec3(1.0) * NdotV * film_thickness + vec3(0.0, 0.10, 0.20)));

        // 4. 光照模型 (PBR-lite)
        // a. 菲涅尔效应 (Fresnel): 边缘反光更强
        float fresnel = pow(1.0 - NdotV, 5.0);
        fresnel = 0.04 + 0.96 * fresnel; // 典型的电介质(水/玻璃)F0值

        // b. 镜面高光 (Specular): 模拟湿润表面
        float NdotH = max(dot(normal, halfDir), 0.0);
        float spec = pow(NdotH, 64.0) * smoothness;

        // 5. 阴影与体积感 (Volume)
        // 根据波的高度(p)和梯度(gradLen)，模拟光线穿过液体产生的聚焦和衰减
        // 波峰处稍微变亮(透镜聚焦)，波谷处稍微变暗(厚度增加)
        float volume_shade = 1.0 + p * visualGain * 2.0;

        // 6. 合成
        // 步骤 A: 基础层是背景 (bg)
        // 让波浪起伏的地方背景稍微变暗，模拟光线折射损耗，增加厚重感
        vec3 baseLayer = bg * (0.9 + 0.1 * volume_shade);

        // 步骤 B: 叠加珠光干涉层
        // 使用 Screen 混合模式或者是简单的加法，但在边缘(Fresnel)处更强
        vec3 coatLayer = iridescence * fresnel * 1.2;

        // 步骤 C: 叠加锐利高光
        vec3 specLayer = vec3(1.0) * spec;

        // 7. 最终混合
        // 透明度掩码：没有波的地方完全显示背景，有波的地方显示材质
        float opacity = smoothstep(0.0, 0.1, abs(p * visualGain) + gradLen * 0.2);

        if (isWall) {
            // 墙体：保持背景，或者加上一层淡淡的磨砂感
            color = bg * 0.8;
        }
        else {
            // 混合逻辑：
            // 背景 + (干涉色 * 透明度) + 高光
            // 这种混合方式保证了背景始终可见，只是上面覆盖了一层流动的光膜
            color = baseLayer + (coatLayer + specLayer) * opacity;
        }
    }
    // === Mode 9: Blue & White Porcelain Effect ===
    else if (mode == 9) {
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 2.0));
        vec3 lightDir = normalize(vec3(-0.5, 0.5, 1.0));
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 halfDir = normalize(lightDir + viewDir);
        vec3 colorBase = vec3(0.96, 0.97, 0.98);
        vec3 colorInk = vec3(0.05, 0.15, 0.55);
        float val = p * visualGain;
        float patternMask = smoothstep(0.05, 0.35, abs(val));
        float inkDepth = smoothstep(0.2, 0.8, abs(val));
        vec3 pigment = mix(colorBase, colorInk, patternMask);
        pigment = mix(pigment, colorInk * 0.6, inkDepth);
        float diff = max(dot(normal, lightDir), 0.0);
        float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);
        float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
        float shadow = 1.0 - smoothstep(0.5, 3.0, gradLen);
        vec3 porcelain = pigment * (0.4 + 0.6 * diff);
        porcelain += vec3(0.1, 0.15, 0.2) * fresnel;
        porcelain += vec3(1.0) * spec * 0.8;
        float opacity = smoothstep(0.01, 0.1, abs(val) + gradLen * 0.1);
        if (isWall) {
            color = bg;
        }
        else {
            color = mix(bg, porcelain * shadow, opacity);
        }
    }
    // === Mode 10: Clay / Terracotta Effect ===
    else if (mode == 10) {
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.5));
        vec3 lightDir = normalize(vec3(-0.7, 0.3, 0.8));
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 clayBase = vec3(0.72, 0.35, 0.22);
        vec3 clayDark = vec3(0.45, 0.20, 0.12);
        float val = p * visualGain;
        vec3 albedo = mix(clayDark, clayBase, 0.5 + 0.5 * clamp(val, -1.0, 1.0));
        float diff = max(dot(normal, lightDir), 0.0);
        float ao = 1.0 - smoothstep(0.0, 4.0, gradLen) * 0.6;
        float rim = 1.0 - max(dot(normal, viewDir), 0.0);
        rim = smoothstep(0.6, 1.0, rim) * 0.3;
        vec3 clayFinal = albedo * (0.3 + 0.7 * diff) * ao;
        clayFinal += vec3(0.8, 0.7, 0.6) * rim;
        float mossFactor = smoothstep(0.1, 0.9, normal.z);
        mossFactor *= smoothstep(0.3, 0.7, val);
        vec3 mossColor = vec3(0.3, 0.5, 0.1);
        clayFinal = mix(clayFinal, mossColor, mossFactor * 1.0);
        float opacity = smoothstep(0.02, 0.15, abs(val));
        if (isWall) {
            color = bg;
        }
        else {
            color = mix(bg, clayFinal, opacity);
        }
    }
    // === Mode 11: PBR Gold Effect ===
    else if (mode == 11) {
        vec2 uv = TexCoord;
        vec3 perturbedGrad = vec3(grad.x + sin(uv.y * 25.0 + time * 1.5) * 0.01, grad.y, 0.0);
        vec3 normal = normalize(vec3(-perturbedGrad.x, -perturbedGrad.y, 1.0));
        vec3 lightDir = normalize(vec3(0.5, 0.5, 0.5));
        vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0));
        vec3 goldF0 = vec3(1.0, 0.843, 0.0);
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfwayDir), 0.0);
        float spec = pow(specAngle, 256.0);
        vec3 specular = goldF0 * spec;
        vec3 reflectDir = reflect(-viewDir, normal);
        vec3 reflectionColor = getEnvironmentColor(reflectDir);
        vec3 fresnel = goldF0 + (vec3(1.0) - goldF0) * pow(1.0 - max(dot(viewDir, normal), 0.0), 5.0);
        vec3 goldFinal = reflectionColor * fresnel + specular;
        float val = p * visualGain;
        float opacity = smoothstep(0.0, 0.05, abs(val));
        if (isWall) {
            color = bg;
        }
        else {
            color = mix(bg, goldFinal, opacity);
        }
    }
    // === Mode 12: Hologram Effect ===
    else if (mode == 12) {
        vec2 uv = TexCoord;
        float breath = sin(time * 2.0) * 0.1;
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 1.0 + breath));
        vec3 viewDir = normalize(vec3(0.0, 0.0, 1.0));
        float rim = pow(1.0 - max(dot(viewDir, normal), 0.0), 2.0);
        float scanline = sin(uv.y * 300.0 - time * 4.0);
        scanline = smoothstep(0.9, 1.0, scanline);
        float flicker = sin(time * 30.0) * 0.1 + 0.9;
        float pulse = sin(time * 3.0) * 0.2 + 0.8;
        vec3 hologramColor = vec3(0.2, 0.7, 1.0);
        vec3 emissive = hologramColor * rim * pulse * flicker;
        emissive += hologramColor * scanline * 0.3;
        float alpha = rim * 0.8 + scanline * 0.5;
        alpha *= pulse;
        color = mix(bg, emissive, alpha);
        if (isWall) {
            color = bg;
        }
    }
    // === Mode 13: Wet Clay / Terracotta Effect ===
    else if (mode == 13) {
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.5));
        vec3 lightDir = normalize(vec3(-0.7, 0.3, 0.8));
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 clayBase = colClayBase;
        vec3 clayDark = colClayDark;
        float val = p * visualGain;
        vec3 albedo = mix(clayDark, clayBase, 0.5 + 0.5 * clamp(val, -1.0, 1.0));
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfwayDir), 0.0);
        float specPower = mix(8.0, 64.0, wetness);
        float spec = pow(specAngle, specPower) * wetness;
        float ao = 1.0 - smoothstep(0.0, 4.0, gradLen) * 0.6;
        float rim = 1.0 - max(dot(normal, viewDir), 0.0);
        rim = smoothstep(0.6, 1.0, rim) * 0.3;
        vec3 clayFinal = albedo * (0.3 + 0.7 * diff) * ao;
        clayFinal += vec3(0.8, 0.7, 0.6) * rim;
        clayFinal += vec3(1.0) * spec * (0.5 + wetness * 0.5);
        float mossFactor = smoothstep(0.1, 0.9, normal.z);
        mossFactor *= smoothstep(0.3, 0.7, val);
        vec3 mossColor = colmossColor;
        clayFinal = mix(clayFinal, mossColor, mossFactor * 1.0);
        float opacity = smoothstep(0.02, 0.15, abs(val));
        if (isWall) {
            color = bg;
        }
        else {
            color = mix(bg, clayFinal, opacity);
        }
    }

    // --- STEP 4: UI & BOUNDARY OVERLAYS ---
    // Draw visual indicators for boundaries, probes, and the mouse cursor on top.
    vec2 gridPos = TexCoord * gridSize;
    float dw = float(dampWidth);
    float dx = min(gridPos.x, gridSize.x - gridPos.x);
    float dy = min(gridPos.y, gridSize.y - gridPos.y);
    float dist = min(dx, dy);
    // 0:Absorb, 1:Reflect, 2:Hide

    vec3 AbsorbColor = colAbsorbColor;//vec3(1.0, 0.5, 0.0)
    // 1. 吸收边界可视化 (Mode 0: Absorb)
    if (boundaryMode == 0 && dist < dw) {
        // 逻辑修正：如果开启了自由表面 (freeSurface=1)，且当前像素位于顶部区域 (gridPos.y < dw)，
        // 则【不】绘制橙色吸波条纹，因为物理上顶部已经不是吸波边界了。
        bool isTopArea = (gridPos.y < dw);
        if (!(freeSurface == 1 && isTopArea)) {
            float s = techStripe(TexCoord);
            color += AbsorbColor * s * 1.0; // 叠加橙色科技感条纹
            // 绘制阻尼层内侧的白色边框线
            if (abs(dist - dw) < 1.0) {
                color = mix(color, vec3(1.0), 0.8);
            }
        }
    }
    // 2. 自由表面可视化 (顶部高亮白线)
    // 放在后面绘制，确保它能覆盖在其他颜色之上
    if (freeSurface == 1 && gridPos.y < 4.0) {
        color = vec3(1.0); // 纯白线，表示空气/水界面
    }
    
    // --- Probe Visualization: Tech Focus Box ---
    vec2 pxPos = TexCoord * resolution;
    vec2 probePx = probePos * resolution;
    vec2 diff = abs(pxPos - probePx); // 取绝对值方便计算矩形

    // 1. 计算矩形距离 (切比雪夫距离)
    float boxDist = max(diff.x, diff.y);

    // 2. 绘制方框线条 (边长 10px)
    float box = smoothstep(1.5, 0.0, abs(boxDist - 10.0));

    // 3. 切角处理 (Corner Brackets)
    // 这里我们做一个缺口的方框：
    float gapMask = step(0.0, abs(diff.x - diff.y) - 3.0);
    // 原理：在对角线附近 (diff.x 接近 diff.y) 保留，形成四个角
    float brackets = box * (1.0 - gapMask);

    // 4. 中心十字准星 (非实心点)
    float cross = (smoothstep(1.0, 0.0, diff.x) + smoothstep(1.0, 0.0, diff.y));
    // 限制十字长度
    cross *= step(boxDist, 4.0);

    // 5. 合成
    float probeShape = max(brackets, cross);
    vec3 probeColor = vec3(1.0, 0.8, 0.2); // 金黄色/琥珀色
    

    color = mix(color, probeColor, probeShape);

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
                color = mix(color, cursorCol, 0.6 * (1.0 - abs(pxPos.x - mousePx.x) / 200.0));
            }
        }
        if (brushRadius < 10.0 && dMouse < 4.0) color = mix(color, cursorCol, 0.8);
    }

    // --- STEP 5: FINAL OUTPUT ---
    FragColor = vec4(color, 1.0);
}