#!/bin/bash

# FlowCoro 统一测试脚本
# 用法: ./run_tests.sh [选项]

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
    echo "FlowCoro 测试运行脚本"
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --build, -b        重新构建项目"
    echo "  --clean, -c        清理构建目录"
    echo "  --core            只运行核心功能测试"
    echo "  --database        只运行数据库测试"  
    echo "  --network         只运行网络测试"
    echo "  --performance     运行性能测试"
    echo "  --all             运行所有测试 (默认)"
    echo "  --legacy          运行旧版测试"
    echo "  --verbose, -v     详细输出"
    echo "  --help, -h        显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 --build --core     # 重新构建并运行核心测试"
    echo "  $0 --performance      # 运行性能基准测试"
    echo "  $0 --clean --all      # 清理重建并运行所有测试"
}

# 默认参数
BUILD=false
CLEAN=false
RUN_CORE=false
RUN_DATABASE=false
RUN_NETWORK=false
RUN_PERFORMANCE=false
RUN_ALL=true
RUN_LEGACY=false
VERBOSE=false

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
        --core)
            RUN_CORE=true
            RUN_ALL=false
            shift
            ;;
        --database)
            RUN_DATABASE=true
            RUN_ALL=false
            shift
            ;;
        --network)
            RUN_NETWORK=true
            RUN_ALL=false
            shift
            ;;
        --performance)
            RUN_PERFORMANCE=true
            RUN_ALL=false
            shift
            ;;
        --all)
            RUN_ALL=true
            shift
            ;;
        --legacy)
            RUN_LEGACY=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
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

# 清理构建目录
if [[ "$CLEAN" == true ]]; then
    print_info "清理构建目录..."
    rm -rf "$BUILD_DIR"
fi

# 确保构建目录存在
if [[ ! -d "$BUILD_DIR" ]]; then
    print_info "创建构建目录..."
    mkdir -p "$BUILD_DIR"
    BUILD=true
fi

# 构建项目
if [[ "$BUILD" == true ]]; then
    print_info "构建 FlowCoro 项目..."
    cd "$BUILD_DIR"
    
    if [[ "$VERBOSE" == true ]]; then
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release > cmake.log 2>&1
        make -j$(nproc) > make.log 2>&1
    fi
    
    if [[ $? -eq 0 ]]; then
        print_success "构建完成"
    else
        print_error "构建失败，查看 cmake.log 和 make.log"
        exit 1
    fi
fi

# 切换到构建目录
cd "$BUILD_DIR"

# 运行测试
print_info "运行 FlowCoro 测试套件..."

# 使用统一的测试运行器
if [[ -f "tests/run_all_tests" ]]; then
    print_info "运行统一测试套件..."
    
    test_args=""
    if [[ "$RUN_PERFORMANCE" == true ]]; then
        test_args="--performance"
    fi
    
    if [[ "$VERBOSE" == true ]]; then
        "./tests/run_all_tests" $test_args
    else
        "./tests/run_all_tests" $test_args > test_unified.log 2>&1
    fi
    
    result=$?
    if [[ $result -eq 0 ]]; then
        print_success "所有测试通过! 🎉"
    else
        print_error "测试失败! 查看 test_unified.log"
        exit 1
    fi
else
    print_warning "统一测试运行器不存在，尝试运行单独的测试..."
    
    # 尝试运行单独的测试文件
    test_found=false
    for test in test_core test_database test_network test_performance; do
        if [[ -f "tests/$test" ]]; then
            print_info "运行 $test..."
            "./tests/$test"
            test_found=true
        fi
    done
    
    if [[ "$test_found" == false ]]; then
        print_error "没有找到可运行的测试文件"
        exit 1
    fi
fi

print_success "测试运行完成!"
