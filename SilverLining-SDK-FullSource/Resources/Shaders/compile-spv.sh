#!/bin/bash

# Check if /usr/bin/glslc and /usr/bin/spirv-cross exist
if [ ! -f "/usr/bin/glslc" ]; then
    echo "Error: /usr/bin/glslc not found. Please ensure the Vulkan SDK is installed."
    exit 1
fi

if [ ! -f "/usr/bin/spirv-cross" ]; then
    echo "Error: /usr/bin/spirv-cross not found. Please ensure the Vulkan SDK is installed."
    exit 1
fi

# Change the directory to where your .vert and .frag files are located
cd "$(dirname "$0")"

# Iterate through all .vert and .frag files in the directory
for f in *.vert *.frag; do
    # Print the name of the file being processed
    echo "Processing file: $f"

    # Construct the output filename with .spv extension
    spirvOutput="${f}.spv"

    # Execute glslc with the required parameters
    echo "Compiling $f to SPIR-V..."
    /usr/bin/glslc -o "$spirvOutput" "$f"
    echo "Compilation complete: $spirvOutput"

    # Construct the JSON output filename
    jsonOutput="${f}.spv.json"

    # Execute spirv-cross with the required parameters
    echo "Converting $spirvOutput to JSON..."
    /usr/bin/spirv-cross "$spirvOutput" --reflect --output "$jsonOutput"
    echo "Conversion complete: $jsonOutput"
done

echo "Processing complete."
