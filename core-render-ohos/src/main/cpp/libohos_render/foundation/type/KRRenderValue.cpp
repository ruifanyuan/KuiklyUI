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

// Out-of-line home for KRRenderValue's ArkTS/JSVM bridge conversions. Keeping
// these (and the heavy engine headers below) out of KRRenderValue.h stops the
// full ArkVM/NAPI function surface from being re-parsed by every render TU that
// only needs the tagged-word accessors.

#include "libohos_render/foundation/type/KRRenderValue.h"

#include <ark_runtime/jsvm.h>
#include <js_native_api.h>
#include <cstring>
#include <string>
#include <vector>

#include "libohos_render/foundation/ark_ts.h"
#include "libohos_render/utils/KRJsUtil.h"
#include "libohos_render/utils/KRRenderLoger.h"
#include "libohos_render/utils/NAPIUtil.h"

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace {
// Materialize a JS string into a UTF-16 KRJSONValue box using the engine's
// two-call length-probe idiom. `get(buf, bufsize, out_count)` must match the
// napi_get_value_string_utf16 / OH_JSVM_GetValueStringUtf16 contract, returning
// `ok` on success. Free function (not a member): it only needs the public JSON
// constructor, so it stays with its two callers here.
template <typename Get, typename Status>
KRJSONValue NewStringFromUtf16Get(Get &&get, Status ok) {
    // Probe with buf == nullptr: `*result` is the full unit count. A filled
    // stack buffer reports at most bufsize-1, so it cannot tell a 255-unit
    // string from a longer one.
    size_t units = 0;
    if (get(nullptr, 0, &units) != ok) {
        return kuikly::util::json::NewStringUtf16(nullptr, 0);
    }
    constexpr size_t kStackUnits = 256;
    if (units < kStackUnits) {
        char16_t stack[kStackUnits];
        size_t copied = 0;
        if (get(stack, kStackUnits, &copied) != ok) {
            return kuikly::util::json::NewStringUtf16(nullptr, 0);
        }
        return kuikly::util::json::NewStringUtf16(reinterpret_cast<const uint16_t *>(stack), copied);
    }
    std::vector<char16_t> heap(units + 1);
    size_t copied = 0;
    if (get(heap.data(), units + 1, &copied) != ok) {
        return kuikly::util::json::NewStringUtf16(nullptr, 0);
    }
    return kuikly::util::json::NewStringUtf16(reinterpret_cast<const uint16_t *>(heap.data()), copied);
}
}  // namespace

KRJSONValue KRRenderValue::Build(const napi_env &env, const napi_value &value) {
    return FromNapi(env, value);
}

KRJSONValue KRRenderValue::Build(const JSVM_Env &env, const JSVM_Value &value) {
    return FromJsVm(env, value);
}

namespace {
constexpr uint32_t kKRJSONWrapMagic = 0x4B524A53u;  // 'KRJS'
constexpr char kKRJSONRoutePayloadTokenKey[] = "__kuiklyKRJsonPayloadId";
constexpr auto kKRJSONRoutePayloadMaxAge = std::chrono::minutes(1);
const napi_type_tag kKRJSONTypeTag = {
    0x4B75696B6C794A53ULL,
    0x4F4E577261704B52ULL,
};

struct KRJSONWrap {
    uint32_t magic;
    KRJSONValue value;
};

struct KRJSONRoutePayloadEntry {
    KRJSONValue value;
    std::chrono::steady_clock::time_point expires_at;
};

std::mutex g_krjson_route_payload_mutex;
std::unordered_map<std::string, KRJSONRoutePayloadEntry> g_krjson_route_payloads;
uint64_t g_krjson_route_payload_next_id = 1;

void PruneExpiredKRJSONRoutePayloadsLocked(std::chrono::steady_clock::time_point now) {
    for (auto it = g_krjson_route_payloads.begin(); it != g_krjson_route_payloads.end();) {
        if (it->second.expires_at < now) {
            kuikly::util::json::Release(it->second.value);
            it = g_krjson_route_payloads.erase(it);
        } else {
            ++it;
        }
    }
}

std::string RegisterKRJSONRoutePayload(KRJSONValue value) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_krjson_route_payload_mutex);
    PruneExpiredKRJSONRoutePayloadsLocked(now);
    const std::string token = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()) +
        "-" + std::to_string(g_krjson_route_payload_next_id++);
    g_krjson_route_payloads.emplace(
        token,
        KRJSONRoutePayloadEntry{
            kuikly::util::json::Retain(value),
            now + kKRJSONRoutePayloadMaxAge,
        });
    return token;
}

