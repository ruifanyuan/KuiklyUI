# TODO：OHOS `FromNapi`（KRRecord → KRRenderValue）转换成本

> 2026-08-27 在真机 `LNG0223C13000049` 上测。对比的是 **同一份 ArkTS `KRRecord`** 进 C++ 的几条路，不是 Kotlin `JSONObject(str)`。
>
> 正式数字以 **release**（`hvigorw … -p buildMode=release`）p50 为准。debug 绝对值大约再慢一个数量级，只能看相对趋势。

## 结论（先看这个）

1. **只打成 `std::string`**（`JSON.stringify` + `GetNApiArgsStdString`）远快于 `FromNapi`。65 KB / 2200 KV 约 **28×**（0.29 ms vs 8.09 ms）。
2. **要得到和 `FromNapi` 同构的 KRJSON object**，stringify 之后还要 `KRRenderValue::Parse`。这时两条路在大包上打平（~8.1 ms）；中小包 stringify+Parse 反而快 **1.4–2.8×**。
3. `FromNapi` 慢，主要是 **每个字段多次跨 VM 的 `napi_*`**，外加建树本身。NAPI **没有**「一次取出全部 key+value」的 C API。
4. `FromNapi` 的产品价值不在 ArkTS→C++ 这一跳，而在后面 Kotlin **不用再 `JSONObject(str)`**。若 C++ 只 `Make(string)`，Kotlin 仍要解析。

## TODO

- [x] **NewString 支持 UTF-16**  
  ArkVM / JSVM / Kotlin 内部都是 UTF-16。`FromNapi` / `FromJsVm` 用 `napi_get_value_string_utf16` / `OH_JSVM_GetValueStringUtf16` 写入 `KRJSONNewStringUtf16`；ArkTS stringify 的 JSON 文本走 `Make(env)`（U16 盒）+ `container()`/`ParseUtf16`（叶子仍是 U16）。Kotlin `asString` 按 `JSON_KIND_U16STRING` 走 `KRJSONGetStringUtf16`。`KRJSONGetString` 只读 UTF-8 盒；Dump / C++ `stringValue()` 在调用方把 UTF-16 转 UTF-8。`instance_id` / `page_name` / event 名 / object key / `KRJSONParse` 仍是 UTF-8。  
  残留转码的具体调用点和下手顺序见 [TODO-ohos-string-encoding.md](TODO-ohos-string-encoding.md)。
- [x] **sendEvent / callNative 的 ArkTS 入参改回 stringify**  
  `anyToCObject` 回滚 86232805：object / 普通 array 走 `JSON.stringify`；含 ArrayBuffer 的数组仍透传。Release 下 stringify+string 远快于 FromNapi，不必在 ArkTS 边界建 KRJSON 树。
- [ ] **评估 CREATE_INSTANCE 是否对中小包改走 stringify+Parse**  
  Release 下 20–400 KV 这条已比 FromNapi 快；65 KB 打平。要权衡：多一次 JSON 文本、key 顺序与 `JSON.stringify` 一致、非法值语义。
- [ ] **减 FromNapi 的 napi 次数（不算批量，但便宜）**  
  - JSON object/array 跳过每个节点的 `napi_is_arraybuffer` / `IsTypedArray`。  
  - key 用栈缓冲读 `napi_get_value_string_utf8`，避免先问长度再拷（现在每个 key 两次）。  
  - 确认 `napi_get_property_names` 与 `napi_get_all_property_names`（仅 enumerable own）哪个更合适。
- [ ] **不要指望 NAPI 批量读 value**  
  已批量的只有 key 列表。`napi_get_all_property_names` 仍只返回名字。`napi_serialize` 是 ArkTS 结构化克隆，反序列化还是 JS 对象，进不了 KRJSON。数组也没有 bulk `get_element`。真正的批量读就是 VM 内一次 `JSON.stringify`。
- [ ] **若继续 FromNapi：用宏打开 bench 在目标机上回归**  
  改完后再跑下表 fixture，对比 FromNapi vs stringify+Parse。

## 怎么复现 bench

默认**关闭**。必须同时：

