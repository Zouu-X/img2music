# V2M 验证记录

日期：2026-10-30。当前固件 **v0.2.0** 已完成：实拍 → 同一张照片显示并作曲 → 显示播放状态及进度 → 喇叭播放。屏幕版连续两轮串口测试通过，用户确认“照片、状态和声音都正常”。早先 v0.1 的三轮串口与真实 BOOT 测试保留在下面，未混作 v0.2 的按键测试。

## v0.2.0 屏幕版

- 屏幕由用户按 GND 对齐插入；采用商家配置的 ST7789、240×320 竖屏、RGB、反色开启、无镜像或 XY 交换、偏移 0。SPI3 20 MHz；背光 GPIO38 直接控制，不占用相机的 LEDC 计时器。
- 照片取自作曲使用的同一个 RGB888 缓冲，缩放为 240×180 保留完整比例。播放结束后保留照片；`j` 诊断导出不会替换屏幕上与旋律对应的照片。
- 中文状态为正在启动、等待拍照、正在拍照、正在播放、播放完成、已停止及操作失败。播放期间最多每 250 ms 更新一次小进度条；进度依据已提交音频帧，可能领先实际声音约一个 DMA 队列的长度，不作为精确音频时间戳。
- 显存 153,600 字节放在 PSRAM；7,680 字节 SPI DMA 暂存区放在内部内存，每批传输完成后才复用。异常时关闭显示、保留可能仍在使用的资源；不阻断相机和音频主流程。
- 固定文字采用 Noto Sans CJK SC Regular（OFL 1.1），十条文字位图共 2,613 字节；来源、许可和可重复生成脚本在 `assets/fonts/`、`tools/generate_ui_assets.py`。

构建 `build/compile-v02.log`：代码 439,909 字节、静态内存 38,196 字节；应用二进制 440,064 字节，仍低于保留的 1 MiB 应用分区。`build/flash-v02.log` 记录仅更新应用并独立摘要校验通过。v0.2.0 SHA-256：

```text
3296dc69adedb21fb7add730559ba6fad56128019d5d01fe77711c38bd8e89c4
```

`build/boot-v02.log` 中屏幕、相机、音频均初始化成功，PSRAM 总量 8,388,608 字节。`DISPLAY ready` 只证明驱动与 SPI 传输工作，物理画面另由用户确认。

运行 `tools/device_check.py --cycles 2 --require-display --out build/display-play-test.log`：

| 轮次 | 拍照与分析 | 照片绘制 | 照片计数 / 播放完成计数 | 空闲堆 | 空闲 PSRAM |
| --- | --- | --- | --- | --- | --- |
| 1 | 613 ms | 52 ms | 1 / 1 | 297,788 B | 7,978,640 B |
| 2 | 594 ms | 52 ms | 2 / 2 | 297,788 B | 7,978,640 B |

两轮结束时均 `camera=1 audio=1 display=1 playing=0`，未见传输错误。用户在照片、中文播放状态、进度条及声音连续性的联合检查中回答“照片、状态和声音都正常”。这是两轮实机流程及现场观感反馈，不等同于长期稳定、客观音频测量或色彩校准。当前版本没有再单独做一次 BOOT 实按或 `p`/`x` 专项测试。

升级前的已验证 v0.1 应用保存于 `build/releases/v2m-v0.1.0.bin`；原始完整出厂备份仍保留，详见后文。

## v0.1 历史验证

首次测试时底板和喇叭尚未接到主板，只验证主板上的相机和 I2S 数据提交。后续按商家视频完成叠插并接好喇叭，再进行下述听音与按键测试。

## 软件与硬件配置

- 主板 GOOUUU ESP32-S3-CAM V1.5，底板 S3-CAM-DevPro V2.0；MAC `d8:85:ac:d4:7e:fc`，TTL 串口 `/dev/cu.usbserial-110`。
- 商家资料归档于 [vendor/goouuu-s3-cam](../vendor/goouuu-s3-cam/README.md)，17 个原样文件与清单 SHA-256、大小匹配。
- 相机、I2S 输出引脚已按商家原理图及配套源码核对，`board_config.h` 两个开关均为 `true`。v0.1 当时未启用屏幕；麦克风、Wi-Fi、SD 和电池管理在 v0.2 中也未启用。
- Arduino ESP32 3.3.10；实际本地 JPEG 解码路径使用 esp_jpeg 1.3.1、RGB888、`swap_color_bytes=0`，因此不额外交换红蓝通道。更换框架后需重新核对。

