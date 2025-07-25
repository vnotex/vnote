#!/bin/bash

# VNote 开发调试脚本
# 功能：构建项目、更新JS文件到用户配置目录、启动调试程序

set -e  # 遇到错误时退出

echo "🔨 开始构建项目..."

# 设置项目路径
PROJECT_ROOT="/Users/cdp/iceblue/clion/vnote3"
BUILD_DIR="/Users/cdp/iceblue/clion/vnote3/build-vnote3-Qt_6_5_3_for_macOS-Debug-cdp"
USER_CONFIG_DIR="/Users/cdp/Library/Application Support/VNote/VNote"

# 构建项目
echo "📦 正在编译..."
cd "$BUILD_DIR"
make -j$(sysctl -n hw.logicalcpu)

echo "✅ 编译完成"

# 更新JavaScript文件到用户配置目录
echo "📁 更新JavaScript文件到用户配置目录..."

# 确保目标目录存在
mkdir -p "$USER_CONFIG_DIR/web"

# 复制修改后的JS文件（使用-f强制覆盖）
echo "   📄 复制 extra..."
cp -rf "$PROJECT_ROOT/src/data/extra/web/" "$USER_CONFIG_DIR/web/" || (echo "   ⚠️  复制失败，可能需要手动复制" && exit 1)



echo "✅ 文件更新完成"



echo "+------------------------------------+"
echo "+              启动程序               +"
echo "+------------------------------------+"

# 启动程序
cd "$BUILD_DIR"
./src/VNote.app/Contents/MacOS/VNote --verbose --log-stderr 