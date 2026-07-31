#!/usr/bin/env bash
#
# 生成 libkuikly_entry.so 的链接期桩（仅 arm64-v8a）。
#
# 产物文件名为 libkuikly_entry_stub.so（避免与真实产物混淆），
# 但其 SONAME 仍为 libkuikly_entry.so，以便仓颉 so 写入正确的 DT_NEEDED。
#
# 产物需要签入仓库：仓颉编译由 DevEco 触发，不保证 CMake 产物此时已生成，
# 桩让 CompileCangjie 与 BuildNativeWithNinja 之间不再有顺序依赖。
#
# 何时需要重新运行：src/main/cpp/cj_bridge.h 中任一签名发生变更时。
# 用法：./gen_stub.sh
set -euo pipefail

NATIVE="${DEVECO_OH_NATIVE_HOME:-/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native}"
DIR="$(cd "$(dirname "$0")" && pwd)"
ABI="arm64-v8a"
TRIPLE="aarch64-linux-ohos"
OUT="$DIR/$ABI/libkuikly_entry_stub.so"

if [ ! -x "$NATIVE/llvm/bin/clang" ]; then
  echo "[gen_stub] 未找到 NDK clang: $NATIVE/llvm/bin/clang" >&2
  echo "[gen_stub] 请设置 DEVECO_OH_NATIVE_HOME 指向 DevEco 的 openharmony/native 目录" >&2
  exit 1
fi

mkdir -p "$DIR/$ABI"

# -nostdlib: 桩函数体为空、无外部引用，不链 libc，保证产物零 DT_NEEDED。
# -Wl,-soname: 关键项，决定仓颉 so 里记录的 DT_NEEDED 名字，必须与真实产物一致。
# 文件名用 _stub，与 SONAME 刻意分离，避免与 CMake 产物混淆。
"$NATIVE/llvm/bin/clang" \
  --target="$TRIPLE" \
  --sysroot="$NATIVE/sysroot" \
  -shared -fPIC -nostdlib -Os \
  -Wl,-soname,libkuikly_entry.so \
  -o "$OUT" \
  "$DIR/cj_bridge_stub.c"

# 去掉调试/本地符号；保留 .dynsym（链接仓颉 so 仍需要那两个导出）。
# 桩从不进 hap、从不执行，体积应尽量小。
if [ -x "$NATIVE/llvm/bin/llvm-strip" ]; then
  "$NATIVE/llvm/bin/llvm-strip" --strip-unneeded "$OUT"
fi

# 清理旧文件名（曾用与真实产物同名的 libkuikly_entry.so）
rm -f "$DIR/$ABI/libkuikly_entry.so"

echo "[gen_stub] 已生成 $OUT ($(wc -c < "$OUT") bytes)"

READELF="$NATIVE/llvm/bin/llvm-readelf"
NM="$NATIVE/llvm/bin/llvm-nm"
if [ -x "$READELF" ] && [ -x "$NM" ]; then
  echo "[gen_stub] --- SONAME / NEEDED ---"
  "$READELF" -d "$OUT" | grep -E 'SONAME|NEEDED' || true
  echo "[gen_stub] --- 导出符号 ---"
  "$NM" -D "$OUT" | grep -E 'RegisterCangjieMyExampleCModule|KRDemoRenderModuleDoCallback' || true
fi
