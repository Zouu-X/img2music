# V2M 图片转音乐装置 v0.2.0

更新日期：2026-10-30。

在 ESP32-S3 上拍一张照片，根据图像特征生成旋律并通过喇叭播放，屏幕显示这张照片、中文状态和播放进度。项目名暂用 V2M，不预设英文全称。

## 当前状态

- V2M **v0.2.0** 已编译、烧录并启动；实物为 **GOOUUU ESP32-S3-CAM V1.5** 主板、**S3-CAM-DevPro V2.0** 底板和 ST7789 240×320 屏幕。
- 商家资料已归档，摄像头和音频引脚已核对并启用。实机识别为 ESP32-S3 rev v0.2、16 MB Flash、8 MB PSRAM；固件实际识别到摄像头 **OV3660（PID 0x3660）**。
- v0.2.0 连续两轮串口拍照、显示照片、生成 16 音符和播放均完成；照片绘制各用 52 ms，两轮结束时空闲内存读数一致。**用户已确认“照片、状态和声音都正常”。**
- BOOT 用法保持不变：v0.1 已实按触发拍照并完整播放，用户确认按下后自动播放；本次 v0.2.0 两轮测试由串口触发，未新增实按测试。
- 完整出厂固件已备份并通过设备摘要校验。本次仅替换原应用分区，保留出厂启动程序、分区表和数据区。
- 麦克风、Wi-Fi、SD 和电池管理未加入本版。

详细证据和验证边界见 [验证记录](docs/VALIDATION.md)。

## 使用最小版本

1. 断开 USB 电源。先把 ESP32 主板两排针脚插入扩展底板两排长插座：镜头朝外，主板双 USB 口朝底板“电池”插座/开关那端，主板天线朝“喇叭”插座那端；两排针脚均逐一对齐，不错开一格。装配见[商家视频](vendor/goouuu-s3-cam/hardware/CAM扩展板套件安装视频.mp4)。
2. 将喇叭接到底板标有“喇叭”的白色插座，不要接到“电池”插座。仍在断电状态下连接屏幕，屏幕 **GND 对齐底板 GND**，整排引脚逐一对齐。
3. 用能传数据的 USB 线，把主板 **TTL** USB-C 口连接电脑。等屏幕显示等待拍照后，短按 **BOOT**：屏幕更新照片，然后播放旋律并显示进度。不要在上电或复位时按住 BOOT，那是下载模式入口。
4. 播放完成后保留刚才的照片。将镜头换向不同画面，再次拍照比较旋律。播放期间的拍照请求会被忽略并报告忙碌。

串口 115200：`c` 拍照并播放、`p` 重播上一段、`x` 停止、`s` 查看状态；`j` 导出 JPEG，由下方工具接收。BOOT 在 v0.1 已实按验证；`p` 重播和 `x` 停止已实现，尚未单独上板测试。

**确认 USB 插对口：**本版通过 CH340 转接串口通信，电脑应显示 `USB Serial` / CH340，本次端口为 `/dev/cu.usbserial-110`。如果显示 Espressif `USB JTAG/serial`、端口类似 `/dev/cu.usbmodem1101`，说明接到了主板的原生 USB 口；把线换到另一个 USB-C 口。本版的串口日志和控制命令使用 CH340 这一路，不能只根据插口旁文字的观察判断。完整设备标识见 [硬件记录](HARDWARE.md)。

## 当前实现

- 相机：QVGA JPEG 单张拍照，丢弃旧帧后获取新图，解码为 RGB888；出错时报告原因并归还帧缓冲。
- 作曲：平均颜色决定根音，亮度决定大小调，亮度与简单边缘决定 60–96 BPM；从左到右分成 16 个竖条，用每条亮部纵向重心生成音阶内旋律。
- 合成：22050 Hz、16 位双声道（左右相同），256 帧一块；正弦加少量谐波及淡入淡出包络。波形幅度上限为满幅的 17.5%，不代表已经测量喇叭响度。一段约 5–8 秒，不分配整段音频。
- 屏幕：竖向 240×320；把参与作曲的同一张 320×240 图片缩放为 240×180，不裁切。中文显示等待、拍照、播放、完成等状态，播放中每 250 ms 更新小段进度条，结束时补齐并保留照片。状态字样由 Noto Sans CJK SC 离线生成，字体许可及来源见 [assets/fonts](assets/fonts)，生成工具为 `tools/generate_ui_assets.py`。
- 本版使用原创简化规则，不要求与参考项目完全同音。低音、更多音色和电池供电留待后续；完整二维 FFT、复杂显著性图、大型 SoundFont 和连续视频实时作曲不在本版范围内。

代码在 [firmware/v2m](firmware/v2m)，引脚统一在 [board_config.h](firmware/v2m/board_config.h)。

## 构建、烧录与检查

本机复用 `/Users/zoe/Projects/seirei/.local` 内的 Arduino ESP32 **3.3.10**、esptool **4.12.0** 和 Python 环境，不修改旧工程。本项目缓存与产物位于 `.local/`、`build/`。`ARDUINO`、`ARDUINO_DATA` 可覆盖编译工具路径，`FLASH_PYTHON`、`FLASH_PORT` 可覆盖烧录工具和串口。

```sh
./tools/test.sh
./tools/build.sh
./tools/flash.sh
```

烧录脚本会重新编译，核对设备 MAC、完整备份 SHA-256、设备分区和构建分区，确认应用不超过 **1 MiB**，然后仅写入 `0x10000` 并独立校验。它只用于当前记录的这块板，不能直接用于其他设备。具体布局见 [partitions.csv](firmware/v2m/partitions.csv)。

v0.2.0 应用二进制为 440,064 字节，校验值见[验证记录](docs/VALIDATION.md)。上一版应用另存于 `build/releases/v2m-v0.1.0.bin`，这是应用备份，不是完整 Flash 镜像；完整出厂备份保持不变。

串口工具不要与烧录或备份同时运行：

```sh
# 查看状态；默认串口 /dev/cu.usbserial-110，重新插拔后可能变化
/Users/zoe/Projects/seirei/.local/venv/bin/python tools/monitor.py --send s --seconds 3
# 串口触发一轮拍照和播放，并保存日志
/Users/zoe/Projects/seirei/.local/venv/bin/python tools/monitor.py --send c --seconds 12 --out build/play.log
# 导出一张新照片供查看
/Users/zoe/Projects/seirei/.local/venv/bin/python tools/capture.py --out build/camera.jpg
# 连续两轮检查拍照、照片传输和播放（实际画面、听感需现场确认）
/Users/zoe/Projects/seirei/.local/venv/bin/python tools/device_check.py --cycles 2 --require-display --out build/device-check.log
```

## 文档与后续验证

- [硬件记录与接口约束](HARDWARE.md)
- [商家资料索引](vendor/goouuu-s3-cam/README.md)
- [验证记录、备份和已知限制](docs/VALIDATION.md)
- [参考项目和移植方案](RESEARCH.md)
- [到货实物照片](assets/IMG_9927.JPG)

后续可比较更多画面并调整旋律规则。v0.2.0 已完成两轮照片、屏幕和声音流程，但长期稳定性、重播与停止操作及颜色校准仍待验证；画面和声音正常来自用户现场反馈，未做色彩、响度或音质仪器测量。麦克风和电池仍未验证。

此前商品图 IMG_9913.JPG、IMG_9915.JPG 的文字信息已整理，原文件当前不在此前提供的 Downloads 路径中，未归档。
