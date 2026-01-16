# 

FlowCoroGoRust

## 

### 
- Go 1.20+
- Rust 1.70+ (with Cargo)
- FlowCoro

### 

```bash
# 1. 
make -C ../build -j$(nproc) # FlowCoro
go build -o go_benchmark go_benchmark.go # Go
cargo build --release # Rust

# 2. 10,000
echo "=== FlowCoro ==="
../build/examples/hello_world_concurrent coroutine 10000

echo "=== Go ==="
./go_benchmark 10000

echo "=== Rust ==="
./target/release/rust_benchmark 10000
```

## 

### 
- ****: 
- ****:  (req/sec)
- ****: 
- ****: 

###  (16Linux)

#### 

|  | FlowCoro | Go | Rust |  |
|----------|----------|-----|------|----------|
| **/** | 4.20M ops/s | 2.27M ops/s | 19.5K ops/s | Go1.85Rust215 |
| **/** | 9.61M ops/s | 11.59M ops/s | 9.15M ops/s | Go17%Rust5% |
| **HTTP** | 36.77M ops/s | 41.99M ops/s | 45.42M ops/s | 12-19% |
| **** | 46.19M ops/s | 21.82M ops/s | 46.34M ops/s | Rust |
| **(1KB)** | 10.18M ops/s | 41.43M ops/s | 46.75M ops/s |  |

#### 

**FlowCoro:**
- 
- WhenAny/WhenAll
- 

**:**
- 
- 

**:**
FlowCoro

### 

**Go**
- goroutine(11.59M ops/s)
- HTTP(41.99M ops/s)
- 41.43M ops/s

**FlowCoro**
- (4.20M ops/s vs Go2.27M ops/s)
- WhenAny/WhenAll
- 

**Rust**
- (46.34M ops/s)
- HTTP(45.42M ops/s)
- 

## 


- IO
- 
- 

##  (10,000)

### 

- CPU16
- Linux
### 

#### 10K ()



| / |  |  |  |  |
|-----------|--------|--------|--------------|----------------|
| **Go Goroutines** | 4ms | 2,500,000 req/sec | +408KB | 41 bytes |
| **Rust Tokio** | 6ms | 1,666,666 req/sec | +0KB | 0 bytes |
| **C++ FlowCoro** | 1.3ms | 7,727,975 ops/sec | - | - |



### 

#### Go Goroutines

```text
: 10000 
: 4 ms
: 0.0004 ms/
: 2500000 /
: 185 KB → 593 KB ( 408 KB)
: 41 bytes/
```

#### Rust Tokio

```text
: 10000 
: 6 ms
: 0.0007 ms/
: 1666666 /
: 750480 KB → 750480 KB ( 0 KB)
: 0 bytes/
```

#### C++ FlowCoro

```text
Basic Coroutine: 10000  | 1.294ms |  0.000129ms | 7727975.27 ops/sec
Coroutine Scheduling: 1000  | 0.143ms |  0.000143ms | 6993006.99 ops/sec
Concurrent Coroutines: 100  | 127.076ms |  1.270760ms | 786.93 ops/sec
```

###  (10K)

1. **Go**
   - 4ms10000
   - goroutine41
   - M:N

2. **Rust**
   - 
   - 
   - 

3. **C++ FlowCoro**
   - 770ops/sec
   - 
   - 

###  vs 

** ()**
- 
- FlowCoro
- GoHTTP
- Rust

** ()**
- 
- Go
- 

### 

- ****FlowCoro
- **HTTP**RustGo
- ****GoRust
- ****RustFlowCoroGo

## 

- `professional_go_benchmark.go`: Go
- `professional_rust_benchmark/`: Rust
- `professional_flowcoro_benchmark.cpp`: FlowCoro
- `go_benchmark.go`: Go ()
- `rust_benchmark.rs`: Rust ()
- `Cargo.toml`: Rust

## 

- .gitignore
- Rust
- 
