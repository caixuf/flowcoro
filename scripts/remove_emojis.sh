#!/bin/bash
# FlowCoro 
# 

set -e

# 
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DRY_RUN=true
BACKUP=false
VERBOSE=false

# 
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 
EMOJIS=(
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
    "" "" "" "" "" "" "" "" ""
)

# 
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
FlowCoro 

: $0 []

:
    --apply 
    --backup 
    --verbose, -v 
    --help, -h 

:
    $0 # 
    $0 --apply # 
    $0 --apply --backup # 
    $0 --verbose # 

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

# 
should_process_file() {
    local file="$1"
    local basename=$(basename "$file")

    # 
    if [[ "$file" =~ /(\.git|\.vscode|build|\.cache|__pycache__)/ ]]; then
        return 1
    fi

    # 
    for pattern in "${FILE_PATTERNS[@]}"; do
        if [[ "$basename" == $pattern ]] || [[ "$file" == *"$pattern" ]]; then
            return 0
        fi
    done

    return 1
}

# 
backup_file() {
    local file="$1"
    if [[ "$BACKUP" == true ]] && [[ "$DRY_RUN" == false ]]; then
        cp "$file" "$file.bak"
        [[ "$VERBOSE" == true ]] && log ": $file.bak"
    fi
}

# 
process_file() {
    local file="$1"
    local changes=0
    local temp_file=$(mktemp)

    [[ "$VERBOSE" == true ]] && log ": $file"

    # 
    cp "$file" "$temp_file"

    # 
    for emoji in "${EMOJIS[@]}"; do
        if grep -q "$emoji" "$temp_file" 2>/dev/null; then
            local count=$(grep -o "$emoji" "$temp_file" | wc -l)
            if [[ $count -gt 0 ]]; then
                changes=$((changes + count))
                sed -i "s/$emoji//g" "$temp_file" 2>/dev/null || true
            fi
        fi
    done

    # 
    if [[ $changes -gt 0 ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            warn "[] $file:  $changes "
        else
            backup_file "$file"
            mv "$temp_file" "$file"
            success "$file:  $changes "
        fi
        return $changes
    else
        [[ "$VERBOSE" == true ]] && log "$file: "
        rm -f "$temp_file"
        return 0
    fi
}

# 
process_all_files() {
    local total_files=0
    local modified_files=0
    local total_changes=0

    log "..."

    # find
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

    # 
    echo
    echo "=============================="
    echo " "
    echo "=============================="
    echo ": $total_files"
    echo ": $modified_files"
    echo ": $total_changes"

    if [[ "$DRY_RUN" == true ]]; then
        echo
        warn ""
        log " --apply "
    else
        echo
        success ""
        if [[ "$BACKUP" == true ]]; then
            log ".bak"
        fi
    fi
}

# 
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
            error ": $1"
            usage
            exit 1
            ;;
    esac
done

# 
main() {
    echo " FlowCoro "
    echo "=============================="
    echo ": $PROJECT_DIR"
    echo ": $([ "$DRY_RUN" = true ] && echo "" || echo "")"
    echo ": $([ "$BACKUP" = true ] && echo "" || echo "")"
    echo ": $([ "$VERBOSE" = true ] && echo "" || echo "")"
    echo

    if [[ "$DRY_RUN" == false ]]; then
        warn ""
        if [[ "$BACKUP" == true ]]; then
            log ""
        fi
        echo
        read -p "(y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            error ""
            exit 1
        fi
    else
        warn ""
        log " --apply "
        echo
    fi

    process_all_files
}

# 
if [[ ! -f "$PROJECT_DIR/CMakeLists.txt" ]] || [[ ! -d "$PROJECT_DIR/include/flowcoro" ]]; then
    error "FlowCoroscripts"
    exit 1
fi

# 
main
