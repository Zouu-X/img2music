# 来源与第三方材料

本仓库用于共享已实现的 V2M 装置和复现上下文。本次整理没有给整个仓库添加一份统一开源许可证，也没有把第三方资料重新许可为 MIT。

| 范围 | 来源与说明 |
| --- | --- |
| `firmware/v2m`、本项目 `tools` / `tests` / 文档 | 本项目实现与说明；设备合成规则独立编写 |
| `vendor/goouuu-s3-cam/source` | 用户购买套件所附的小智 ESP32 2.5.2 CAM 源码摘选；保留其 [MIT LICENSE](vendor/goouuu-s3-cam/source/LICENSE) |
| `vendor/goouuu-s3-cam/hardware` | 商家提供的原理图、引脚图、介绍图、安装视频；保留原文件和 [来源清单](vendor/goouuu-s3-cam/manifest.json)，未确认其采用源码的 MIT 许可 |
| `assets/fonts` | Noto Sans CJK SC Regular 2.004，SIL Open Font License 1.1；版权、下载源和摘要均随字体保存 |
| `firmware/v2m/ui_assets.h` | 使用上述字体生成的固定屏幕文字位图；原字体没有修改 |
| `assets/IMG_9927.JPG` | 项目实物照片；公开版本已清除 EXIF / 定位等元数据，画面像素保持不变 |

`RESEARCH.md` 和接手说明提到外部图像音乐项目与 playmusictheory.net，是研究背景。该网页下载的源码没有纳入仓库；公开可读不等于获得复用许可。

Arduino ESP32、esptool、pyserial、Pillow 等通过各自发行渠道安装，不打包作者本机的工具缓存。其许可由各依赖项目提供。GitHub 上的历史标签保留固件功能版本；本机原始 Git 历史和设备完整 Flash 备份不随仓库公开。
