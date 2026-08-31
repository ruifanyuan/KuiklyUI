# TODO：OHOS KRJSON 字符串 U16/U8 残留转码

> 2026-08-31 真机 `LNG0223C13000049` debug HAP。跑了 router / JsonPlatformTestPage / InputViewDemoPage / image_demo / ListExamplePage / RichTextSpanLongPressExamplePage / MyModuleDemoPage / PageDataTestPage。
>
> 计数来自 `EncodingStats`（C++ tag `KRJSON_ENC`）和 `JsonNative`（Kotlin `kotlin_asString`）。`new` 是建盒次数；`conv` 是真正跑了 `Utf8ToUtf16` / `Utf16ToUtf8` / `AsciiToUtf16`。

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

Kotlin 各页 `asString utf8=0`；JsonPlatform `utf16=508 key_utf16=1220`。

## TODO

### 1. C++ / JSON 文本只要 UTF-8（U16 盒现场转）— 日志里 U16→U8 的大头

- [ ] **Dump / `WriteTo` 保持 UTF-8，不当优化项**  
  `Value.cpp`：`KRJSON_U16STRING` 叶子和 U16 object key 先 `Utf16ToUtf8` 再 `w.String` / `w.Key`。RapidJSON 只要 `const char *`。调用方：
  - Kotlin `LazyJsonMap` / `LazyJsonList`.`toString()` → `JsonNative.print` → `KRJSONDump`
  - `OhosJsonPerfHooks` before 臂：`KRJSONDump` 再宽松 parse
  - `KRRenderValue::toString()`：值是 map/array 时 `json::Dump(value_)`
  - `ToNapiValue` / `ToJsVmValue` 碰到 **object**：不建 JS object，`Dump` 成文本再 `napi_create_string_utf8`（数组是逐元素建的，不走这条）

- [ ] **C++ 读叶子别一律 `toString()`**  
  `stringValue()` 对 U16 盒会 `Utf16ToUtf8`。现在这些都会走：
  | 调用 | 读的字段 | 下游 |
  |---|---|---|
  | `KRImageView::SetImageSrc` | `value->toString()` → `image_src_` | 和 adapter / 路径判断比字符串 |
  | `KRBasePropsHandler` | `backgroundColor` / `border` / `transform` / `boxShadow` / `animation` / `accessibility*` | `ConvertToHexColor`、`UpdateNodeBorder` 等 |
  | `KRRichTextShadow` | span `value`/`text`/`fontFamily`/`color`/`textAlign`/`textShadow` | `OH_Drawing_*` |
  | `KRCodecModule::CallMethod` | `params->toString()` | `KREncodeURLComponent` 等按字节 |
  | `KRSharedPreferences*` / `KRCalendarModule` / `KRFileModule` | `params->toString()` | `cJSON_Parse`、`GetSync`、拼路径 |
  | `KRRenderCore::OnCallNative` | `arg0->toString()` | `instance_id` 当 `std::string` map key |
  | `CreateRenderView` / `SetProp` / `CallModuleMethod` | `arg2->toString()` | view 名、prop key、module/method 名 |

  多数是 ASCII（`#ff0000`、view 名、instance id），但仍走完整 UTF-16→UTF-8。可做 ASCII 快路径，或关键路径改读 U16。

- [ ] **`KRAnyDataGetMap` / `ObjectForEach` visitor 仍是 `const char *key`**  
  `KRAnyData.cpp`：object 已是 U16 key 时，每个 key `ObjectKeyAtUtf16` → `Utf16ToUtf8` 再交给业务。要消掉就得改 visitor 签名（或提供 U16 重载）。

- [ ] **`toMap()` 对 UTF-8 key 的 object 会 `Utf8ToUtf16`**  
  `KRRenderValue::Map` key 是 `std::u16string`。热路径 `NewObjectUtf16` 不走这条；老 `NewObject` 才走。确认没有剩余 `NewObject` 建业务 object 后再删/并。

### 2. 平台 API 是 UTF-8，进出桥各转一次 — 用户可见字符串