KRJSONValue TakeKRJSONRoutePayload(const std::string &token) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_krjson_route_payload_mutex);
    PruneExpiredKRJSONRoutePayloadsLocked(now);
    const auto it = g_krjson_route_payloads.find(token);
    if (it == g_krjson_route_payloads.end()) {
        return KRJSON_INVALID;
    }
    const KRJSONValue value = it->second.value;
    g_krjson_route_payloads.erase(it);
    return value;
}

void FinalizeKRJSONWrap(napi_env, void *data, void *) {
    auto *wrap = static_cast<KRJSONWrap *>(data);
    if (wrap == nullptr) {
        return;
    }
    if (wrap->magic == kKRJSONWrapMagic) {
        wrap->magic = 0;
        kuikly::util::json::Release(wrap->value);
    }
    delete wrap;
}

KRJSONWrap *UnwrapKRJSON(napi_env env, napi_callback_info info, size_t argc, napi_value *args) {
    napi_value self = nullptr;
    if (napi_get_cb_info(env, info, &argc, args, &self, nullptr) != napi_ok || self == nullptr) {
        return nullptr;
    }
    bool matches = false;
    if (napi_check_object_type_tag(env, self, &kKRJSONTypeTag, &matches) != napi_ok || !matches) {
        return nullptr;
    }
    void *raw = nullptr;
    if (napi_unwrap(env, self, &raw) != napi_ok || raw == nullptr) {
        return nullptr;
    }
    auto *wrap = static_cast<KRJSONWrap *>(raw);
    return wrap->magic == kKRJSONWrapMagic ? wrap : nullptr;
}

napi_value MakeNapiFromKRJSON(napi_env env, KRJSONValue value) {
    if (value == KRJSON_INVALID) {
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }
    auto render_value = KRRenderValue::MakeBorrowed(value);
    if (render_value.isMap() || render_value.isArray()) {
        return KRRenderValue::WrapKRJSON(env, value);
    }
    napi_value result = nullptr;
    napi_status status = napi_ok;
    render_value.ToNapiValue(env, &result, status);
    return status == napi_ok ? result : nullptr;
}

bool ReadUtf16Key(napi_env env, napi_value value, std::u16string *out) {
    if (out == nullptr || value == nullptr) {
        return false;
    }
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_string) {
        return false;
    }
    kuikly::util::GetNApiArgsStdU16String(env, value, *out);
    return true;
}

KRJSONValue GetObjectChild(KRJSONValue object, const std::u16string &key) {
    if (kuikly::util::json::ObjectKeysAreUtf16(object)) {
        return kuikly::util::json::ObjectGetUtf16(
            object, reinterpret_cast<const uint16_t *>(key.data()), key.size());
    }
    const std::string utf8 = kuikly::util::json::Utf16ToUtf8(
        reinterpret_cast<const uint16_t *>(key.data()), key.size());
    return kuikly::util::json::ObjectGet(object, utf8.data(), utf8.size());
}

napi_value KRJsonGet(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 1, args);
    std::u16string key;
    if (wrap == nullptr || args[0] == nullptr || !ReadUtf16Key(env, args[0], &key) ||
        kuikly::util::json::GetType(wrap->value) != KRJSON_OBJECT) {
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }
    return MakeNapiFromKRJSON(env, GetObjectChild(wrap->value, key));
}

