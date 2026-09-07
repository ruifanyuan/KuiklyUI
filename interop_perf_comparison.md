# InteropPerfTestPage Debug 真机对比

## 测试环境

- 设备：HarmonyOS 真机 `LNG0223C13000049`
- 构建模式：Debug
- 包名：`com.tencent.kuiklyohosdemo`
- 测试页面：`InteropPerfTestPage`

对比对象：

| 代码基线 | 说明 |
|---|---|
| 当前分支 `dev3` | 工作目录 `/Users/steven/code/KuiklyUI_bridging_platform_json_export_jsonobject` |
| 主仓库 `main` | 主工作目录 `/Users/steven/code/KuiklyUI`，仅同步了 InteropPerfTestPage 相关代码，并按约定屏蔽了 ArkTS 调用 `KRJSONValue` 的用例 |

## 重要口径

1. `main` 工作区未包含 `KRJSONValue` 跨 runtime 基础设施，因此本对比只统计两类分支都具备的 9 个用例：

   ```text
   普通 string × 128 KB / 384 KB / 3 MB
   JSON string × 128 KB / 384 KB / 3 MB
   KRRecord    × 128 KB / 384 KB / 3 MB
   ```

2. `dev3` 额外包含 `KRJsonValue × 128 KB / 384 KB / 3 MB`，结果单独列出，不进入主对比表。

3. 所有数值均为 Debug 模式下的单轮采样，且 `Date.now()` / `DateTime.currentTimestamp()` 为毫秒级。

## 一、跨 Runtime 调用对比（ArkTS → Kotlin）

### 1.1 文本测量基线

| 基线 | dev3 | main |
|---|---:|---:|
| 10,000 次文本测量 | 3,682 ms | 3,942 ms |
| 平均每次 | 0.37 ms | 0.39 ms |

### 1.2 9 个共同用例

| Payload | 规模 | 次数 | dev3 累计 | dev3 平均 | main 累计 | main 平均 |
|---|---|---:|---:|---:|---:|---:|
| 普通 string | 128 KB | 50 | 10 ms | 0.20 ms | 178 ms | 3.56 ms |
| JSON string | 128 KB | 50 | 6 ms | 0.12 ms | 174 ms | 3.48 ms |
| KRRecord | 128 KB | 50 | 45 ms | 0.90 ms | 213 ms | 4.26 ms |
| 普通 string | 384 KB | 20 | 14 ms | 0.70 ms | 219 ms | 10.95 ms |
| JSON string | 384 KB | 20 | 7 ms | 0.35 ms | 212 ms | 10.60 ms |
| KRRecord | 384 KB | 20 | 61 ms | 3.05 ms | 259 ms | 12.95 ms |
| 普通 string | 3 MB | 3 | 7 ms | 2.33 ms | 256 ms | 85.33 ms |
| JSON string | 3 MB | 3 | 24 ms | 8.00 ms | 255 ms | 85.00 ms |
| KRRecord | 3 MB | 3 | 60 ms | 20.00 ms | 305 ms | 101.67 ms |

### 1.3 dev3 额外：KRJsonValue

| Payload | 规模 | 次数 | 累计 | 平均 |
|---|---|---:|---:|---:|
| KRJsonValue | 128 KB | 50 | 1 ms | 0.02 ms |
| KRJsonValue | 384 KB | 20 | 1 ms | 0.05 ms |
| KRJsonValue | 3 MB | 3 | 0 ms | 0.00 ms |

### 1.4 观察

- dev3 在普通 string 和 JSON string 两个路径上显著更快，且随 payload 增大的差距进一步拉大。
- 3 MB 普通 string：dev3 约 7 ms，main 约 256 ms。
- 3 MB JSON string：dev3 约 24 ms，main 约 255 ms。
- KRRecord 的差距主要来自 ArkTS object → C++ `KRJSON` 的构建/拷贝路径，main 同样高于 dev3。

## 二、JSON 解析测试对比

