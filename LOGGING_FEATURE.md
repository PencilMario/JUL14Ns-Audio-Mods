# 日志栏和设备查询功能实现总结

## 新增功能

### 1. 日志栏（新增标签页）
- **位置**: 配置对话框 → 日志标签页
- **功能**: 显示所有设备切换相关的操作日志
- **特性**:
  - 自动时间戳（[HH:MM:SS.zzz]格式）
  - 自动滚动到最新日志
  - 只读文本框，防止用户误删

### 2. 日志栏按钮

#### 显示当前设备按钮
- **功能**: 查询并显示当前系统默认播放设备信息
- **输出内容**:
  - 设备友好名称
  - 设备 ID（Instance GUID）
  - 设备类型（Render/Playback）
- **实现**: 每次按下都重新初始化 WASAPI 枚举器（参考 PyAudio 方式）

#### 切换到默认设备按钮
- **功能**: 手动将 TeamSpeak 3 播放设备切换到系统默认设备
- **操作流程**:
  1. 获取系统默认设备名称
  2. 遍历所有活跃 TeamSpeak 连接
  3. 为每个连接调用 openPlaybackDevice()
  4. 在日志中显示成功/失败状态

#### 清空日志按钮
- **功能**: 清除日志栏中的所有内容
- **用途**: 在诊断时清理过期信息

## 代码改进

### device_switcher.h 改进

#### 1. 重新初始化 WASAPI 枚举器
**原理**（参考 Python 代码）:
```python
# Python 方式：每次检查都重新初始化
p = pyaudio.PyAudio()  # 每次都创建新实例
default_device = p.get_default_output_device_info()
p.terminate()
```

**C++ WASAPI 实现**:
```cpp
// 每次调用 getDefaultPlaybackDeviceName() 都重新创建枚举器
IMMDeviceEnumerator* tempEnumerator = nullptr;
CoCreateInstance(...);  // 创建新枚举器
// 获取设备信息
tempEnumerator->Release();  // 清理
```

#### 2. 新增 getDefaultDeviceInfo() 方法
返回详细的设备信息字符串：
- 设备友好名称
- 设备 ID
- 设备类型

### config.h 新增方法

```cpp
void appendLog(const QString& message);  // 添加带时间戳的日志
void clearLogs();                          // 清空日志
```

### config.cpp 实现

#### appendLog() 实现
- 自动添加时间戳：`[HH:MM:SS.zzz]`
- 追加到 QPlainTextEdit（logs_output）
- 自动滚动到最新内容

#### plugin.cpp 集成

在 `ts3plugin_init()` 中：
1. 初始化日志，显示插件启动
2. 查询并显示当前默认设备
3. 连接日志按钮的信号槽
4. 处理设备查询和切换逻辑

## 系统设备获取改进

### 问题分析
- **原问题**: WASAPI 枚举器可能缓存设备列表，导致设备更改时不被检测到
- **解决方案**: 每次检查时重新创建枚举器（参考 PyAudio 的方式）

### 实现细节

```cpp
// 旧方式（缓存枚举器）
if (!m_deviceEnumerator) {
    return result;
}
hr = m_deviceEnumerator->GetDefaultAudioEndpoint(...);

// 新方式（每次重新初始化）
IMMDeviceEnumerator* tempEnumerator = nullptr;
CoCreateInstance(__uuidof(MMDeviceEnumerator), ...);
hr = tempEnumerator->GetDefaultAudioEndpoint(...);
tempEnumerator->Release();
```

## 日志输出示例

```
[23:45:12.345] 插件初始化
[23:45:12.346] Device: Realtek High Definition Audio
ID: {0.0.1.00000000}.{12345678-1234-5678-1234-567812345678}
Type: Render (Playback)
[23:45:15.123] 用户触发：切换到默认设备
[23:45:15.124] 正在切换到：Realtek High Definition Audio
[23:45:15.125] ✓ 切换成功
```

## 文件修改清单

### 修改的文件
1. **src/config.ui**
   - 新增"日志"标签页
   - 添加 QPlainTextEdit（logs_output）
   - 添加三个按钮：显示设备、切换设备、清空日志

2. **src/config.h**
   - 添加 `appendLog()` 方法声明
   - 添加 `clearLogs()` 方法声明

3. **src/config.cpp**
   - 实现 `appendLog()` 方法（带时间戳）
   - 实现 `clearLogs()` 方法
   - 连接"清空日志"按钮信号槽
   - 添加必要的头文件（QDateTime, QPlainTextEdit, QTextCursor）

4. **src/device_switcher.h**
   - 改进 `getDefaultPlaybackDeviceName()` 方法
     - 每次都重新创建 WASAPI 枚举器
     - 改进错误检查（检查 varName.pwszVal 是否为空）
   - 新增 `getDefaultDeviceInfo()` 方法
     - 返回详细的设备信息字符串

5. **src/plugin.cpp**
   - 添加头文件：QPushButton, QObject
   - 在 `ts3plugin_init()` 中：
     - 初始化日志输出
     - 显示当前默认设备信息
     - 连接日志按钮信号槽

## 编译和使用

### 编译
```powershell
.\build.ps1
```

### 使用步骤
1. 打开 TeamSpeak 3 插件设置
2. 切换到"日志"标签页
3. 点击"显示当前设备"查看系统默认设备信息
4. 点击"切换到默认设备"手动切换 TS3 播放设备
5. 点击"清空日志"清理日志记录

## 故障排除

### 显示"无活跃连接"
**原因**: 没有任何 TeamSpeak 服务器连接
**解决**: 先连接到 TeamSpeak 服务器，然后再尝试切换设备

### 设备切换失败
**原因**:
- 设备不兼容
- TS3 权限问题
- 设备驱动问题

**解决**:
- 检查系统设备是否正常
- 查看日志中的错误信息
- 尝试手动更改 TS3 的播放设备

## 关键改进要点

✅ **重新初始化 WASAPI** - 每次都创建新枚举器，检测设备变化
✅ **带时间戳的日志** - 便于诊断和排查问题
✅ **详细的设备信息** - 显示设备名称、ID、类型
✅ **手动切换功能** - 用户可以主动切换设备
✅ **错误处理** - 显示切换成功/失败状态
✅ **参考 PyAudio 方式** - 采用业界成熟的设备检测方法

---

**实现日期**: 2026-03-09
**功能版本**: 1.0