napi_value KRJsonHas(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 1, args);
    std::u16string key;
    bool has = wrap != nullptr && args[0] != nullptr && ReadUtf16Key(env, args[0], &key) &&
               kuikly::util::json::GetType(wrap->value) == KRJSON_OBJECT &&
               GetObjectChild(wrap->value, key) != KRJSON_INVALID;
    napi_value result = nullptr;
    napi_get_boolean(env, has, &result);
    return result;
}

napi_value KRJsonAt(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 1, args);
    int32_t index = -1;
    if (wrap == nullptr || args[0] == nullptr ||
        napi_get_value_int32(env, args[0], &index) != napi_ok || index < 0 ||
        kuikly::util::json::GetType(wrap->value) != KRJSON_ARRAY) {
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }
    return MakeNapiFromKRJSON(env, kuikly::util::json::ArrayGet(wrap->value, index));
}

napi_value KRJsonToJSONString(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 0, args);
    const std::string json = wrap == nullptr ? std::string() : kuikly::util::json::Dump(wrap->value);
    napi_value result = nullptr;
    napi_create_string_utf8(env, json.data(), json.size(), &result);
    return result;
}

napi_value KRJsonToJSON(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 0, args);
    napi_value result = nullptr;
    if (wrap == nullptr || napi_create_object(env, &result) != napi_ok) {
        return nullptr;
    }
    const std::string token = RegisterKRJSONRoutePayload(wrap->value);
    napi_value token_value = nullptr;
    if (napi_create_string_utf8(env, token.data(), token.size(), &token_value) != napi_ok ||
        napi_set_named_property(env, result, kKRJSONRoutePayloadTokenKey, token_value) != napi_ok) {
        return nullptr;
    }
    return result;
}

napi_value KRJsonGetType(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 0, args);
    napi_value result = nullptr;
    napi_create_int32(env, wrap == nullptr ? KRJSON_NULL : kuikly::util::json::GetType(wrap->value), &result);
    return result;
}

napi_value KRJsonGetSize(napi_env env, napi_callback_info info) {
    napi_value args[1] = {nullptr};
    auto *wrap = UnwrapKRJSON(env, info, 0, args);
    napi_value result = nullptr;
    napi_create_double(env, wrap == nullptr ? 0 : static_cast<double>(kuikly::util::json::GetSize(wrap->value)),
                       &result);
    return result;
}
}  // namespace

napi_value KRRenderValue::WrapKRJSON(napi_env env, KRJSONValue value) {
    KR_LOG_INFO_WITH_TAG("KRJsonNative") << "stage=wrap_enter, type=" << kuikly::util::json::GetType(value)
                                         << ", size=" << kuikly::util::json::GetSize(value);
    napi_value result = nullptr;
    if (env == nullptr || napi_create_object(env, &result) != napi_ok) {
        KR_LOG_ERROR_WITH_TAG("KRJsonNative") << "stage=wrap_create_object_failed";
        return nullptr;
    }
    auto *wrap = new KRJSONWrap{kKRJSONWrapMagic, kuikly::util::json::Retain(value)};
    if (napi_wrap(env, result, wrap, FinalizeKRJSONWrap, nullptr, nullptr) != napi_ok) {
        KR_LOG_ERROR_WITH_TAG("KRJsonNative") << "stage=wrap_napi_wrap_failed";
        FinalizeKRJSONWrap(env, wrap, nullptr);
        return nullptr;
    }
    (void)napi_type_tag_object(env, result, &kKRJSONTypeTag);
    napi_property_descriptor descriptors[] = {
        {"get", nullptr, KRJsonGet, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"has", nullptr, KRJsonHas, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"at", nullptr, KRJsonAt, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"toJSONString", nullptr, KRJsonToJSONString, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"toJSON", nullptr, KRJsonToJSON, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"type", nullptr, nullptr, KRJsonGetType, nullptr, nullptr, napi_default, nullptr},
        {"size", nullptr, nullptr, KRJsonGetSize, nullptr, nullptr, napi_default, nullptr},
    };
    if (napi_define_properties(env, result, sizeof(descriptors) / sizeof(descriptors[0]), descriptors) != napi_ok) {
        KR_LOG_ERROR_WITH_TAG("KRJsonNative") << "stage=wrap_define_properties_failed";
        return nullptr;
    }
    KR_LOG_INFO_WITH_TAG("KRJsonNative") << "stage=wrap_ready";
    return result;
}