## 已通过的检查

| 检查 | 实际结果及证据 |
| --- | --- |
| 设备识别 | ESP32-S3 rev v0.2、16 MB Flash、8 MB 内置 PSRAM（AP_3v3）；`build/device-identify.log` |
| 出厂状态 | 原程序持续打印 UART/stdout/stderr 示例；`build/factory-passive.log` |
| 原生算法测试 | `./tools/test.sh` 通过严格编译警告、AddressSanitizer、UndefinedBehaviorSanitizer；核心代码之后未变更 |
| 完整配置构建 | 相机和音频启用后编译通过；代码占用 403,857 字节，静态内存 37,936 字节；`build/compile.log` |
| 烧录与校验 | 应用二进制 404,000 字节，写入 `0x10000` 后 `verify_flash` 报告摘要一致；`build/flash.log` |
| 正常启动 | `V2M 0.1.0`；应用检测 Flash 16,777,216、PSRAM 8,388,608 字节；`build/boot-v01.log` |
| 相机初始化 | PID `0x3660`，即 OV3660；JPEG 320×240；`camera=1` |
| 音频初始化 | 22050 Hz、16 位双声道 I2S；`audio=1`，只证明驱动初始化成功 |
| 实拍与传输 | `build/camera-first.jpg` 为真实 320×240 JPEG，3,029 字节；目视可见天花板画面。未做颜色或方向校准 |
| 一次串口拍照播放 | `capture_ms=590`，生成 16 音符、71 BPM，经历 `PLAY begin` → `PLAY complete`；`build/play-first.log` |

原生测试覆盖输入长度与尺寸边界、黑白及单色图、亮部上下位置与音高关系、音阶归属、分块一致性、精确时长、包络和幅度、重复开始及异常参数。

第一次串口流程的图像特征为亮度 `0.598`、饱和度 `0.030`、边缘 `0.002`，根音音级 `4`、大调；音符为 `68,66,66,66,64,64,64,63,63,63,63,64,63,63,63,63`。结束时 `completed=1`、空闲堆 308,620、空闲 PSRAM 8,134,600 字节。启动时空闲 PSRAM 同为 8,134,600；单次记录不足以证明长期无泄漏或稳定性。

该日志的触发来源明确为 `serial-test`，不计作真实 BOOT 按键测试。`PLAY complete` 表示应用已生成并向 I2S 提交整段 PCM；当时没有连接喇叭，不能据此声称听音成功。

## 完整装配后的测试

重新连接时曾识别到原生 USB `303a:1001`、`USB JTAG/serial debug unit`、`/dev/cu.usbmodem1101`。换到另一 USB-C 口后识别为 CH340 `1a86:7523`、`USB Serial`、`/dev/cu.usbserial-110`，恢复当前固件的 UART 日志与命令。

运行 `tools/device_check.py --cycles 3 --out build/device-check-speaker-connected.log`，三轮均有 `SONG`、`PLAY begin`、`PLAY complete`，完成计数依次为 1、2、3：

| 轮次 | 拍照与分析耗时 | BPM | 结束时空闲堆 | 结束时空闲 PSRAM |
| --- | --- | --- | --- | --- |
| 1 | 611 ms | 70 | 308,620 B | 8,134,600 B |
| 2 | 582 ms | 70 | 308,620 B | 8,134,600 B |
| 3 | 611 ms | 70 | 308,620 B | 8,134,600 B |

用户在这轮测试中明确确认喇叭“有旋律，声音正常”。这属于现场试听反馈，不是客观声学测量；三轮空闲内存一致也不等同于长期稳定性验证。

