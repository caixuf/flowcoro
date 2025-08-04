#!/bin/bash
# FlowCoro 表情符号清理脚本
# 一键替换项目中所有代码文件的表情符号

set -e

# 脚本配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DRY_RUN=true
BACKUP=false
VERBOSE=false

# 颜色代码
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 常见表情符号列表
EMOJIS=(
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "️" "" "️"
    "" "️" "" "️" "" "" "️" "" "️"
    "" "" "️" "" "" "" "" "" ""
)

# 支持的文件扩展名
FILE_PATTERNS=(
    "*.cpp" "*.hpp" "*.h" "*.c" "*.cc" "*.cxx"
    "*.md" "*.txt" "*.rst"
    "*.cmake" "CMakeLists.txt"
    "*.py" "*.sh" "*.bash"
    "*.json" "*.yaml" "*.yml"
    "*.log"
)

usage() {
    cat << EOF
FlowCoro 表情符号清理工具

用法: $0 [选项]

选项:
    --apply 实际执行清理（默认为试运行模式）
    --backup 修改前备份原文件
    --verbose, -v 显示详细输出
    --help, -h 显示此帮助信息

示例:
    $0 # 试运行模式，不修改文件
    $0 --apply # 实际执行清理
    $0 --apply --backup # 执行清理并备份
    $0 --verbose # 详细模式试运行

EOF
}

log() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# 检查文件是否应该被处理
should_process_file() {
    local file="$1"
    local basename=$(basename "$file")

    # 跳过特定目录
    if [[ "$file" =~ /(\.git|\.vscode|build|\.cache|__pycache__)/ ]]; then
        return 1
    fi

    # 检查文件模式
    for pattern in "${FILE_PATTERNS[@]}"; do
        if [[ "$basename" == $pattern ]] || [[ "$file" == *"$pattern" ]]; then
            return 0
        fi
    done

    return 1
}

# 备份文件
backup_file() {
    local file="$1"
    if [[ "$BACKUP" == true ]] && [[ "$DRY_RUN" == false ]]; then
        cp "$file" "$file.bak"
        [[ "$VERBOSE" == true ]] && log "已备份: $file.bak"
    fi
}

# 处理单个文件
process_file() {
    local file="$1"
    local changes=0
    local temp_file=$(mktemp)

    [[ "$VERBOSE" == true ]] && log "处理文件: $file"

    # 复制原文件到临时文件
    cp "$file" "$temp_file"

    # 逐个替换表情符号
    for emoji in "${EMOJIS[@]}"; do
        if grep -q "$emoji" "$temp_file" 2>/dev/null; then
            local count=$(grep -o "$emoji" "$temp_file" | wc -l)
            if [[ $count -gt 0 ]]; then
                changes=$((changes + count))
                sed -i "s/$emoji//g" "$temp_file" 2>/dev/null || true
            fi
        fi
    done

    # 如果有变化
    if [[ $changes -gt 0 ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            warn "[试运行] $file: 将清理 $changes 个表情符号"
        else
            backup_file "$file"
            mv "$temp_file" "$file"
            success "$file: 已清理 $changes 个表情符号"
        fi
        return $changes
    else
        [[ "$VERBOSE" == true ]] && log "$file: 无需处理"
        rm -f "$temp_file"
        return 0
    fi
}

# 查找并处理所有文件
process_all_files() {
    local total_files=0
    local modified_files=0
    local total_changes=0

    log "开始扫描项目文件..."

    # 使用find命令查找所有支持的文件
    while IFS= read -r -d '' file; do
        if should_process_file "$file"; then
            ((total_files++))
            changes=$(process_file "$file")
            if [[ $changes -gt 0 ]]; then
                ((modified_files++))
                total_changes=$((total_changes + changes))
            fi
        fi
    done < <(find "$PROJECT_DIR" -type f -print0)

    # 打印摘要
    echo
    echo "=============================="
    echo " 处理摘要"
    echo "=============================="
    echo "总处理文件数: $total_files"
    echo "修改文件数: $modified_files"
    echo "总替换数量: $total_changes"

    if [[ "$DRY_RUN" == true ]]; then
        echo
        warn "这是试运行模式，实际文件未被修改"
        log "使用 --apply 参数来实际执行修改"
    else
        echo
        success "表情符号清理完成！"
        if [[ "$BACKUP" == true ]]; then
            log "原文件已备份（.bak扩展名）"
        fi
    fi
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --apply)
            DRY_RUN=false
            shift
            ;;
        --backup)
            BACKUP=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            error "未知参数: $1"
            usage
            exit 1
            ;;
    esac
done

# 主程序
main() {
    echo " FlowCoro 表情符号清理工具"
    echo "=============================="
    echo "项目目录: $PROJECT_DIR"
    echo "运行模式: $([ "$DRY_RUN" = true ] && echo "试运行" || echo "实际执行")"
    echo "备份选项: $([ "$BACKUP" = true ] && echo "是" || echo "否")"
    echo "详细模式: $([ "$VERBOSE" = true ] && echo "是" || echo "否")"
    echo

    if [[ "$DRY_RUN" == false ]]; then
        warn "即将开始清理表情符号，这将修改文件内容！"
        if [[ "$BACKUP" == true ]]; then
            log "将创建备份文件"
        fi
        echo
        read -p "确认继续？(y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            error "操作已取消"
            exit 1
        fi
    else
        warn "当前为试运行模式，不会修改任何文件"
        log "使用 --apply 参数来实际执行修改"
        echo
    fi

    process_all_files
}

# 检查是否在正确的目录中运行
if [[ ! -f "$PROJECT_DIR/CMakeLists.txt" ]] || [[ ! -d "$PROJECT_DIR/include/flowcoro" ]]; then
    error "请在FlowCoro项目根目录或scripts目录中运行此脚本"
    exit 1
fi

# 运行主程序
main
