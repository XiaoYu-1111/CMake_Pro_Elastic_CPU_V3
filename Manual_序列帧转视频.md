以下是整理好的 Markdown 格式文档。你可以直接复制保存为 `ffmpeg_commands.md` 或放入你的笔记软件中。

我已经修正了原指令中多余的空格（例如 `- framerate` 改为 `-framerate`），确保复制到终端即可直接运行。

***

# FFmpeg 图片序列转视频指令大全

> **注意**：
> 1. 请在包含图片的文件夹内打开终端（CMD/PowerShell）。
> 2. 确保图片命名格式为 `frame_00000.png` 这种连续数字（`%05d` 代表5位数字）。

## 1. CPU 编码 (通用/保底)
如果显卡驱动有问题，或者需要最极致的画质压缩比（速度慢），使用此模式。

```bash
# H.264 编码, 60帧/秒
ffmpeg -framerate 60 -i frame_%05d.png -c:v libx264 -pix_fmt yuv420p output.mp4
```

## 2. NVIDIA 显卡加速 (推荐 RTX 4060)

### 方案 A：H.264 编码 (最常用/兼容性最好)
适合发给别人看，所有电脑和手机都能播放。

**按码率控制 (推荐，文件大小可控):**
```bash
# 60帧, 10Mbps 码率 (高清)
ffmpeg -framerate 60 -i frame_%05d.png -c:v h264_nvenc -b:v 10M -pix_fmt yuv420p output.mp4
```

**按质量控制 (CQ):**
```bash
# 24帧, 恒定质量 (CQ 20, 数值越小画质越好)
ffmpeg -framerate 24 -i frame_%05d.png -c:v h264_nvenc -cq 20 -pix_fmt yuv420p output.mp4
```

### 方案 B：AV1 编码 (RTX 40系专属)
**体积最小，画质最好**。适合存档或在支持 AV1 的现代设备上播放。

```bash
# 60帧, 15Mbps 码率, 使用 av1_nvenc
ffmpeg -framerate 60 -i frame_%05d.png -c:v av1_nvenc -pix_fmt yuv420p -b:v 15M output_av1.mp4
```

---

## 3. 防报错版 (自动修复奇数分辨率)
如果遇到 `height not divisible by 2` 或 `unsupported param` 错误，请使用此指令。它会自动给奇数分辨率的图片补黑边。

```bash
ffmpeg -framerate 60 -i frame_%05d.png -vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" -c:v h264_nvenc -b:v 20M -pix_fmt yuv420p output_safe.mp4
```

---

## 参数解释表

| 参数 | 说明 |
| :--- | :--- |
| `-framerate 60` | 视频帧率（每秒播放多少张图）。 |
| `-i frame_%05d.png` | 输入文件匹配模式。`%05d` 匹配 `00000`, `00001`... |
| `-c:v h264_nvenc` | 视频编码器。`libx264` 是 CPU，`h264_nvenc` 是 N 卡。 |
| `-b:v 10M` | 目标码率。`10M` 代表 10Mbps。数值越大画质越好。 |
| `-pix_fmt yuv420p` | **关键参数**。将 PNG 的 RGB 颜色转为视频通用的 YUV420，防止黑屏。 |
| `-cq 20` | Constant Quality (恒定质量)。范围 1-51，20 左右是画质与体积的平衡点。 |