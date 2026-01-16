# FlowCoro Profile-Guided Optimization (PGO) 

## 

F## 

|  |  |  |
|------|--------|------|
| `FLOWCORO_ENABLE_PGO` | ON | PGO |
| `FLOWCORO_PGO_GENERATE` | OFF | profile |

## 

```text
FlowCoro/
 pgo_profiles/           # PGO profile
    *.gcda             # profile
 build/                 # PGO
```ided Optimization (PGO)profile37.5%

## 

>  ****: PGO [](PERFORMANCE_DATA.md)

PGO

- ****: +32%
- **WhenAny**: +75%
- ****: +25.6%
- ****: +37.5%
- **WhenAny**: +75%  
- ****: +37.5%
- **vs**: 139.8x102.2x

## 

### 1. PGO

FlowCoroPGO profile

```bash
# PGOprofile
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 2. PGO profiles



```bash
# profile
mkdir build_pgo && cd build_pgo
cmake -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_PGO_GENERATE=ON ..
make -j$(nproc)

# 
./examples/hello_world 50000
./benchmarks/professional_flowcoro_benchmark
./tests/test_core

# profile
find . -name "*.gcda" -exec cp {} ../pgo_profiles/ \;

# PGO
cd .. && rm -rf build_pgo
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 3. PGO

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_ENABLE_PGO=OFF ..
```

## 

|  |  |  |
|------|--------|------|
| `FLOWCORO_ENABLE_PGO` | ON | PGO |
| `FLOWCORO_PGO_USE` | ON | profile |
| `FLOWCORO_PGO_GENERATE` | OFF | profile |

## 

1. **Profile**: `-fprofile-generate`
2. ****: `-fprofile-use`profile
3. ****: CMake`pgo_profiles/`profile

## 

PGO

```bash
cd build
./examples/hello_world 30000
./benchmarks/professional_flowcoro_benchmark
```


- : + /
- vs: 

## Profile

profile

## 

1. **profile**: profile
2. ****: profile
3. ****: PGO
4. ****: `pgo_profiles/`

## 

### CMakePGO profile

profile

```bash
ls -la pgo_profiles/
```



### profile count data file not found

profilePGO

### 

1. Release
2. PGOCMake
3. profile

## 

FlowCoroPGOGCCProfile-Guided Optimization

- **Profile**: GCDA (GCC Data Archive)
- ****: 
- ****: lockfree

## 

>  ****:  [](PERFORMANCE_DATA.md)

FlowCoro

1. **Memory Pool**: 
2. **LockFree**:   
3. **PGO**: 
4. ****: /
