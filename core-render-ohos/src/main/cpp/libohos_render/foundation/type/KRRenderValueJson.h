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

#ifndef CORE_RENDER_OHOS_KRRENDERVALUEJSON_H
#define CORE_RENDER_OHOS_KRRENDERVALUEJSON_H

#include <ark_runtime/jsvm.h>
#include <ark_runtime/jsvm_types.h>
#include <js_native_api.h>
#include <js_native_api_types.h>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libohos_render/foundation/ark_ts.h"
#include "libohos_render/foundation/type/KRRenderCValue.h"
#include "libohos_render/utils/KRJsUtil.h"
#include "libohos_render/utils/NAPIUtil.h"
#include "libohos_render/utils/json/Reader.h"
#include "libohos_render/utils/json/Value.h"

struct NapiValue {
    NapiValue(napi_env e, napi_value v) : env(e), value(v) {}
    napi_env env = nullptr;
    napi_value value = nullptr;
};

class KRRenderValue;
using KRRenderValueMap = std::unordered_map<std::string, KRRenderValue>;
using KRRenderValueArray = std::vector<KRRenderValue>;

/**
 * Compatibility façade over the unified KRJSONValue storage.
 *
 * Every ordinary bridge value is represented by one tagged KRJSONValue word.
 * This façade is an RAII value type: copy retains, move transfers and destruction
 * releases the word. Raw NAPI handles are an ArkTS-only side channel (snapshot
 * PixelMap/drawableDescriptor) and must never cross the Kotlin ABI.
 *
 * TODO: drop `raw_napi_` from the hot object. `shared_ptr` dominates the size of
 * this type versus the 8-byte KRJSONValue; move raw NAPI onto a dedicated callback
 * payload so ordinary values stay a tagged word (+ empty sentinel).
 */
class KRRenderValue {
 public:
    using Map = KRRenderValueMap;
    using Array = KRRenderValueArray;
    using ByteArray = std::shared_ptr<std::vector<uint8_t>>;

    // Default/nullptr construction preserves the old empty shared_ptr state.
    // KRRenderValue::Make() creates a present JSON null instead.
    KRRenderValue() = default;
    KRRenderValue(std::nullptr_t) {}

    KRRenderValue(const KRRenderValue &other)
        : value_(kuikly::util::json::Retain(other.value_)), raw_napi_(other.raw_napi_) {}

    KRRenderValue &operator=(const KRRenderValue &other) {
        if (this != &other) {
            const KRJSONValue retained = kuikly::util::json::Retain(other.value_);
            kuikly::util::json::Release(value_);
            value_ = retained;
            raw_napi_ = other.raw_napi_;
        }
        return *this;
    }

    KRRenderValue(KRRenderValue &&other) noexcept
        : value_(std::exchange(other.value_, KRJSON_INVALID)), raw_napi_(std::move(other.raw_napi_)) {}

    KRRenderValue &operator=(KRRenderValue &&other) noexcept {
        if (this != &other) {
            kuikly::util::json::Release(value_);
            value_ = std::exchange(other.value_, KRJSON_INVALID);
            raw_napi_ = std::move(other.raw_napi_);
        }
        return *this;
    }

    KRRenderValue &operator=(std::nullptr_t) {
        kuikly::util::json::Release(value_);
        value_ = KRJSON_INVALID;
        raw_napi_.reset();
        return *this;
    }

    ~KRRenderValue() {
        kuikly::util::json::Release(value_);
    }

    explicit operator bool() const {
        return value_ != KRJSON_INVALID || raw_napi_ != nullptr;
    }
    bool operator==(std::nullptr_t) const { return !static_cast<bool>(*this); }
    bool operator!=(std::nullptr_t) const { return static_cast<bool>(*this); }

    // Transitional compatibility: existing KRAnyValue call sites may keep `value->`.
    KRRenderValue *operator->() { return this; }
    const KRRenderValue *operator->() const { return this; }

    template<typename... Args>
    static KRRenderValue Make(Args &&...args) {
        return MakeOwned(Build(std::forward<Args>(args)...));
    }

    static KRRenderValue MakeNull() {
        return MakeOwned(kuikly::util::json::NewNull());
    }

    static KRRenderValue MakeEmptyString() {
        static const auto value = MakeOwned(kuikly::util::json::NewString("", 0));
        return value;
    }

    static KRRenderValue MakeBorrowed(KRJSONValue value) {
        return MakeOwned(kuikly::util::json::Retain(value));
    }