随后只监听、不发送拍照命令，用户更换画面并短按 BOOT。`build/button-test.log` 捕获到 `CAPTURE source=button`：第一段耗时 615 ms、亮度 0.432、饱和度 0.228、边缘 0.005、68 BPM、根音 1、大调；音符为 `66,61,58,56,56,58,61,63,66,65,63,63,65,65,66,68`。日志出现 `PLAY complete`、`completed=4`，空闲堆及 PSRAM 与三轮串口测试一致。用户确认“按下后自动播放了”。这次才计入真实按键验证。

监测窗口内还捕获第二次真实按键拍照：602 ms、亮度 0.325、66 BPM、根音 0、小调，并进入播放。播放中再次按键得到 `BUSY playing`，符合当前忽略忙碌期间拍照请求的设计，不会排队。

最终 `build/final-status.log` 显示 `camera=1 audio=1 playing=0 completed=11 heap=308620 psram=8134600`，设备已空闲。用户继续操作期间完成计数增加至 11，但没有为每轮保留完整日志，因此不将它表述为 11 轮完整自动化测试。不同场景输出不同特征与音符已观察到，但未做控制变量实验或图像颜色校准。

v0.1 应用 SHA-256：

```text
dc1171636900b18d784163b2de6475ff94ab3f8f9066b39be2295e9cd02bbfa9
```

`build/FIRMWARE-SHA256SUMS` 保存当前最新构建的摘要，旧版摘要归档为 `build/releases/v0.1.0-SHA256SUMS`。Arduino 自定义分区配置的编译摘要显示最大空间为 16 MB；实际应用上限为下述 **1 MiB**，已由构建与烧录脚本另行检查。

## 出厂备份与烧录边界

完整出厂 Flash 已读取，并通过独立 `verify_flash` 设备摘要比对（`verify OK (digest matched)`）。

- 文件：`backups/factory-d885acd47efc-20261030.bin`
- 长度：16,777,216 字节（16 MiB）。
- SHA-256：`cd0f30ac9491da499eaf2167f07b5accf23cf6779c506110431aa766f346273e`
- 元数据：`backups/factory-d885acd47efc-20261030.json`
- 读取波特率：460800，约 400.5 秒；921600 尝试未成功。
- 日志：`build/factory-backup.log`、`build/factory-verify.log`。

[partitions.csv](../firmware/v2m/partitions.csv) 保留出厂布局：

| 分区 | 起始地址 | 大小 |
| --- | --- | --- |
| nvs | `0x9000` | `0x6000` |
| phy_init | `0xf000` | `0x1000` |
| factory 应用 | `0x10000` | `0x100000`（1 MiB） |

`tools/flash.sh` 先编译，检查目标 MAC 和完整备份 SHA-256，读取设备分区并与备份、构建分区逐项比对，确认二进制不超过 1 MiB，才写入 `0x10000`。随后独立校验应用并复位。此次未改写启动程序、分区表、NVS 或 PHY 数据，未整片擦除。

出厂启动程序为 ESP-IDF `v4.4-dev-2594-ga20df743f1-dirty`，日志显示 Flash 2 MB；应用及 esptool 均检测 16 MB。当前应用保持在原 1 MiB 分区内，且实际启动成功。保留的分区表没有 core dump 分区，所以启动时出现 `No core dump partition found!`；本版不会将崩溃转储持久保存到 Flash。

备份实际恢复尚未演练。读取及摘要一致证明备份内容与当时设备一致，不能替代恢复验证。

## 接下来仍需验证

1. 更长时间、多场景的重复拍照播放，以及图像方向和颜色校准。
2. 串口重播 `p` 和停止 `x` 的专项行为验证；本轮重点是实际 BOOT 拍照播放。
3. 客观响度、音质和长时间运行测试。
4. 电池供电、显示关闭时恢复策略及相机/音频/屏幕更长时间共同运行。屏幕的基本照片、状态、进度显示已在 v0.2 两轮测试中通过。

可用命令与串口工具见 [项目 README](../README.md)。本记录不将单次流程成功解释为长期稳定、音质达标或所有硬件已验证。
