@echo off
setlocal enabledelayedexpansion

REM Change the directory to where your .vert and .frag files are located
cd /d %~dp0

REM Iterate through all .vert and .frag files in the directory
for %%f in (*.vert, *.frag) do (
    REM Print the name of the file being processed
    echo Processing file: %%f

    REM Construct the output filename with .spirv extension
    set "spirvOutput=%%~dpnxf.spv"

    REM Execute glslc.exe with the required parameters
    echo Compiling %%f to SPIR-V...
    "%VULKAN_SDK%\bin\glslc.exe" -o "!spirvOutput!" "%%f"
    echo Compilation complete: !spirvOutput!

    REM Construct the JSON output filename
    set "jsonOutput=%%~dpnxf.spv.json"

    REM Execute spirv-cross.exe with the required parameters
    echo Converting !spirvOutput! to JSON...
    "%VULKAN_SDK%\bin\spirv-cross.exe" "!spirvOutput!" --reflect --output "!jsonOutput!"
    echo Conversion complete: !jsonOutput!
)

echo Processing complete.

endlocal
