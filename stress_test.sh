#!/bin/bash

# RPC 压测脚本
# 使用方法：./stress_test.sh [OPTIONS]

# 默认参数
CONCURRENT_CLIENTS=10    # 并发客户端数量
REQUESTS_PER_CLIENT=100  # 每个客户端的请求数
TEST_DURATION=0          # 测试时长（秒），0表示不限制时长
SERVER_HOST="127.0.0.1"  # 服务器地址
SERVER_PORT=8989         # 服务器端口
OUTPUT_FILE="stress_test_results.txt"  # 结果输出文件
BUILD_DIR="build"        # 构建目录
VERBOSE=false            # 是否显示详细日志

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 帮助信息
show_help() {
    echo "RPC 压测脚本"
    echo ""
    echo "使用方法: $0 [OPTIONS]"
    echo ""
    echo "选项:"
    echo "  -c, --clients NUM        并发客户端数量 (默认: $CONCURRENT_CLIENTS)"
    echo "  -r, --requests NUM       每个客户端的请求数 (默认: $REQUESTS_PER_CLIENT)"
    echo "  -t, --time NUM          测试时长(秒),0表示不限制 (默认: $TEST_DURATION)"
    echo "  -h, --host HOST         服务器地址 (默认: $SERVER_HOST)"
    echo "  -p, --port PORT         服务器端口 (默认: $SERVER_PORT)"
    echo "  -o, --output FILE       结果输出文件 (默认: $OUTPUT_FILE)"
    echo "  -b, --build-dir DIR     构建目录 (默认: $BUILD_DIR)"
    echo "  -v, --verbose           显示详细日志"
    echo "  --help                  显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 -c 50 -r 200                    # 50个并发客户端，每个发送200个请求"
    echo "  $0 -c 20 -t 120                    # 20个并发客户端，持续测试120秒"
    echo "  $0 -c 100 -r 1000 -o results.txt   # 自定义输出文件"
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clients)
            CONCURRENT_CLIENTS="$2"
            shift 2
            ;;
        -r|--requests)
            REQUESTS_PER_CLIENT="$2"
            shift 2
            ;;
        -t|--time)
            TEST_DURATION="$2"
            shift 2
            ;;
        -h|--host)
            SERVER_HOST="$2"
            shift 2
            ;;
        -p|--port)
            SERVER_PORT="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -b|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            echo "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

# 验证参数
if [[ ! $CONCURRENT_CLIENTS =~ ^[0-9]+$ ]] || [[ $CONCURRENT_CLIENTS -le 0 ]]; then
    echo -e "${RED}错误: 并发客户端数量必须是正整数${NC}"
    exit 1
fi

if [[ ! $REQUESTS_PER_CLIENT =~ ^[0-9]+$ ]] || [[ $REQUESTS_PER_CLIENT -le 0 ]]; then
    echo -e "${RED}错误: 每个客户端请求数必须是正整数${NC}"
    exit 1
fi

if [[ ! $TEST_DURATION =~ ^[0-9]+$ ]] || [[ $TEST_DURATION -lt 0 ]]; then
    echo -e "${RED}错误: 测试时长必须是非负整数${NC}"
    exit 1
fi

# 检查构建目录和可执行文件
if [[ ! -d "$BUILD_DIR" ]]; then
    echo -e "${RED}错误: 构建目录 '$BUILD_DIR' 不存在${NC}"
    echo "请先运行 './rpc_build.sh' 构建项目"
    exit 1
fi

CLIENT_BINARY="$BUILD_DIR/stress_test_client"
if [[ ! -f "$CLIENT_BINARY" ]]; then
    echo -e "${RED}错误: 压测客户端可执行文件 '$CLIENT_BINARY' 不存在${NC}"
    echo "请先运行 './rpc_build.sh' 构建项目"
    exit 1
fi

# 创建临时目录
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# 打印测试配置
echo -e "${BLUE}========== RPC 压测配置 ==========${NC}"
echo -e "${YELLOW}并发客户端数量:${NC} $CONCURRENT_CLIENTS"
echo -e "${YELLOW}每客户端请求数:${NC} $REQUESTS_PER_CLIENT"
if [[ $TEST_DURATION -gt 0 ]]; then
    echo -e "${YELLOW}测试时长:${NC} ${TEST_DURATION}秒"
