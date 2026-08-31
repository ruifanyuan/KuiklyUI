# TODO：OHOS KRJSON 字符串 U16/U8 残留转码

> 2026-08-31 真机 `LNG0223C13000049` debug HAP。跑了 router / JsonPlatformTestPage / InputViewDemoPage / image_demo / ListExamplePage / RichTextSpanLongPressExamplePage / MyModuleDemoPage / PageDataTestPage。
>
> 计数来自当时的 `EncodingStats` / `kotlin_asString`（已移除）。`new` 是建盒次数；`conv` 是真正跑了 `Utf8ToUtf16` / `Utf16ToUtf8` / `AsciiToUtf16`。

## 结论（先看这个）

1. **存量已经是 U16**：各页新建字符串盒 91–97% 是 `kTagU16String`。Kotlin `asString` / object key **100% U16**（`utf8=0`，`key_utf8=0`）。解析全走 `ParseUtf16`。
2. **主动转码几乎全是 U16→U8**。JsonPlatform 一页 `u16->u8=2566`，其它页 66–862。来源是 Dump / `toString()` / C++ 把叶子读成 `std::string`。
3. **U8→U16 每页只有 4–18 次**，对得上 Input 回抛 `text`、Image adapter 改 src、File/Preferences 回传。
4. **Dump 削不掉**：RapidJSON `Writer::String` 只要 `const char *`，JSON 文本就是 UTF-8。
5. **还能砍的**是 C++ 侧 `std::string` 缓存（Input `cached_text_`、Image `image_src_`）和 prop 一律 `toString()`，以及 `instance_id` 仍走 `napi_get_value_string_utf8`。

| 页面 | U8 盒 | U16 盒 | U16% | U8→U16 | U16→U8 | ASCII 拓宽 | NAPI U8 | NAPI U16 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| router | 25 | 350 | 93 | 7 | 307 | 30 | 3 | 7 |
| JsonPlatformTestPage | 99 | 3516 | 97 | 6 | 2566 | 516 | 3 | 7 |
| InputViewDemoPage | 23 | 349 | 93 | 7 | 306 | 21 | 3 | 7 |
| image_demo | 53 | 547 | 91 | 18 | 491 | 32 | 3 | 7 |
| ListExamplePage | 39 | 787 | 95 | 7 | 637 | 79 | 3 | 7 |
| RichTextSpanLongPress | 17 | 231 | 93 | 7 | 190 | 16 | 3 | 7 |
| MyModuleDemoPage | 52 | 979 | 94 | 6 | 862 | 146 | 3 | 7 |
| PageDataTestPage | 4 | 95 | 95 | 4 | 66 | 5 | 3 | 7 |

优化后（2026-08-31 14:13，同机冷启动）：

| 页面 | U8 盒 | U16 盒 | U16% | U8→U16 | U16→U8 | ASCII 拓宽 | NAPI U8 | NAPI U16 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| router | 25 | 349 | 93 | 6 | **54** | 30 | **0** | 10 |
| JsonPlatformTestPage | 91 | 3440 | 97 | 5 | **667** | 516 | **0** | 10 |
| InputViewDemoPage | 23 | 343 | 93 | 5 | **38** | 21 | **0** | 10 |
| image_demo | 53 | 534 | 90 | **5** | **40** | 32 | **0** | 10 |
| ListExamplePage | 22 | 785 | 97 | 5 | **56** | 79 | **0** | 10 |
| RichTextSpanLongPress | 17 | 229 | 93 | 5 | **39** | 16 | **0** | 10 |
| MyModuleDemoPage | 52 | 978 | 94 | 5 | **96** | 146 | **0** | 10 |
| PageDataTestPage | 4 | 98 | 96 | 5 | **25** | 5 | **0** | 10 |

Kotlin 各页 `asString utf8=0`；JsonPlatform `utf16=508 key_utf16=1220`。

EncodedText + 选区 U16 缓存后（2026-08-31 15:28，同机冷启动）：

| 页面 | U8 盒 | U16 盒 | U16% | U8→U16 | U16→U8 | Δ vs 14:13 | NAPI U8 |
|---|---:|---:|---:|---:|---:|---:|---:|
| router | 25 | 349 | 93 | 6 | **43** | −11 | 0 |
| JsonPlatformTestPage | 91 | 3469 | 97 | 5 | **583** | −84 | 0 |
| InputViewDemoPage | 23 | 366 | 94 | 6 | **37** | −1 | 0 |
| image_demo | 53 | 534 | 90 | 5 | **32** | −8 | 0 |
| ListExamplePage | 26 | 785 | 96 | 5 | **31** | −25 | 0 |
| RichTextSpanLongPress | 17 | 229 | 93 | 5 | **31** | −8 | 0 |
| MyModuleDemoPage | 52 | 978 | 94 | 5 | **66** | −30 | 0 |
| PageDataTestPage | 4 | 95 | 95 | 4 | **22** | −3 | 0 |

