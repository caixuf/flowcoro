package main

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"runtime"
	"sort"
	"sync"
	"time"
)

// BenchmarkStats holds statistical information for a benchmark
type BenchmarkStats struct {
	MinNs    float64 `json:"min_ns"`
	MaxNs    float64 `json:"max_ns"`
	MeanNs   float64 `json:"mean_ns"`
	MedianNs float64 `json:"median_ns"`
	StddevNs float64 `json:"stddev_ns"`
	P95Ns    float64 `json:"p95_ns"`
	P99Ns    float64 `json:"p99_ns"`
}

// BenchmarkResult represents the result of a single benchmark
type BenchmarkResult struct {
	Name        string          `json:"name"`
	Stats       BenchmarkStats  `json:"stats"`
	Iterations  int             `json:"iterations"`
	TotalTimeNs float64         `json:"total_time_ns"`
}

// Calculate computes all statistical metrics
func (bs *BenchmarkStats) Calculate(measurements []float64) {
	if len(measurements) == 0 {
		return
	}

	sort.Float64s(measurements)

	bs.MinNs = measurements[0]
	bs.MaxNs = measurements[len(measurements)-1]

	// Calculate mean
	sum := 0.0
	for _, m := range measurements {
		sum += m
	}
	bs.MeanNs = sum / float64(len(measurements))

	// Calculate median
	n := len(measurements)
	if n%2 == 0 {
		bs.MedianNs = (measurements[n/2-1] + measurements[n/2]) / 2.0
	} else {
		bs.MedianNs = measurements[n/2]
	}

	// Calculate percentiles
	bs.P95Ns = measurements[int(float64(n)*0.95)]
	bs.P99Ns = measurements[int(float64(n)*0.99)]

	// Calculate standard deviation
	variance := 0.0
	for _, m := range measurements {
		variance += (m - bs.MeanNs) * (m - bs.MeanNs)
	}
	bs.StddevNs = math.Sqrt(variance / float64(len(measurements)))
}

// PrintSummary prints a one-line summary of the benchmark result
func (br *BenchmarkResult) PrintSummary() {
	throughput := 1e9 / br.Stats.MeanNs
	fmt.Printf("%-30s %10d %12.0f ns %12.0f ns %14.2f ops/sec\n",
		br.Name, br.Iterations, br.Stats.MeanNs, br.Stats.MedianNs, throughput)
}

// PrintDetailed prints detailed statistics
func (br *BenchmarkResult) PrintDetailed() {
	throughput := 1e9 / br.Stats.MeanNs
	fmt.Printf("\n%s - Detailed Statistics:\n", br.Name)
	fmt.Printf("  Iterations:    %d\n", br.Iterations)
	fmt.Printf("  Mean:          %.0f ns\n", br.Stats.MeanNs)
	fmt.Printf("  Median:        %.0f ns\n", br.Stats.MedianNs)
	fmt.Printf("  Min:           %.0f ns\n", br.Stats.MinNs)
	fmt.Printf("  Max:           %.0f ns\n", br.Stats.MaxNs)
	fmt.Printf("  Std Dev:       %.0f ns\n", br.Stats.StddevNs)
	fmt.Printf("  95th pct:      %.0f ns\n", br.Stats.P95Ns)
	fmt.Printf("  99th pct:      %.0f ns\n", br.Stats.P99Ns)
	fmt.Printf("  Throughput:    %.2f ops/sec\n", throughput)
}

// BenchmarkRunner provides utilities for running benchmarks
type BenchmarkRunner struct {
	warmupIterations    int
	minIterations       int
	maxIterations       int
	minBenchmarkTimeNs  int64
}

// NewBenchmarkRunner creates a new benchmark runner with default settings
func NewBenchmarkRunner() *BenchmarkRunner {
	return &BenchmarkRunner{
		warmupIterations:   10,
		minIterations:      100,
		maxIterations:      10000,
		minBenchmarkTimeNs: 100_000_000, // 100ms minimum
	}
}