else
    echo -e "${YELLOW}测试模式:${NC} 固定请求数"
fi
echo -e "${YELLOW}服务器地址:${NC} $SERVER_HOST:$SERVER_PORT"
echo -e "${YELLOW}总请求数:${NC} $((CONCURRENT_CLIENTS * REQUESTS_PER_CLIENT))"
echo -e "${BLUE}=================================${NC}"
echo

# 检查服务器是否可达
echo -e "${YELLOW}检查服务器连接...${NC}"
if ! nc -z $SERVER_HOST $SERVER_PORT 2>/dev/null; then
    echo -e "${RED}警告: 无法连接到 $SERVER_HOST:$SERVER_PORT${NC}"
    echo "请确保服务器正在运行"
    read -p "是否继续测试? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
else
    echo -e "${GREEN}服务器连接正常${NC}"
fi

# 开始压测
echo -e "${GREEN}开始压测...${NC}"
START_TIME=$(date +%s)

# 计算总请求数
TOTAL_REQUESTS=$((CONCURRENT_CLIENTS * REQUESTS_PER_CLIENT))

# 构建压测客户端参数
CLIENT_ARGS="-c $CONCURRENT_CLIENTS -n $TOTAL_REQUESTS"
if [[ $TEST_DURATION -gt 0 ]]; then
    CLIENT_ARGS="-c $CONCURRENT_CLIENTS -t $TEST_DURATION"
fi

if [[ "$VERBOSE" == "true" ]]; then
    CLIENT_ARGS="$CLIENT_ARGS -v"
fi

echo -e "${YELLOW}运行命令: $CLIENT_BINARY $CLIENT_ARGS${NC}"

# 运行压测客户端
$CLIENT_BINARY $CLIENT_ARGS > "$TEMP_DIR/stress_test.log" 2>&1
TEST_EXIT_CODE=$?

END_TIME=$(date +%s)
TOTAL_TIME=$((END_TIME - START_TIME))

if [[ $TEST_EXIT_CODE -eq 0 ]]; then
    echo -e "${GREEN}压测完成！${NC}"
else
    echo -e "${RED}压测失败，退出码: $TEST_EXIT_CODE${NC}"
fi

# 显示结果
echo -e "${BLUE}========== 压测结果 ==========${NC}"
cat "$TEMP_DIR/stress_test.log"

# 保存详细结果到文件
{
    echo "==================== RPC 压测结果 ===================="
    echo "测试时间: $(date)"
    echo "测试配置:"
    echo "  - 并发客户端数量: $CONCURRENT_CLIENTS"
    echo "  - 每客户端请求数: $REQUESTS_PER_CLIENT"
    echo "  - 测试时长: ${TOTAL_TIME}秒"
    echo "  - 服务器地址: $SERVER_HOST:$SERVER_PORT"
    echo ""
    echo "压测客户端输出:"
    cat "$TEMP_DIR/stress_test.log"
    echo ""
    echo "================================================="
} > "$OUTPUT_FILE"

echo -e "${GREEN}详细结果已保存到: $OUTPUT_FILE${NC}"

# 提供优化建议
echo -e "${BLUE}========== 优化建议 ==========${NC}"
if [[ $(echo "$success_rate < 95" | bc -l) -eq 1 ]]; then
    echo -e "${RED}• 成功率偏低，建议检查服务器性能和网络连接${NC}"
fi

if [[ $(echo "$qps < 100" | bc -l) -eq 1 ]]; then
    echo -e "${YELLOW}• QPS较低，可以尝试：${NC}"
    echo "  - 增加服务器线程池大小"
    echo "  - 优化网络配置"
    echo "  - 检查服务器资源使用情况"
fi

if [[ $failed_requests -gt 0 ]]; then
    echo -e "${YELLOW}• 存在失败请求，建议检查：${NC}"
    echo "  - 服务器错误日志"
    echo "  - 网络稳定性"
    echo "  - 超时配置"
fi

echo -e "${GREEN}压测完成！${NC}" 