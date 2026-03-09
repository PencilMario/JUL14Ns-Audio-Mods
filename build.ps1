#Requires -Version 5.0
<#
.SYNOPSIS
    构建 JUL14Ns Audio Mods TeamSpeak 3 插件并生成 .ts3_plugin 包

.DESCRIPTION
    此脚本执行以下步骤：
    1. 提取版本号（从git tags或默认值）
    2. 替换版本号占位符
    3. 使用CMake配置和编译项目
    4. 复制生成的DLL到部署目录
    5. 创建.ts3_plugin包文件

.PARAMETER Version
    指定版本号（如果不指定，将从git tags自动提取）

.PARAMETER Arch
    构建架构：x64（默认）或Win32

.PARAMETER Config
    构建配置：Release（默认）或Debug

.PARAMETER QtPath
    Qt根目录路径（默认：C:\Qt\5.15.2\msvc2019_64）

.EXAMPLE
    .\build.ps1
    # 使用默认设置构建

.EXAMPLE
    .\build.ps1 -Version 1.0.5 -Arch x64 -Config Release
    # 指定版本和构建配置
#>

param(
    [string]$Version = "",
    [ValidateSet("x64", "Win32")][string]$Arch = "x64",
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [string]$QtPath = "C:\Qt\5.15.2\msvc2019_64"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = $ScriptDir
$BuildDir = "$ProjectDir\build"
$DeployDir = "$ProjectDir\deploy"
$PluginsDir = "$DeployDir\plugins"
$PluginName = "JUL14Ns_Audio_Mods"

Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  JUL14Ns Audio Mods - TeamSpeak 3 插件构建脚本" -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# 步骤 1: 确定版本号
# ============================================================================
if (-not $Version) {
    Write-Host "[1/6] 获取版本号..." -ForegroundColor Yellow
    try {
        $gitVersion = & git describe --tags 2>$null
        if ($LASTEXITCODE -eq 0) {
            # 格式：v1.0.3 -> 1.0.3
            $Version = $gitVersion -replace '^v', '' -replace '-.*', ''
            Write-Host "      从Git标签获取版本: $Version" -ForegroundColor Green
        } else {
            $Version = "1.0.0"
            Write-Host "      使用默认版本: $Version" -ForegroundColor Green
        }
    } catch {
        $Version = "1.0.0"
        Write-Host "      使用默认版本: $Version" -ForegroundColor Green
    }
} else {
    Write-Host "[1/6] 使用指定版本: $Version" -ForegroundColor Yellow
}

Write-Host "      版本号: $Version" -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# 步骤 2: 替换版本号占位符
# ============================================================================
Write-Host "[2/6] 替换版本号占位符..." -ForegroundColor Yellow

$filesWithVersion = @(
    "$ProjectDir\src\definitions.hpp",
    "$ProjectDir\CMakeLists.txt",
    "$DeployDir\package.ini"
)

foreach ($file in $filesWithVersion) {
    if (Test-Path $file) {
        $content = Get-Content $file -Raw
        $originalContent = $content
        $content = $content -replace '<version>', $Version

        if ($content -ne $originalContent) {
            Set-Content -Path $file -Value $content -NoNewline
            Write-Host "      ✓ $([System.IO.Path]::GetFileName($file))" -ForegroundColor Green
        } else {
            Write-Host "      ✓ $([System.IO.Path]::GetFileName($file)) (无需更改)" -ForegroundColor Gray
        }
    }
}

Write-Host ""

# ============================================================================
# 步骤 3: 配置和编译
# ============================================================================
Write-Host "[3/6] 配置CMake项目..." -ForegroundColor Yellow

# 清理旧的构建目录
if (Test-Path $BuildDir) {
    Write-Host "      清理旧的构建目录..." -ForegroundColor Gray
    Remove-Item -Path $BuildDir -Recurse -Force | Out-Null
}

# 创建构建目录
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
Push-Location $BuildDir

# CMake 配置
$platform = if ($Arch -eq "x64") { "x64" } else { "Win32" }
$cmakeArgs = @(
    "-G", "Visual Studio 16 2019",
    "-A", $platform,
    "-DCMAKE_PREFIX_PATH=$QtPath",
    ".."
)

Write-Host "      运行: cmake $($cmakeArgs -join ' ')" -ForegroundColor Gray
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ CMake配置失败" -ForegroundColor Red
    Pop-Location
    exit 1
}

