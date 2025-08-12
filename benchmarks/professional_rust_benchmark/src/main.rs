use std::time::{Instant, SystemTime, UNIX_EPOCH};
use std::collections::VecDeque;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use tokio::task::JoinSet;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkStats {
    pub measurements: Vec<f64>,
    pub min_ns: f64,
    pub max_ns: f64,
    pub mean_ns: f64,
    pub median_ns: f64,
    pub stddev_ns: f64,
    pub p95_ns: f64,
    pub p99_ns: f64,
}

impl BenchmarkStats {
    pub fn new() -> Self {
        Self {
            measurements: Vec::new(),
            min_ns: 0.0,
            max_ns: 0.0,
            mean_ns: 0.0,
            median_ns: 0.0,
            stddev_ns: 0.0,
            p95_ns: 0.0,
            p99_ns: 0.0,
        }
    }

    pub fn calculate(&mut self) {
        if self.measurements.is_empty() {
            return;
        }

        self.measurements.sort_by(|a, b| a.partial_cmp(b).unwrap());

        self.min_ns = self.measurements[0];
        self.max_ns = self.measurements[self.measurements.len() - 1];

        // Calculate mean
        let sum: f64 = self.measurements.iter().sum();
        self.mean_ns = sum / self.measurements.len() as f64;

        // Calculate median
        let n = self.measurements.len();
        self.median_ns = if n % 2 == 0 {
            (self.measurements[n / 2 - 1] + self.measurements[n / 2]) / 2.0
        } else {
            self.measurements[n / 2]
        };

        // Calculate percentiles
        self.p95_ns = self.measurements[(n as f64 * 0.95) as usize];
        self.p99_ns = self.measurements[(n as f64 * 0.99) as usize];

        // Calculate standard deviation
        let variance: f64 = self.measurements
            .iter()
            .map(|x| (x - self.mean_ns).powi(2))
            .sum::<f64>() / self.measurements.len() as f64;
        self.stddev_ns = variance.sqrt();
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkResult {
    pub name: String,
    pub stats: BenchmarkStats,
    pub iterations: usize,
    pub total_time_ns: f64,
}

impl BenchmarkResult {
    pub fn new(name: String) -> Self {
        Self {
            name,
            stats: BenchmarkStats::new(),
            iterations: 0,
            total_time_ns: 0.0,
        }
    }

    pub fn print_summary(&self) {
        let throughput = 1e9 / self.stats.mean_ns;
        println!("{:<30} {:>10} {:>12.0} ns {:>12.0} ns {:>14.2} ops/sec",
            self.name, self.iterations, self.stats.mean_ns, self.stats.median_ns, throughput);
    }

    pub fn print_detailed(&self) {
        let throughput = 1e9 / self.stats.mean_ns;
        println!("\n{} - Detailed Statistics:", self.name);
        println!("  Iterations:    {}", self.iterations);
        println!("  Mean:          {:.0} ns", self.stats.mean_ns);
        println!("  Median:        {:.0} ns", self.stats.median_ns);
        println!("  Min:           {:.0} ns", self.stats.min_ns);
        println!("  Max:           {:.0} ns", self.stats.max_ns);
        println!("  Std Dev:       {:.0} ns", self.stats.stddev_ns);
        println!("  95th pct:      {:.0} ns", self.stats.p95_ns);
        println!("  99th pct:      {:.0} ns", self.stats.p99_ns);
        println!("  Throughput:    {:.2} ops/sec", throughput);
    }
}

pub struct BenchmarkRunner {
    warmup_iterations: usize,
    min_iterations: usize,
    max_iterations: usize,
    min_benchmark_time_ns: u128,
}

impl BenchmarkRunner {
    pub fn new() -> Self {
        Self {
            warmup_iterations: 10,
            min_iterations: 100,
            max_iterations: 10000,
            min_benchmark_time_ns: 100_000_000, // 100ms minimum
        }
    }

    pub async fn run<F, Fut>(&self, name: &str, mut benchmark_func: F) -> BenchmarkResult
    where
        F: FnMut() -> Fut,
        Fut: std::future::Future<Output = ()>,
    {
        // Warmup phase
        for _ in 0..self.warmup_iterations {
            benchmark_func().await;
        }

        let mut result = BenchmarkResult::new(name.to_string());
        let total_start = Instant::now();
        let mut iterations = self.min_iterations;
        let mut elapsed = 0u128;

        while elapsed < self.min_benchmark_time_ns && iterations <= self.max_iterations {
            for _ in 0..iterations {
                let start = Instant::now();
                benchmark_func().await;
                let duration = start.elapsed();
                result.stats.measurements.push(duration.as_nanos() as f64);
            }

            elapsed = total_start.elapsed().as_nanos();
            if elapsed < self.min_benchmark_time_ns {
                iterations = std::cmp::min(iterations * 2, self.max_iterations);
            }
        }

        result.iterations = result.stats.measurements.len();
        result.total_time_ns = elapsed as f64;
        result.stats.calculate();
        result
    }

    pub fn run_sync<F>(&self, name: &str, mut benchmark_func: F) -> BenchmarkResult
    where
        F: FnMut(),
    {
        // Warmup phase
        for _ in 0..self.warmup_iterations {
            benchmark_func();
        }

        let mut result = BenchmarkResult::new(name.to_string());
        let total_start = Instant::now();
        let mut iterations = self.min_iterations;
        let mut elapsed = 0u128;

        while elapsed < self.min_benchmark_time_ns && iterations <= self.max_iterations {
            for _ in 0..iterations {
                let start = Instant::now();
                benchmark_func();
                let duration = start.elapsed();
                result.stats.measurements.push(duration.as_nanos() as f64);
            }

            elapsed = total_start.elapsed().as_nanos();
            if elapsed < self.min_benchmark_time_ns {
                iterations = std::cmp::min(iterations * 2, self.max_iterations);
            }
        }

        result.iterations = result.stats.measurements.len();
        result.total_time_ns = elapsed as f64;
        result.stats.calculate();
        result
    }
}

// Benchmark functions

async fn benchmark_task_creation() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run("Task Creation", || async {
        let handle = tokio::spawn(async {
            42
        });
        let _ = handle.await.unwrap();
    }).await
}

async fn benchmark_channel_ops() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run("Channel Operations", || async {
        let (tx, mut rx) = tokio::sync::mpsc::channel(1);
        tx.send(42).await.unwrap();
        let _ = rx.recv().await.unwrap();
    }).await
}

