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

#ifndef CORE_RENDER_OHOS_KRJSON_H
#define CORE_RENDER_OHOS_KRJSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "KuiklyExport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A JSON value is a tagged 8-byte word ("Scheme A"): the low byte holds a type
 * tag and the high 56 bits hold either an inline payload (null / bool / 56-bit
 * int) or a 48-bit pointer to a reference-counted heap box (double, large
 * integer, string, bytes, array, object). Copying the word does NOT change ownership;
 * use KRJSONRetain / KRJSONRelease to manage lifetime.
 *
 * Immediate values (null/bool/small-int) need no allocation and KRJSONRetain /
 * KRJSONRelease on them are no-ops. Heap-backed values are atomically
 * reference counted.
 */
typedef uint64_t KRJSONValue;

/** Sentinel returned by KRJSONParse on failure (also distinguishable via err).
 *  Note: KRJSONGetType(KRJSON_INVALID) reports KRJSON_NULL — test == KRJSON_INVALID. */
#define KRJSON_INVALID ((KRJSONValue)0xFFu)

/** Public semantic value type (independent of internal storage tag). */
typedef enum {
    KRJSON_NULL = 0,
    KRJSON_BOOL = 1,
    KRJSON_INT = 2,     // JSON signed integer (materialized as Int/Long by range)
    KRJSON_UINT = 3,    // unsigned integer strictly greater than INT64_MAX
    KRJSON_DOUBLE = 4,
    KRJSON_STRING = 5,
    KRJSON_ARRAY = 6,
    KRJSON_OBJECT = 7,
    /** Bridge-only types below are not produced by JSON parsing. */
    KRJSON_BYTES = 8,
    KRJSON_LONG = 9,
    KRJSON_FLOAT = 10,
} KRJSONType;

/** Object iteration callback; return false to stop early. `value` is borrowed. */
typedef bool (*KRJSONObjectVisitor)(const char *key, size_t key_len, KRJSONValue value, void *userdata);

// ---- lifetime ----
/** Increment refcount (no-op for immediates). Returns the same value. */
KUIKLY_EXPORT KRJSONValue KRJSONRetain(KRJSONValue value);
/** Decrement refcount; frees the heap box (and its children) at zero. */
KUIKLY_EXPORT void KRJSONRelease(KRJSONValue value);

// ---- parse / serialize ----
/**
 * Parse UTF-8 JSON. Returns an OWNED value (release with KRJSONRelease), or
 * KRJSON_INVALID on error. If `err` is non-null it receives a malloc'd message
 * on failure (free with KRJSONFreeString); it is set to NULL on success.
 */
KUIKLY_EXPORT KRJSONValue KRJSONParse(const char *data, size_t len, char **err);
/** Serialize to a malloc'd, NUL-terminated JSON string (free with KRJSONFreeString). */
KUIKLY_EXPORT char *KRJSONDump(KRJSONValue value);
/** Free a string returned by KRJSONDump / KRJSONParse's err. */
KUIKLY_EXPORT void KRJSONFreeString(char *str);

// ---- type / scalar accessors (by value) ----
KUIKLY_EXPORT KRJSONType KRJSONGetType(KRJSONValue value);
KUIKLY_EXPORT bool KRJSONGetBool(KRJSONValue value, bool default_value);
KUIKLY_EXPORT int64_t KRJSONGetInt(KRJSONValue value, int64_t default_value);
KUIKLY_EXPORT uint64_t KRJSONGetUint(KRJSONValue value, uint64_t default_value);
KUIKLY_EXPORT double KRJSONGetDouble(KRJSONValue value, double default_value);
/** Borrowed, NUL-terminated bytes valid while `value` is retained; empty on mismatch. */
KUIKLY_EXPORT const char *KRJSONGetString(KRJSONValue value, size_t *out_len);
/** Borrowed binary data valid while `value` is retained; NULL on mismatch. */
KUIKLY_EXPORT const uint8_t *KRJSONGetBytes(KRJSONValue value, size_t *out_len);

// ---- containers ----
/** Array length, object member count, or byte payload length; else 0. */
KUIKLY_EXPORT size_t KRJSONGetSize(KRJSONValue value);
/** Array element by index; BORROWED (retain to keep). KRJSON_INVALID if out of range. */
KUIKLY_EXPORT KRJSONValue KRJSONArrayGet(KRJSONValue array, size_t index);
/** Object member by key; BORROWED. KRJSON_INVALID if missing. O(n) linear scan
 *  over the flat member list (fast for the small objects on the render path). */
KUIKLY_EXPORT KRJSONValue KRJSONObjectGet(KRJSONValue object, const char *key);
/** Object member by insertion index; BORROWED. KRJSON_INVALID if out of range. */
KUIKLY_EXPORT KRJSONValue KRJSONObjectValueAt(KRJSONValue object, size_t index);
/** Borrowed key C string, valid while `object` is retained. NULL if out of range. */
KUIKLY_EXPORT const char *KRJSONObjectKeyAt(KRJSONValue object, size_t index);
/** Iterate object members in insertion order. */
KUIKLY_EXPORT void KRJSONObjectForEach(KRJSONValue object, KRJSONObjectVisitor visitor, void *userdata);

// ---- constructors (return OWNED values) ----
KUIKLY_EXPORT KRJSONValue KRJSONNewNull(void);
KUIKLY_EXPORT KRJSONValue KRJSONNewBool(bool v);
KUIKLY_EXPORT KRJSONValue KRJSONNewInt32(int32_t v);
KUIKLY_EXPORT KRJSONValue KRJSONNewInt(int64_t v);
KUIKLY_EXPORT KRJSONValue KRJSONNewLong(int64_t v);
KUIKLY_EXPORT KRJSONValue KRJSONNewUint(uint64_t v);
KUIKLY_EXPORT KRJSONValue KRJSONNewFloat(float v);
KUIKLY_EXPORT KRJSONValue KRJSONNewDouble(double v);
KUIKLY_EXPORT KRJSONValue KRJSONNewString(const char *data, size_t len);
KUIKLY_EXPORT KRJSONValue KRJSONNewBytes(const uint8_t *data, size_t len);
KUIKLY_EXPORT KRJSONValue KRJSONNewArray(void);
KUIKLY_EXPORT KRJSONValue KRJSONNewObject(void);
/** Append `child` to array; the array retains it (caller still owns its own ref). */
KUIKLY_EXPORT void KRJSONArrayAppend(KRJSONValue array, KRJSONValue child);
/** Put key->child into object; object retains child (replaces + releases any existing). */
KUIKLY_EXPORT void KRJSONObjectPut(KRJSONValue object, const char *key, size_t key_len, KRJSONValue child);

#ifdef __cplusplus
}
#endif

#endif  // CORE_RENDER_OHOS_KRJSON_H
