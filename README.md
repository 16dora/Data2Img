# Data2Img 命令使用

CLI 命令在 `screen_codec.exe` 所在目录执行，当前为 `F:\data2img\Data2Img\d2i_install`。示例中的输入文件和输出路径请按实际情况替换。

## 查看帮助

```powershell
.\screen_codec.exe --help
```

## encode：文件转 PNG

使用默认参数（3840×2060、2×2 色块）：

```powershell
.\screen_codec.exe encode -i ".\input.zip" -o ".\pages"
```

指定图片宽高和色块边长：

```powershell
.\screen_codec.exe encode -i ".\input.zip" -o ".\pages" --width 1920 --height 1080 --cell 3
```

允许覆盖已有图片，并显示详细日志：

```powershell
.\screen_codec.exe encode -i ".\input.zip" -o ".\pages" --overwrite --verbose
```

## decode：PNG/JPG/JPEG 还原文件

从图片目录恢复：

```powershell
.\screen_codec.exe decode -i ".\pages" -o ".\restored.zip"
```

指定一张或多张图片：

```powershell
.\screen_codec.exe decode -i ".\frame_000001.png" -o ".\restored.zip"
.\screen_codec.exe decode -i ".\frame_000002.png" ".\frame_000001.png" -o ".\restored.zip"
```

读取 JPG/JPEG，或混合输入不同格式：

```powershell
.\screen_codec.exe decode -i ".\shot01.jpg" -o ".\restored.zip"
.\screen_codec.exe decode -i ".\shot01.jpeg" ".\shot02.png" -o ".\restored.zip"
```

JPG/JPEG 有损压缩可能导致恢复失败；只有完整校验通过才会输出文件。encode 仍生成 PNG。

递归读取子目录，允许覆盖恢复文件：

```powershell
.\screen_codec.exe decode -i ".\screenshots" -o ".\restored.zip" --recursive --overwrite
```

目录包含多个文件的图片时，按原文件 SHA-256 的前 32 位十六进制筛选：

```powershell
$fileId = (Get-FileHash ".\input.zip" -Algorithm SHA256).Hash.Substring(0, 32)
.\screen_codec.exe decode -i ".\mixed_pages" -o ".\restored.zip" --file-id $fileId
```

## 参数

| 参数 | 用途 |
|---|---|
| `-i` | 输入文件或图片目录；encode 只接受一个文件，decode 可接受多个路径 |
| `-o` | encode 的输出目录，或 decode 的输出文件完整路径 |
| `--width` | encode 图片宽度，默认 3840，范围 384～16384 像素 |
| `--height` | encode 图片高度，默认 2060，范围 128～16384 像素 |
| `--cell` | encode 正方形色块边长，默认 2，范围 1～32 像素 |
| `--overwrite` | 允许覆盖同名输出，默认不覆盖 |
| `--verbose` | 显示详细日志 |
| `--recursive` | decode 递归读取图片目录 |
| `--file-id` | decode 筛选指定文件，值为 SHA-256 的前 32 位十六进制 |

画布最多 6400 万像素，网格必须足够容纳头部和数据。decode 自动读取宽高、色块参数；输入须包含全部必需图片。

## 构建、测试、安装

以下命令在工程根目录 `F:\data2img\Data2Img` 执行；OpenCV 路径按实际安装位置替换。

```powershell
cmake -S . -B build/vs2022-x64 -G "Visual Studio 17 2022" -A x64 "-DOpenCV_DIR=D:/Opencv/opencv/build/x64/vc15/lib" "-DCMAKE_INSTALL_PREFIX=./d2i_install"
cmake --build build/vs2022-x64 --config Release --parallel
ctest --test-dir build/vs2022-x64 -C Release --output-on-failure
cmake --install build/vs2022-x64 --config Release
```

安装完成后进入运行目录：

```powershell
Set-Location ".\d2i_install"
.\screen_codec.exe --help
```

