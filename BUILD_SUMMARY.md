# 构建脚本完成总结

## 创建的文件

### 核心构建脚本
| 文件 | 说明 |
|------|------|
| `build.ps1` | PowerShell 主构建脚本（8.7 KB） |
| `build.bat` | Windows 批处理包装器（719 B） |

### 文档文件
| 文件 | 说明 |
|------|------|
| `BUILD_SCRIPT_README.md` | 详细构建文档（4.3 KB） |
| `TS3_PLUGIN_FORMAT.md` | 插件格式说明（5.1 KB） |
| `QUICK_BUILD.md` | 快速参考指南 |

## 关键改进

### 1. DLL 位置修复 ✅
- **之前**: DLL 被放在 `plugins/JUL14Ns_Audio_Mods/` 子目录
- **现在**: DLL 直接放在 `plugins/` 根目录
- **理由**: TeamSpeak 3 只会在根目录搜索插件，子目录中的 DLL 不会被加载

### 2. 自动化流程
脚本完全自动化以下步骤：
```
版本提取 → 占位符替换 → CMake配置 → 编译 → 部署 → 打包
```

### 3. 灵活的版本管理
- 自动从 git 标签提取版本号（`v1.0.5` → `1.0.5`）
- 支持手动指定版本
- 自动替换源代码中的版本占位符

## 使用方法

### 最简单的方式
```bash
# 直接双击运行（Windows）
build.bat

# 或在 PowerShell 中
.\build.ps1
```

### 自定义参数
```powershell
# 指定版本
.\build.ps1 -Version 1.0.5

# 指定架构和配置
.\build.ps1 -Arch Win32 -Config Debug

# 指定 Qt 路径
.\build.ps1 -QtPath "D:\Qt\5.15.2\msvc2019_64"
```

## 输出内容

### 生成的文件
```
项目根目录/
├── JUL14Ns_Audio_Mods_1.0.5.ts3_plugin    ← 最终插件包
└── deploy/
    ├── plugins/
    │   └── JUL14Ns_Audio_Mods.dll         ← DLL（plugins 根目录）
    ├── package.ini
    └── ...
```

### 包文件内部结构
```
.ts3_plugin (ZIP 格式)
├── package.ini                             ← 插件元数据
└── plugins/
    ├── JUL14Ns_Audio_Mods.dll            ← DLL（根目录！）
    └── ... (其他依赖库)
```

## 构建流程详解

### 步骤 1: 版本号获取
- 尝试从 `git describe --tags` 获取
- 格式转换：`v1.0.5-1-gd8412ed` → `1.0.5`
- 失败则使用默认值 `1.0.0`

### 步骤 2: 版本占位符替换
替换以下文件中的 `<version>`：
- `src/definitions.hpp`
- `CMakeLists.txt`
- `deploy/package.ini`

### 步骤 3: CMake 配置
```
cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH=...
```

### 步骤 4: 编译
```
cmake --build . --config Release
```

### 步骤 5: 部署
- 将生成的 DLL 复制到 `deploy/plugins/`
- **重要**: 直接复制到 `plugins/` 根目录

### 步骤 6: 打包
- 创建临时目录
- 复制 `package.ini` 和 `plugins/*`
- 使用 `Compress-Archive` 创建 ZIP
- 重命名为 `.ts3_plugin`

## 关键特性

✅ **完全自动化** - 一条命令完成所有步骤
✅ **版本管理** - 自动从 git 获取或手动指定
✅ **跨平台** - 支持 x64 和 Win32
✅ **多种配置** - Release 和 Debug
✅ **错误处理** - 任何步骤失败都会停止并显示错误
✅ **彩色输出** - 清晰的进度显示
✅ **DLL 位置正确** - 符合 TeamSpeak 3 要求

## 故障排除

### CMake 找不到 Qt
```powershell
.\build.ps1 -QtPath "正确的Qt路径"
```

### 编译失败
检查：
1. Visual Studio 2019 Build Tools 是否安装
2. CMake 是否在 PATH 中
3. Qt 开发文件是否完整

### 包文件无法使用
验证：
1. `.ts3_plugin` 是否为有效的 ZIP 文件
2. `package.ini` 是否在根目录
3. DLL 是否在 `plugins/` 根目录（不是子文件夹）

## 高级用法

### 自定义依赖库
在编译后，可以添加额外的 DLL 到 `deploy/plugins/` 目录，它们会自动被包含在 `.ts3_plugin` 包中。

示例：添加 Qt 库
```powershell
# 手动复制依赖库
Copy-Item "C:\Qt\5.15.2\msvc2019_64\bin\Qt5Core.dll" "deploy\plugins\"
Copy-Item "C:\Qt\5.15.2\msvc2019_64\bin\Qt5Gui.dll" "deploy\plugins\"

# 然后运行打包部分
.\build.ps1
```

### 修改脚本
脚本易于定制，可以：
- 改变输出目录
- 添加额外的编译步骤
- 修改打包方式
- 集成自动测试

## 后续改进建议

1. **自动依赖库复制** - 在脚本中添加自动复制 Qt 库的功能
2. **包验证** - 验证生成的 `.ts3_plugin` 包的正确性
3. **自动测试** - 在打包前运行测试
4. **CI/CD 集成** - 集成到 GitHub Actions 等 CI 系统
5. **多架构支持** - 同时构建 x64 和 Win32 版本

## 参考资源

- **TeamSpeak 3 SDK**: https://github.com/TeamSpeak-Systems/teamspeak-3-sdk
- **Qt 文档**: https://doc.qt.io/
- **CMake 文档**: https://cmake.org/cmake/help/latest/
- **PowerShell 文档**: https://docs.microsoft.com/powershell/

---

**构建脚本版本**: 1.0
**创建时间**: 2026-03-08
**最后修改**: 2026-03-08
