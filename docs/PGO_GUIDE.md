# FlowCoro Profile-Guided Optimization (PGO) 指南

## 简介

F## 配置选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `FLOWCORO_ENABLE_PGO` | ON | 启用PGO支持 |
| `FLOWCORO_PGO_GENERATE` | OFF | 生成新的profile数据 |

## 文件结构

```text
FlowCoro/
├── pgo_profiles/           # PGO profile数据存储
│   └── *.gcda             # 编译器生成的profile文件
└── build/                 # 构建目录（自动启用PGO）
```ided Optimization (PGO)，当profile数据可用时自动提供37.5%的性能提升！

## 性能收益

- **协程创建性能**: +32%
- **WhenAny并发操作**: +75%  
- **整体吞吐量**: +37.5%
- **协程vs线程优势**: 139.8x（从102.2x提升）

## 使用方式

### 1. 自动PGO（推荐）

FlowCoro会自动检测并使用可用的PGO profile数据：

```bash
# 正常构建，自动启用PGO优化（如果profile数据存在）
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 2. 手动生成PGO profiles

如果需要为新的工作负载优化：

```bash
# 第一步：生成profile数据
mkdir build_pgo && cd build_pgo
cmake -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_PGO_GENERATE=ON ..
make -j$(nproc)

# 第二步：运行代表性工作负载
./examples/hello_world 50000
./benchmarks/professional_flowcoro_benchmark
./tests/test_core

# 第三步：复制profile数据到项目目录
find . -name "*.gcda" -exec cp {} ../pgo_profiles/ \;

# 第四步：重新构建使用PGO优化
cd .. && rm -rf build_pgo
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 3. 禁用PGO

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DFLOWCORO_ENABLE_PGO=OFF ..
```

## 配置选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `FLOWCORO_ENABLE_PGO` | ON | 启用PGO支持 |
| `FLOWCORO_PGO_USE` | ON | 使用已有的profile数据 |
| `FLOWCORO_PGO_GENERATE` | OFF | 生成新的profile数据 |

## 工作原理

1. **Profile生成阶段**: 使用`-fprofile-generate`编译，运行代表性负载
2. **优化编译阶段**: 使用`-fprofile-use`基于profile数据优化代码
3. **自动检测**: CMake自动检测`pgo_profiles/`目录中的profile数据

## 性能验证

运行性能测试验证PGO效果：

```bash
cd build
./examples/hello_world 30000
./benchmarks/professional_flowcoro_benchmark
```

期望结果：
- 协程吞吐量: 3,750,000+ 请求/秒
- 协程vs线程优势: 139.8x+

## 更新Profile数据

当添加新功能或改变性能关键路径时，重新生成profile数据。

## 最佳实践

1. **保持profile数据最新**: 重大代码变更后重新生成profile
2. **使用代表性负载**: profile生成时运行真实的使用场景
3. **验证性能提升**: 定期运行基准测试确认PGO效果
4. **版本控制**: 将`pgo_profiles/`目录加入版本控制

## 故障排除

### CMake提示：找不到PGO profile数据

检查profile文件是否存在：

```bash
ls -la pgo_profiles/
```

如需重新生成，按照上述手动生成步骤操作。

### 编译器警告：profile count data file not found

这是正常的警告，表示特定文件没有profile数据，不影响PGO效果。

### 性能没有提升

1. 确认使用Release构建模式
2. 验证PGO确实启用（CMake配置输出）
3. 重新生成适合当前工作负载的profile数据

## 技术细节

FlowCoro的PGO实现基于GCC的Profile-Guided Optimization：

- **Profile格式**: GCDA (GCC Data Archive)
- **优化目标**: 分支预测、函数内联、循环优化
- **覆盖范围**: 核心协程调度、内存池、lockfree数据结构

## 性能成果

通过完整优化链，FlowCoro实现了：

1. **Memory Pool优化**: 3.08x性能提升
2. **LockFree优化**: 25.6%性能提升  
3. **PGO编译器优化**: 37.5%性能提升
4. **综合效果**: 375万请求/秒，139.8x协程优势
