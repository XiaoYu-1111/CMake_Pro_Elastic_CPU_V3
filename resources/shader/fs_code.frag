
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

// === 输入资源 ===
uniform sampler2D velTexture;
uniform sampler2D modelTex;

// === 控制参数 ===
uniform float scale;
uniform vec2 simResolution;
uniform float dampWidth;
uniform float time;

// === 样式选择 ===
// 0=None, 1=Geo, 2=Grey, 3=Wall
uniform int modelStyle; 
// 0=Magma, 1=RedBlue, 2=Liquid, 3=Coolwarm, 4=Gold, 5=Hologram, 6=Clay, 7=Porcelain, 8=ThinFilm, 9=InkWash
uniform int waveStyle;

// ------------------------------------------------------------------
// 随机函数 (用于水墨纸张纹理)
// ------------------------------------------------------------------
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453123);
}

// ------------------------------------------------------------------
// 辅助配色
// ------------------------------------------------------------------
vec3 getCoolWarmColor(float t) {
    vec3 c0 = vec3(0.23, 0.299, 0.754);
    vec3 c1 = vec3(0.865, 0.865, 0.865);
    vec3 c2 = vec3(0.706, 0.016, 0.150);
    if(t < 0.0) return mix(c1, c0, -t);
    else return mix(c1, c2, t);
}

// ------------------------------------------------------------------
// 1. 背景模型渲染
// ------------------------------------------------------------------
vec3 getModelColor(vec2 uv) {
    // 特殊处理：水墨模式强制使用白纸背景
    if (waveStyle == 9) { // 9 = InkWash
        float grain = random(uv * 1.5) * 0.05;
        return vec3(0.95, 0.95, 0.92) - grain; // 暖白纸张 + 噪点
    }

    float rho = texture(modelTex, uv).r;
    float t = clamp((rho - 2000.0) / 3000.0, 0.0, 1.0);
    bool isWall = rho > 4000.0;

    if (modelStyle == 0) return vec3(0.02, 0.02, 0.05);

    if (modelStyle == 1) { // Geological
        vec3 baseColor;
        if (isWall) {
            float stripe = step(0.5, fract((gl_FragCoord.x - gl_FragCoord.y) * 0.05));
            baseColor = mix(vec3(0.15, 0.16, 0.18), vec3(0.20, 0.21, 0.23), stripe);
        } else {
            vec3 cWater = vec3(0.0, 0.25, 0.5);
            vec3 cSed = vec3(0.3, 0.6, 0.45);
            vec3 cRock = vec3(0.8, 0.7, 0.3);
            vec3 cHard = vec3(0.7, 0.2, 0.15);
            if (t < 0.33) baseColor = mix(cWater, cSed, t * 3.0);
            else if (t < 0.66) baseColor = mix(cSed, cRock, (t - 0.33) * 3.0);
            else baseColor = mix(cRock, cHard, (t - 0.66) * 3.0);
            
            float contours = 0.5 + 0.5 * sin(rho * 0.05);
            baseColor = mix(baseColor, vec3(1.0), smoothstep(0.95, 1.0, contours) * 0.15);
        }
        return baseColor;
    }
    
    if (modelStyle == 2) return vec3(t * 0.8 + 0.1);
    if (modelStyle == 3) return isWall ? vec3(0.6) : vec3(0.1);
    return vec3(0.0);
}

