#!/bin/bash

# FlowCoro 
# 

set -e

SESSION_NAME="flowcoro_auto_test"
BUILD_DIR="/home/caixuf/MyCode/flowcord/build"
TEST_PORT=12349
TEST_DURATION=30  # 

echo "FlowCoro "
echo "================================="

# 
cleanup() {
    echo "..."
    tmux kill-session -t "$SESSION_NAME" 2>/dev/null || true
    pkill -f "echo_server $TEST_PORT" 2>/dev/null || true
    rm -f /tmp/flowcoro_test_*.log
}

# 
trap cleanup EXIT

# 
if ! command -v tmux &> /dev/null; then
    echo ": tmux "
    exit 1
fi

if ! command -v nc &> /dev/null; then
    echo ": netcat "
    exit 1
fi

# 
if [[ ! -d "$BUILD_DIR" ]]; then
    echo ": "
    exit 1
fi

# 
echo "..."
cd "$BUILD_DIR"
make echo_server test_network_integration -j$(nproc) > /dev/null

# 
cleanup

echo "..."

# 
tmux new-session -d -s "$SESSION_NAME" -c "$BUILD_DIR"

# 
tmux split-window -h -t "$SESSION_NAME":0 -c "$BUILD_DIR"
tmux split-window -v -t "$SESSION_NAME":0.1 -c "$BUILD_DIR"

# 
run_automated_test() {
    local session="$1"
    
    echo "..."
    # echo
    tmux send-keys -t "$session":0.0 "cd examples && ./echo_server $TEST_PORT" Enter
    
    # 
    sleep 3
    
    echo "..."
    local retry_count=0
    while [[ $retry_count -lt 10 ]]; do
        if netstat -tlnp 2>/dev/null | grep -q ":$TEST_PORT " || ss -tlnp 2>/dev/null | grep -q ":$TEST_PORT "; then
            break
        fi
        sleep 1
        ((retry_count++))
    done
    
    if [[ $retry_count -ge 10 ]]; then
        echo ""
        return 1
    fi
    
    echo " $TEST_PORT"
    
    # 
    echo "..."
    
    # Echo
    tmux send-keys -t "$session":0.1 "echo '=== Echo ==='" Enter
    tmux send-keys -t "$session":0.1 "echo 'Hello FlowCoro AutoTest!' | nc localhost $TEST_PORT" Enter
    sleep 2
    
    # 
    tmux send-keys -t "$session":0.1 "echo '===  ==='" Enter
    tmux send-keys -t "$session":0.1 "printf 'Line1\\nLine2\\nLine3\\n' | nc localhost $TEST_PORT" Enter
    sleep 2
    
    # 
    tmux send-keys -t "$session":0.1 "echo '===  ==='" Enter
    tmux send-keys -t "$session":0.1 "for i in {1..5}; do echo \"Client-\$i\" | nc localhost $TEST_PORT & done; wait" Enter
    sleep 3
    
    # 
    tmux send-keys -t "$session":0.2 "echo '===  ==='" Enter
    tmux send-keys -t "$session":0.2 "cd tests && timeout 10 ./test_network_integration" Enter
    sleep 11
    
    # 
    tmux send-keys -t "$session":0.1 "echo '===  ==='" Enter
    tmux send-keys -t "$session":0.1 "time (for i in {1..50}; do echo \"msg\$i\" | nc localhost $TEST_PORT >/dev/null; done)" Enter
    sleep 5
    
    # 
    tmux send-keys -t "$session":0.1 "echo '===  ==='" Enter
    tmux send-keys -t "$session":0.1 "for batch in {1..5}; do (for i in {1..10}; do echo \"batch\$batch-msg\$i\" | nc localhost $TEST_PORT & done; wait; echo \"Batch \$batch complete\"); done" Enter
    sleep 8
    
    echo ""
    return 0
}

# 
start_time=$(date +%s)
echo " (: ${TEST_DURATION})..."

if run_automated_test "$SESSION_NAME"; then
    echo ""
    
    # 
    echo "..."
    sleep $TEST_DURATION
    
    # 
    echo "..."
    
    # 
    if netstat -tlnp 2>/dev/null | grep -q ":$TEST_PORT "; then
        echo ": "
    else
        echo ": "
    fi
    
    # 
    if pgrep -f "echo_server $TEST_PORT" > /dev/null; then
        echo ": echo_server "
    else
        echo ": echo_server "
    fi
    
    end_time=$(date +%s)
    duration=$((end_time - start_time))
    
    echo ""
    echo ""
    echo "========"
    echo ": $TEST_PORT"
    echo ": ${duration}"
    echo ": Echo"
    echo ": "
    echo ""
    echo ":"
    echo "  tmux capture-pane -t $SESSION_NAME:0.0 -p  # "
    echo "  tmux capture-pane -t $SESSION_NAME:0.1 -p  # "
    echo "  tmux capture-pane -t $SESSION_NAME:0.2 -p  # "
    echo ""
    
    # 
    log_file="/tmp/flowcoro_test_$(date +%Y%m%d_%H%M%S).log"
    echo ": $log_file"
    {
        echo "=== FlowCoro  ==="
        echo ": $(date)"
        echo ": $TEST_PORT"
        echo ": ${duration}"
        echo ""
        echo "===  ==="
        tmux capture-pane -t "$SESSION_NAME":0.0 -p
        echo ""
        echo "===  ==="
        tmux capture-pane -t "$SESSION_NAME":0.1 -p
        echo ""
        echo "===  ==="
        tmux capture-pane -t "$SESSION_NAME":0.2 -p
    } > "$log_file"
    
    echo "! "
    
else
    echo ""
    exit 1
fi