    static KRRenderValue Make(NapiValue value) {
        return MakeOwned(Build(), std::make_shared<NapiValue>(value));
    }

    KRJSONValue jsonValue() const {
        return value_;
    }

    bool isNull() const { return !raw_napi_ && type() == KRJSON_NULL; }
    bool isBool() const { return type() == KRJSON_BOOL; }
    bool isInt() const { return kuikly::util::json::TagOf(value_) == kuikly::util::json::kTagInt32; }
    bool isLong() const { return kuikly::util::json::TagOf(value_) == kuikly::util::json::kTagLong; }
    bool isFloat() const { return type() == KRJSON_FLOAT; }
    bool isDouble() const { return type() == KRJSON_DOUBLE; }
    bool isString() const { return type() == KRJSON_STRING; }
    bool isMap() const { return type() == KRJSON_OBJECT; }
    bool isArray() const { return type() == KRJSON_ARRAY; }
    bool isByteArray() const { return type() == KRJSON_BYTES; }
    bool isNapiValue() const { return raw_napi_ != nullptr; }

    // toMap()/toArray() parse a JSON *string* payload transparently. Point
    // queries keep that conversion explicit and single-shot: bridge payloads
    // that may arrive as text call container() once, then opt/at on the result.
    KRRenderValue container() const {
        return isString() ? MakeParsed(stringValue()) : *this;
    }

    // Object/array point query without materializing unordered_map/vector.
    // Missing key / OOB index / wrong type → empty. String JSON is not
    // auto-parsed; call container()/Parse() once, then opt/at.
    KRRenderValue opt(const char *key) const {
        if (key == nullptr || !isMap()) {
            return KRRenderValue();
        }
        return ChildOrEmpty(kuikly::util::json::ObjectGet(value_, key, std::strlen(key)));
    }
    KRRenderValue opt(const std::string &key) const { return opt(key.c_str()); }
    KRRenderValue at(size_t index) const {
        if (!isArray()) {
            return KRRenderValue();
        }
        return ChildOrEmpty(kuikly::util::json::ArrayGet(value_, index));
    }
    size_t size() const { return kuikly::util::json::GetSize(value_); }

    static KRRenderValue Parse(const std::string &json) { return MakeParsed(json); }

    NapiValue toNapiValue() const {
        return raw_napi_ ? *raw_napi_ : NapiValue(nullptr, nullptr);
    }

    bool toBool() const {
        if (isBool()) {
            return kuikly::util::json::GetBool(value_, false);
        }
        return toDouble() != 0.0;
    }

    int32_t toInt() const { return static_cast<int32_t>(toLong()); }

    int64_t toLong() const {
        if (isString()) {
            try {
                return std::stoll(toString());
            } catch (...) {
                return 0;
            }
        }
        return kuikly::util::json::GetInt(value_, 0);
    }

    float toFloat() const {
        float result = static_cast<float>(toDouble());
        return std::isnan(result) ? 0.0f : result;
    }

    double toDouble() const {
        if (isString()) {
            try {
                const auto str = stringValue();
                return str.empty() ? 0.0 : std::stod(str);
            } catch (...) {
                return 0.0;
            }
        }
        if (isBool()) {
            return toBool() ? 1.0 : 0.0;
        }
        return kuikly::util::json::GetDouble(value_, 0.0);
    }

    std::string toString() const {
        if (isString()) {
            return stringValue();
        }
        if (isBool()) {
            return toBool() ? "1" : "0";
        }
        if (isInt() || isLong() || type() == KRJSON_INT) {
            return std::to_string(toLong());
        }
        if (type() == KRJSON_UINT) {
            return std::to_string(kuikly::util::json::GetUint(value_, 0));
        }
        if (isFloat() || isDouble()) {
            std::string result(32, '\0');
            for (;;) {
                auto conversion = std::to_chars(result.data(), result.data() + result.size(), toDouble(),
                                                std::chars_format::fixed);
                if (conversion.ec == std::errc()) {
                    result.resize(static_cast<size_t>(conversion.ptr - result.data()));
                    return result;
                }
                if (conversion.ec != std::errc::value_too_large || result.size() > 128) {
                    return std::string();
                }
                result.resize(result.size() * 2);
            }
        }
        if (isMap() || isArray()) {
            return kuikly::util::json::Dump(value_);
        }
        return std::string();
    }

