# PowerShell script for configuring Conan dependencies for both Debug and Release build types

# Get the script's directory (project root)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host "Starting Conan dependency configuration in $scriptDir..."

# Create Debug and Release build directories
$debugBuildDir = Join-Path -Path $scriptDir -ChildPath "build_debug"
$releaseBuildDir = Join-Path -Path $scriptDir -ChildPath "build_release"

Write-Host "Creating build directories..."
New-Item -ItemType Directory -Force -Path $debugBuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $releaseBuildDir | Out-Null

# Configure Debug build
try {
    Write-Host "Configuring Debug build dependencies..."
    # Use explicit command with conanfile path and install folder
    $conanCommand = "conan install " + $scriptDir + " --output-folder=" + $debugBuildDir + " -s build_type=Debug --build=missing -o openscenegraph/*:shared=True -g CMakeDeps -g CMakeToolchain"
    Write-Host "Running: $conanCommand"
    Invoke-Expression $conanCommand
    Write-Host "Debug build dependencies configured successfully!"
} catch {
    Write-Host "Failed to configure Debug build dependencies: $_"
    exit 1
}

# Configure Release build
try {
    Write-Host "Configuring Release build dependencies..."
    # Use explicit command with conanfile path and install folder
    $conanCommand = "conan install " + $scriptDir + " --output-folder=" + $releaseBuildDir + " -s build_type=Release --build=missing -o openscenegraph/*:shared=True -g CMakeDeps -g CMakeToolchain"
    Write-Host "Running: $conanCommand"
    Invoke-Expression $conanCommand
    Write-Host "Release build dependencies configured successfully!"
} catch {
    Write-Host "Failed to configure Release build dependencies: $_"
    exit 1
}

# Create simple build scripts for both configurations
$debugBuildScript = Join-Path -Path $debugBuildDir -ChildPath "build_debug.bat"
$releaseBuildScript = Join-Path -Path $releaseBuildDir -ChildPath "build_release.bat"

# Debug build script
$debugBuildContent = @"
@echo off
cd "%~dp0"
cmake .. -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
cmake --build .
"@
Set-Content -Path $debugBuildScript -Value $debugBuildContent

# Release build script
$releaseBuildContent = @"
@echo off
cd "%~dp0"
cmake .. -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
cmake --build .
"@
Set-Content -Path $releaseBuildScript -Value $releaseBuildContent

Write-Host "Conan dependency configuration completed!"
Write-Host "Created build scripts: $debugBuildScript and $releaseBuildScript"
Write-Host "To build the project, run the appropriate build script from its directory."