`richtext` 来源：JsonPlatform 216→133、List 27→3、MyModule 44→15、RichText 页 13→6。剩余 `richtext` 主要是样式 `toString`（`color`/`textAlign` 等）。Dump / log / toAscii_fallback 没变。

Kotlin stringify 走 `KRJSONDumpUtf16` 后（2026-08-31 16:11，同机冷启动；`GetLength()` 按 unit 计）：

| 页面 | U8 盒 | U16 盒 | U16% | U8→U16 | U16→U8 | Δ vs 15:28 | NAPI U8 |
|---|---:|---:|---:|---:|---:|---:|---:|
| router | 25 | 349 | 93 | 6 | **43** | 0 | 0 |
| JsonPlatformTestPage | 91 | 3469 | 97 | 5 | **187** | **−396** | 0 |
| InputViewDemoPage | 23 | 343 | 93 | 5 | **33** | −4 | 0 |
| image_demo | 53 | 534 | 90 | 5 | **32** | 0 | 0 |
| ListExamplePage | 26 | 785 | 96 | 5 | **31** | 0 | 0 |
| RichTextSpanLongPress | 17 | 229 | 93 | 5 | **31** | 0 | 0 |
| MyModuleDemoPage | 52 | 978 | 94 | 5 | **66** | 0 | 0 |
| PageDataTestPage | 4 | 98 | 96 | 5 | **23** | +1 | 0 |

JsonPlatform top：`dump_key=322` / `dump_leaf=75` 消失（322+74=396）。剩余 `richtext=133/1180`、`log=26/1851`、`toString=6`。各页只剩 `dump_leaf=1/5`（启动底噪，C++ `KRJSONDump` UTF-8）。Kotlin 仍 `asString utf8=0`；JsonPlatform `utf16=508 key_utf16=1220`。CONFORMANCE 322/322、BRIDGE PASS。

## TODO

### 1. C++ / JSON 文本只要 UTF-8（U16 盒现场转）— 日志里 U16→U8 的大头

- [x] **Kotlin stringify 走 `KRJSONDumpUtf16`**（2026-08-31）  
  `JsonNative.print` → `KRJSONDumpUtf16` → `stringFromUtf16Chars`。U16 叶子 / object key 直写，不再 `dump_key` / `dump_leaf`。  
  `KRJSONDump` / `WriteTo` 仍是 UTF-8，给 C / NAPI / 文件：
  - `OhosJsonPerfHooks` before 臂：`KRJSONDump` 再宽松 parse
  - `KRRenderValue::toString()`：值是 map/array 时 `json::Dump(value_)`
  - `ToNapiValue` / `ToJsVmValue` 碰到 **object**：不建 JS object，`Dump` 成文本再 `napi_create_string_utf8`（数组是逐元素建的，不走这条）

- [x] **C++ 读 ASCII 叶子用 `toAsciiString()` / `equalsAscii`，别一律 `toString()`**（2026-08-31）  
  已加 `equalsAscii` / `stringEquals` / `utf16View` / `toAsciiString`（U16 全 <0x80 时按字节收窄，不跑 `Utf16ToUtf8`）。
  已切：`KRRenderCore` 的 instance_id / view 名 / prop key / module / method / callbackId；`KRBasePropsHandler` 的 color/border/transform/animation 等；Image resize/tint/filter。
  未切（下游真要 UTF-8 或 JSON 文本）：
  | 调用 | 读的字段 | 下游 |
  |---|---|---|
  | `KRRichTextShadow` | span `value`/`text`/`fontFamily` | `OH_Drawing_*` |
  | `KRCodecModule::CallMethod` | `params->toString()` | URL/Base64 按字节 |
  | Preferences / Calendar / File | `params->toString()` | `cJSON_Parse`、拼路径 |

- [ ] **`KRAnyDataGetMap` / `ObjectForEach` visitor 仍是 `const char *key`**  
  `KRAnyData.cpp`：object 已是 U16 key 时，每个 key `ObjectKeyAtUtf16` → `Utf16ToUtf8` 再交给业务。要消掉就得改 visitor 签名（或提供 U16 重载）。

