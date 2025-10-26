# Simple DLL copy script

# Set output directories
$debugDir = "D:\gitWork\project\ConanTest\build_debug\Debug"
$releaseDir = "D:\gitWork\project\ConanTest\build_release\Release"

Write-Host "Starting DLL copy process..."

# Create output directories
if (!(Test-Path $debugDir)) {
    New-Item -ItemType Directory -Force -Path $debugDir
    Write-Host "Created Debug output directory"
}

if (!(Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Force -Path $releaseDir
    Write-Host "Created Release output directory"
}

# Manual Conan path setup
$conanDir = "d:\Conan\conan\p"
Write-Host "Checking Conan directory: $conanDir"

# Try to find OSG DLL files
if (Test-Path $conanDir) {
    Write-Host "Conan directory exists, searching for DLL files..."
    # Look for OpenSceneGraph related files
    $osgFiles = Get-ChildItem -Path $conanDir -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "osg*.dll" -or $_.Name -like "OpenThreads.dll" }
    if ($osgFiles) {
        Write-Host "Found $($osgFiles.Count) DLL files"
        # Copy files to Debug directory
        foreach ($file in $osgFiles) {
            Copy-Item -Path $file.FullName -Destination $debugDir -Force
            Copy-Item -Path $file.FullName -Destination $releaseDir -Force
            Write-Host "Copied: $($file.Name)"
        }
        Write-Host "DLL copy completed successfully!"
    } else {
        Write-Host "No OSG DLL files found. Please copy them manually."
    }
} else {
    Write-Host "Conan directory does not exist. Please check path settings."
}