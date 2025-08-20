#!/bin/bash

# FlowCoro 网络模块全自动化测试脚本
# 完全自动运行，无需用户交互，自动清理

set -e

SESSION_NAME="flowcoro_auto_test"
BUILD_DIR="/home/caixuf/MyCode/flowcord/build"
TEST_PORT=12349
TEST_DURATION=30  # 测试运行时间（秒）

echo "FlowCoro 网络模块自动化测试"
echo "================================="

# 清理函数
cleanup() {
    echo "清理测试环境..."
    tmux kill-session -t "$SESSION_NAME" 2>/dev/null || true
    pkill -f "echo_server $TEST_PORT" 2>/dev/null || true
    rm -f /tmp/flowcoro_test_*.log
}

# 设置清理钩子
trap cleanup EXIT

# 检查依赖
if ! command -v tmux &> /dev/null; then
    echo "错误: tmux 未安装"
    exit 1
fi

if ! command -v nc &> /dev/null; then
    echo "错误: netcat 未安装"
    exit 1
fi

# 确保构建目录存在
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "错误: 构建目录不存在"
    exit 1
fi

# 构建最新版本
echo "构建最新版本..."
cd "$BUILD_DIR"
make echo_server test_network_integration -j$(nproc) > /dev/null

# 清理现有会话
cleanup

echo "创建测试会话..."

# 创建新会话
tmux new-session -d -s "$SESSION_NAME" -c "$BUILD_DIR"

# 分割窗口：左侧服务器，右侧测试，底部状态
tmux split-window -h -t "$SESSION_NAME":0 -c "$BUILD_DIR"
tmux split-window -v -t "$SESSION_NAME":0.1 -c "$BUILD_DIR"

# 自动化测试主函数
run_automated_test() {
    local session="$1"
    
    echo "启动服务器..."
    # 在左窗格启动echo服务器
    tmux send-keys -t "$session":0.0 "cd examples && ./echo_server $TEST_PORT" Enter
    
    # 等待服务器启动
    sleep 3
    
    echo "验证服务器启动..."
    local retry_count=0
    while [[ $retry_count -lt 10 ]]; do
        if netstat -tlnp 2>/dev/null | grep -q ":$TEST_PORT " || ss -tlnp 2>/dev/null | grep -q ":$TEST_PORT "; then
            break
        fi
        sleep 1
        ((retry_count++))
    done
    
    if [[ $retry_count -ge 10 ]]; then
        echo "服务器启动失败"
        return 1
    fi
    
    echo "服务器已启动在端口 $TEST_PORT"
    
    # 执行自动化测试序列
    echo "执行测试序列..."
    
    # 基础Echo测试
    tmux send-keys -t "$session":0.1 "echo '=== 基础Echo测试 ==='" Enter
    tmux send-keys -t "$session":0.1 "echo 'Hello FlowCoro AutoTest!' | nc localhost $TEST_PORT" Enter
    sleep 2
    
    # 多行测试
    tmux send-keys -t "$session":0.1 "echo '=== 多行消息测试 ==='" Enter
    tmux send-keys -t "$session":0.1 "printf 'Line1\\nLine2\\nLine3\\n' | nc localhost $TEST_PORT" Enter
    sleep 2
    
    # 并发测试
    tmux send-keys -t "$session":0.1 "echo '=== 并发连接测试 ==='" Enter
    tmux send-keys -t "$session":0.1 "for i in {1..5}; do echo \"Client-\$i\" | nc localhost $TEST_PORT & done; wait" Enter
    sleep 3
    
    # 集成测试
    tmux send-keys -t "$session":0.2 "echo '=== 网络集成测试 ==='" Enter
    tmux send-keys -t "$session":0.2 "cd tests && timeout 10 ./test_network_integration" Enter
    sleep 11
    
    # 性能测试
    tmux send-keys -t "$session":0.1 "echo '=== 性能测试 ==='" Enter
    tmux send-keys -t "$session":0.1 "time (for i in {1..50}; do echo \"msg\$i\" | nc localhost $TEST_PORT >/dev/null; done)" Enter
    sleep 5
    
    # 压力测试
    tmux send-keys -t "$session":0.1 "echo '=== 压力测试 ==='" Enter
    tmux send-keys -t "$session":0.1 "for batch in {1..5}; do (for i in {1..10}; do echo \"batch\$batch-msg\$i\" | nc localhost $TEST_PORT & done; wait; echo \"Batch \$batch complete\"); done" Enter
    sleep 8
    
    echo "所有测试完成"
    return 0
}

# 执行测试
start_time=$(date +%s)
echo "开始自动化测试 (运行时间: ${TEST_DURATION}秒)..."

if run_automated_test "$SESSION_NAME"; then
    echo "测试序列执行成功"
    
    # 等待指定时间让测试充分运行
    echo "等待测试完成..."
    sleep $TEST_DURATION
    
    # 收集测试结果
    echo "收集测试结果..."
    
    # 检查服务器状态
    if netstat -tlnp 2>/dev/null | grep -q ":$TEST_PORT "; then
        echo "服务器状态: 运行中"
    else
        echo "服务器状态: 已停止"
    fi
    
    # 检查进程
    if pgrep -f "echo_server $TEST_PORT" > /dev/null; then
        echo "进程状态: echo_server 运行中"
    else
        echo "进程状态: echo_server 已停止"
    fi
    
    end_time=$(date +%s)
    duration=$((end_time - start_time))
    
    echo ""
    echo "测试报告"
    echo "========"
    echo "测试端口: $TEST_PORT"
    echo "运行时间: ${duration}秒"
    echo "测试项目: 基础Echo、多行消息、并发连接、集成测试、性能测试、压力测试"
    echo "状态: 完成"
    echo ""
    echo "详细日志可通过以下命令查看:"
    echo "  tmux capture-pane -t $SESSION_NAME:0.0 -p  # 服务器日志"
    echo "  tmux capture-pane -t $SESSION_NAME:0.1 -p  # 客户端测试日志"
    echo "  tmux capture-pane -t $SESSION_NAME:0.2 -p  # 集成测试日志"
    echo ""
    
    # 保存测试日志
    log_file="/tmp/flowcoro_test_$(date +%Y%m%d_%H%M%S).log"
    echo "保存测试日志到: $log_file"
    {
        echo "=== FlowCoro 网络模块测试日志 ==="
        echo "时间: $(date)"
        echo "端口: $TEST_PORT"
        echo "运行时间: ${duration}秒"
        echo ""
        echo "=== 服务器日志 ==="
        tmux capture-pane -t "$SESSION_NAME":0.0 -p
        echo ""
        echo "=== 客户端测试日志 ==="
        tmux capture-pane -t "$SESSION_NAME":0.1 -p
        echo ""
        echo "=== 集成测试日志 ==="
        tmux capture-pane -t "$SESSION_NAME":0.2 -p
    } > "$log_file"
    
    echo "测试完成! 日志已保存"
    
else
    echo "测试执行失败"
    exit 1
fi