// Run executes a benchmark function with the given name
func (br *BenchmarkRunner) Run(name string, benchmarkFunc func()) BenchmarkResult {
	// Warmup phase
	for i := 0; i < br.warmupIterations; i++ {
		benchmarkFunc()
	}

	result := BenchmarkResult{
		Name:  name,
		Stats: BenchmarkStats{},
	}

	var measurements []float64
	totalStart := time.Now()
	iterations := br.minIterations
	elapsed := int64(0)

	for elapsed < br.minBenchmarkTimeNs && iterations <= br.maxIterations {
		for i := 0; i < iterations; i++ {
			start := time.Now()
			benchmarkFunc()
			duration := time.Since(start)
			measurements = append(measurements, float64(duration.Nanoseconds()))
		}

		elapsed = time.Since(totalStart).Nanoseconds()
		if elapsed < br.minBenchmarkTimeNs {
			iterations = min(iterations*2, br.maxIterations)
		}
	}

	result.Iterations = len(measurements)
	result.TotalTimeNs = float64(elapsed)
	result.Stats.Calculate(measurements)
	return result
}

// Goroutine creation and execution benchmark
func benchmarkGoroutineCreationAndExecution() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Goroutine Creation & Execution", func() {
		done := make(chan int)
		go func() {
			// 模拟协程执行中的一些计算
			sum := 0
			for i := 0; i < 10; i++ {
				sum += i
			}
			done <- sum
		}()
		_ = <-done
	})
}

// Channel operations benchmark
func benchmarkChannelOps() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Channel Operations", func() {
		ch := make(chan int, 1)
		ch <- 42
		<-ch
	})
}

// Simple computation benchmark
func benchmarkSimpleComputation() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Simple Computation", func() {
		sum := 0
		for i := 0; i < 100; i++ {
			sum += i
		}
		_ = sum
	})
}

// Concurrent goroutines benchmark
func benchmarkConcurrentGoroutines() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Concurrent Goroutines (10)", func() {
		var wg sync.WaitGroup
		for i := 0; i < 10; i++ {
			wg.Add(1)
			go func() {
				defer wg.Done()
				time.Sleep(1 * time.Microsecond)
			}()
		}
		wg.Wait()
	})
}

// Echo server simulation benchmark
func benchmarkEchoServer() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Echo Server Simulation", func() {
		// Simulate echo server processing
		message := "Hello, Echo Server!"
		processed := make([]byte, len(message))
		copy(processed, message)
		_ = processed
	})
}

// Data transfer benchmarks
func benchmarkSmallDataTransfer() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Small Data Transfer (64B)", func() {
		data := make([]byte, 64)
		for i := range data {
			data[i] = byte(i % 256)
		}
		// Simulate checksum
		sum := 0
		for _, b := range data {
			sum += int(b)
		}
		_ = sum
	})
}

func benchmarkMediumDataTransfer() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Medium Data Transfer (4KB)", func() {
		data := make([]byte, 4096)
		for i := range data {
			data[i] = byte(i % 256)
		}
		// Simulate checksum
		sum := 0
		for _, b := range data {
			sum += int(b)
		}
		_ = sum
	})
}

func benchmarkLargeDataTransfer() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Large Data Transfer (64KB)", func() {
		data := make([]byte, 65536)
		for i := range data {
			data[i] = byte(i % 256)
		}
		// Simulate compression
		compressedSize := 0
		for i := 0; i < len(data); i += 64 {
			if i > 0 && data[i] == data[i-64] {
				compressedSize += 1 // compression marker
			} else {
				compressedSize += 64 // raw data
			}
		}
		_ = compressedSize
	})
}

// Memory allocation benchmark
func benchmarkMemoryAllocation() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("Memory Allocation (1KB)", func() {
		data := make([]byte, 1024)
		// Use the data to prevent optimization
		data[0] = 1
		data[1023] = 1
		_ = data
	})
}

// HTTP request processing simulation
func benchmarkHTTPProcessing() BenchmarkResult {
	runner := NewBenchmarkRunner()
	return runner.Run("HTTP Request Processing", func() {
		request := "GET /api/data HTTP/1.1\r\nHost: localhost\r\n\r\n"
		response := "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!"
		
		// Simulate request parsing
		_ = len(request)
		// Simulate response generation
		_ = len(response)
	})
}