bool KRRenderValue::TryUnwrapKRJSON(napi_env env, napi_value value, KRJSONValue *out) {
    if (env == nullptr || value == nullptr || out == nullptr) {
        return false;
    }
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_object) {
        return false;
    }
    bool matches = false;
    if (napi_check_object_type_tag(env, value, &kKRJSONTypeTag, &matches) != napi_ok || !matches) {
        return false;
    }
    void *raw = nullptr;
    if (napi_unwrap(env, value, &raw) != napi_ok || raw == nullptr) {
        return false;
    }
    auto *wrap = static_cast<KRJSONWrap *>(raw);
    if (wrap->magic != kKRJSONWrapMagic) {
        return false;
    }
    *out = wrap->value;
    return true;
}

napi_value KRRenderValue::TakeKRJSONRoutePayload(napi_env env, napi_value token) {
    std::string token_string;
    napi_valuetype type = napi_undefined;
    if (env == nullptr || token == nullptr ||
        napi_typeof(env, token, &type) != napi_ok || type != napi_string) {
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }
    kuikly::util::GetNApiArgsStdString(env, token, token_string);
    const KRJSONValue value = ::TakeKRJSONRoutePayload(token_string);
    if (value == KRJSON_INVALID) {
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }
    napi_value result = WrapKRJSON(env, value);
    kuikly::util::json::Release(value);
    return result;
}

KRJSONValue KRRenderValue::FromNapi(napi_env env, napi_value value, int depth) {
    if (depth > kMaxBridgeDepth) {
        return Build();  // too deep; drop to null rather than overflow the stack
    }
    KRJSONValue wrapped = KRJSON_INVALID;
    if (TryUnwrapKRJSON(env, value, &wrapped)) {
        return kuikly::util::json::Retain(wrapped);
    }
    napi_valuetype value_type = napi_undefined;
    napi_typeof(env, value, &value_type);
    if (value_type == napi_boolean) {
        bool result = false;
        napi_get_value_bool(env, value, &result);
        return Build(result);
    }
    if (value_type == napi_number) {
        double result = 0;
        napi_get_value_double(env, value, &result);
        return Build(result);
    }
    if (value_type == napi_string) {
        return NewStringFromUtf16Get(
            [&](char16_t *buf, size_t bufsize, size_t *result) {
                return napi_get_value_string_utf16(env, value, buf, bufsize, result);
            },
            napi_ok);
    }
    if (value_type != napi_object) {
        return Build();
    }

    bool is_array_buffer = false;
    napi_is_arraybuffer(env, value, &is_array_buffer);
    if (is_array_buffer) {
        void *data = nullptr;
        size_t size = 0;
        napi_get_arraybuffer_info(env, value, &data, &size);
        return kuikly::util::json::NewBytes(static_cast<const uint8_t *>(data), size);
    }

    ArkTS ark_ts(env);
    if (ark_ts.IsTypedArray(value)) {
        napi_typedarray_type typed_type;
        size_t length = 0;
        void *data = nullptr;
        napi_value array_buffer = nullptr;
        size_t byte_offset = 0;
        if (napi_get_typedarray_info(env, value, &typed_type, &length, &data, &array_buffer, &byte_offset) == napi_ok &&
            typed_type == napi_int8_array) {
            return kuikly::util::json::NewBytes(static_cast<const uint8_t *>(data), length);
        }
    }

    bool is_array = false;
    napi_is_array(env, value, &is_array);
    if (is_array) {
        uint32_t length = 0;
        napi_get_array_length(env, value, &length);
        KRJSONValue result = kuikly::util::json::NewArray();
        for (uint32_t i = 0; i < length; ++i) {
            napi_value element = nullptr;
            napi_get_element(env, value, i, &element);
            KRJSONValue child = FromNapi(env, element, depth + 1);
            kuikly::util::json::ArrayAppend(result, child);
            kuikly::util::json::Release(child);
        }
        return result;
    }

    KRJSONValue result = kuikly::util::json::NewObjectUtf16();
    napi_value names = nullptr;
    if (napi_get_property_names(env, value, &names) == napi_ok) {
        uint32_t count = 0;
        napi_get_array_length(env, names, &count);
        for (uint32_t i = 0; i < count; ++i) {
            napi_value key_value = nullptr;
            napi_value property = nullptr;
            std::u16string key;
            napi_get_element(env, names, i, &key_value);
            kuikly::util::GetNApiArgsStdU16String(env, key_value, key);
            napi_get_property(env, value, key_value, &property);
            KRJSONValue child = FromNapi(env, property, depth + 1);
            kuikly::util::json::ObjectPutUtf16(result, reinterpret_cast<const uint16_t *>(key.data()), key.size(),
                                               child);
            kuikly::util::json::Release(child);
        }
    }
    return result;
}

