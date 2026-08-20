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

#ifndef CORE_RENDER_OHOS_KRJSONVALUE_H
#define CORE_RENDER_OHOS_KRJSONVALUE_H

// Internal representation for the "Scheme A" tagged JSON value. The public,
// C-ABI surface lives in libohos_render/api/include/Kuikly/KRJson.h; this
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

#include "libohos_render/api/include/Kuikly/KRJson.h"

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
    kTagDouble = 3,   // heap KRNumberBox (double bits)
    kTagInt64 = 4,    // heap KRNumberBox (int64 out of 56-bit range)
    kTagUint64 = 5,   // heap KRNumberBox (uint64 > int64 max)
    kTagString = 6,   // heap KRStringBox
    kTagArray = 7,    // heap KRArrayBox
    kTagObject = 8,   // heap KRObjectBox
    kFirstHeapTag = kTagDouble,
    kTagInvalid = 0xFF,
};

// Common header of every heap box: an intrusive atomic refcount (starts at 1).
struct KRJSONBox {
    std::atomic<int32_t> rc{1};
};

// Number box: holds the raw 8 bytes of a double / int64 / uint64.
struct KRNumberBox : KRJSONBox {
    uint64_t bits = 0;
};

// Immutable, single-allocation string: [KRJSONBox rc][uint32 len][bytes][NUL].
struct KRStringBox : KRJSONBox {
    uint32_t len = 0;
    const char *data() const { return reinterpret_cast<const char *>(this + 1); }
    static KRStringBox *Create(const char *s, size_t n);
    static void Free(KRStringBox *b);
};

// Array box: children stored by value as KRJSONValue words (each retained).
struct KRArrayBox : KRJSONBox {
    std::vector<KRJSONValue> items;
    ~KRArrayBox();  // releases every element
};

// Object box: insertion-ordered flat entries; values are retained KRJSONValue
// words. Lookup is linear — for the small objects on the render path this beats
// a hash map (fewer allocations, better cache locality) and preserves order.
struct KRObjectBox : KRJSONBox {
    std::vector<std::pair<std::string, KRJSONValue>> members;
    ~KRObjectBox();  // releases every value
};

// ---- tag / encode / decode helpers ----
inline uint8_t TagOf(KRJSONValue v) {
    return static_cast<uint8_t>(v & 0xFFu);
}
inline bool IsHeapTag(uint8_t t) {
    return t >= kFirstHeapTag && t != kTagInvalid;
}
inline KRJSONBox *AsBox(KRJSONValue v) {
    return IsHeapTag(TagOf(v)) ? reinterpret_cast<KRJSONBox *>(static_cast<uintptr_t>(v >> 8)) : nullptr;
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

// ---- internal value operations (the C wrappers in KRJson.cpp forward here;
//      reader/builder call these directly to stay off the C-ABI hot path) ----
KRJSONValue Retain(KRJSONValue v);
void Release(KRJSONValue v);

KRJSONValue NewNull();
KRJSONValue NewBool(bool b);
KRJSONValue NewInt(int64_t x);
KRJSONValue NewUint(uint64_t x);
KRJSONValue NewDouble(double d);
KRJSONValue NewString(const char *s, size_t n);
KRJSONValue NewArray();
KRJSONValue NewObject();
void ArrayAppend(KRJSONValue array, KRJSONValue child);
void ObjectPut(KRJSONValue object, const char *key, size_t key_len, KRJSONValue child);

KRJSONType GetType(KRJSONValue v);
bool GetBool(KRJSONValue v, bool default_value);
int64_t GetInt(KRJSONValue v, int64_t default_value);
uint64_t GetUint(KRJSONValue v, uint64_t default_value);
double GetDouble(KRJSONValue v, double default_value);
const char *GetString(KRJSONValue v, size_t *out_len);
size_t GetSize(KRJSONValue v);
KRJSONValue ArrayGet(KRJSONValue array, size_t index);
KRJSONValue ObjectGet(KRJSONValue object, const char *key, size_t key_len);
/** O(1) indexed access over the insertion-ordered member vector. Missing → INVALID. */
KRJSONValue ObjectValueAt(KRJSONValue object, size_t index);
/** Borrowed key bytes, valid while `object` is retained. Missing → nullptr. */
const char *ObjectKeyAt(KRJSONValue object, size_t index);
void ObjectForEach(KRJSONValue object, KRJSONObjectVisitor visitor, void *userdata);

std::string Dump(KRJSONValue v);

}  // namespace json
}  // namespace util
}  // namespace kuikly

#endif  // CORE_RENDER_OHOS_KRJSONVALUE_H
