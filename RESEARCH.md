# 图片转音乐参考项目与移植判断

调研日期：2026-10-27 至 2026-10-30。

## 参考项目

[trungtin-dinh/photo_sonification](https://github.com/trungtin-dinh/photo_sonification)

调研时阅读了 main 分支源码。该项目是 Python 实现的图像声化应用，以 Pillow、NumPy 提取特征，以人工规则作曲，再合成声音。网页主入口使用 Streamlit，旧入口使用 Gradio。未使用图像语义识别或音乐生成大模型。

本记录是源码阅读结论，没有运行试听或进行性能测试。链接指向可变化的 main 分支。

## 图片如何决定音乐

### 全局特征

亮度由 RGB 加权求得：Y = 0.2126R + 0.7152G + 0.0722B。代码还计算对比度、亮暗部比例、边缘、梯度直方图熵、对称性及颜色特征。“dominant_hue” 实际是按饱和度和亮度加权的色相圆周均值，不能理解为识别图中主体。

- 色相映射到 12 个音名之一，亮度还会调整基础音高。
- 亮度、冷暖、饱和度和对比度通过阈值决定音阶。
- 边缘、对比度、高空间频率能量、周期性分数等参与计算 BPM。
- 暗部比例和低空间频率能量影响低音力度。
- 自动复杂度默认为 0.25 + 0.65 × 纹理熵。
- 自动变化幅度默认为 0.25 + 0.60 × (1 − 对称度)。用户参数可能覆盖自动值。

### 旋律

图像被划成“8 × 小节数”个竖条，从左到右对应时间。每条计算亮度统计和亮部纵向重心，重心靠上时基础音高偏高，靠下时偏低，再映射到音阶候选音符。

源码的基础位置公式：pos = clip(1 − y_centroid + 0.18 × (局部平均亮度 − 全局平均亮度), 0, 1)。随后还有分段音高偏移，因此不能声称最终所有音符都严格留在原音阶内。

每条原则上对应半拍；复杂度低时隔条取样，暗条可能跳过。局部对比度影响力度，局部亮度和高频能量影响音长。

### 傅里叶与显著性

二维 FFT 用来测量图像空间频率，不是直接把图片频谱播放成声音。低频反映缓慢变化的大结构，高频反映细节、边缘和纹理；提取的统计量控制音乐参数。

显著性由边缘、颜色偏离均值、亮度偏离均值和中心偏好计算。在 GeneralUser GS 模式的可选独奏中，显著点横坐标决定时间、纵坐标决定音高、强度影响力度。

### 伴奏和声音

和弦进行从预设集合选择，叠加主旋律、纹理、低音、持续和声、和弦及可选独奏。Simple 后端以波形叠加、衰减和 ADSR 包络合成音色；另一后端使用 FluidSynth 与 GeneralUser GS SoundFont。音符也可导出 MIDI，音频可导出 MP3。

这些对应是作者设计的规则，不是物理定律或学得的语义关系。视觉特征相似的不同题材可能产生相近音乐。

## ESP32 移植判断

核心算法可离线运行，没有必需的模型 API。默认示例图和部分界面资源来自网络，离线部署应改用本地资源。

电脑可运行 Python 版本；项目 v2.0.0 发布了 Windows 和 Linux x86_64 桌面包。这些二进制不能直接用于 ESP32 或 ARM 树莓派。

ESP32-S3 有摄像头和音频接口，适合作为简化版的候选平台；相机、作曲、音频同时运行的耗时和稳定性需要实测。树莓派的优势是更容易复用 Python 生态和完整音色后端，不是实现这项功能的唯一选择。

原代码默认分析最大边 512 像素，音频为 44.1 kHz 双声道，并分配整段 float64 数组。按 60 秒计算，单个音频数组约 42.3 MB，尚不包含图像、临时数组及运行时开销。因此 ESP32 版本应采用小图、较低精度和流式音频，而不是照搬原 Python 内存结构。

建议保留全局特征定风格、局部竖条定旋律的核心设计，先以 C/C++ 实现简单合成器。FFT 可在后续使用 ESP-DSP 评估，不应把“第一版省去 FFT”理解成芯片完全不能计算 FFT。

连续摄像头模式应将画面分析与音频播放分开调度，让音频按稳定时钟持续运行，特征平滑后在拍点或小节边界应用；不要每一帧重新开始整首音乐。这不属于目前第一版的必选功能。

## 直接来源

- [图像分析](https://github.com/trungtin-dinh/photo_sonification/blob/main/image_analysis.py)
- [规则作曲](https://github.com/trungtin-dinh/photo_sonification/blob/main/composition.py)
- [音频合成](https://github.com/trungtin-dinh/photo_sonification/blob/main/audio.py)
- [默认配置](https://github.com/trungtin-dinh/photo_sonification/blob/main/config.py)
- [依赖](https://github.com/trungtin-dinh/photo_sonification/blob/main/requirements.txt)
- [桌面版发布](https://github.com/trungtin-dinh/photo_sonification/releases/tag/v2.0.0)
- [ESP-DSP](https://github.com/espressif/esp-dsp)

参考项目声明采用 MIT 许可；若后续复用代码，应保留适用的版权及许可文本，音色库等附带资产需分别核对许可。
