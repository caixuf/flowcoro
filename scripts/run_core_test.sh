#!/bin/bash

# FlowCoro 核心测试脚本
# 用法: ./run_core_test.sh [选项]

set -e

# 脚本配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示帮助信息
show_help() {
    echo "FlowCoro 核心测试运行脚本"
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --build, -b        重新构建项目"
    echo "  --clean, -c        清理构建目录"
    echo "  --help, -h         显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                 # 运行核心测试"
    echo "  $0 --build         # 重新构建并运行核心测试"
    echo "  $0 --clean --build # 清理重建并运行核心测试"
}

# 默认参数
BUILD=false
CLEAN=false

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --build|-b)
            BUILD=true
            shift
            ;;
        --clean|-c)
            CLEAN=true
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            print_error "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

# 如果清理，则同时构建
if [[ "$CLEAN" == true ]]; then
    BUILD=true
fi

print_info "开始 FlowCoro 核心测试..."
print_info "项目根目录: $PROJECT_ROOT"
print_info "构建目录: $BUILD_DIR"

# 确保在项目根目录
cd "$PROJECT_ROOT" || {
    print_error "无法切换到项目根目录: $PROJECT_ROOT"
    exit 1
}

# 清理构建目录
if [[ "$CLEAN" == true ]]; then
    print_info "清理构建目录..."
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        print_success "构建目录已清理"
    else
        print_info "构建目录不存在，跳过清理"
    fi
fi

# 创建构建目录
if [[ ! -d "$BUILD_DIR" ]]; then
    print_info "创建构建目录..."
    mkdir -p "$BUILD_DIR"
fi

# 构建项目
if [[ "$BUILD" == true ]]; then
    print_info "配置项目..."
    cd "$BUILD_DIR" || {
        print_error "无法切换到构建目录"
        exit 1
    }
    
    cmake .. || {
        print_error "CMake 配置失败"
        exit 1
    }
    
    print_info "构建项目..."
    make -j"$(nproc)" || {
        print_error "构建失败"
        exit 1
    }
    
    print_success "项目构建完成"
    cd "$PROJECT_ROOT"
fi

# 确保测试可执行文件存在
TEST_EXECUTABLE="$BUILD_DIR/tests/test_core"
if [[ ! -f "$TEST_EXECUTABLE" ]]; then
    print_error "测试可执行文件不存在: $TEST_EXECUTABLE"
    print_info "请先运行: $0 --build"
    exit 1
fi

# 运行核心测试
print_info "运行核心功能测试..."
echo ""
echo "========================================"
echo "🧪 FlowCoro 核心功能测试开始"
echo "========================================"

# 记录开始时间
START_TIME=$(date +%s)

# 运行测试并捕获退出状态
if "$TEST_EXECUTABLE"; then
    TEST_RESULT=0
    print_success "核心测试通过!"
else
    TEST_RESULT=$?
    print_error "核心测试失败! (退出码: $TEST_RESULT)"
fi

# 记录结束时间并计算耗时
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo ""
echo "========================================"
if [[ $TEST_RESULT -eq 0 ]]; then
    echo -e "🎉 ${GREEN}所有核心测试通过!${NC}"
else
    echo -e "💥 ${RED}测试失败!${NC}"
fi
echo "测试耗时: ${DURATION}秒"
echo "========================================"

# 显示项目状态
print_info "项目状态:"
echo "  - 版本: $(grep 'FLOWCORO_VERSION' include/flowcoro.hpp | head -1 | cut -d'"' -f2 2>/dev/null || echo '未知')"
echo "  - 构建目录: $BUILD_DIR"
echo "  - 测试可执行文件: $TEST_EXECUTABLE"

exit $TEST_RESULT
