# 简单DLL复制脚本

# 设置输出目录
$debugDir = "D:\gitWork\project\ConanTest\build_debug\Debug"
$releaseDir = "D:\gitWork\project\ConanTest\build_release\Release"

Write-Host "开始复制DLL文件..."

# 创建输出目录
if (!(Test-Path $debugDir)) {
    New-Item -ItemType Directory -Force -Path $debugDir
    Write-Host "已创建Debug输出目录"
}

if (!(Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Force -Path $releaseDir
    Write-Host "已创建Release输出目录"
}

# 手动设置Conan包路径
$conanDir = "d:\Conan\conan\p"
Write-Host "检查Conan目录: $conanDir"

# 尝试查找OSG DLL文件
if (Test-Path $conanDir) {
    Write-Host "Conan目录存在，尝试查找DLL文件..."
    # 查找OpenSceneGraph相关文件
    $osgFiles = Get-ChildItem -Path $conanDir -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "osg*.dll" -or $_.Name -like "OpenThreads.dll" }
    if ($osgFiles) {
        Write-Host "找到 $($osgFiles.Count) 个DLL文件"
        # 复制文件到Debug目录
        foreach ($file in $osgFiles) {
            Copy-Item -Path $file.FullName -Destination $debugDir -Force
            Copy-Item -Path $file.FullName -Destination $releaseDir -Force
            Write-Host "已复制: $($file.Name)"
        }
        Write-Host "DLL文件复制完成！"
    } else {
        Write-Host "未找到OSG DLL文件，请手动复制。"
    }
} else {
    Write-Host "Conan目录不存在，请检查路径设置。"
}