    Map toMap() const {
        if (isString()) {
            return MakeParsed(stringValue())->toMap();
        }
        Map result;
        if (!isMap()) {
            return result;
        }
        const size_t count = kuikly::util::json::GetSize(value_);
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const char *key = kuikly::util::json::ObjectKeyAt(value_, i);
            const KRJSONValue child = kuikly::util::json::ObjectValueAt(value_, i);
            if (key != nullptr && child != KRJSON_INVALID) {
                result.emplace(key, MakeBorrowed(child));
            }
        }
        return result;
    }

    Array toArray() const {
        if (isString()) {
            return MakeParsed(stringValue())->toArray();
        }
        Array result;
        if (!isArray()) {
            return result;
        }
        const size_t count = kuikly::util::json::GetSize(value_);
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const KRJSONValue child = kuikly::util::json::ArrayGet(value_, i);
            if (child != KRJSON_INVALID) {
                result.emplace_back(MakeBorrowed(child));
            }
        }
        return result;
    }

    ByteArray toByteArray() const {
        auto result = std::make_shared<std::vector<uint8_t>>();
        size_t size = 0;
        const uint8_t *bytes = kuikly::util::json::GetBytes(value_, &size);
        if (bytes != nullptr && size > 0) {
            result->assign(bytes, bytes + size);
        }
        return result;
    }

    KRJSONValue toCValue() const {
        return !static_cast<bool>(*this) || raw_napi_ ? kuikly::util::json::NewNull() : value_;
    }

    void ToNapiValue(const napi_env &env, napi_value *result, napi_status &status) const {
        if (raw_napi_) {
            *result = raw_napi_->value;
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
            case KRJSON_STRING: {
                const auto str = stringValue();
                status = napi_create_string_utf8(env, str.data(), str.size(), result);
                break;
            }
            case KRJSON_BYTES:
                status = ToNapiBytes(env, result);
                break;
            case KRJSON_ARRAY:
                status = ToNapiArray(env, result);
                break;
            case KRJSON_OBJECT: {
                const auto json = kuikly::util::json::Dump(value_);
                status = napi_create_string_utf8(env, json.data(), json.size(), result);
                break;
            }
            default:
                status = napi_get_null(env, result);
                break;
        }
    }

    void ToJsVmValue(JSVM_Env env, JSVM_Value *result, JSVM_Status &status) const {
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
            case KRJSON_STRING: {
                const auto str = stringValue();
                status = OH_JSVM_CreateStringUtf8(env, str.data(), str.size(), result);
                break;
            }
            case KRJSON_BYTES:
                status = ToJsVmBytes(env, result);
                break;
            case KRJSON_ARRAY:
            case KRJSON_OBJECT: {
                const auto json = kuikly::util::json::Dump(value_);
                status = OH_JSVM_CreateStringUtf8(env, json.data(), json.size(), result);
                break;
            }
            default:
                status = OH_JSVM_GetNull(env, result);
                break;
        }
    }

 private:
    explicit KRRenderValue(KRJSONValue value, std::shared_ptr<NapiValue> raw_napi = nullptr)
        : value_(value), raw_napi_(std::move(raw_napi)) {}

    static KRRenderValue MakeOwned(
        KRJSONValue value, std::shared_ptr<NapiValue> raw_napi = nullptr) {
        return KRRenderValue(value, std::move(raw_napi));
    }

    static KRJSONValue Build() { return kuikly::util::json::NewNull(); }
    static KRJSONValue Build(std::nullptr_t) { return Build(); }
    static KRJSONValue Build(bool value) { return kuikly::util::json::NewBool(value); }
    static KRJSONValue Build(int32_t value) { return kuikly::util::json::NewInt32(value); }
    static KRJSONValue Build(int64_t value) { return kuikly::util::json::NewLong(value); }
    static KRJSONValue Build(float value) { return kuikly::util::json::NewFloat(value); }
    static KRJSONValue Build(double value) { return kuikly::util::json::NewDouble(value); }
    static KRJSONValue Build(const std::string &value) {
        return kuikly::util::json::NewString(value.data(), value.size());
    }
    static KRJSONValue Build(const char *value) {
        return value == nullptr ? Build() : kuikly::util::json::NewString(value, std::char_traits<char>::length(value));
    }
    static KRJSONValue Build(const ByteArray &value) {
        if (!value || value->empty()) {
            return kuikly::util::json::NewBytes(nullptr, 0);
        }
        return kuikly::util::json::NewBytes(value->data(), value->size());
    }
    static KRJSONValue Build(const Map &value) {
        KRJSONValue object = kuikly::util::json::NewObject();
        for (const auto &entry : value) {
            const KRJSONValue child = entry.second ? entry.second->value_ : kuikly::util::json::NewNull();
            kuikly::util::json::ObjectPut(object, entry.first.data(), entry.first.size(), child);
        }
        return object;
    }
    static KRJSONValue Build(const Array &value) {
        KRJSONValue array = kuikly::util::json::NewArray();
        for (const auto &entry : value) {
            const KRJSONValue child = entry ? entry->value_ : kuikly::util::json::NewNull();
            kuikly::util::json::ArrayAppend(array, child);
        }
        return array;
    }
    static KRJSONValue Build(const KRRenderCValue &value) {
        return kuikly::util::json::Retain(value);
    }
    static KRJSONValue Build(const napi_env &env, const napi_value &value) {
        return FromNapi(env, value);
    }
    static KRJSONValue Build(const JSVM_Env &env, const JSVM_Value &value) {
        return FromJsVm(env, value);
    }

    static KRRenderValue ChildOrEmpty(KRJSONValue child) {
        return child == KRJSON_INVALID ? KRRenderValue() : MakeBorrowed(child);
    }

    static KRRenderValue MakeParsed(const std::string &json) {
        std::string error;
        KRJSONValue parsed = kuikly::util::json::Reader::Parse(json.data(), json.size(), &error);
        return parsed == KRJSON_INVALID ? MakeNull() : MakeOwned(parsed);
    }

    static KRJSONValue FromNapi(napi_env env, napi_value value) {
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
            std::string result;
            kuikly::util::GetNApiArgsStdString(env, value, result);
            return Build(result);
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
                KRJSONValue child = FromNapi(env, element);
                kuikly::util::json::ArrayAppend(result, child);
                kuikly::util::json::Release(child);
            }
            return result;
        }

        KRJSONValue result = kuikly::util::json::NewObject();
        napi_value names = nullptr;
        if (napi_get_property_names(env, value, &names) == napi_ok) {
            uint32_t count = 0;
            napi_get_array_length(env, names, &count);
            for (uint32_t i = 0; i < count; ++i) {
                napi_value key_value = nullptr;
                napi_value property = nullptr;
                std::string key;
                napi_get_element(env, names, i, &key_value);
                kuikly::util::GetNApiArgsStdString(env, key_value, key);
                napi_get_property(env, value, key_value, &property);
                KRJSONValue child = FromNapi(env, property);
                kuikly::util::json::ObjectPut(result, key.data(), key.size(), child);
                kuikly::util::json::Release(child);
            }
        }
        return result;
    }

    static KRJSONValue FromJsVm(JSVM_Env env, JSVM_Value value) {
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
            std::string result;
            kuikly::util::get_str_from_js_str(env, value, result);
            return Build(result);
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
                KRJSONValue child = FromJsVm(env, element);
                kuikly::util::json::ArrayAppend(result, child);
                kuikly::util::json::Release(child);
            }
            return result;
        }
        return Build();
    }

    std::string stringValue() const {
        size_t size = 0;
        const char *data = kuikly::util::json::GetString(value_, &size);
        return std::string(data, size);
    }

    napi_status ToNapiBytes(napi_env env, napi_value *result) const {
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

    napi_status ToNapiArray(napi_env env, napi_value *result) const {
        const size_t count = kuikly::util::json::GetSize(value_);
        napi_status status = napi_create_array_with_length(env, count, result);
        for (size_t i = 0; status == napi_ok && i < count; ++i) {
            auto child = MakeBorrowed(kuikly::util::json::ArrayGet(value_, i));
            napi_value element = nullptr;
            child->ToNapiValue(env, &element, status);
            if (status == napi_ok) {
                status = napi_set_element(env, *result, i, element);
            }
        }
        return status;
    }

    JSVM_Status ToJsVmBytes(JSVM_Env env, JSVM_Value *result) const {
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

    KRJSONType type() const {
        return raw_napi_ ? KRJSON_NULL : kuikly::util::json::GetType(value_);
    }

    KRJSONValue value_ = KRJSON_INVALID;
    // TODO: not on the hot path — see class comment. Snapshot callbacks only.
    std::shared_ptr<NapiValue> raw_napi_;
};

template<>
inline KRRenderValue KRRenderValue::Make<const char *>(const char *&&value) {
    return value == nullptr || value[0] == '\0' ? MakeEmptyString() : MakeOwned(Build(value));
}

#endif  // CORE_RENDER_OHOS_KRRENDERVALUEJSON_H
