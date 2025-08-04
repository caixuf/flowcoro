#!/bin/bash

# FlowCoro 构建脚本
# 用法: ./scripts/build.sh [选项]

set -e

# 默认参数
BUILD_TYPE="Release"
BUILD_DIR="build"
INSTALL_PREFIX=""
CMAKE_ARGS=""
PARALLEL_JOBS=$(nproc)

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印帮助信息
print_help() {
    echo "FlowCoro 构建脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo " -h, --help 显示此帮助信息"
    echo " -t, --type TYPE 构建类型: Debug, Release, RelWithDebInfo (默认: Release)"
    echo " -d, --dir DIR 构建目录 (默认: build)"
    echo " -j, --jobs N 并行作业数 (默认: CPU核心数)"
    echo " -p, --prefix PREFIX 安装前缀"
    echo " --tests 启用测试构建"
    echo " --examples 启用示例构建"
    echo " --benchmarks 启用基准测试构建"
    echo " --sanitizers 启用内存检查器"
    echo " --docs 启用文档构建"
    echo " --clean 清理构建目录"
    echo ""
    echo "示例:"
    echo " $0 # 默认Release构建"
    echo " $0 -t Debug --tests # Debug构建并启用测试"
    echo " $0 --clean # 清理构建目录"
}

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    log_info "检查构建依赖..."

    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake 未安装，请先安装 CMake 3.16+"
        exit 1
    fi

    local cmake_version=$(cmake --version | head -n1 | cut -d' ' -f3)
    log_info "CMake 版本: $cmake_version"

    # 检查编译器
    if command -v g++ &> /dev/null; then
        local gcc_version=$(g++ --version | head -n1)
        log_info "GCC: $gcc_version"
    elif command -v clang++ &> /dev/null; then
        local clang_version=$(clang++ --version | head -n1)
        log_info "Clang: $clang_version"
    else
        log_error "未找到支持C++20的编译器"
        exit 1
    fi
}

# 清理构建目录
clean_build() {
    if [ -d "$BUILD_DIR" ]; then
        log_info "清理构建目录: $BUILD_DIR"
        rm -rf "$BUILD_DIR"
        log_success "构建目录已清理"
    else
        log_info "构建目录不存在，无需清理"
    fi
}

# 配置构建
configure_build() {
    log_info "配置构建..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    local cmake_cmd="cmake"
    cmake_cmd="$cmake_cmd -DCMAKE_BUILD_TYPE=$BUILD_TYPE"

    if [ -n "$INSTALL_PREFIX" ]; then
        cmake_cmd="$cmake_cmd -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    fi

    cmake_cmd="$cmake_cmd $CMAKE_ARGS .."

    log_info "执行: $cmake_cmd"
    eval $cmake_cmd

    cd ..
}

# 执行构建
build_project() {
    log_info "开始构建..."

    cd "$BUILD_DIR"
    cmake --build . --parallel $PARALLEL_JOBS
    cd ..

    log_success "构建完成"
}

# 运行测试
run_tests() {
    if [[ "$CMAKE_ARGS" == *"FLOWCORO_BUILD_TESTS=ON"* ]]; then
        log_info "运行测试..."

        cd "$BUILD_DIR"
        ctest --output-on-failure --parallel $PARALLEL_JOBS
        cd ..

        log_success "测试完成"
    fi
}

# 主函数
main() {
    local clean_only=false

    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                print_help
                exit 0
                ;;
            -t|--type)
                BUILD_TYPE="$2"
                shift 2
                ;;
            -d|--dir)
                BUILD_DIR="$2"
                shift 2
                ;;
            -j|--jobs)
                PARALLEL_JOBS="$2"
                shift 2
                ;;
            -p|--prefix)
                INSTALL_PREFIX="$2"
                shift 2
                ;;
            --tests)
                CMAKE_ARGS="$CMAKE_ARGS -DFLOWCORO_BUILD_TESTS=ON"
                shift
                ;;
            --examples)
                CMAKE_ARGS="$CMAKE_ARGS -DFLOWCORO_BUILD_EXAMPLES=ON"
                shift
                ;;
            --benchmarks)
                CMAKE_ARGS="$CMAKE_ARGS -DFLOWCORO_BUILD_BENCHMARKS=ON"
                shift
                ;;
            --sanitizers)
                CMAKE_ARGS="$CMAKE_ARGS -DFLOWCORO_ENABLE_SANITIZERS=ON"
                shift
                ;;
            --docs)
                CMAKE_ARGS="$CMAKE_ARGS -DFLOWCORO_BUILD_DOCS=ON"
                shift
                ;;
            --clean)
                clean_only=true
                shift
                ;;
            *)
                log_error "未知参数: $1"
                print_help
                exit 1
                ;;
        esac
    done

    # 显示构建信息
    log_info "FlowCoro 构建配置:"
    log_info " 构建类型: $BUILD_TYPE"
    log_info " 构建目录: $BUILD_DIR"
    log_info " 并行作业: $PARALLEL_JOBS"
    log_info " CMake参数: $CMAKE_ARGS"

    # 执行清理
    if [ "$clean_only" = true ]; then
        clean_build
        exit 0
    fi

    # 检查依赖
    check_dependencies

    # 执行构建流程
    configure_build
    build_project
    run_tests

    log_success "FlowCoro 构建成功完成！"
    log_info "构建产物位于: $BUILD_DIR"
}

# 运行主函数
main "$@"
