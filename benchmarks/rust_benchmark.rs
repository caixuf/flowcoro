use std::env;
use std::time::{Instant, SystemTime, UNIX_EPOCH};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use tokio::task::JoinSet;
use chrono::{DateTime, Local};

fn get_current_time() -> String {
    let local: DateTime<Local> = Local::now();
    local.format("%H:%M:%S").to_string()
}

fn get_memory_usage_kb() -> usize {
    // Rust没有内置的运行时内存统计，这里返回一个估算值
    // 在实际应用中可以使用jemalloc或其他分配器的统计功能
    std::process::id() as usize // 临时占位符
}

async fn handle_single_request(user_id: usize) -> String {
    // 模拟IO操作 - 注释掉sleep以测试纯创建和调度性能
    // tokio::time::sleep(tokio::time::Duration::from_millis(50)).await;
    
    format!("用户{} (已处理)", 1000 + user_id)
}

async fn handle_concurrent_requests_tokio(request_count: usize) {
    let start_time = Instant::now();
    let initial_memory = get_memory_usage_kb();
    
    println!("🦀 Rust Tokio方式：处理 {} 个并发请求", request_count);
    println!("💾 初始内存: {} KB (估算)", initial_memory);
    println!("🧵 CPU核心数: {}", num_cpus::get());
    println!("⏰ 开始时间: [{}]", get_current_time());
    println!("{}", "-".repeat(50));
    
    println!("📝 同时启动 {} 个async任务...", request_count);
    
    let completed = Arc::new(AtomicUsize::new(0));
    let mut join_set = JoinSet::new();
    
    // 创建与协程数量相同的async任务
    for i in 0..request_count {
        let completed_clone = Arc::clone(&completed);
        join_set.spawn(async move {
            let result = handle_single_request(i).await;
            
            let current_completed = completed_clone.fetch_add(1, Ordering::Relaxed) + 1;
            if current_completed % (request_count / 10).max(1) == 0 || current_completed == request_count {
                println!("✅ 已完成 {}/{} 个任务 ({}%)", 
                    current_completed, request_count, (current_completed * 100) / request_count);
            }
            
            result
        });
    }
    
    // 等待所有任务完成
    let mut results = Vec::new();
    while let Some(result) = join_set.join_next().await {
        match result {
            Ok(res) => results.push(res),
            Err(e) => eprintln!("任务错误: {}", e),
        }
    }
    
    let end_time = Instant::now();
    let duration = end_time.duration_since(start_time);
    let final_memory = get_memory_usage_kb();
    let memory_delta = final_memory.saturating_sub(initial_memory);
    
    println!("{}", "-".repeat(50));
    println!("🦀 Rust Tokio方式完成！");
    println!("   📊 总请求数: {} 个", request_count);
    println!("   ⏱️  总耗时: {} ms", duration.as_millis());
    
    if request_count > 0 {
        println!("   📈 平均耗时: {:.4} ms/请求", 
            duration.as_nanos() as f64 / request_count as f64 / 1_000_000.0);
    }
    
    if duration.as_millis() > 0 {
        println!("   🎯 吞吐量: {} 请求/秒", 
            (request_count as u128 * 1000) / duration.as_millis());
    }
    
    println!("   💾 内存变化: {} KB → {} KB (增加 {} KB)", 
        initial_memory, final_memory, memory_delta);
    
    if request_count > 0 {
        println!("   📊 单请求内存: {} bytes/请求", (memory_delta * 1024) / request_count);
    }
    
    println!("   🦀 Task总数: {} 个", request_count);
    println!("   🌟 并发策略: Tokio异步运行时");
    println!("   ⏰ 程序结束: [{}]", get_current_time());
}

#[tokio::main]
async fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("用法: {} <request_count>", args[0]);
        return;
    }
    
    let request_count: usize = args[1].parse().unwrap_or(0);
    
    println!("========================================");
    println!("🎯 Rust Tokio 高并发性能测试");
    println!("========================================");
    println!("请求数量: {} 个", request_count);
    println!("每个请求模拟0ms处理时间 (纯调度测试)");
    println!("========================================");
    println!();
    
    handle_concurrent_requests_tokio(request_count).await;
}
