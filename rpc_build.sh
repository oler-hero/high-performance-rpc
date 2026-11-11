#!/bin/bash
clear
set -e  # 遇到错误立即退出

echo "=== RPC Project Build Script ==="

# 设置环境变量
export VCPKG_ROOT="/root/.vcpkg"
PROTOC="/root/.vcpkg/installed/x64-linux/tools/protobuf/protoc"

echo "Step 1: Compiling Proto Files..."
# 进入 protos 目录
cd protos

# 检查 protoc 是否可用
if [ ! -x "$PROTOC" ]; then
    echo "ERROR: vcpkg protoc not found at $PROTOC"
    exit 1
fi

# 获取 protoc 版本
PROTOC_VERSION=$("$PROTOC" --version | cut -d' ' -f2)
echo "Using protoc version: $PROTOC_VERSION"

# 清理之前生成的文件
rm -f *.pb.h *.pb.cc

# 编译 proto 文件
"$PROTOC" \
    --cpp_out=. \
    --proto_path=. \
    --experimental_allow_proto3_optional \
    *.proto

if [ $? -eq 0 ]; then
    echo "✓ Successfully compiled proto files"
else
    echo "✗ Failed to compile proto files"
    exit 1
fi

# 返回项目根目录
cd ..

echo "Step 2: Building Project..."
# 清理旧的构建文件
rm -rf build
mkdir -p build
cd build

# 配置项目
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# 构建项目
if [ "$(uname)" = "Darwin" ]; then
    cmake --build . -j$(sysctl -n hw.ncpu)
else
    cmake --build . -j$(nproc)
fi

echo "✓ Build completed successfully!"


