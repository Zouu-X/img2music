# 屏幕文字

使用 Noto Sans CJK SC Regular 2.004（© 2014–2021 Adobe），遵循随附 SIL Open Font License 1.1。原字体从本机已有项目复制，官方下载来源与 SHA-256 保存在 `source.json`；本项目未修改原字体。

`tools/generate_ui_assets.py` 用 Pillow 生成十三条固定文案的 1bpp 位图，验证原字体摘要及每个字符的 Unicode 字形覆盖。生成结果为 `firmware/v2m/ui_assets.h`，只把这些文字位图编译到设备，不烧入整份字体。现有位图可直接编译；只有修改文字才需要重新生成。

从仓库根目录、建立项目 Python 环境后重新生成（见 `docs/SETUP.md`）：

```sh
.local/venv/bin/python -m pip install Pillow==11.3.0
.local/venv/bin/python tools/generate_ui_assets.py
```