Write-Host "      ✓ CMake配置成功" -ForegroundColor Green
Write-Host ""

Write-Host "[4/6] 编译项目..." -ForegroundColor Yellow

# 编译
$buildArgs = @(
    "--build", ".",
    "--config", $Config
)

Write-Host "      运行: cmake $($buildArgs -join ' ')" -ForegroundColor Gray
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ 编译失败" -ForegroundColor Red
    Pop-Location
    exit 1
}

Write-Host "      ✓ 编译成功" -ForegroundColor Green
Write-Host ""

Pop-Location

# ============================================================================
# 步骤 5: 复制DLL到部署目录
# ============================================================================
Write-Host "[5/6] 复制DLL到部署目录..." -ForegroundColor Yellow

$dllPath = "$BuildDir\$Config\${PluginName}.dll"

if (-not (Test-Path $dllPath)) {
    Write-Host "❌ 找不到生成的DLL: $dllPath" -ForegroundColor Red
    exit 1
}

# 创建plugins根目录
New-Item -ItemType Directory -Path $PluginsDir -Force | Out-Null

# 复制DLL到plugins根目录
$destDll = "$PluginsDir\${PluginName}.dll"
Copy-Item -Path $dllPath -Destination $destDll -Force
Write-Host "      ✓ 复制 DLL: plugins/${PluginName}.dll ($((Get-Item $destDll).Length / 1KB)KB)" -ForegroundColor Green

Write-Host ""

# ============================================================================
# 步骤 6: 创建 .ts3_plugin 包
# ============================================================================
Write-Host "[6/6] 创建 .ts3_plugin 包..." -ForegroundColor Yellow

$packageName = "${PluginName}_${Version}.ts3_plugin"
$packagePath = "$ProjectDir\$packageName"

# 删除旧的包文件
if (Test-Path $packagePath) {
    Remove-Item -Path $packagePath -Force
}

# 创建临时目录用于打包
$tempDir = "$BuildDir\ts3_plugin_temp"
if (Test-Path $tempDir) {
    Remove-Item -Path $tempDir -Recurse -Force
}
New-Item -ItemType Directory -Path "$tempDir\plugins" -Force | Out-Null

# 复制package.ini
Copy-Item -Path "$DeployDir\package.ini" -Destination "$tempDir\package.ini" -Force

# 复制所有plugins中的文件（包括DLL和依赖库）
Copy-Item -Path "$PluginsDir\*" -Destination "$tempDir\plugins\" -Recurse -Force

# 创建ZIP文件
Push-Location $tempDir

Write-Host "      创建: $packageName" -ForegroundColor Gray

# 使用内置的Compress-Archive
Compress-Archive -Path @("package.ini", "plugins") -DestinationPath "$packagePath" -Force

if (-not (Test-Path $packagePath)) {
    Write-Host "❌ 创建包文件失败" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# 清理临时目录
Remove-Item -Path $tempDir -Recurse -Force

$packageSize = (Get-Item $packagePath).Length / 1KB
Write-Host "      ✓ 包文件创建成功: $packageSize KB" -ForegroundColor Green

Write-Host ""
Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Green
Write-Host "  ✓ 构建完成！" -ForegroundColor Green
Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Green
Write-Host ""
Write-Host "📦 输出文件:" -ForegroundColor Cyan
Write-Host "   DLL:          $destDll" -ForegroundColor White
Write-Host "   插件包:       $packagePath" -ForegroundColor White
Write-Host ""
Write-Host "ℹ️  版本信息:" -ForegroundColor Cyan
Write-Host "   版本:         $Version" -ForegroundColor White
Write-Host "   架构:         $Arch" -ForegroundColor White
Write-Host "   配置:         $Config" -ForegroundColor White
Write-Host ""
