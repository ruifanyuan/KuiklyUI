# callKotlin Interop Performance Optimization (OHOS)

> Goal: optimize Native (`core-render-ohos`) → Kotlin `callKotlin` interop performance.
> Device: physical `LNG0223C13000049` (primary); earlier numbers also from emulator.

## Progress Log

| Step | Status | Notes |
|------|--------|-------|
| 1. Understand code (render-ohos / core-ksp / core) | DONE | |
| 2. Demo page + OHOS measurement | DONE | `CallKotlinPerfPage` + ohosApp `CallKotlinPerfTestModule` |
| 3. Optimization plan + execute | DONE | A/B/C/D tried |
| 4. Re-test early opts; keep what matters | DONE | Keep A, B, D; drop C |
| 5. Phase distribution + long JSON | DONE | |
| 6. Lazy cJSON* JSONObject proxy | DONE | Flat ~50–70µs even at 3MB if unread |

---

## Keep / Drop verdict (device A/B, 2026-08-03)

Re-measured on `LNG0223C13000049` with and without KSP opt B (`logs/kuikly_ab_with_opts.log` vs `logs/kuikly_ab_no_ksp.log`).

| Opt | What | Device evidence | Verdict |
|-----|------|-----------------|---------|
| **A** | Cache `instanceIdValue_` on `KRRenderCore` | Within same run: `layout_full` 27.8µs vs `layout_cachedArg0` 24.9µs (~**10% / ~3µs**) | **KEEP** — small absolute, cheap, always on hot path |
| **B** | KSP OHOS entry: method-specific `toAny`, no lambda, codegen try/catch | LAYOUT `cpp_callKotlin`: **37µs (with B)** vs **76µs (without B)** ≈ **2×** | **KEEP** — matters for high-frequency small calls |
| **C** | Eager structured Map ARRAY (`__kuikly_map_v1__`) | Superseded by D; never called from `toCValue` after D | **DROP** — removed dead C++/Kotlin marker path |
| **D** | Lazy `NATIVE_JSON` (cJSON* + refcount) | 64KB FIRE string **~7.8ms** vs lazy **~60–70µs**; scales flat when unread | **KEEP** — dominant win for Map/Array events |

Notes:
- FIRE string path is dominated by `toKString` + `JSONObject` parse; A/B do not change that.
- Opt B does not materially change large-string FIRE or lazy Map numbers (noise-level).
- Absolute LAYOUT numbers vary by device thermal/load; relative A/B deltas above are from paired runs.

---

## Call path

```
KRRenderCore::CallKotlinMethod
  └─ instanceIdValue_ (cached)                         // A
  └─ argN->toCValue()                                  // Map → NATIVE_JSON (D)
  └─ callKotlin_(...)
       └─ KSP staticCFunction (method-specific toAny)  // B
            └─ BridgeManager → fireViewEvent
                 └─ TypeUtils → CJsonJSONObject (ohos) / String
                      └─ BridgeManager → fireViewEvent (String|JSONObject)
```

---

## Measurement notes

Kotlin-side phase counters in `core` were removed (too intrusive on Bridge/TypeUtils).
Bench timing is C++-only in ohosApp `CallKotlinPerfTestModule`
(`cpp_toCValue_ns_per` / `cpp_callKotlin_ns_per`). For deeper Kotlin breakdowns use
platform profilers (HiTrace / Perfetto), not framework hooks.

### Lazy cJSON* bridge (kept)

Map/Array `toCValue` passes `NATIVE_JSON` (`KuiklyCJsonOwner*` in `longValue`, refcounted).
Kotlin ohosArm64 `CJsonJSONObject.fromOwner` **retain**s; Cleaner **release**s on GC. C++ dtor also **release**s.

| ~bytes | string (legacy) | lazy cJSON (unread) | speedup |
|------:|----------------:|--------------------:|--------:|
| 1K | ~193µs | ~67µs | ~3× |
| 64K | ~7.8ms | ~69µs | ~110× |
| 3MB | ~326ms | ~48µs | ~6800× |

---

## Optimizations kept in tree

### A. Cache instanceId on `KRRenderCore` (`core-render-ohos`)
- Construct `instanceIdValue_` once in ctor; reuse in `CallKotlinMethod`.

### B. KSP OHOS entry hot path (`core-ksp`)
- Remove per-call `callKotlinClosure` lambda allocation.
- Codegen-time try/catch (use `catchException` flag) instead of runtime branch.
- Method-specific `toAny` arity:
  - `LAYOUT_VIEW` → arg0 only
  - `FIRE_VIEW_EVENT` → arg0–arg3
  - `FIRE_CALLBACK` / `UPDATE` / `DESTROY` → arg0–arg2
  - `CREATE_INSTANCE` → reuse `asString()` as instanceId (no double convert)

### D. Lazy cJSON* bridge (OHOS only)
- C++ Map/Array `toCValue` → `NATIVE_JSON` (opaque `shared_ptr<KuiklyCJsonOwner>` handle).
- Kotlin **ohosArm64**: `CJsonJSONObject` subclass + `CJsonNative` FFI; `retain` returns a new shared handle; `TypeUtils` wraps owner → `JSONObject`.
- **commonMain** stays free of cJSON: `JSONObject` is `open` for subclassing; `fireViewEvent` accepts `String` or `JSONObject`.
- Field access via `kuikly_cjson_*`; large leaves not copied until read.

### Dropped: C. Eager structured Map bridge
- Removed `ToStructuredMapLocked` / `ToJsonMapOrArrayLocked` and `__kuikly_map_v1__` Kotlin decode.
- Was ~2.5–3.5× vs string JSON but still paid `toKString` on large leaves; D is strictly better for unread/partial-read Maps.

---

## How to re-run

Open demo page `CallKotlinPerfPage` (router / in-app navigation; Want `--ps pageName` is not wired).
Debug OHOS build only — native `CallKotlinPerfTestModule` is `#ifndef NDEBUG` / CMake Debug-gated.

```bash
hdc -t LNG0223C13000049 shell aa force-stop com.tencent.kuiklyohosdemo
hdc -t LNG0223C13000049 shell aa start -a EntryAbility -b com.tencent.kuiklyohosdemo
# Navigate to CallKotlinPerfPage; hilog tag CallKotlinPerf — BASIC / PHASES_CPP / DONE
```

### Follow-ups (optional)

- Prefer Map/Array at Native call sites instead of pre-serializing JSON strings.
- Cache Kotlin-side pageId string to skip `toKString` on LAYOUT_VIEW.
- Measure try/catch-off build to shrink LAYOUT FFI residual.