// ------------------------------------------------------------------
// 2. 波场特效渲染
// outMode: 0=AlphaMix, 1=Additive, 2=InkMultiply
// ------------------------------------------------------------------
vec4 getWaveColor(vec2 grad, float gradLen, float p, out int outMode) {
    float visualGain = scale * 0.5;
    float val = p * visualGain;
    float absVal = abs(val);
    outMode = 0; // Default Alpha Blend

    // === Style 0: Magma (Energy) ===
    if (waveStyle == 0) {
        outMode = 1; // Additive Glow
        float energy = 1.0 - exp(-absVal * 2.5);
        vec3 c0=vec3(0.02,0.02,0.05); vec3 c1=vec3(0.25,0.05,0.35);
        vec3 c2=vec3(0.70,0.15,0.40); vec3 c3=vec3(0.95,0.50,0.15); vec3 c4=vec3(1.0);
        vec3 col;
        if(energy<0.25) col=mix(c0,c1,energy*4.0);
        else if(energy<0.5) col=mix(c1,c2,(energy-0.25)*4.0);
        else if(energy<0.75) col=mix(c2,c3,(energy-0.5)*4.0);
        else col=mix(c3,c4,(energy-0.75)*4.0);
        return vec4(col, smoothstep(0.05, 0.4, energy));
    }

    // === Style 1: Red/Blue ===
    if (waveStyle == 1) {
        float amp = clamp(val, -1.0, 1.0);
        vec3 red=vec3(1.0,0.2,0.2); vec3 blue=vec3(0.0,0.4,1.0); vec3 white=vec3(1.0);
        vec3 col = (amp>0.0) ? mix(white,red,amp) : mix(white,blue,-amp);
        return vec4(col, smoothstep(0.02, 0.6, abs(amp)));
    }

    // === Style 2: Liquid 3D ===
    if (waveStyle == 2) {
        // Z=4.0 模拟液体表面张力
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 4.0));
        vec3 lightDir = normalize(vec3(-0.5, 0.5, 1.0));
        vec3 halfDir = normalize(lightDir + vec3(0,0,1));
        
        vec3 deep = vec3(0.0, 0.3, 0.7);
        vec3 shallow = vec3(0.5, 0.8, 1.0);
        vec3 tint = (val > 0.0) ? shallow : deep;
        
        float spec = pow(max(dot(normal, halfDir), 0.0), 80.0);
        float fresnel = pow(1.0 - max(dot(normal, vec3(0,0,1)), 0.0), 3.0);
        
        vec3 col = tint * 0.5 + vec3(1.0)*spec + vec3(0.8)*fresnel;
        
        // 色散微调
        if (absVal > 0.5) col += vec3(0.1, 0.0, -0.1) * fresnel * absVal;
        
        return vec4(col, smoothstep(0.05, 0.8, absVal));
    }

    // === Style 3: Coolwarm (Relief) ===
    if (waveStyle == 3) {
        float v = clamp(val * 1.5, -1.0, 1.0);
        vec3 base = getCoolWarmColor(v);
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.5));
        vec3 light = normalize(vec3(-0.8, 0.8, 1.0));
        
        float diff = max(dot(normal, light), 0.0);
        float spec = pow(max(dot(normal, normalize(light+vec3(0,0,1))), 0.0), 32.0);
        
        vec3 col = base * (0.6 + 0.4*diff) + vec3(1.0)*spec*0.5*abs(v);
        return vec4(col, smoothstep(0.02, 0.2, abs(v)));
    }

    // === Style 4: Gold (PBR) ===
    if (waveStyle == 4) {
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.8));
        vec3 lightDir = normalize(vec3(0.5, 0.5, 0.5));
        vec3 viewDir = vec3(0,0,1);
        vec3 goldF0 = vec3(1.0, 0.843, 0.0);
        
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);
        vec3 fresnel = goldF0 + (vec3(1.0)-goldF0) * pow(1.0 - max(dot(viewDir, normal), 0.0), 5.0);
        
        vec3 col = fresnel * 0.3 + goldF0 * spec * 1.5;
        return vec4(col, smoothstep(0.02, 0.15, absVal));
    }

    // === Style 5: Hologram ===
    if (waveStyle == 5) {
        outMode = 1; // Additive
        float breath = sin(time * 2.0) * 0.1;
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 1.5 + breath));
        float rim = pow(1.0 - max(dot(vec3(0,0,1), normal), 0.0), 3.0);
        float scanline = smoothstep(0.9, 1.0, sin(gl_FragCoord.y * 0.5 - time * 5.0));
        
        vec3 holoCol = vec3(0.1, 0.8, 1.0);
        vec3 emissive = holoCol * rim * 2.0;
        emissive += holoCol * scanline * 0.5;
        
        return vec4(emissive, smoothstep(0.0, 0.1, absVal + gradLen*0.5));
    }

    // === Style 6: Clay ===
    if (waveStyle == 6) {
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 0.4));
        vec3 light = normalize(vec3(-0.7, 0.3, 0.8));
        vec3 clay = vec3(0.8, 0.4, 0.25);
        vec3 dark = vec3(0.5, 0.25, 0.15);
        
        vec3 albedo = mix(dark, clay, 0.5 + 0.5 * clamp(val, -1.0, 1.0));
        float diff = max(dot(normal, light), 0.0);
        float ao = 1.0 - smoothstep(0.0, 3.0, gradLen) * 0.5;
        
        vec3 col = albedo * (0.4 + 0.6*diff) * ao;
        return vec4(col, smoothstep(0.02, 0.2, absVal));
    }

    // === Style 7: Porcelain ===
    if (waveStyle == 7) {
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 1.5));
        vec3 light = normalize(vec3(-0.5, 0.5, 1.0));
        vec3 base = vec3(0.96, 0.97, 0.98);
        vec3 ink = vec3(0.05, 0.15, 0.55);
        
        float pattern = smoothstep(0.1, 0.4, absVal);
        vec3 pigment = mix(base, ink, pattern);
        
        float diff = max(dot(normal, light), 0.0);
        float spec = pow(max(dot(normal, normalize(light+vec3(0,0,1))), 0.0), 64.0);
        
        vec3 col = pigment * (0.5 + 0.5*diff) + vec3(1.0)*spec*0.8;
        return vec4(col, smoothstep(0.02, 0.15, absVal));
    }

    // === Style 8: Thin-Film (Pearlescent) [还原] ===
    if (waveStyle == 8) {
        float film_thickness = 3.0; 
        vec3 normal = normalize(vec3(-grad.x, -grad.y, 2.0)); // 丝绸般的起伏
        vec3 viewDir = vec3(0.0, 0.0, 1.0);
        vec3 lightDir = normalize(vec3(-1.0, 1.0, 1.5));
        vec3 halfDir = normalize(lightDir + viewDir);

        float NdotV = clamp(dot(normal, viewDir), 0.0, 1.0);
        // 干涉色计算
        vec3 iridescence = vec3(0.5) + 0.5 * cos(6.28318 * (vec3(1.0) * NdotV * film_thickness + vec3(0.0, 0.10, 0.20)));

        float fresnel = 0.04 + 0.96 * pow(1.0 - NdotV, 5.0);
        float spec = pow(max(dot(normal, halfDir), 0.0), 64.0);

        // 珠光层 + 高光
        vec3 col = iridescence * fresnel * 1.5 + vec3(1.0) * spec;
        return vec4(col, smoothstep(0.0, 0.1, absVal + gradLen * 0.2));
    }

    // === Style 9: Ink Wash (Black & White) [还原] ===
    if (waveStyle == 9) {
        outMode = 2; // Ink Multiply Mode
        
        // 轮廓线 (Contour Lines)
        float d_grad_x = dFdx(gradLen);
        float d_grad_y = dFdy(gradLen);
        float edge = sqrt(d_grad_x * d_grad_x + d_grad_y * d_grad_y);
        float edge_line = smoothstep(0.05, 0.2, edge);
        
        float zero_crossing = (1.0 - smoothstep(0.0, 0.1, absVal)) * smoothstep(0.2, 1.0, gradLen);

        // 水墨晕染
        float inkWash = smoothstep(0.0, 0.8, -val) * 0.7; // 负压晕染
        float highlight = smoothstep(0.0, 0.8, val) * 0.5; // 正压留白
        
        // 这里的 RGB 代表“墨的浓度”，0=无墨，1=最黑
        float inkDensity = inkWash + edge_line * 0.8 + zero_crossing * 0.8 - highlight;
        inkDensity = clamp(inkDensity, 0.0, 1.0);

        return vec4(vec3(0.0), inkDensity); // 颜色为黑，Alpha为浓度
    }

    return vec4(0.0);
}

