# GOOUUU S3-CAM 商家资料索引

整理日期：2026-10-30。本目录保存本项目使用的硬件依据和少量参考源码，供核对接线、配置及后续恢复研究；不是完整的小智工程。

## 来源与完整性

原始资料位于 `/Users/zoe/Downloads/ESP32-S3-CAM带摄像头资料小智最新`。V2.0 原理图、介绍图和安装视频取自其中的 `CAM智能扩展板新版本打字V2.0版本教程`；主板引脚图和接线图取自 `面包板DIY教程 ESP32-S3-CAM摄像头小智资料包`。

源码来自 V2.0 教程下的 `固件跟源码/xiaozhi-esp32-2.5.2CAM源码.rar`，用 `bsdtar` 解压到项目的 `.local/vendor-extract/`，再选取下列文件。复制的 17 个文件保持原样，原路径、字节数和 SHA-256 记录在 [manifest.json](manifest.json)，整理时已逐项校验。此索引为新增说明，不属于原始资料。

源码保留原始 [MIT LICENSE](source/LICENSE)。硬件图片、PDF 和视频未据此认定采用同一许可。未复制 Windows 可执行程序、重复固件、完整压缩包及与本项目无关的源码；原始资料仍在 Downloads 中。

## 常用文件

| 文件 | 用途 |
| --- | --- |
| [V2.0 扩展板原理图](hardware/ESP32S3-CAM-KZB-V2_0.pdf) | 音频、屏幕、电源及接口连线依据 |
| [扩展板介绍](hardware/CAM智能扩展板介绍.jpg)、[尺寸图](hardware/CAM智能扩展板尺寸图.png) | 接口位置和结构 |
| [套件安装视频](hardware/CAM扩展板套件安装视频.mp4) | 装配步骤和方向参考 |
| [主板引脚图](hardware/CAM开发板引脚图片1.png)、[接线图](hardware/CAM开发板接线图2.jpg) | 主板 GPIO 对照与外围接线 |
| [二次开发参数](hardware/2次开发参数选择.png)、[烧录接口说明](hardware/2个接口都可以烧录，只是打开软件选择不一样.png) | 商家构建、烧录配置参考 |
| [板级 config.h](source/main/boards/bread-compact-wifi-s3cam/config.h) | 摄像头、喇叭、麦克风、屏幕 GPIO 定义 |
| [板级初始化源码](source/main/boards/bread-compact-wifi-s3cam/compact_wifi_board_s3cam.cc) | 外设初始化方式；旁边保留原 README、config.json 和 power_manager.h |
| [音频实现](source/main/audio/codecs/no_audio_codec.cc)及[头文件](source/main/audio/codecs/no_audio_codec.h) | I2S 输出格式与独立麦克风输入通道 |
| [sdkconfig](source/sdkconfig) | 商家工程实际选中的屏幕等构建选项 |

## 本项目使用的硬件配置

以下为资料中的接线和配置，不等同于每项功能已上板验证。

| 外设 | 信号与 GPIO |
| --- | --- |
| 摄像头 | D0–D7：11、9、8、10、12、18、17、16；XCLK：15；PCLK：13；VSYNC：6；HREF：7；SDA：4；SCL：5；PWDN、RESET 无独立 GPIO；XCLK 20 MHz |
| 喇叭 I2S | DOUT：39；BCLK：40；WS/LRCK：41；无独立功放使能 GPIO |
| 麦克风 I2S | WS：1；SCK：2；DIN：42；V2M 最小版本不启用麦克风 |
| 屏幕 SPI | MOSI：20；CLK：19；DC：47；RST：21；CS：45；背光：38 |
| BOOT 按键 | GPIO 0 |

喇叭使用标准 Philips I2S：商家源码的 `bit_shift=true`、`ws_pol=false`，输出与麦克风分别使用独立 I2S 通道，MCLK 不接。商家示例默认输出为 24 kHz、32 位单声道；这是参考固件的格式，V2M 的采样率、位宽以自身实现及实测为准。原理图未明确给出功放的完整芯片型号；只能确认其引脚排列与 MAX98357A/B 类器件兼容，不能写成“已确认 MAX98357A”。

屏幕配置以 `sdkconfig` 中启用的 `CONFIG_LCD_ST7789_240X320=y` 及 `config.h` 对应分支为依据，即 ST7789、240×320。PDF 中部分旧屏幕尺寸文字与此不一致，不用它覆盖实际工程配置；显示效果仍需上板核验。

连接电脑使用主板标有 **TTL** 的 USB-C 口。屏幕占用 GPIO 19/20，与原生 USB 信号重叠；启用屏幕后不能同时把这两脚用于原生 USB。最小版本也不启用 SD 卡：其 GPIO 38/39/40 已与屏幕背光和喇叭输出共用。
