# 构建脚本使用说明

本目录中包含用于构建 JUL14Ns Audio Mods TeamSpeak 3 插件的脚本文件。

## 文件说明

- **`build.ps1`** - PowerShell 构建脚本（推荐）
- **`build.bat`** - Windows 批处理脚本（包装器）

## 快速开始

### 方法 1: 双击运行（最简单）
直接双击 `build.bat` 文件，脚本会自动：
1. 检测版本号
2. 替换版本占位符
3. 配置 CMake
4. 编译项目
5. 生成 `.ts3_plugin` 包文件

### 方法 2: 使用 PowerShell（更灵活）

打开 PowerShell，进入项目根目录，执行：

```powershell
# 使用默认设置构建
.\build.ps1

# 指定版本号
.\build.ps1 -Version 1.0.5

# 指定 Win32 架构
.\build.ps1 -Arch Win32

# 构建 Debug 版本
.\build.ps1 -Config Debug

# 指定 Qt 路径
.\build.ps1 -QtPath "D:\Qt\5.15.2\msvc2019_64"

# 组合使用
.\build.ps1 -Version 1.0.5 -Arch x64 -Config Release
```

## 参数说明

| 参数 | 可选值 | 默认值 | 说明 |
|------|--------|--------|------|
| `-Version` | 任意版本号 | 从 git 标签自动提取 | 插件版本号 |
| `-Arch` | `x64`, `Win32` | `x64` | 构建架构 |
| `-Config` | `Release`, `Debug` | `Release` | 构建配置 |
| `-QtPath` | 目录路径 | `C:\Qt\5.15.2\msvc2019_64` | Qt 安装路径 |

## 构建流程

脚本执行以下步骤：

### 1. 版本号获取
- 优先从 git 标签获取版本号（格式：`vX.Y.Z`）
- 如果没有 git 标签，使用默认版本 `1.0.0`
- 支持通过 `-Version` 参数手动指定

### 2. 版本占位符替换
替换以下文件中的 `<version>` 占位符：
- `src/definitions.hpp`
- `CMakeLists.txt`
- `deploy/package.ini`

### 3. CMake 配置
- 使用 Visual Studio 16 2019 作为生成器
- 配置 Qt 依赖路径
- 生成项目文件

### 4. 编译
- 根据指定的配置（Release/Debug）编译项目
- 生成 `JUL14Ns_Audio_Mods.dll`

### 5. 部署
- 将 DLL 复制到 `deploy/plugins/JUL14Ns_Audio_Mods/` 目录

### 6. 打包
- 创建 `.ts3_plugin` 包文件（ZIP 格式）
- 文件名格式：`JUL14Ns_Audio_Mods_X.Y.Z.ts3_plugin`
- 包含 `package.ini` 和 `plugins/` 目录

## 输出文件

构建成功后，主要输出文件为：

```
项目根目录/
├── build/                                    # 构建目录
│   ├── Release/
│   │   └── JUL14Ns_Audio_Mods.dll          # 生成的 DLL 文件
│   └── ...
├── deploy/
│   ├── plugins/
│   │   └── JUL14Ns_Audio_Mods.dll          # DLL 放在 plugins 根目录
│   ├── package.ini                          # 插件配置
│   └── ...
└── JUL14Ns_Audio_Mods_1.0.5.ts3_plugin     # 最终的插件包
```

### 包文件结构

生成的 `.ts3_plugin` 文件解压后的结构：

```
JUL14Ns_Audio_Mods_1.0.5.ts3_plugin
├── package.ini                              # 插件元数据
└── plugins/
    └── JUL14Ns_Audio_Mods.dll              # DLL（根目录，TeamSpeak会加载）
```

**重要**: DLL 必须放在 `plugins/` 根目录，而不是子文件夹中，否则 TeamSpeak 无法加载插件。

## 常见问题

### Q: CMake 找不到 Qt？
A: 检查 Qt 安装路径，使用 `-QtPath` 参数指定正确的路径：
```powershell
.\build.ps1 -QtPath "D:\Qt\5.15.2\msvc2019_64"
```

### Q: 编译失败怎么办？
A:
1. 确保已安装 Visual Studio 2019 Build Tools
2. 确保 Qt5 开发包已正确安装
3. 检查控制台输出的错误信息

### Q: 如何构建 Win32 版本？
A:
```powershell
.\build.ps1 -Arch Win32
```

### Q: 如何构建 Debug 版本？
A:
```powershell
.\build.ps1 -Config Debug
```

### Q: 版本号是如何自动获取的？
A: 脚本从 git 标签自动提取版本号。例如：
- git 标签 `v1.0.5` → 版本号 `1.0.5`
- 如果没有标签，使用默认版本 `1.0.0`

## 环境要求

- **Windows 10/11** (64-bit)
- **Visual Studio 2019** Build Tools 或 Community 版本
- **CMake 3.10+**
- **Qt 5.15.2** (msvc2019_64 或其他版本)
- **PowerShell 5.0+**
- **Git**（用于自动获取版本号）

## 清理构建

如果需要完全清理构建文件：

```powershell
Remove-Item -Path ".\build" -Recurse -Force
```

## 许可证

MIT License - 详见 LICENSE 文件