// ------------------------------------------------------------------
// 主函数
// ------------------------------------------------------------------
void main() {
    // 1. 读取基础数据 & 计算物理量
    vec4 velData = texture(velTexture, TexCoord);
    float p = length(velData.rg); 
    if (waveStyle == 1 || waveStyle == 3 || waveStyle == 9) p = velData.r + velData.g; 
    
    // 增强梯度计算，用于法线
    vec2 grad = vec2(dFdx(p), dFdy(p)) * 50.0;
    float gradLen = length(grad);

    // ======================================================
    // 2. 核心互动逻辑：折射 UV
    // ======================================================
    float refractionStrength = 0.005; 
    if (waveStyle == 2) refractionStrength = 0.015; // 水: 强折射
    if (waveStyle == 8) refractionStrength = 0.008; // 薄膜: 中折射
    if (waveStyle == 5 || waveStyle == 9) refractionStrength = 0.0; // 全息/水墨: 无折射
    
    vec2 distortedUV = TexCoord - grad * refractionStrength;
    
    // 3. 读取背景
    vec3 bgCol = getModelColor(distortedUV);

    // 4. 吸收边界可视化
    if (waveStyle != 9) { // 水墨模式不画边界网格，保持纸张干净
        vec2 pixelPos = TexCoord * simResolution;
        float dist = min(min(pixelPos.x, simResolution.x - pixelPos.x), 
                         min(pixelPos.y, simResolution.y - pixelPos.y));
        if (dist < dampWidth) {
            float stripe = step(0.5, fract((pixelPos.x + pixelPos.y) / 10.0));
            bgCol = mix(bgCol, vec3(0.15), 0.4 + 0.1 * stripe);
        } else {
            // 体积阴影 (Volume Shadow) - 让波浪有厚度感
            if (waveStyle != 5) { 
                 bgCol *= (1.0 - smoothstep(0.0, 1.0, abs(p * scale)) * 0.2);
            }
        }
    }

    // 5. 获取波场前景
    int blendMode = 0;
    vec4 waveInfo = getWaveColor(grad, gradLen, p, blendMode);

    // 6. 最终混合
    vec3 finalColor;
    if (blendMode == 1) {
        // [Additive] 发光模式 (Magma, Hologram)
        finalColor = bgCol * 0.8 + waveInfo.rgb * waveInfo.a;
    } 
    else if (blendMode == 2) {
        // [Ink Multiply] 水墨模式
        // bgCol 是纸张(白)，waveInfo.a 是墨浓度(黑)
        // 墨越浓，颜色越接近黑色
        finalColor = mix(bgCol, vec3(0.05, 0.05, 0.05), waveInfo.a);
    }
    else {
        // [Alpha Blend] 实体模式 (Liquid, Clay, etc.)
        finalColor = mix(bgCol, waveInfo.rgb, waveInfo.a);
    }

    FragColor = vec4(finalColor, 1.0);
}