KRJSONValue KRRenderValue::FromJsVm(JSVM_Env env, JSVM_Value value, int depth) {
    if (depth > kMaxBridgeDepth) {
        return Build();  // too deep; drop to null rather than overflow the stack
    }
    JSVM_ValueType value_type;
    OH_JSVM_Typeof(env, value, &value_type);
    if (value_type == JSVM_BOOLEAN) {
        bool result = false;
        OH_JSVM_GetValueBool(env, value, &result);
        return Build(result);
    }
    if (value_type == JSVM_NUMBER) {
        double result = 0;
        OH_JSVM_GetValueDouble(env, value, &result);
        return Build(result);
    }
    if (value_type == JSVM_STRING) {
        return NewStringFromUtf16Get(
            [&](char16_t *buf, size_t bufsize, size_t *result) {
                return OH_JSVM_GetValueStringUtf16(env, value, buf, bufsize, result);
            },
            JSVM_OK);
    }
    bool is_array_buffer = false;
    OH_JSVM_IsArraybuffer(env, value, &is_array_buffer);
    if (is_array_buffer) {
        void *data = nullptr;
        size_t size = 0;
        OH_JSVM_GetArraybufferInfo(env, value, &data, &size);
        return kuikly::util::json::NewBytes(static_cast<const uint8_t *>(data), size);
    }
    bool is_array = false;
    OH_JSVM_IsArray(env, value, &is_array);
    if (is_array) {
        uint32_t length = 0;
        OH_JSVM_GetArrayLength(env, value, &length);
        KRJSONValue result = kuikly::util::json::NewArray();
        for (uint32_t i = 0; i < length; ++i) {
            JSVM_Value element = nullptr;
            OH_JSVM_GetElement(env, value, i, &element);
            KRJSONValue child = FromJsVm(env, element, depth + 1);
            kuikly::util::json::ArrayAppend(result, child);
            kuikly::util::json::Release(child);
        }
        return result;
    }
    return Build();
}

void KRRenderValue::ToNapiValue(const napi_env &env, napi_value *result, napi_status &status) const {
    if (isNapiValue()) {
        *result = toNapiValue().value;
        status = napi_ok;
        return;
    }
    switch (type()) {
        case KRJSON_BOOL:
            status = napi_get_boolean(env, toBool(), result);
            break;
        case KRJSON_INT:
            status = isInt() ? napi_create_int32(env, toInt(), result)
                             : napi_create_int64(env, toLong(), result);
            break;
        case KRJSON_LONG:
            status = napi_create_int64(env, toLong(), result);
            break;
        case KRJSON_FLOAT:
        case KRJSON_DOUBLE:
        case KRJSON_UINT:
            status = napi_create_double(env, toDouble(), result);
            break;
        case KRJSON_U16STRING: {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            status = napi_create_string_utf16(env, reinterpret_cast<const char16_t *>(utf16), units, result);
            break;
        }
        case KRJSON_STRING: {
            size_t len = 0;
            const char *utf8 = kuikly::util::json::GetString(value_, &len);
            status = napi_create_string_utf8(env, utf8, len, result);
            break;
        }
        case KRJSON_BYTES:
            status = ToNapiBytes(env, result);
            break;
        case KRJSON_ARRAY:
        case KRJSON_OBJECT:
            // Internal C++ → ArkTS container transport keeps the original
            // KRJSON tree alive. ArkTS consumers read the wrapper directly
            // and only dump/materialize at an explicit legacy boundary.
            *result = WrapKRJSON(env, value_);
            status = *result == nullptr ? napi_generic_failure : napi_ok;
            break;
        default:
            status = napi_get_null(env, result);
            break;
    }
}

