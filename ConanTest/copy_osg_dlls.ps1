# 最简化的PowerShell脚本，用于复制OpenSceneGraph DLL文件

# 直接设置输出目录
$debugDir = "D:\gitWork\project\ConanTest\build_debug\Debug"
$releaseDir = "D:\gitWork\project\ConanTest\build_release\Release"

Write-Host "开始复制OpenSceneGraph DLL文件..."

# 确保输出目录存在
if (-not (Test-Path $debugDir)) {
    New-Item -ItemType Directory -Force -Path $debugDir | Out-Null
    Write-Host "已创建Debug输出目录"
}
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
    Write-Host "已创建Release输出目录"
}

# 使用两个可能的Conan路径
$possiblePaths = @(
    "d:\Conan\conan\p",
    "$env:USERPROFILE\.conan\data"
)

$foundDll = $false

# 尝试每个可能的路径
foreach ($basePath in $possiblePaths) {
    if (Test-Path $basePath) {
        Write-Host "检查路径: $basePath"
        
        # 查找osg相关的DLL文件
        $osgDlls = Get-ChildItem -Path $basePath -Recurse -File -Filter "osg*.dll" -ErrorAction SilentlyContinue
        $threadDll = Get-ChildItem -Path $basePath -Recurse -File -Filter "OpenThreads.dll" -ErrorAction SilentlyContinue
        
        if ($osgDlls -or $threadDll) {
            # 只复制找到的第一个DLL目录中的文件
            if ($osgDlls) {
                $dllDir = Split-Path -Path $osgDlls[0].FullName
                Write-Host "找到OSG DLL文件在: $dllDir"
                Copy-Item -Path "$dllDir\osg*.dll" -Destination $debugDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$dllDir\osg*.dll" -Destination $releaseDir -Force -ErrorAction SilentlyContinue
                Write-Host "已复制OSG DLL文件"
                $foundDll = $true
            }
            
            if ($threadDll) {
                $dllDir = Split-Path -Path $threadDll[0].FullName
                Write-Host "找到OpenThreads DLL在: $dllDir"
                Copy-Item -Path "$dllDir\OpenThreads.dll" -Destination $debugDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$dllDir\OpenThreads.dll" -Destination $releaseDir -Force -ErrorAction SilentlyContinue
                Write-Host "已复制OpenThreads DLL文件"
                $foundDll = $true
            }
            
            break
        }
    }
}

if ($foundDll) {
    Write-Host "DLL文件复制完成！"
} else {
    Write-Host "警告：未找到OpenSceneGraph DLL文件。请手动复制以下DLL到指定位置："
    Write-Host "Debug目录: $debugDir"
    Write-Host "Release目录: $releaseDir"
}