- [ ] **Input：`std::string` 缓存改持 U16，或回抛时别再编**  
  - `GetInputNodeContentText`（`KRViewUtil.cpp`）读 `NODE_TEXT_INPUT_TEXT` 的 `item->string`，ArkUI 给 UTF-8。  
  - `KRTextFieldView`：`CreateTextInputStateMap`、`OnTextDidChanged`、`OnInputFocus`、`OnInputBlur`、`OnInputReturn` 都是 `map[u"text"] = Make(Utf8ToUtf16(GetContentText()))`。  
  - `KRTextEditor*`：读 `state_.cached_text_`（`std::string` raw）。同样几条回抛 `Utf8ToUtf16(state_.cached_text_)`。  
  - Kotlin 下发：`SetContentText(value->toString())` 先 U16→U8 写入缓存/ArkUI，用户再改又 U8→U16 抛回。一次输入周期最多两趟。  
  ArkUI set/get 仍是 UTF-8，但 `cached_text_` 和回抛 map 可以只转边界一次。

- [ ] **Image：避免 `toString()` 落盘再 `Utf8ToUtf16` 建新盒**  
  `SetImageSrc`：`src = value->toString()` 写入 `image_src_`，同时 `image_src_value_ = value` 留原盒。  
  `LoadFromSrc`：adapter 可能改写 `image_option_->src_`（仍 UTF-8），然后 `image_src_value_ = Make(Utf8ToUtf16(image_src_))`。  
  loadSuccess / loadError / complete 优先用 `image_src_value_`；空了才 `Utf8ToUtf16(image_src_)`。  
  image_demo 的 U8→U16=18（其它页约 7）就是 adapter 改 src 后重建 U16 盒。adapter 不改写时不应再转。

- [ ] **RichText：Drawing 要 UTF-8，post-processor / 选区再吐 UTF-8**  
  - 排版：span `value`/`text`/`fontFamily`/`color` 走 `GetKRValue(...)->toString()` 进 `OH_Drawing_*`。  
  - `textPostProcessor`：`RunTextPostProcessor` 返回 UTF-8 `seg.text_or_src`，再 `Utf8ToUtf16` 写回新 span 的 `value`/`text`，图片段写 `__kr_image_src__`。  
  - 选区：`selection_rects_.text_content` / shadow `text_content_` 是 UTF-8。`GetSelectedContent` 先 `Utf8ToUtf16` 再按 UTF-16 下标 `substr`。  
  - `KRView` 拼选区：相邻 text 的 `GetTextContent()` 也是这条 UTF-8 缓存，再 `Utf8ToUtf16` 塞进 `preContent`/`postContent`。  
  Drawing 边界难消；选区缓存若改 U16 可少一趟。

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

- [ ] **`getNApiArgsStdString(env, args[0])` 改为 UTF-16 或定长 ASCII 拷**  
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
  `JSON_KIND_STRING` 会走手写 UTF-8 decoder。本轮 `utf8=0`。回归时看 `kotlin_asString utf8=` 是否回升。
- [x] **出桥 `TypeUtils` 用 `KRJSONNewStringUtf16`；解析 `KRJSONParseUtf16`**

## 建议下手顺序

1. Input `cached_text_` / Image `image_src_`：用户串进出各少一趟（第 2 类）。  
2. C++ 读 ASCII prop / instance_id / view 名：别走完整 `toString()`（第 1、4 类）。  
3. 列清 `SendEvent(string, string)` 残留调用点（第 2 类最后一条）。  
4. Dump / Drawing / fopen / Preferences 边界保持转码，不要为了「全 U16」改这些 API。  
5. ASCII 拓宽最后做，收益小。

## 怎么复现计数

C++：`EncodingStats.cpp`，hilog tag `KRJSON_ENC`。  
Kotlin：`JsonNative.ohosArm64.kt`，`[KRJSON_ENC] kotlin_asString ...`。  
`aa start -a EntryAbility -b com.tencent.kuiklyohosdemo --ps pageName <Page>`，按 PID 取该进程最后一条 snapshot。