- [ ] **`toMap()` 对 UTF-8 key 的 object 会 `Utf8ToUtf16`**  
  `KRRenderValue::Map` key 是 `std::u16string`。热路径 `NewObjectUtf16` 不走这条；老 `NewObject` 才走。确认没有剩余 `NewObject` 建业务 object 后再删/并。

### 2. 平台 API 是 UTF-8，进出桥各转一次 — 用户可见字符串

- [x] **Input：回抛复用 Kotlin U16 盒**（2026-08-31）  
  `KRUtf16TextCache`：`SetFromBox` 只在下发时 U16→U8 给 ArkUI；`BoxForUtf8` 在内容未变时复用原盒。  
  已接 `KRTextFieldView` / `KRTextEditorFieldView` / `BuildTextInputStatePayload`。`cached_text_` 仍是 UTF-8（styled text / shortcode 算法依赖）。

- [x] **Image：避免 `toString()` 落盘再 `Utf8ToUtf16` 建新盒**（2026-08-31）  
  `SetImageSrc` 用 `stringEquals` 比盒；`LoadFromSrc` 仅在 adapter 改写 src / 盒与最终 UTF-8 不一致时才 `Utf8ToUtf16`。adapter 不改写时回抛继续用 Kotlin 盒。

- [x] **RichText 选区缓存改 U16**（2026-08-31）  
  `KRRichTextShadow::text_content_` / `KRParagraphInfo` / `KRParagraphSelectionInfo` 改为 `std::u16string`。API 20+ EncodedText 路径按 unit append，不再 `toString` 填选区。placeholder 写入 `U+FFFC` 对齐 Drawing 下标。`GetSelectedContent` / `KRView` 拼 `preContent` 直接 `Make(u16)`。  
  仍是 UTF-8：`fontFamily` / `color` 等样式、`textPostProcessor`、`KRParagraph` 的 `StyledString_AddText`；低版本 `AddText` 仍 `toString()`。

- [ ] **File：回 callback 的 `path` 才转 U16**  
  `WriteFile` / `AppendFile`：`fopen` 用 UTF-8 `dir + "/" + filename`；成功后 `Make(Utf8ToUtf16(filePath))`。`GetFilesDir` 同样。拼路径可继续 UTF-8，回 Kotlin 的 path 若目录本身能留 U16 可少转。

- [ ] **Preferences：`GetSync` 是 UTF-8，`GetItem` 再 `Utf8ToUtf16`**  
  `KRSharedPreferencesModule` / `KROhSharedPreferencesModule`：`GetItem` 把 `preferences->GetSync(key, "")` 转 U16 回 Kotlin。`SetItem`：`params->toString()` 再 parse，`SetSync(key, value)` 只留 UTF-8。

- [ ] **Calendar：`Format` 的结果串走 `Utf8ToUtf16`**  
  `KRCalendarModule::Format`：去掉 `'` 后 `Utf8ToUtf16(convertResult)`（可含本地化日期）。`Parse` / `getFullYear` 等是 `AsciiToUtf16(std::to_string(...))`，见第 3 类。

- [ ] **Codec：编解码按 UTF-8 字节，结果再装 U16 盒**  
  `UrlEncode` / `UrlDecode` / `Base64Encode` / `Base64Decode`：`params->toString()` 进 C 函数，结果 `Utf8ToUtf16`。`Md5` / `Sha256` 走 ASCII 拓宽。

- [ ] **Snapshot：ArkTS 已给 U16 path，立刻转 UTF-8 给文件 I/O**  
  `KRSnapshotManager`：`type=="file"` 用 `GetStringUtf16` 取 `path`，马上 `Utf16ToUtf8` 给 `ProcessSnapshotResultWithFileType`。`type=="cacheKey"` 对 `path` 和 `pathURI` 都转。文件 API 要 UTF-8，path 在 C++ 里可只在 fopen 前转，不要先落成成员 `std::string` 再转来转去。

- [ ] **CacheImage：`toU16String()` 紧接着 `Utf16ToUtf8`**  
  `KRMemoryCacheModule::CacheImage`：`params.opt(src).toU16String()` → `Utf16ToUtf8`，给 `OH_ImageSourceNative_*`、拼 assets 目录、`GenerateCacheKey`。若 key/路径比较能在 U16 上做，只在 Native 解码前转一次。