fn benchmark_simple_computation() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run_sync("Simple Computation", || {
        let mut sum = 0;
        for i in 0..100 {
            sum += i;
        }
        let _ = sum;
    })
}

async fn benchmark_concurrent_tasks() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run("Concurrent Tasks (10)", || async {
        let mut join_set = JoinSet::new();
        
        for _ in 0..10 {
            join_set.spawn(async {
                tokio::time::sleep(tokio::time::Duration::from_micros(1)).await;
            });
        }

        while let Some(result) = join_set.join_next().await {
            let _ = result.unwrap();
        }
    }).await
}

async fn benchmark_echo_server() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run("Echo Server Simulation", || async {
        // Simulate echo server processing
        let message = b"Hello, Echo Server!";
        let mut processed = vec![0u8; message.len()];
        processed.copy_from_slice(message);
        let _ = processed;
    }).await
}

fn benchmark_small_data_transfer() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run_sync("Small Data Transfer (64B)", || {
        let mut data = vec![0u8; 64];
        for (i, byte) in data.iter_mut().enumerate() {
            *byte = (i % 256) as u8;
        }
        // Simulate checksum
        let sum: usize = data.iter().map(|&b| b as usize).sum();
        let _ = sum;
    })
}

fn benchmark_medium_data_transfer() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run_sync("Medium Data Transfer (4KB)", || {
        let mut data = vec![0u8; 4096];
        for (i, byte) in data.iter_mut().enumerate() {
            *byte = (i % 256) as u8;
        }
        // Simulate checksum
        let sum: usize = data.iter().map(|&b| b as usize).sum();
        let _ = sum;
    })
}

fn benchmark_large_data_transfer() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run_sync("Large Data Transfer (64KB)", || {
        let mut data = vec![0u8; 65536];
        for (i, byte) in data.iter_mut().enumerate() {
            *byte = (i % 256) as u8;
        }
        // Simulate compression
        let mut compressed_size = 0;
        for i in (0..data.len()).step_by(64) {
            if i > 0 && data[i] == data[i - 64] {
                compressed_size += 1; // compression marker
            } else {
                compressed_size += 64; // raw data
            }
        }
        let _ = compressed_size;
    })
}

