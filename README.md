# V2M 图片转音乐装置 v0.3.0

固件验证日期：2026-10-30。GitHub 接手资料更新：2026-11-01。

在 ESP32-S3 上拍一张照片，根据图像特征生成旋律并通过喇叭播放，屏幕显示这张照片、中文状态、扫描线和播放进度。项目名暂用 V2M，不预设英文全称。

## 从这里开始

这是一个可在 ESP32-S3 上离线运行的拍照音乐装置。朋友使用同款硬件时，先按指南连接，再让自己的 Agent 建立本机工具和设备备份。

- **给使用者：** [一步一步接线与体验](docs/GETTING_STARTED.md)
- **给接手的 Agent：** 先读 [AGENTS.md](AGENTS.md)，再读 [关键上下文](docs/AGENT_HANDOFF.md)
- **换一台电脑：** [安装、构建、新板备份与烧录](docs/SETUP.md)
- **固件附件：** [GitHub 发布页](https://github.com/Zouu-X/img2music/releases)；源码和文档以本仓库 `main` 为入口

已验证套件是 **GOOUUU ESP32-S3-CAM V1.5 + S3-CAM-DevPro V2.0 + ST7789 240×320 + 喇叭**。主板必须插到底板上；屏幕 GND 对齐；USB 用 CH340 / TTL 口。接线图和安装视频都在仓库内，新板不能照抄原作者的 MAC、串口或完整 Flash 备份。

可以直接把下面这段交给朋友的 Agent：

> 请克隆 https://github.com/Zouu-X/img2music.git，先读 AGENTS.md、docs/AGENT_HANDOFF.md 和 docs/GETTING_STARTED.md。我使用与项目相同的硬件，请逐步指导我接线、安装工具、备份我自己的板子、烧录并测试照片转音乐。不要照搬原作者的串口、设备编号或整片备份；请以实际识别为准。先跑通当前 v0.3.0，再讨论改动。

## 当前状态

- V2M **v0.3.0 已编译、烧录并通过板上串口控制测试**：五声音阶、四种合成音色、可切换鼓点、照片扫描线，以及底板第二按键控制。三轮完成播放与中途重播、停止测试通过，音频断供计数均为 0；用户已确认重播、鼓点切换和屏幕扫描线正常，能听出鼓点变化。
- 已验证的基线为 **v0.2.0**，原开发机提交 `d502d25`、公开标签 `v0.2.0`；实物为 **GOOUUU ESP32-S3-CAM V1.5** 主板、**S3-CAM-DevPro V2.0** 底板和 ST7789 240×320 屏幕。
- 商家资料已归档，摄像头和音频引脚已核对并启用。实机识别为 ESP32-S3 rev v0.2、16 MB Flash、8 MB PSRAM；固件实际识别到摄像头 **OV3660（PID 0x3660）**。
- v0.2.0 连续两轮串口拍照、显示照片、生成 16 音符和播放均完成；照片绘制各用 52 ms，两轮结束时空闲内存读数一致。**用户已确认“照片、状态和声音都正常”。**
- BOOT 用法保持不变：v0.1 已实按触发拍照并完整播放，用户确认按下后自动播放；v0.2.0 两轮测试由串口触发，未新增实按测试。v0.3.0 已记录一次实体拍照触发，用户另确认底板 BUTTON2 的重播、鼓点切换和扫描线正常。
- 完整出厂固件已备份并通过设备摘要校验。此前仅替换原应用分区，保留出厂启动程序、分区表和数据区；本轮继续使用相同烧录边界。
- 伴奏仅加入 **Drums**，默认关闭；本轮没有 Bass 或琶音伴奏。麦克风、Wi-Fi、SD 和电池管理未加入本版。

详细证据和验证边界见 [验证记录](docs/VALIDATION.md)。

## v0.3.0 使用方法

1. 断开 USB 电源。先把 ESP32 主板两排针脚插入扩展底板两排长插座：镜头朝外，主板双 USB 口朝底板“电池”插座/开关那端，主板天线朝“喇叭”插座那端；两排针脚均逐一对齐，不错开一格。装配见[商家视频](vendor/goouuu-s3-cam/hardware/CAM扩展板套件安装视频.mp4)。
2. 将喇叭接到底板标有“喇叭”的白色插座，不要接到“电池”插座。仍在断电状态下连接屏幕，屏幕 **GND 对齐底板 GND**，整排引脚逐一对齐。
3. 用能传数据的 USB 线，把主板 **TTL** USB-C 口连接电脑。等屏幕显示等待拍照后，短按主板 **BOOT** 或底板 **BUTTON1（打断/配网按键）**：屏幕更新照片，然后播放旋律，扫描线从左向右经过照片。两个按键共用 GPIO0；不要在上电或复位时按住，那是下载模式入口。
4. 短按底板 **BUTTON2（预留按键，GPIO46）**，从头重播上一张照片的音乐；播放中也可重播，不重新拍照。长按约 **650 ms** 切换鼓点，每次长按只切换一次。屏幕显示鼓点开关状态；默认关闭，播放中开启会在后续节拍处进入，关闭时平滑淡出。
5. 播放完成后保留刚才的照片。将镜头换向不同画面，再次拍照比较旋律。播放期间的拍照请求会被忽略并报告忙碌；重播和鼓点切换可用。

串口 115200：`c` 拍照并播放、`p` 从头重播（含播放中重启）、`d` 切换鼓点、`x` 停止、`s` 查看状态；`j` 导出 JPEG，由下方工具接收。没有拍照记录时，重播会提示先拍照。上述串口控制已通过上板测试，用户也已确认实体按键操作及鼓点变化正常。

**确认 USB 插对口：**本版通过 CH340 转接串口通信，电脑应显示 `USB Serial` / CH340，本次端口为 `/dev/cu.usbserial-110`。如果显示 Espressif `USB JTAG/serial`、端口类似 `/dev/cu.usbmodem1101`，说明接到了主板的原生 USB 口；把线换到另一个 USB-C 口。本版的串口日志和控制命令使用 CH340 这一路，不能只根据插口旁文字的观察判断。完整设备标识见 [硬件记录](HARDWARE.md)。

## 当前实现

- 相机：QVGA JPEG 单张拍照，丢弃旧帧后获取新图，解码为 RGB888；出错时报告原因并归还帧缓冲。
- 作曲：有效色相加权投票决定根音，亮度决定大调或小调五声音阶，亮度与简单边缘决定 60–96 BPM；从左到右分成 16 个竖条，用每条亮部纵向重心生成音阶内旋律。每条按颜色选择音色：红橙为 **Pluck**、黄绿为 **Keys**、青蓝为 **Flute**、紫粉为 **Bell**；低饱和度或近黑区域默认 Keys。
- 合成：22050 Hz、16 位双声道（左右相同），256 帧一块；不同谐波和包络形成四种音色，鼓点包含底鼓、军鼓和踩镲，与旋律共用节拍。旋律和鼓点分别预留混音余量，最终波形幅度限制为满幅的 17.5%，不代表已经测量喇叭响度。一段约 5–8 秒，不分配整段音频。
- 屏幕：竖向 240×320；把参与作曲的同一张 320×240 图片缩放为 240×180，不裁切。中文显示等待、拍照、播放、完成及鼓点状态，播放中约每 50 ms 更新扫描线与进度条，结束后保留照片。状态字样由 Noto Sans CJK SC 离线生成，字体许可及来源见 [assets/fonts](assets/fonts)，生成工具为 `tools/generate_ui_assets.py`。
- 播放同步：扫描线使用 I2S `on_sent` 回调累计的 DMA 发送帧数，不使用提前生成的音频帧数。每次开始或重播都先停音频、预填充 4×256 帧 DMA 缓冲，再从环首启动；若检测到供数不足则停止并显示错误，可重播恢复。用户已确认扫描线与播放过程正常，未进行声学时间差测量。
- 本版使用原创简化规则，不要求与参考项目完全同音。伴奏仅含 Drums，不含 Bass 或琶音；完整二维 FFT、复杂显著性图、大型 SoundFont 和连续视频实时作曲不在本版范围内。

代码在 [firmware/v2m](firmware/v2m)，引脚统一在 [board_config.h](firmware/v2m/board_config.h)。

## 构建、烧录与检查

依赖 Arduino ESP32 **3.3.10**、esptool **4.12.0**、Python / pyserial。按 [SETUP](docs/SETUP.md) 安装到当前电脑；构建脚本使用 `ARDUINO`、项目本地或 PATH 中的 CLI，`ARDUINO_DATA` 可覆盖平台目录。`FLASH_PYTHON`、`FLASH_PORT` 可覆盖解释器和实际串口。缓存、设备 profile、备份和构建产物留在 `.local/`、`build/`，不随 Git 克隆。

```sh
sh tools/test.sh
sh tools/build.sh
# 新板必须先完成 SETUP 中的 prepare_device.py --backup
sh tools/flash.sh
```

新板先用 `tools/prepare_device.py --port <实际串口> --backup` 备份并校验自己的 Flash，生成 `.local/device.json`。烧录脚本读取本机 profile，重新核对设备 MAC、完整备份 SHA-256、设备与构建分区，确认应用不超过 **1 MiB**，只写入 `0x10000` 并独立校验。不同设备或分区会拒绝烧录，不自动擦除或迁移分区。具体布局见 [partitions.csv](firmware/v2m/partitions.csv)。

已验证的 v0.2.0 应用二进制为 440,064 字节，保存在 `build/releases/v2m-v0.2.0.bin`，摘要文件为 `build/releases/v0.2.0-SHA256SUMS`；对应源码为提交 `d502d25` / 标签 `v0.2.0`。更早的应用保留在 `build/releases/v2m-v0.1.0.bin`。这些都是应用备份，不是完整 Flash 镜像；完整出厂备份保持不变。v0.3.0 应用为 446,000 字节，保存在 `build/releases/v2m-v0.3.0.bin`；构建、烧录及验证摘要见 [验证记录](docs/VALIDATION.md)。

以下历史 `build/` 路径在原开发机保留，新克隆不会包含它们；可从 [发布页](https://github.com/Zouu-X/img2music/releases) 取得附带摘要的应用固件。验证摘要已写入文档，历史日志不是克隆后必备输入。

串口工具不要与烧录或备份同时运行；下例先将 `V2M_PORT` 设为本机识别的端口：

```sh
# 先运行 python -m serial.tools.list_ports -v 查看端口
export V2M_PORT="<实际串口>"
# 查看状态
.local/venv/bin/python tools/monitor.py --port "$V2M_PORT" --send s --seconds 3
# 串口触发一轮拍照和播放，并保存日志
.local/venv/bin/python tools/monitor.py --port "$V2M_PORT" --send c --seconds 12 --out build/play.log
# 导出一张新照片供查看
.local/venv/bin/python tools/capture.py --port "$V2M_PORT" --out build/camera.jpg
# 连续两轮检查拍照、照片传输和播放（实际画面、听感需现场确认）
.local/venv/bin/python tools/device_check.py --port "$V2M_PORT" --cycles 2 --require-display --out build/device-check.log
# v0.3.0 串口检查：鼓点切换、重播、播放中重启、停止后重播
# 会拍一张照片并播放；不能替代 BUTTON2 实按及现场听音/扫线确认
.local/venv/bin/python tools/check_controls.py --port "$V2M_PORT" --out build/controls-check.log
```

## 文档与后续验证

- [来源与第三方材料](THIRD_PARTY_NOTICES.md)
- [硬件记录与接口约束](HARDWARE.md)
- [商家资料索引](vendor/goouuu-s3-cam/README.md)
- [验证记录、备份和已知限制](docs/VALIDATION.md)
- [参考项目和移植方案](RESEARCH.md)
- [到货实物照片](assets/IMG_9927.JPG)

v0.3.0 已完成本轮构建、烧录及串口控制检查，用户已实按并确认重播、鼓点切换与扫描线正常，能听出鼓点变化。四种音色各自对应特定实物颜色的系统听辨尚未逐项验证。`tools/test.sh` 包含离线音乐与按键测试；`tools/check_controls.py` 检查设备日志和控制流程，不能代替现场听音。v0.2.0 的两轮照片、屏幕和声音流程仍是上一版证据，不作为 v0.3.0 已上板的证明。长期稳定性、更多画面的音乐变化、颜色校准、麦克风和电池仍待验证；尚未做色彩、响度或音质仪器测量。

此前商品图 IMG_9913.JPG、IMG_9915.JPG 的文字信息已整理，原文件当前不在此前提供的 Downloads 路径中，未归档。
