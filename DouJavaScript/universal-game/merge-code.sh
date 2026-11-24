#!/bin/bash

# 抖音小游戏项目合并脚本
# 用于将多个源文件合并成一个 game.js 文件

echo "================================"
echo "抖音小游戏代码合并工具"
echo "================================"
echo ""

# 设置项目目录
PROJECT_DIR="/home/mte/FOLD/DouJavaScript/universal-game"
OUTPUT_FILE="$PROJECT_DIR/game.js"

# 检查项目目录是否存在
if [ ! -d "$PROJECT_DIR" ]; then
    echo "错误：项目目录不存在"
    echo "请先创建项目或修改 PROJECT_DIR 变量"
    exit 1
fi

# 进入项目目录
cd "$PROJECT_DIR"

# 显示要合并的文件
echo "将要合并的文件："
echo "1. src/adapter.js"
echo "2. src/ad-manager.js"
echo "3. src/game-core.js"
echo "4. game-douyin.js"
echo ""

# 询问用户确认
read -p "确认合并？(y/n): " confirm
if [ "$confirm" != "y" ] && [ "$confirm" != "Y" ]; then
    echo "已取消"
    exit 0
fi

# 创建输出文件
echo "" > "$OUTPUT_FILE"

# 添加文件头注释
echo "/**" >> "$OUTPUT_FILE"
echo " * 抖音小游戏 - 合并后的代码" >> "$OUTPUT_FILE"
echo " * 自动生成时间：$(date '+%Y-%m-%d %H:%M:%S')" >> "$OUTPUT_FILE"
echo " */" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# 合并文件
files=(
    "src/adapter.js"
    "src/ad-manager.js"
    "src/game-core.js"
    "game-douyin.js"
)

for file in "${files[@]}"; do
    if [ -f "$file" ]; then
        echo "合并：$file"
        echo "// ========== $file ==========" >> "$OUTPUT_FILE"
        cat "$file" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    else
        echo "警告：文件不存在 - $file"
    fi
done

echo ""
echo "✅ 合并完成！"
echo "输出文件：$OUTPUT_FILE"
echo ""

# 显示文件大小
file_size=$(wc -c < "$OUTPUT_FILE")
echo "文件大小：$file_size 字节 ($(echo "scale=2; $file_size / 1024" | bc) KB)"
echo ""

# 检查文件大小是否超过限制
max_size=$((4 * 1024 * 1024))  # 4MB
if [ "$file_size" -gt "$max_size" ]; then
    echo "⚠️  警告：文件大小超过 4MB 限制！"
    echo "建议："
    echo "1. 移除未使用的代码"
    echo "2. 压缩代码"
    echo "3. 使用分包加载"
else
    echo "✅ 文件大小符合要求（< 4MB）"
fi

echo ""
echo "下一步："
echo "1. 在抖音开发者工具中导入项目"
echo "2. 配置 AppID 和广告 ID"
echo "3. 真机测试"
echo "4. 上传发布"