// System information
type SystemInfo struct {
	GoVersion     string `json:"go_version"`
	OS            string `json:"os"`
	Arch          string `json:"arch"`
	NumCPU        int    `json:"num_cpu"`
	NumGoroutine  int    `json:"num_goroutine"`
	Timestamp     int64  `json:"timestamp"`
}

// BenchmarkSuite contains all benchmark results and system info
type BenchmarkSuite struct {
	SystemInfo SystemInfo        `json:"system_info"`
	Results    []BenchmarkResult `json:"results"`
}

func printSystemInfo() {
	fmt.Println("\n=== System Information ===")
	fmt.Printf("Go Version: %s\n", runtime.Version())
	fmt.Printf("OS/Arch: %s/%s\n", runtime.GOOS, runtime.GOARCH)
	fmt.Printf("CPU Cores: %d\n", runtime.NumCPU())
	fmt.Printf("Goroutines: %d\n", runtime.NumGoroutine())
	fmt.Println("==========================")
}

func printBenchmarkHeader() {
	fmt.Println("\n=== Go Performance Benchmarks ===")
	fmt.Println("====================================================================================================")
	fmt.Printf("%-30s %10s %12s %12s %14s\n", "Benchmark Name", "Iterations", "Mean Time", "Median Time", "Throughput")
	fmt.Println("----------------------------------------------------------------------------------------------------")
}

func printBenchmarkFooter() {
	fmt.Println("====================================================================================================")
	fmt.Println("\nBenchmark completed successfully.")
	fmt.Println("Note: Results may vary based on system load and hardware configuration.")
}

func saveBenchmarkResultsJSON(results []BenchmarkResult) {
	systemInfo := SystemInfo{
		GoVersion:    runtime.Version(),
		OS:           runtime.GOOS,
		Arch:         runtime.GOARCH,
		NumCPU:       runtime.NumCPU(),
		NumGoroutine: runtime.NumGoroutine(),
		Timestamp:    time.Now().Unix(),
	}

	suite := BenchmarkSuite{
		SystemInfo: systemInfo,
		Results:    results,
	}

	jsonData, err := json.MarshalIndent(suite, "", "  ")
	if err != nil {
		fmt.Printf("Error marshaling JSON: %v\n", err)
		return
	}

	err = os.WriteFile("go_benchmark_results.json", jsonData, 0644)
	if err != nil {
		fmt.Printf("Error writing JSON file: %v\n", err)
		return
	}

	fmt.Println("\nGo benchmark results saved to go_benchmark_results.json")
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func main() {
	printSystemInfo()
	printBenchmarkHeader()

	var results []BenchmarkResult

	// Core Go benchmarks
	results = append(results, benchmarkGoroutineCreationAndExecution())
	results = append(results, benchmarkChannelOps())
	results = append(results, benchmarkSimpleComputation())

	// Concurrency benchmarks
	results = append(results, benchmarkConcurrentGoroutines())

	// Memory benchmarks
	results = append(results, benchmarkMemoryAllocation())

	// Network and IO simulation benchmarks
	results = append(results, benchmarkEchoServer())
	results = append(results, benchmarkHTTPProcessing())

	// Data transfer benchmarks
	results = append(results, benchmarkSmallDataTransfer())
	results = append(results, benchmarkMediumDataTransfer())
	results = append(results, benchmarkLargeDataTransfer())

	// Print summary
	for _, result := range results {
		result.PrintSummary()
	}

	printBenchmarkFooter()

	// Save JSON results
	saveBenchmarkResultsJSON(results)

	// Print detailed statistics for key benchmarks
	fmt.Println("\n=== Detailed Statistics ===")
	for _, result := range results {
		if result.Name == "Goroutine Creation" ||
			result.Name == "Echo Server Simulation" ||
			result.Name == "HTTP Request Processing" ||
			len(result.Name) > 12 && result.Name[:12] == "Data Transfer" {
			result.PrintDetailed()
		}
	}
}
