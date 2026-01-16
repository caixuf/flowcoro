# FlowCoro 

 FlowCoro 

##  ()

### 

|  | FlowCoro | Go | Rust | FlowCoro |
|---------|----------|-----|------|-------------|
| **/** | **15.6M ops/s** | 2.0M ops/s | 17.5K ops/s | **Go7.8Rust891** |
| **(100)** | **5.3K ops/s** | 5.7K ops/s | 779 ops/s | **Go(-7.3%)Rust6.8** |
| **** | **40.0M ops/s** | 19.4M ops/s | 41.6M ops/s | **Go2.1** |
| **HTTP** | **41.5M ops/s** | 36.7M ops/s | 40.2M ops/s | **Go1.1** |
| **** | **23.9M ops/s** | - | 8.1M ops/s | **Rust2.9** |
| **** | **40.4M ops/s** | 37.0M ops/s | 42.1M ops/s | **** |

### 

#### FlowCoro (1 + 16)

- : 15.6M ops/s (63ns)
- : 5.3K ops/s (188μs)
- : 40.0M ops/s (25ns)
- WhenAny 2: 3.3M ops/s (302ns)
- WhenAny 4: 2.4M ops/s (407ns)
- : 23.9M ops/s (42ns)
- : 25.9M ops/s (39ns)
- : 40.4M ops/s (24ns)
- Echo: 20.0M ops/s (50ns)
- HTTP: 41.5M ops/s (24ns)

#### Go

- Goroutine: 2.0M ops/s (491ns)
- : 5.7K ops/s (174μs)
- : 19.4M ops/s (51ns)
- : 10.6M ops/s (94ns)
- : 37.0M ops/s (27ns)
- Echo: 14.8M ops/s (68ns)
- HTTP: 36.7M ops/s (27ns)

#### Rust (Tokio)

- : 17.5K ops/s (57μs)
- : 779 ops/s (1.28ms)
- : 41.6M ops/s (24ns)
- : 8.1M ops/s (123ns)
- : 42.1M ops/s (24ns)
- Echo: 41.3M ops/s (24ns)
- HTTP: 40.2M ops/s (25ns)

## 

### FlowCoro

- : 1
- : 16
- : Release

### 

- CPU: 16
- : GCC 13.3.0 / Go 1.22.2 / Rust 1.x
- : Linux x86_64

### 

- : 1001000 + 1μs
- : 
- 

---

*: *
