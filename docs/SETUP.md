# 在另一台电脑构建和烧录

适用：与原项目相同的硬件，先完成 [接线指南](GETTING_STARTED.md)。这里的 `<实际串口>` 必须替换为本机识别的 CH340 端口。

## 1. 克隆与基础工具

```sh
git clone https://github.com/Zouu-X/img2music.git
cd img2music
```

需要 Git、Python 3.9 或更新版、Arduino CLI；离线 C++ 测试需要 Clang 或 GCC。固件通过 Arduino CLI 自带的交叉编译器编译。

macOS 可用 Homebrew 安装 `arduino-cli`；其它平台从 [Arduino 官方安装页](https://arduino.github.io/arduino-cli/1.5/installation/) 选择对应的发行包。把 `arduino-cli` 放入 PATH，或用 `ARDUINO` 指定完整路径。不要复制文档中的原作者家目录。

下面是 macOS / Linux 的 shell 命令；Windows 可由 Agent 改用原生 Python、Arduino CLI 和 Git Bash，Python 虚拟环境解释器通常在 `.local/venv/Scripts/python.exe`。本项目的设备测试是在 macOS 完成的，尚未实测 Windows 串口流程；WSL 需要独立处理 USB 转接，不能假定自动可用。

```sh
python3 -m venv .local/venv
.local/venv/bin/python -m pip install -r requirements.txt
```

`requirements.txt` 固定 pyserial 3.5 和 esptool 4.12.0。不要把 esptool 升到另一主版本再直接复用命令。

## 2. 安装指定 ESP32 平台

使用已验证的 **Arduino ESP32 3.3.10**。官方稳定索引来源见 [乐鑫安装说明](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)。以下安装到 Arduino CLI 的默认数据目录：

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.10 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core list
```

指定版本语法是 Arduino CLI 的 `PACKAGER:ARCH@VERSION`，见 [官方命令说明](https://arduino.github.io/arduino-cli/1.5/commands/arduino-cli_core_install/)。若希望使用已有数据目录，在构建时设置 `ARDUINO_DATA`；必须指向真正安装了 3.3.10 的目录，不能只创建空目录。项目临时构建配置、下载、缓存均留在 `.local/` / `build/`。

## 3. 先做不接触硬件的检查

```sh
sh tools/test.sh
.local/venv/bin/python -m unittest discover -s tests -p 'test_device_profile.py'
sh tools/build.sh
```

`tools/test.sh` 检查图像到音符、音色、鼓点、音量、按键消抖等；Python 测试检查设备配置和分区校验。`build.sh` 生成 `build/v2m/v2m.ino.bin`，验证应用不超过 1 MiB。它不会烧录。

实际构建选项已在脚本固定：ESP32-S3、16 MB Flash、DIO、OPI PSRAM、自定义分区、UART 日志。不要使用 Arduino 默认上传命令覆盖本项目的分区保护流程。

## 4. 识别本机串口并备份自己的板子

接回 **TTL / CH340** USB 口，关闭占用端口的监视器。可以列出设备：

```sh
.local/venv/bin/python -m serial.tools.list_ports -v
```

CH340 常见 VID/PID 为 `1a86:7523`。macOS 常见 `/dev/cu.usbserial-*`，Linux 常见 `/dev/ttyUSB*`，Windows 为 `COM*`；以实际识别结果为准。

```sh
.local/venv/bin/python tools/prepare_device.py --port <实际串口> --backup
.local/venv/bin/python tools/prepare_device.py --check-profile
```

第一条会让板子进入下载模式，识别芯片、MAC、容量，完整读取 Flash 并进行设备端校验，成功后写入 `.local/device.json`。这一步不写固件。完整读取可能需要数分钟，备份属于**这块板子**，不要发到仓库或用另一块板子的完整备份替换。

当前只支持与已验证记录相同的分区：NVS `0x9000/0x6000`、PHY `0xf000/0x1000`、factory 应用 `0x10000/0x100000`。若脚本报告不同布局，保留备份，请 Agent 读取原分区并分析；不要绕过检查去擦除整片。

串口变了可以用 `FLASH_PORT` 覆盖连接端口，设备身份仍必须匹配 profile。第二块板要建立自己的 profile；不要修改旧 profile 的 MAC 来冒充新设备。

## 5. 烧录与现场验证

```sh
FLASH_PYTHON=.local/venv/bin/python FLASH_PORT=<实际串口> sh tools/flash.sh
.local/venv/bin/python tools/monitor.py --port <实际串口> --send s --seconds 3
.local/venv/bin/python tools/device_check.py --port <实际串口> --cycles 2 --require-display
```

烧录脚本重新编译，核对当前设备、备份摘要和设备/构建分区，只写应用并单独校验。历史发布附件中的 `.bin` 也是应用文件，不能作为从地址 0 开始的完整 Flash 镜像。

状态应出现 `camera=1 audio=1 display=1`。若烧录后不回应，可短按 RST 复位，再读启动日志；不要按住 BOOT 复位。脚本校验通过并不等于喇叭实际有声音，按 [接线指南的现场验收](GETTING_STARTED.md) 检查照片、状态、扫描线、重播与鼓点。

可补做串口流程测试（会出声并刻意中途重播）：

```sh
.local/venv/bin/python tools/check_controls.py --port <实际串口>
```

## 可选：修改中文屏幕文字

已有 `ui_assets.h` 可直接构建。只有修改固定文字时才需要 Pillow：

```sh
.local/venv/bin/python -m pip install Pillow==11.3.0
.local/venv/bin/python tools/generate_ui_assets.py
```

字体与来源说明在 [assets/fonts](../assets/fonts/README.md)。重新生成后查看 `build/ui-labels-preview.png` 并重编译。依赖不需要访问作者电脑的 Codex 环境。