1. C++：CMake **显式** `-DKUIKLY_OHOS_NAPI_RECORD_BENCH=1`（未定义或为 0 都不编译 bench）。见 `core-render-ohos/src/main/cpp/CMakeLists.txt`。
2. ArkTS：把 `core-render-ohos/src/main/ets/utils/NapiRecordConvertBench.ets` 里的 `KUIKLY_OHOS_NAPI_RECORD_BENCH` **改为 `1`**。
3. `hvigorw assembleHap … -p buildMode=release`
4. `aa start -a EntryAbility -b com.tencent.kuiklyohosdemo --ps pageName NapiRecordBench`
5. hilog tag：`NAPI_RECORD_BENCH`

代码位置：`napi_init.cpp` 的 `BenchNapiRecordConvert`（`#if KUIKLY_OHOS_NAPI_RECORD_BENCH == 1`）。

## 路径说明

```
A  FromNapi     Record → 递归 napi_* → KRJSON object
B  stringify    Record → JSON.stringify → std::string          （KRJSON string，若再 Make(str)）
C  stringify+Parse  B → KRRenderValue::Parse → KRJSON object   （与 A 同构）
```

`FromNapi` 每个 object 先 `typeof` / `is_arraybuffer` / `IsTypedArray` / `is_array` / `get_property_names`；每个 key 再 `get_element` + 两次读 string + `get_property` + 对 value 再 `typeof`+读。`JSON.stringify` 不按字段跨 C++。

## Fixture KV

| case | JSON 体积 | 顶层 key | 对象字段合计 | 数组元素 |
|---|---:|---:|---:|---:|
| Tiny | 42 B | 3 | 3 | 0 |
| Nested_8 | 184 B | 2 | 18 | 0 |
| PageData_lifecycle | 342 B | 16 | 20 | 0 |
| Wide_100 | 1.2 KB | 100 | 100 | 0 |
| Array_100 | 5.4 KB | 1 | 401 | 100 |
| Wide ~6.6 KB | 6.6 KB | 280 | 280 | 0 |
| Wide ~65 KB | 65 KB | 2200 | 2200 | 0 |

## Release p50（`LNG0223C13000049`）

| case | FromNapi | stringify+string | stringify+Parse | FromNapi / Parse路径 |
|---|---:|---:|---:|---:|
| Tiny 3 KV | 1.6 µs | 1.6 µs | 2.1 µs | 0.75× |
| Nested 18 KV | 9.9 µs | 2.6 µs | 5.2 µs | 1.90× |
| PageData 20 KV | 9.9 µs | 2.6 µs | 5.7 µs | 1.73× |
| Wide 100 KV | 67 µs | 6.8 µs | 38 µs | 1.77× |
| Array 401 字段 | 255 µs | 35 µs | 93 µs | 2.75× |
| Wide 280 KV | 263 µs | 21 µs | 187 µs | 1.41× |
| Wide 2200 KV | 8.09 ms | 0.29 ms | 8.10 ms | 1.00× |

debug 同机 Large FromNapi 约 105 ms，release 约 8.1 ms（~13×），不要拿 debug 绝对值对外。

## Release p50 再测（UTF-16 `FromNapi`，同机 2026-08-27 20:14）

`FromNapi` 改为 `napi_get_value_string_utf16` → `NewStringUtf16` 后重跑。stringify 路径不变。

| case | FromNapi | stringify+string | stringify+Parse | FromNapi / Parse | vs 上表 FromNapi |
|---|---:|---:|---:|---:|---:|
| Tiny 3 KV | 1.6 µs | 1.6 µs | 2.1 µs | 0.75× | ~0% |
| Nested 18 KV | 10.4 µs | 2.6 µs | 5.2 µs | 2.00× | +5%（噪声） |
| PageData 20 KV | 9.4 µs | 2.6 µs | 5.7 µs | 1.64× | −5% |
| Wide 100 KV | 65 µs | 7.3 µs | 39 µs | 1.65× | −3% |
| Array 401 字段 | 231 µs | 37 µs | 92 µs | 2.51× | −9% |
| Wide 280 KV | 240 µs | 24 µs | 185 µs | 1.29× | −9% |
| Wide 2200 KV | 7.46 ms | 0.29 ms | 7.32 ms | 1.02× | −8% |

UTF-16 让 FromNapi 在字符串多的包上大约快 **8–9%**，没有改变主结论：瓶颈仍是逐字段 `napi_*`，不是 utf16↔utf8。
