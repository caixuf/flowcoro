#!/bin/bash

# FlowCoro 
# : ./run_core_test.sh []

set -e

# 
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# 
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 
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

# 
show_help() {
    echo "FlowCoro "
    echo ": $0 []"
    echo ""
    echo ":"
    echo " --build, -b "
    echo " --clean, -c "
    echo " --help, -h "
    echo ""
    echo ":"
    echo " 1.  - "
    echo " 2.  - HTTPSocket"
    echo " 3.  - MySQL"
    echo " 4. RPC - JSON-RPC"
    echo ""
    echo ":"
    echo " $0 # "
    echo " $0 --build # "
    echo " $0 --clean --build # "
}

# 
BUILD=false
CLEAN=false

# 
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
            print_error ": $1"
            show_help
            exit 1
            ;;
    esac
done

# 
if [[ "$CLEAN" == true ]]; then
    BUILD=true
fi

print_info " FlowCoro ..."
print_info ": $PROJECT_ROOT"
print_info ": $BUILD_DIR"

# 
cd "$PROJECT_ROOT" || {
    print_error ": $PROJECT_ROOT"
    exit 1
}

# 
if [[ "$CLEAN" == true ]]; then
    print_info "..."
    if [[ -d "$BUILD_DIR" ]]; then
        rm -rf "$BUILD_DIR"
        print_success ""
    else
        print_info ""
    fi
fi

# 
if [[ ! -d "$BUILD_DIR" ]]; then
    print_info "..."
    mkdir -p "$BUILD_DIR"
fi

# 
if [[ "$BUILD" == true ]]; then
    print_info "..."
    cd "$BUILD_DIR" || {
        print_error ""
        exit 1
    }

    cmake .. || {
        print_error "CMake "
        exit 1
    }

    print_info "..."
    make -j"$(nproc)" || {
        print_error ""
        exit 1
    }

    print_success ""
    cd "$PROJECT_ROOT"
fi

# 
TEST_CORE="$BUILD_DIR/tests/test_core"
TEST_NETWORK="$BUILD_DIR/tests/test_network"
TEST_DATABASE="$BUILD_DIR/tests/test_database"
TEST_RPC="$BUILD_DIR/tests/test_rpc"

if [[ ! -f "$TEST_CORE" ]]; then
    print_error ": $TEST_CORE"
    print_info ": $0 --build"
    exit 1
fi

if [[ ! -f "$TEST_NETWORK" ]]; then
    print_warning ": $TEST_NETWORK"
fi

if [[ ! -f "$TEST_DATABASE" ]]; then
    print_warning ": $TEST_DATABASE"
fi

if [[ ! -f "$TEST_RPC" ]]; then
    print_warning "RPC: $TEST_RPC"
fi

# 
print_info " FlowCoro ..."
echo ""
echo "========================================"
echo " FlowCoro "
echo "========================================"

# 
START_TIME=$(date +%s)

# 
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 
print_info "1. ..."
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if "$TEST_CORE"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
    print_success " !"
else
    FAILED_TESTS=$((FAILED_TESTS + 1))
    print_error " !"
fi
echo ""

# 
if [[ -f "$TEST_NETWORK" ]]; then
    print_info "2. ..."
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if "$TEST_NETWORK"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        print_success " !"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        print_error " !"
    fi
    echo ""
fi

# 
if [[ -f "$TEST_DATABASE" ]]; then
    print_info "3. ..."
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if "$TEST_DATABASE"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        print_success " !"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        print_error " !"
    fi
    echo ""
fi

# RPC
if [[ -f "$TEST_RPC" ]]; then
    print_info "4. RPC..."
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if "$TEST_RPC"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        print_success " RPC!"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        print_error " RPC!"
    fi
    echo ""
fi

# 
if [[ $FAILED_TESTS -eq 0 ]]; then
    TEST_RESULT=0
else
    TEST_RESULT=1
fi

# 
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo ""
echo "========================================"
if [[ $TEST_RESULT -eq 0 ]]; then
    echo -e " ${GREEN}! ($PASSED_TESTS/$TOTAL_TESTS)${NC}"
else
    echo -e " ${RED}! ($PASSED_TESTS/$TOTAL_TESTS , $FAILED_TESTS )${NC}"
fi
echo ": ${DURATION}"
echo "========================================"

# 
print_info ":"
echo " - : $(grep 'FLOWCORO_VERSION' include/flowcoro.hpp | head -1 | cut -d'"' -f2 2>/dev/null || echo '')"
echo " - : $BUILD_DIR"
echo " - : $TOTAL_TESTS"
echo " - /: $PASSED_TESTS/$FAILED_TESTS"

exit $TEST_RESULT
