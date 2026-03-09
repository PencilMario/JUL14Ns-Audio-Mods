# TeamSpeak 3 插件包（.ts3_plugin）格式说明

## 包结构

`.ts3_plugin` 文件是一个 ZIP 格式的包，TeamSpeak 3 客户端在加载插件时会自动解压。

### 正确的包结构

```
JUL14Ns_Audio_Mods_1.0.5.ts3_plugin
├── package.ini                        # 必需：插件元数据
└── plugins/                           # 必需：DLL 根目录
    ├── JUL14Ns_Audio_Mods.dll        # 必需：主插件 DLL
    ├── Qt5Core.dll                   # 可选：依赖库
    ├── Qt5Gui.dll                    # 可选：依赖库
    ├── Qt5Widgets.dll                # 可选：依赖库
    ├── Qt5Network.dll                # 可选：依赖库
    └── ...                           # 其他依赖库
```

### ❌ 错误的包结构（DLL 不会被加载）

```
JUL14Ns_Audio_Mods_1.0.5.ts3_plugin
├── package.ini
└── plugins/
    └── JUL14Ns_Audio_Mods/           # ❌ DLL 在子文件夹中
        └── JUL14Ns_Audio_Mods.dll    # ❌ 错误位置
```

**重要**: DLL 必须直接放在 `plugins/` 目录中，而不是任何子文件夹。否则 TeamSpeak 3 会找不到插件并无法加载。

## package.ini 文件格式

`package.ini` 是插件包的元数据文件，位于包的根目录。

### 示例内容

```ini
Name = JUL14Ns Audio Mods
Type = Plugin
Author = JUL14N
Version = 1.0.5
Platforms = win64, win32, linux_amd64, mac
Description = "A collection of audio mods for the TeamSpeak 3 client"
```

### 字段说明

| 字段 | 说明 |
|------|------|
| `Name` | 插件显示名称 |
| `Type` | 必须是 `Plugin` |
| `Author` | 作者名称 |
| `Version` | 插件版本号 |
| `Platforms` | 支持的平台（多个用逗号分隔）|
| `Description` | 插件描述 |

### 支持的平台

- `win64` - Windows 64-bit
- `win32` - Windows 32-bit
- `linux_amd64` - Linux x86_64
- `mac` - macOS
- `freebsd_amd64` - FreeBSD

## 添加依赖库

如果插件依赖于其他库（如 Qt 库），需要将这些库文件也包含在 `plugins/` 目录中。

### 构建脚本中添加依赖库

1. 将依赖库复制到 `deploy/plugins/` 目录：

```batch
# 复制 Qt 库（以 Windows 为例）
copy "C:\Qt\5.15.2\msvc2019_64\bin\Qt5Core.dll" "deploy\plugins\"
copy "C:\Qt\5.15.2\msvc2019_64\bin\Qt5Gui.dll" "deploy\plugins\"
copy "C:\Qt\5.15.2\msvc2019_64\bin\Qt5Widgets.dll" "deploy\plugins\"
copy "C:\Qt\5.15.2\msvc2019_64\bin\Qt5Network.dll" "deploy\plugins\"
```

2. 或在 `build.ps1` 中自动添加：

```powershell
# 在"复制 DLL 到部署目录"部分之后添加以下代码
$qtDlls = @(
    "Qt5Core.dll",
    "Qt5Gui.dll",
    "Qt5Widgets.dll",
    "Qt5Network.dll"
)

foreach ($dll in $qtDlls) {
    $srcDll = "$QtPath\bin\$dll"
    if (Test-Path $srcDll) {
        Copy-Item -Path $srcDll -Destination "$PluginsDir\$dll" -Force
        Write-Host "      ✓ 复制依赖库: $dll" -ForegroundColor Green
    }
}
```

## 打包命令（手动）

如果不使用构建脚本，可以手动创建 `.ts3_plugin` 包：

### 在 Windows 中

1. 准备目录结构：
   ```
   ts3_package/
   ├── package.ini
   └── plugins/
       └── JUL14Ns_Audio_Mods.dll
   ```

2. 创建 ZIP 文件并重命名：
   ```powershell
   Compress-Archive -Path @("package.ini", "plugins") -DestinationPath "JUL14Ns_Audio_Mods_1.0.5.zip"
   Rename-Item "JUL14Ns_Audio_Mods_1.0.5.zip" "JUL14Ns_Audio_Mods_1.0.5.ts3_plugin"
   ```

3. 或使用系统的文件压缩工具直接压缩为 ZIP，再修改扩展名为 `.ts3_plugin`

### 在 Linux 中

```bash
cd ts3_package
zip -r ../JUL14Ns_Audio_Mods_1.0.5.ts3_plugin package.ini plugins/
```

## 验证包文件

使用 `7-Zip` 或其他 ZIP 工具打开 `.ts3_plugin` 文件，验证目录结构是否正确：

1. 打开 `.ts3_plugin` 文件
2. 检查根目录是否包含 `package.ini` 和 `plugins/` 目录
3. 检查 `plugins/` 目录中是否直接包含 DLL 文件（不在子文件夹中）

## 安装插件

将 `.ts3_plugin` 文件放在以下位置，TeamSpeak 3 会自动安装：

### Windows
```
C:\Users\<Username>\AppData\Roaming\TS3Client\plugins\
```

### Linux
```
~/.ts3client/plugins/
```

### macOS
```
~/Library/Preferences/TS3Client/plugins/
```

双击 `.ts3_plugin` 文件也可以触发安装。

## 常见问题

### Q: 插件无法加载，显示"Unknown plugin ID"
A: 检查 `package.ini` 文件是否存在且格式正确。

### Q: 插件加载但不能正常工作
A: 检查 DLL 是否直接在 `plugins/` 目录中，确保没有被放在子文件夹中。

### Q: 缺少依赖库错误
A: 确保所有依赖的 DLL（如 Qt 库）都被包含在 `plugins/` 目录中。

### Q: 如何调试插件包内容
A: 使用 7-Zip、WinRAR 等 ZIP 工具打开 `.ts3_plugin` 文件，检查内部结构和文件。

## 参考资源

- [TeamSpeak 3 Plugin SDK](https://github.com/TeamSpeak-Systems/teamspeak-3-sdk)
- [TeamSpeak 3 插件开发文档](https://docs.teamspeak.com/client/plugins/)
