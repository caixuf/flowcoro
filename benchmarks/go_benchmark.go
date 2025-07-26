package main

import (
	"fmt"
	"os"
	"runtime"
	"sync"
	"time"
)

func getMemoryUsageKB() int {
	var m runtime.MemStats
	runtime.GC()
	runtime.ReadMemStats(&m)
	return int(m.Alloc / 1024)
}

func getCurrentTime() string {
	return time.Now().Format("15:04:05")
}

func handleConcurrentRequestsGoroutines(requestCount int) {
	startTime := time.Now()
	initialMemory := getMemoryUsageKB()
	
	fmt.Printf("🧵 Go Goroutine方式：处理 %d 个并发请求\n", requestCount)
	fmt.Printf("💾 初始内存: %d KB\n", initialMemory)
	fmt.Printf("🧵 CPU核心数: %d\n", runtime.NumCPU())
	fmt.Printf("⏰ 开始时间: [%s]\n", getCurrentTime())
	fmt.Println(string(make([]byte, 50, 50)[0:50]) + "")
	
	fmt.Printf("📝 同时启动 %d 个goroutine...\n", requestCount)
	
	var wg sync.WaitGroup
	completed := make(chan int, requestCount)
	
	// 创建与协程数量相同的goroutine
	for i := 0; i < requestCount; i++ {
		wg.Add(1)
		go func(userID int) {
			defer wg.Done()
			
			// 模拟IO操作 - 注释掉sleep以测试纯创建和调度性能
			// time.Sleep(50 * time.Millisecond)
			
			result := fmt.Sprintf("用户%d (已处理)", 1000+userID)
			_ = result // 使用结果避免优化
			
			completed <- userID
		}(i)
	}
	
	// 监控完成进度
	go func() {
		completedCount := 0
		for range completed {
			completedCount++
			if completedCount%(requestCount/10) == 0 || completedCount == requestCount {
				fmt.Printf("✅ 已完成 %d/%d 个goroutine (%d%%)\n", 
					completedCount, requestCount, (completedCount*100)/requestCount)
			}
		}
	}()
	
	// 等待所有goroutine完成
	wg.Wait()
	close(completed)
	
	endTime := time.Now()
	duration := endTime.Sub(startTime)
	finalMemory := getMemoryUsageKB()
	memoryDelta := finalMemory - initialMemory
	if memoryDelta < 0 {
		memoryDelta = 0
	}
	
	fmt.Println(string(make([]byte, 50, 50)[0:50]) + "")
	fmt.Printf("🧵 Go Goroutine方式完成！\n")
	fmt.Printf("   📊 总请求数: %d 个\n", requestCount)
	fmt.Printf("   ⏱️  总耗时: %d ms\n", duration.Milliseconds())
	
	if requestCount > 0 {
		fmt.Printf("   📈 平均耗时: %.4f ms/请求\n", float64(duration.Nanoseconds())/float64(requestCount)/1000000.0)
	}
	
	if duration.Milliseconds() > 0 {
		fmt.Printf("   🎯 吞吐量: %d 请求/秒\n", (requestCount*1000)/int(duration.Milliseconds()))
	}
	
	fmt.Printf("   💾 内存变化: %d KB → %d KB (增加 %d KB)\n", 
		initialMemory, finalMemory, memoryDelta)
	
	if requestCount > 0 {
		fmt.Printf("   📊 单请求内存: %d bytes/请求\n", (memoryDelta*1024)/requestCount)
	}
	
	fmt.Printf("   🧵 Goroutine总数: %d 个\n", requestCount)
	fmt.Printf("   🌟 并发策略: Go M:N调度器\n")
	fmt.Printf("   ⏰ 程序结束: [%s]\n", getCurrentTime())
}

func main() {
	if len(os.Args) != 2 {
		fmt.Printf("用法: %s <request_count>\n", os.Args[0])
		return
	}
	
	requestCount := 0
	fmt.Sscanf(os.Args[1], "%d", &requestCount)
	
	fmt.Println("========================================")
	fmt.Println("🎯 Go Goroutine 高并发性能测试")
	fmt.Println("========================================")
	fmt.Printf("请求数量: %d 个\n", requestCount)
	fmt.Println("每个请求模拟0ms处理时间 (纯调度测试)")
	fmt.Println("========================================")
	fmt.Println()
	
	handleConcurrentRequestsGoroutines(requestCount)
}
