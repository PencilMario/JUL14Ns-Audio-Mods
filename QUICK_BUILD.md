# 快速构建指南

## 最简单的方式：双击运行

1. 打开项目根目录
2. 双击 `build.bat`
3. 脚本会自动编译并生成 `.ts3_plugin` 插件包

## 命令行方式

### 基本命令（使用默认设置）
```powershell
.\build.ps1
```

### 指定版本号
```powershell
.\build.ps1 -Version 1.0.5
```

### 指定构建架构和配置
```powershell
# 64-bit Release 版本（默认）
.\build.ps1 -Arch x64 -Config Release

# 32-bit Debug 版本
.\build.ps1 -Arch Win32 -Config Debug
```

### 指定 Qt 路径
```powershell
.\build.ps1 -QtPath "D:\Qt\5.15.2\msvc2019_64"
```

## 输出文件

构建完成后，会在项目根目录生成：

```
JUL14Ns_Audio_Mods_1.0.5.ts3_plugin  ← 这是最终的插件包
```

以及部署文件在 `deploy/` 目录中：

```
deploy/
└── plugins/
    └── JUL14Ns_Audio_Mods.dll  ← DLL 在 plugins 根目录中
```

## ⚠️ 重要说明

- **DLL 位置**: DLL 文件必须放在 `plugins/` 目录的根位置，而不是子文件夹
- **包格式**: `.ts3_plugin` 是 ZIP 格式，TeamSpeak 会自动解压加载
- **版本号**: 如果没有指定，脚本会自动从 git 标签提取版本号

## 环境要求

- Windows 10/11
- Visual Studio 2019 Build Tools
- CMake 3.10+
- Qt 5.15.2
- PowerShell 5.0+

## 出现问题？

1. 查看 `BUILD_SCRIPT_README.md` 获取详细文档
2. 查看 `TS3_PLUGIN_FORMAT.md` 了解插件格式
3. 检查构建输出中的错误信息

## 文件说明

| 文件 | 说明 |
|------|------|
| `build.ps1` | PowerShell 构建脚本（核心） |
| `build.bat` | Windows 批处理（双击运行） |
| `BUILD_SCRIPT_README.md` | 详细使用文档 |
| `TS3_PLUGIN_FORMAT.md` | 插件包格式说明 |
| `QUICK_BUILD.md` | 此文件（快速参考） |
