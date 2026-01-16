#!/bin/bash

# FlowCoro 
# : ./scripts/build.sh []

set -e

# 
BUILD_TYPE="Release"
BUILD_DIR="build"
INSTALL_PREFIX=""
CMAKE_ARGS=""
PARALLEL_JOBS=$(nproc)

# 
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 
print_help() {
    echo "FlowCoro "
    echo ""
    echo ": $0 []"
    echo ""
    echo ":"
    echo " -h, --help "
    echo " -t, --type TYPE : Debug, Release, RelWithDebInfo (: Release)"
    echo " -d, --dir DIR  (: build)"
    echo " -j, --jobs N  (: CPU)"
    echo " -p, --prefix PREFIX "
    echo " --tests "
    echo " --examples "
    echo " --benchmarks "
    echo " --sanitizers "
    echo " --docs "
    echo " --clean "
    echo ""
    echo ":"
    echo " $0 # Release"
    echo " $0 -t Debug --tests # Debug"
    echo " $0 --clean # "
}

# 
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

# 
check_dependencies() {
    log_info "..."

    # CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake  CMake 3.16+"
        exit 1
    fi

    local cmake_version=$(cmake --version | head -n1 | cut -d' ' -f3)
    log_info "CMake : $cmake_version"

    # 
    if command -v g++ &> /dev/null; then
        local gcc_version=$(g++ --version | head -n1)
        log_info "GCC: $gcc_version"
    elif command -v clang++ &> /dev/null; then
        local clang_version=$(clang++ --version | head -n1)
        log_info "Clang: $clang_version"
    else
        log_error "C++20"
        exit 1
    fi
}

# 
clean_build() {
    if [ -d "$BUILD_DIR" ]; then
        log_info ": $BUILD_DIR"
        rm -rf "$BUILD_DIR"
        log_success ""
    else
        log_info ""
    fi
}

# 
configure_build() {
    log_info "..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    local cmake_cmd="cmake"
    cmake_cmd="$cmake_cmd -DCMAKE_BUILD_TYPE=$BUILD_TYPE"

    if [ -n "$INSTALL_PREFIX" ]; then
        cmake_cmd="$cmake_cmd -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    fi

    cmake_cmd="$cmake_cmd $CMAKE_ARGS .."

    log_info ": $cmake_cmd"
    eval $cmake_cmd

    cd ..
}

# 
build_project() {
    log_info "..."

    cd "$BUILD_DIR"
    cmake --build . --parallel $PARALLEL_JOBS
    cd ..

    log_success ""
}

# 
run_tests() {
    if [[ "$CMAKE_ARGS" == *"FLOWCORO_BUILD_TESTS=ON"* ]]; then
        log_info "..."

        cd "$BUILD_DIR"
        ctest --output-on-failure --parallel $PARALLEL_JOBS
        cd ..

        log_success ""
    fi
}

# 
main() {
    local clean_only=false

    # 
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
                log_error ": $1"
                print_help
                exit 1
                ;;
        esac
    done

    # 
    log_info "FlowCoro :"
    log_info " : $BUILD_TYPE"
    log_info " : $BUILD_DIR"
    log_info " : $PARALLEL_JOBS"
    log_info " CMake: $CMAKE_ARGS"

    # 
    if [ "$clean_only" = true ]; then
        clean_build
        exit 0
    fi

    # 
    check_dependencies

    # 
    configure_build
    build_project
    run_tests

    log_success "FlowCoro "
    log_info ": $BUILD_DIR"
}

# 
main "$@"