- [ ] **历史 `SendEvent(string, string)` 整包 `Utf8ToUtf16(json_data)`**  
  `KRRenderCore::SendEvent`：故意不 parse 成 Map（怕 `unordered_map` 打乱 key 顺序）。谁还走这个 overload，谁就多一次整包转码。结构化 `SendEvent(event, KRAnyValue)` 不走这条。先列还在调字符串 overload 的调用点，能改结构化的改掉。

### 3. ASCII 拓宽（`AsciiToUtf16`）— 不算真转码，可后做

按字节 `char → char16_t`，只适合已是 ASCII 的标识。JsonPlatform 516 次，主要是测试页反复 `callModule`。

- [ ] **事件名 / module / view / node / method 统一走 `u"..."` 或直接 U16 字面量，少一次拓宽**  
  - `KRRenderCore::SendEvent`：`event_name`（`click`、`scroll`、`textDidChange`）  
  - `IKRRenderModuleExport`：`GetModuleName()`、`method` → `CallArkTSMethod`  
  - `KRForwardArkTSView(V2)`：`GetViewName()`、`prop_key`、`GetNodeId()`、`method`  
  - `KRBasePropsHandler`：动画回调 `propKey` / `animationKey`  
  - `KRSnapshotManager` / `KRLogModule` / `IKRRenderViewExport`：`method_name`、`nodeId`、`accessibilityFocus`  
  - `KRMemoryCacheModule`：自生成 `cache_key`  
  - `KRCalendarModule`：`std::to_string(year/month/time)`  
  - `KRCodecModule`：`KRMd5` / `KRSha256` hex  
  - `ConvertSizeToString`：`%.2lf` 宽高  

  收益小，优先保证标识确实是 ASCII，避免误用 `AsciiToUtf16` 吃非 ASCII。

### 4. NAPI 还在 `napi_get_value_string_utf8` 的只有 instance_id

每页 `napi utf8=3`、`napi utf16=7`。3 次对得上启动读同一个 ASCII id。

- [x] **`getNApiArgsStdString(env, args[0])` 改为 UTF-16 或定长 ASCII 拷**（2026-08-31）  
  新增 `getNApiArgsAsciiStdString`（`napi_get_value_string_utf16` + ASCII 收窄）。`napi_init` instance_id 与 `KRRenderManager::CreateRenderViewIfNeeded` 已切。`OnInitRenderView` 的 `instance_id.toAsciiString()`。  
  - `napi_init.cpp`：`OnLaunchStart`、`UpdateConfig`，以及 destroy / sendEvent 那一组的 `args[0]`  
  - `KRRenderManager`：`getNApiArgsStdString(env, args[1])` 当 renderView map key  
  实现链：`getNApiArgsStdString` → `getNApiArgsString` → `napi_get_value_string_utf8`。map key 仍是 `std::string` 的话，至少用 ASCII 快路径，不要走完整 UTF-8 解码。  
  用户数据（pageName、pageData、FromNapi 叶子）已经是 `napi_get_value_string_utf16`。

- [ ] **bench 里的 `GetNApiArgsStdString` 保持关宏，不要当产品路径**  
  `napi_init.cpp` bench 仍用 UTF-8 helper；`KUIKLY_OHOS_NAPI_RECORD_BENCH` 默认关。

### 5. Kotlin 热路径 — 无需改 asString

- [x] **`asString` U16：`memcpy` 进 `CharArray`，零转码**  
  本轮全部页面走 `JSON_KIND_U16STRING`。
- [ ] **确认没有新路径再往 Kotlin 塞 U8 盒**  
  `JSON_KIND_STRING` 会走手写 UTF-8 decoder。本轮 `utf8=0`。
- [x] **出桥 `TypeUtils` 用 `KRJSONNewStringUtf16`；解析 `KRJSONParseUtf16`**

## 建议下手顺序

1. [x] Input 回抛复用 U16 盒 / Image 不重建盒（第 2 类）。  
2. [x] C++ 读 ASCII prop / instance_id / view 名：`toAsciiString()`（第 1、4 类）。  
3. 列清 `SendEvent(string, string)` 残留调用点（第 2 类最后一条）。  
4. Dump / Drawing / fopen / Preferences 边界保持转码，不要为了「全 U16」改这些 API。  
5. ASCII 拓宽最后做，收益小。  
6. （未做）`SetProp` key 改 `KRAnyValue` + `equalsAscii`：虚函数面大，本轮用入口 `toAsciiString` 代替。

编码统计辅助代码（`EncodingStats` / `KRJSON_ENC` / `kotlin_asString`）已于 2026-08-31 移除。