fn benchmark_memory_allocation() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run_sync("Memory Allocation (1KB)", || {
        let mut data = vec![0u8; 1024];
        // Use the data to prevent optimization
        data[0] = 1;
        data[1023] = 1;
        let _ = data;
    })
}

async fn benchmark_http_processing() -> BenchmarkResult {
    let runner = BenchmarkRunner::new();
    runner.run("HTTP Request Processing", || async {
        let request = "GET /api/data HTTP/1.1\r\nHost: localhost\r\n\r\n";
        let response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
        
        // Simulate request parsing
        let _ = request.len();
        // Simulate response generation
        let _ = response.len();
    }).await
}

#[derive(Serialize, Deserialize)]
struct SystemInfo {
    rust_version: String,
    os: String,
    arch: String,
    num_cpus: usize,
    timestamp: u64,
}

#[derive(Serialize, Deserialize)]
struct BenchmarkSuite {
    system_info: SystemInfo,
    results: Vec<BenchmarkResult>,
}

fn print_system_info() {
    println!("\n=== System Information ===");
    println!("Rust Version: {}", env!("CARGO_PKG_VERSION"));
    println!("OS/Arch: {}/{}", std::env::consts::OS, std::env::consts::ARCH);
    println!("CPU Cores: {}", num_cpus::get());
    println!("==========================");
}

fn print_benchmark_header() {
    println!("\n=== Rust Performance Benchmarks ===");
    println!("====================================================================================================");
    println!("{:<30} {:>10} {:>12} {:>12} {:>14}", "Benchmark Name", "Iterations", "Mean Time", "Median Time", "Throughput");
    println!("----------------------------------------------------------------------------------------------------");
}

fn print_benchmark_footer() {
    println!("====================================================================================================");
    println!("\nBenchmark completed successfully.");
    println!("Note: Results may vary based on system load and hardware configuration.");
}

async fn save_benchmark_results_json(results: Vec<BenchmarkResult>) {
    let system_info = SystemInfo {
        rust_version: env!("CARGO_PKG_VERSION").to_string(),
        os: std::env::consts::OS.to_string(),
        arch: std::env::consts::ARCH.to_string(),
        num_cpus: num_cpus::get(),
        timestamp: SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs(),
    };

    let suite = BenchmarkSuite {
        system_info,
        results,
    };

    match serde_json::to_string_pretty(&suite) {
        Ok(json_data) => {
            match tokio::fs::write("rust_benchmark_results.json", json_data).await {
                Ok(_) => println!("\nRust benchmark results saved to rust_benchmark_results.json"),
                Err(e) => println!("Error writing JSON file: {}", e),
            }
        }
        Err(e) => println!("Error marshaling JSON: {}", e),
    }
}

#[tokio::main]
async fn main() {
    print_system_info();
    print_benchmark_header();

    let mut results = Vec::new();

    // Core Rust benchmarks
    results.push(benchmark_task_creation().await);
    results.push(benchmark_channel_ops().await);
    results.push(benchmark_simple_computation());

    // Concurrency benchmarks
    results.push(benchmark_concurrent_tasks().await);

    // Memory benchmarks
    results.push(benchmark_memory_allocation());

    // Network and IO simulation benchmarks
    results.push(benchmark_echo_server().await);
    results.push(benchmark_http_processing().await);

    // Data transfer benchmarks
    results.push(benchmark_small_data_transfer());
    results.push(benchmark_medium_data_transfer());
    results.push(benchmark_large_data_transfer());

    // Print summary
    for result in &results {
        result.print_summary();
    }

    print_benchmark_footer();

    // Save JSON results
    save_benchmark_results_json(results.clone()).await;

    // Print detailed statistics for key benchmarks
    println!("\n=== Detailed Statistics ===");
    for result in &results {
        if result.name == "Task Creation" ||
           result.name == "Echo Server Simulation" ||
           result.name == "HTTP Request Processing" ||
           result.name.contains("Data Transfer") {
            result.print_detailed();
        }
    }
}
