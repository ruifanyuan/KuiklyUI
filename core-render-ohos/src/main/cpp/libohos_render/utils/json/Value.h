/*
 * Tencent is pleased to support the open source community by making KuiklyUI
 * available.
 * Copyright (C) 2026 Tencent. All rights reserved.
 * Licensed under the License of KuiklyUI;
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * https://github.com/Tencent-TDS/KuiklyUI/blob/main/LICENSE
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CORE_RENDER_OHOS_JSON_VALUE_H
#define CORE_RENDER_OHOS_JSON_VALUE_H

// Internal representation for the "Scheme A" tagged JSON value. The public,
// C-ABI surface lives in libohos_render/api/include/Kuikly/KRJSON.h; this
// header holds the C++ implementation details (heap boxes + encode/decode) and
// the internal functions the C wrappers, reader and builder call directly.
//
// Layout of a KRJSONValue (uint64):
//   bits[0..7]  : storage tag (kTag*)
//   bits[8..63] : inline payload (null/bool/56-bit int) OR a 48-bit pointer to
//                 a reference-counted heap box (ptr = value >> 8).
// See utils/json/DESIGN.md for the full rationale and the alternative
// NaN-boxing design (Scheme B).

#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "libohos_render/api/include/Kuikly/KRJSON.h"

namespace kuikly {
namespace util {
namespace json {

// Scheme A packs a 48-bit pointer into the high 56 bits of the 64-bit word.
static_assert(sizeof(void *) == 8, "KRJSONValue tagging assumes 64-bit pointers");

// Storage tags stored in the low byte of a KRJSONValue.
enum : uint8_t {
    kTagNull = 0,   // immediate
    kTagBool = 1,   // immediate, payload bit 8 = 0/1
    kTagInt = 2,    // immediate, 56-bit signed int in bits[8..63]
    kTagDouble = 3,   // heap NumberBox (double bits that are not lossless float)
    kTagInt64 = 4,    // heap NumberBox (int64 out of 56-bit range)
    kTagUint64 = 5,   // heap NumberBox (uint64 > int64 max)
    kTagString = 6,   // heap StringBox (UTF-8)
    kTagArray = 7,    // heap ArrayBox
    kTagObject = 8,   // heap ObjectBox
    kTagBytes = 9,    // heap BytesBox (bridge-only, not valid JSON text)
    kTagInt32 = 10,   // immediate bridge Int (preserves Kotlin/C++ type)
    kTagFloat = 11,   // immediate, 32-bit IEEE bits in [8..39]; GetType = KRJSON_FLOAT
    kTagLong = 12,    // heap NumberBox (preserves Kotlin/C++ type)
    kTagU16String = 13,  // heap U16StringBox (UTF-16; GetType = KRJSON_U16STRING = 13)
    kTagDoubleF32 = 14,  // immediate float bits; GetType = KRJSON_DOUBLE (lossless)
    kFirstHeapTag = kTagDouble,
    kTagInvalid = 0xFF,
};

// Common header of every heap box: an intrusive atomic refcount (starts at 1).
struct HeapBox {
    std::atomic<int32_t> rc{1};
};

// Number box: holds the raw 8 bytes of a double / int64 / uint64.
struct NumberBox : HeapBox {
    uint64_t bits = 0;
};

// Immutable UTF-8 string: [HeapBox rc][uint32 len][bytes][NUL].
struct StringBox : HeapBox {
    uint32_t len = 0;
    const char *data() const { return reinterpret_cast<const char *>(this + 1); }
    static StringBox *Create(const char *s, size_t n);
    static void Free(StringBox *b);
};

// Immutable UTF-16 string: [HeapBox rc][uint32 unit_count][units][0].
// GetString does not convert this box; callers use GetStringUtf16.
struct U16StringBox : HeapBox {
    uint32_t len = 0;  // code units, not bytes
    const uint16_t *data() const { return reinterpret_cast<const uint16_t *>(this + 1); }
    uint16_t *data() { return reinterpret_cast<uint16_t *>(this + 1); }
    static U16StringBox *Create(const uint16_t *s, size_t n);
    static void Free(U16StringBox *b);
};
static_assert(sizeof(U16StringBox) % alignof(uint16_t) == 0,
              "UTF-16 payload follows U16StringBox and must be 2-byte aligned");

// Immutable bytes: [HeapBox rc][uint32 len][bytes]. One allocation; no vector.
struct BytesBox : HeapBox {
    uint32_t len = 0;
    const uint8_t *data() const { return reinterpret_cast<const uint8_t *>(this + 1); }
    uint8_t *data() { return reinterpret_cast<uint8_t *>(this + 1); }
    static BytesBox *Create(const uint8_t *s, size_t n);
    static void Free(BytesBox *b);
};

// Array box: children stored by value as KRJSONValue words (each retained).
struct ArrayBox : HeapBox {
    std::vector<KRJSONValue> items;
    ~ArrayBox();  // releases every element
};

// Object box: insertion-ordered flat entries; values are retained KRJSONValue
// words. Lookup is linear — for the small objects on the render path this beats
// a hash map (fewer allocations, better cache locality) and preserves order.
// One member list per object (`union`): UTF-8 parse → `utf8`, UTF-16 parse →
// `utf16`. The two encodings are not mixed.
struct ObjectBox : HeapBox {
    using Utf8Members = std::vector<std::pair<std::string, KRJSONValue>>;
    using Utf16Members = std::vector<std::pair<std::u16string, KRJSONValue>>;
    struct Utf16Keys {};
    bool keys_utf16 = false;
    union {
        Utf8Members utf8;
        Utf16Members utf16;
    };
    ObjectBox() : utf8() {}
    explicit ObjectBox(Utf16Keys) : keys_utf16(true), utf16() {}
    ObjectBox(const ObjectBox &) = delete;
    ObjectBox &operator=(const ObjectBox &) = delete;
    ~ObjectBox();  // releases every value, destroys the active union member
};

// ---- tag / encode / decode helpers ----
inline uint8_t TagOf(KRJSONValue v) {
    return static_cast<uint8_t>(v & 0xFFu);
}
static_assert(KRJSON_NULL == kTagNull);
static_assert(KRJSON_BOOL == kTagBool);
static_assert(KRJSON_INT == kTagInt);
static_assert(KRJSON_DOUBLE == kTagDouble);
static_assert(KRJSON_UINT == kTagUint64);
static_assert(KRJSON_STRING == kTagString);
static_assert(KRJSON_ARRAY == kTagArray);
static_assert(KRJSON_OBJECT == kTagObject);
static_assert(KRJSON_BYTES == kTagBytes);
static_assert(KRJSON_FLOAT == kTagFloat);
static_assert(KRJSON_LONG == kTagLong);
static_assert(KRJSON_U16STRING == kTagU16String);

inline bool IsHeapTag(uint8_t t) {
    return t == kTagDouble || t == kTagInt64 || t == kTagUint64 || t == kTagString ||
           t == kTagArray || t == kTagObject || t == kTagBytes ||
           t == kTagLong || t == kTagU16String;
}
inline HeapBox *AsBox(KRJSONValue v) {
    return IsHeapTag(TagOf(v)) ? reinterpret_cast<HeapBox *>(static_cast<uintptr_t>(v >> 8)) : nullptr;
}
inline bool IsUnique(KRJSONValue v) {
    HeapBox *box = AsBox(v);
    return box != nullptr && box->rc.load(std::memory_order_acquire) == 1;
}
inline KRJSONValue EncodePtr(const void *p, uint8_t tag) {
    const uintptr_t u = reinterpret_cast<uintptr_t>(p);
    // Scheme A stores the pointer in bits[8..63]; it must fit in 56 bits.
    // aarch64/OHOS user VA is <= 48 bits today; a >56-bit or tag/MTE pointer
    // would corrupt the round-trip. Guarded in debug builds.
    assert((u >> 56) == 0 && "pointer exceeds 56-bit VA (Scheme A tag assumption)");
    return (static_cast<uint64_t>(u) << 8) | tag;
}
inline KRJSONValue EncodeInt56(int64_t x) {
    return (static_cast<uint64_t>(x) << 8) | kTagInt;
}
inline int64_t DecodeInt56(KRJSONValue v) {
    return static_cast<int64_t>(v) >> 8;  // arithmetic shift sign-extends
}

// ---- internal value operations (the C wrappers in KRJSON.cpp forward here;
//      reader/builder call these directly to stay off the C-ABI hot path) ----
KRJSONValue Retain(KRJSONValue v);
void Release(KRJSONValue v);

KRJSONValue NewNull();
KRJSONValue NewBool(bool b);
KRJSONValue NewInt32(int32_t x);
KRJSONValue NewInt(int64_t x);
KRJSONValue NewLong(int64_t x);
KRJSONValue NewUint(uint64_t x);
KRJSONValue NewFloat(float f);
KRJSONValue NewDouble(double d);
KRJSONValue NewString(const char *s, size_t n);
KRJSONValue NewStringUtf16(const uint16_t *s, size_t n);
KRJSONValue NewBytes(const uint8_t *data, size_t n);
KRJSONValue NewArray();
KRJSONValue NewObject();
KRJSONValue NewObjectUtf16();
void ArrayAppend(KRJSONValue array, KRJSONValue child);
void ArraySet(KRJSONValue array, size_t index, KRJSONValue child);
void ObjectPut(KRJSONValue object, const char *key, size_t key_len, KRJSONValue child);
void ObjectPutUtf16(KRJSONValue object, const uint16_t *key, size_t units, KRJSONValue child);

KRJSONType GetType(KRJSONValue v);
bool GetBool(KRJSONValue v, bool default_value);
int64_t GetInt(KRJSONValue v, int64_t default_value);
uint64_t GetUint(KRJSONValue v, uint64_t default_value);
double GetDouble(KRJSONValue v, double default_value);
const char *GetString(KRJSONValue v, size_t *out_len);
std::string Utf16ToUtf8(const uint16_t *s, size_t n);
std::u16string Utf8ToUtf16(const char *s, size_t n);
/** Borrowed UTF-16 units; NULL if not a UTF-16 string box. `out_units` is code-unit count. */
const uint16_t *GetStringUtf16(KRJSONValue v, size_t *out_units);
const uint8_t *GetBytes(KRJSONValue v, size_t *out_len);
size_t GetSize(KRJSONValue v);
KRJSONValue ArrayGet(KRJSONValue array, size_t index);
KRJSONValue ObjectGet(KRJSONValue object, const char *key, size_t key_len);
KRJSONValue ObjectGetUtf16(KRJSONValue object, const uint16_t *key, size_t units);
bool ObjectKeysAreUtf16(KRJSONValue object);
/** O(1) indexed access over the insertion-ordered member vector. Missing → INVALID. */
KRJSONValue ObjectValueAt(KRJSONValue object, size_t index);
/** Borrowed UTF-8 key bytes. nullptr if missing. Debug-asserts on UTF-16-key objects. */
const char *ObjectKeyAt(KRJSONValue object, size_t index);
/** Borrowed UTF-16 key units. nullptr if missing. Debug-asserts on UTF-8-key objects. */
const uint16_t *ObjectKeyAtUtf16(KRJSONValue object, size_t index, size_t *out_units);
void ObjectForEach(KRJSONValue object, KRJSONObjectVisitor visitor, void *userdata);

std::string Dump(KRJSONValue v);
/** Compact JSON text as UTF-16 units (same contract as Dump: `{"a":1}`). */
std::u16string DumpUtf16(KRJSONValue v);

}  // namespace json
}  // namespace util
}  // namespace kuikly

#endif  // CORE_RENDER_OHOS_JSON_VALUE_H