void KRRenderValue::ToJsVmValue(JSVM_Env env, JSVM_Value *result, JSVM_Status &status) const {
    switch (type()) {
        case KRJSON_BOOL:
            status = OH_JSVM_GetBoolean(env, toBool(), result);
            break;
        case KRJSON_INT:
            status = isInt() ? OH_JSVM_CreateInt32(env, toInt(), result)
                             : OH_JSVM_CreateInt64(env, toLong(), result);
            break;
        case KRJSON_LONG:
            status = OH_JSVM_CreateInt64(env, toLong(), result);
            break;
        case KRJSON_FLOAT:
        case KRJSON_DOUBLE:
        case KRJSON_UINT:
            status = OH_JSVM_CreateDouble(env, toDouble(), result);
            break;
        case KRJSON_U16STRING: {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            status = OH_JSVM_CreateStringUtf16(env, reinterpret_cast<const char16_t *>(utf16), units, result);
            break;
        }
        case KRJSON_STRING: {
            size_t len = 0;
            const char *utf8 = kuikly::util::json::GetString(value_, &len);
            status = OH_JSVM_CreateStringUtf8(env, utf8, len, result);
            break;
        }
        case KRJSON_BYTES:
            status = ToJsVmBytes(env, result);
            break;
        case KRJSON_ARRAY:
        case KRJSON_OBJECT: {
            // JSVM bridge contract: BOTH arrays and objects cross as a JSON
            // *string* (the JS side parses). Note this differs from the NAPI
            // path above, where arrays become real JS arrays — the two
            // engines have different consumers, so the mapping is
            // deliberately not shared.
            const auto json = kuikly::util::json::Dump(value_);
            status = OH_JSVM_CreateStringUtf8(env, json.data(), json.size(), result);
            break;
        }
        default:
            status = OH_JSVM_GetNull(env, result);
            break;
    }
}

napi_status KRRenderValue::ToNapiBytes(napi_env env, napi_value *result) const {
    size_t size = 0;
    const uint8_t *source = kuikly::util::json::GetBytes(value_, &size);
    void *destination = nullptr;
    napi_value array_buffer = nullptr;
    napi_status status = napi_create_arraybuffer(env, size, &destination, &array_buffer);
    if (status == napi_ok && size > 0) {
        std::memcpy(destination, source, size);
    }
    if (status == napi_ok) {
        status = napi_create_typedarray(env, napi_int8_array, size, array_buffer, 0, result);
    }
    return status;
}

JSVM_Status KRRenderValue::ToJsVmBytes(JSVM_Env env, JSVM_Value *result) const {
    size_t size = 0;
    const uint8_t *source = kuikly::util::json::GetBytes(value_, &size);
    void *destination = nullptr;
    JSVM_Value array_buffer = nullptr;
    JSVM_Status status = OH_JSVM_CreateArraybuffer(env, size, &destination, &array_buffer);
    if (status == JSVM_OK && size > 0) {
        std::memcpy(destination, source, size);
    }
    if (status == JSVM_OK) {
        status = OH_JSVM_CreateTypedarray(env, JSVM_INT8_ARRAY, size, array_buffer, 0, result);
    }
    return status;
}
