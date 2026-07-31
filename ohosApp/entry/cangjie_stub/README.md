# 仓颉链接期桩（libkuikly_entry_stub.so）

## 这是什么

`arm64-v8a/libkuikly_entry_stub.so` 是一个**空壳共享库**，只导出两个桥接符号的空实现：

- `RegisterCangjieMyExampleCModule`
- `KRDemoRenderModuleDoCallback`

磁盘文件名带 `_stub`，避免与 CMake 真实产物混淆；但其 **SONAME 被显式设为
`libkuikly_entry.so`**，与真实产物一致。

## 为什么需要它

`libohos_app_cangjie_entry.so`（仓颉）通过 FFI 调用上面两个符号，它们的真实实现在
`src/main/cpp/napi_init.cpp`，编入 CMake 产物 `libkuikly_entry.so`。仓颉侧必须在链接期
解析到这两个符号，才能记录 `DT_NEEDED libkuikly_entry.so`，运行时 CJ-RUNTIME 以
`RTLD_LOCAL` 加载仓颉 so 时才解析得到（机制与踩坑见
`.ai/references/ohos-cangjie-native-bridge.md`）。

问题在于 hvigor 的 `@CompileCangjie` 与 `@BuildNativeWithNinja` 之间没有依赖边，
clean 构建时仓颉可能先于 CMake 编译，此时真实 `.so` 尚未产出，链接直接失败。

链接器只需要一个**提供了这两个符号、SONAME 正确**的库即可完成链接并写入 DT_NEEDED，
并不关心实现内容。于是签入这个桩：仓颉链接文件名 `libkuikly_entry_stub.so`，
运行时动态链接器按 SONAME 解析到 hap 中真正的 `libkuikly_entry.so`，桩里的空实现
永远不会被执行。

这样仓颉编译与 CMake 编译彻底解耦，不再需要任何构建顺序 hack。

## 何时需要重新生成

**修改 `../src/main/cpp/cj_bridge.h` 中任一函数签名后**，必须重新运行：

```sh
./gen_stub.sh
```

并把更新后的 `arm64-v8a/libkuikly_entry_stub.so` 一并提交。

`cj_bridge.h` 同时被 `napi_init.cpp` 和 `cj_bridge_stub.c` 包含，签名不一致会在编译期
报错，不会留到运行时才暴露。

脚本默认使用 DevEco 内置 NDK；如果装在非默认位置，用 `DEVECO_OH_NATIVE_HOME` 指定
`openharmony/native` 目录。产物会经 `llvm-strip --strip-unneeded` 压缩（保留动态导出符号）。

## 校验

桩本身（SONAME 正确、无任何 NEEDED、两个符号为 `T`）：

```sh
NATIVE=/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native
$NATIVE/llvm/bin/llvm-readelf -d arm64-v8a/libkuikly_entry_stub.so | grep -E 'SONAME|NEEDED'
$NATIVE/llvm/bin/llvm-nm -D arm64-v8a/libkuikly_entry_stub.so
```

构建后的仓颉产物（DT_NEEDED 含 `libkuikly_entry.so`——注意是真货名而非 `_stub`，
两个符号为 `U`）：

```sh
CJ=../build/default/intermediates/cj/build/default/aarch64-linux-ohos/release/ohos_app_cangjie_entry/libohos_app_cangjie_entry.so
$NATIVE/llvm/bin/llvm-readelf -d $CJ | grep NEEDED
$NATIVE/llvm/bin/llvm-nm -D $CJ | grep -E 'RegisterCangjie|KRDemoRender'
```

## 注意事项

- 本目录**不在 `libs/` 下**，因此不会被 hvigor 当作待打包 native 库收集。hap 中的
  `libkuikly_entry.so` 应当只有一份，来自 CMake 真实产物。
- 仓库根 `.gitignore` 有 `*.so` 规则，`ohosApp/.gitignore` 里已加否定规则放行本桩。
- 目前只生成 `arm64-v8a`。`cjpm.toml` 的 `[target.x86_64-linux-ohos]` 未配置
  `--link-options`，因此 **x86_64 模拟器下仓颉 FFI 桥接不可用**；如需支持，
  需补一份 x86_64 桩并在该 target 下加上同样的链接参数。