### 2.1 扩容并准备 C++ backed 数据

| 阶段 | 指标 | dev3 | main |
|---|---|---:|---:|
| init | 字段数 | 2,025 | 1,900 |
| init | payload 体积 | 34,246 B | 33,924 B |
| init | stringify | 17 ms | 16 ms |
| init | parse | 5 ms | 10 ms |
| init | prepare total | 22 ms | 26 ms |
| +30 KB | 字段数 | 4,052 | 3,801 |
| +30 KB | payload 体积 | 70,734 B | 70,016 B |
| +30 KB | stringify | 34 ms | 32 ms |
| +30 KB | parse | 11 ms | 19 ms |
| +30 KB | prepare total | 45 ms | 51 ms |
| +300 KB | 字段数 | 24,322 | 22,816 |
| +300 KB | payload 体积 | 458,161 B | 451,232 B |
| +300 KB | stringify | 307 ms | 285 ms |
| +300 KB | parse | 94 ms | 185 ms |
| +300 KB | prepare total | 401 ms | 470 ms |
| +3 MB | 字段数 | 231,892 | 217,537 |
| +3 MB | payload 体积 | 4,763,478 B | 4,672,382 B |
| +3 MB | stringify | 3,024 ms | 2,500 ms |
| +3 MB | parse | 901 ms | 1,876 ms |
| +3 MB | prepare total | 3,925 ms | 4,376 ms |

### 2.2 打开 Kotlin backed 页面

| 指标 | dev3 | main |
|---|---:|---:|
| router openPage 返回 | 11 ms | 22 ms |
| click → result created | 40 ms | 74 ms |
| params 读取 | 0 ms | 0 ms |
| 实际 key 数 | 2,026 | 1,901 |
| 真实 JSON 体积 | 34,246 B | 33,924 B |
| 真实序列化 | 1 ms | 15 ms |
| 动态字段抽样 | 11 ms | 3 ms |
| click → pageDidAppear | 60 ms | 101 ms |

### 2.3 打开 C++ backed 页面

| 指标 | dev3 | main |
|---|---:|---:|
| router openPage 返回 | 4 ms | 20 ms |
| click → result created | 41 ms | 75 ms |
| params 读取 | 0 ms | 0 ms |
| 实际 key 数 | 2,026 | 1,901 |
| 真实 JSON 体积 | 34,246 B | 33,924 B |
| 真实序列化 | 1 ms | 16 ms |
| 动态字段抽样 | 11 ms | 3 ms |
| click → pageDidAppear | 60 ms | 100 ms |

### 2.4 观察

- dev3 的 C++ backed JSON 解析路径在 300 KB / 3 MB 两个大档位明显快于 main。
- dev3 打开页面时真实 JSON 序列化耗时约 1 ms，main 约 15–16 ms。
- 字段数略有差异是 `bytesPerField` 校准结果不同导致的，体积基本一致，不影响同规模对比。

## 三、结论

1. 在本次 Debug 真机测试中，当前 `dev3` 在普通 string / JSON string 跨 runtime 路径以及 C++ backed JSON 解析路径上均显著优于 `main`。
2. 大 payload 场景差异最大：
   - 3 MB 普通 string：约 36 倍；
   - 3 MB JSON string：约 10 倍；
   - 3 MB C++ backed parse：约 2 倍。
3. `dev3` 额外提供的 KRJsonValue 路径表现最好，三种规模均接近 0–0.05 ms/次，且不随 payload 增大。

## 四、相关日志

### dev3

```text
logs/interop_debug_run_cross2.log
logs/interop_debug_add_cases_uinput.log
logs/interop_debug_open_kotlin_dump.log
logs/interop_debug_open_cpp_dump.log
```

### main（KuiklyUI 主工作目录）

```text
logs/kuikly_main_interop_run_cross3.log
logs/kuikly_main_interop_add3.log
logs/kuikly_main_interop_open_kotlin_dump.log
logs/kuikly_main_interop_open_cpp_dump.log
```
