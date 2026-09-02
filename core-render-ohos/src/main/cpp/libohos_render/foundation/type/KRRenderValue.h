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

#ifndef CORE_RENDER_OHOS_KRRENDERVALUE_H
#define CORE_RENDER_OHOS_KRRENDERVALUE_H

// The bridge-conversion method *bodies* live in KRRenderValue.cpp, so editing
// them no longer recompiles every render TU that includes KRCommon.h, and this
// header is ~350 lines lighter. The include set below is kept at parity with the
// old all-inline header on purpose: a large number of render TUs reach these
// engine/util headers transitively through KRCommon.h and rely on that. Trimming
// them to cut header fan-out is a separate include-what-you-use cleanup across
// those TUs (tracked as a follow-up), not part of this header/impl split.
#include <ark_runtime/jsvm.h>
#include <ark_runtime/jsvm_types.h>
#include <js_native_api.h>
#include <js_native_api_types.h>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
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
using KRRenderValueMap = std::unordered_map<std::u16string, KRRenderValue>;
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
        static const auto value = MakeOwned(kuikly::util::json::NewStringUtf16(nullptr, 0));
        return value;
    }

    /** Box a C ABI / ArkUI UTF-8 buffer as UTF-16. Prefer a u16string source and Make(). */
    static KRRenderValue MakeUtf16(const std::string &utf8) {
        return MakeOwned(BuildUtf16FromUtf8(utf8.data(), utf8.size()));
    }
    static KRRenderValue MakeUtf16(const char *utf8) {
        return utf8 == nullptr ? MakeEmptyString()
                               : MakeOwned(BuildUtf16FromUtf8(utf8, std::char_traits<char>::length(utf8)));
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
    bool isString() const { return type() == KRJSON_STRING || type() == KRJSON_U16STRING; }
    bool isMap() const { return type() == KRJSON_OBJECT; }
    bool isArray() const { return type() == KRJSON_ARRAY; }
    bool isByteArray() const { return type() == KRJSON_BYTES; }
    bool isNapiValue() const { return raw_napi_ != nullptr; }

    // toMap()/toArray() parse a JSON *string* payload transparently. Point
    // queries keep that conversion explicit and single-shot: bridge payloads
    // that may arrive as text call container() once, then opt/at on the result.
    KRRenderValue container() const {
        return isString() ? parsedFromJsonText() : *this;
    }

    // Object/array point query without materializing unordered_map/vector.
    // Missing key / OOB index / wrong type → empty. String JSON is not
    // auto-parsed; call container()/Parse() once, then opt/at.
    // Prefer opt(u"key") / opt(std::u16string) for C++ literals. Pointer
    // overloads forward to the string versions. Query-key encoding is converted
    // only at this lookup edge when it does not match the object's stored keys.
    KRRenderValue opt(std::nullptr_t) const { return KRRenderValue(); }
    KRRenderValue opt(const std::string &key) const {
        if (!isMap()) {
            return KRRenderValue();
        }
        if (!kuikly::util::json::ObjectKeysAreUtf16(value_)) {
            return ChildOrEmpty(kuikly::util::json::ObjectGet(value_, key.data(), key.size()));
        }
        uint16_t stack[64];
        std::vector<uint16_t> heap;
        uint16_t *units = stack;
        if (key.size() > 64) {
            heap.resize(key.size());
            units = heap.data();
        }
        for (size_t i = 0; i < key.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(key[i]);
            assert(c < 0x80u && "opt(string) on UTF-16-key object requires ASCII keys");
            units[i] = static_cast<uint16_t>(c);
        }
        return ChildOrEmpty(kuikly::util::json::ObjectGetUtf16(value_, units, key.size()));
    }
    KRRenderValue opt(const std::u16string &key) const {
        if (!isMap()) {
            return KRRenderValue();
        }
        const uint16_t *key16 = reinterpret_cast<const uint16_t *>(key.data());
        if (kuikly::util::json::ObjectKeysAreUtf16(value_)) {
            return ChildOrEmpty(kuikly::util::json::ObjectGetUtf16(value_, key16, key.size()));
        }
        std::string utf8;
        utf8.resize(key.size());
        for (size_t i = 0; i < key.size(); ++i) {
            assert(key[i] < 0x80 && "opt(u16string) on UTF-8-key object requires ASCII keys");
            utf8[i] = static_cast<char>(key[i]);
        }
        return ChildOrEmpty(kuikly::util::json::ObjectGet(value_, utf8.data(), utf8.size()));
    }
    KRRenderValue opt(const char *key) const {
        return key == nullptr ? KRRenderValue() : opt(std::string(key));
    }
    KRRenderValue opt(const char16_t *key) const {
        return key == nullptr ? KRRenderValue() : opt(std::u16string(key));
    }
    KRRenderValue opt(const uint16_t *key) const {
        return key == nullptr ? KRRenderValue()
                              : opt(std::u16string(reinterpret_cast<const char16_t *>(key)));
    }
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
                return std::stoll(toAsciiString());
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
                const auto str = toAsciiString();
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

    std::u16string toU16String() const {
        if (type() == KRJSON_U16STRING) {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            return utf16 == nullptr ? std::u16string()
                                    : std::u16string(reinterpret_cast<const char16_t *>(utf16), units);
        }
        if (type() == KRJSON_STRING) {
            size_t size = 0;
            const char *data = kuikly::util::json::GetString(value_, &size);
            return data == nullptr ? std::u16string() : kuikly::util::json::Utf8ToUtf16(data, size);
        }
        const std::string utf8 = toString();
        return kuikly::util::json::Utf8ToUtf16(utf8.data(), utf8.size());
    }

    // Zero-copy view of a U16 string box. {nullptr, 0} if not KRJSON_U16STRING.
    std::pair<const uint16_t *, size_t> utf16View() const {
        if (type() != KRJSON_U16STRING) {
            return {nullptr, 0};
        }
        size_t units = 0;
        const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
        return {utf16, units};
    }

    // Compare this string box to an ASCII literal without allocating or transcoding.
    bool equalsAscii(const char *lit) const {
        if (lit == nullptr || !isString()) {
            return false;
        }
        const size_t lit_len = std::strlen(lit);
        if (type() == KRJSON_U16STRING) {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            if (utf16 == nullptr || units != lit_len) {
                return false;
            }
            for (size_t i = 0; i < units; ++i) {
                if (utf16[i] != static_cast<unsigned char>(lit[i])) {
                    return false;
                }
            }
            return true;
        }
        size_t size = 0;
        const char *data = kuikly::util::json::GetString(value_, &size);
        return data != nullptr && size == lit_len && std::memcmp(data, lit, lit_len) == 0;
    }

    // Compare two string boxes. Same-encoding path is memcmp; mixed encoding falls back.
    bool stringEquals(const KRRenderValue &other) const {
        if (!isString() || !other.isString()) {
            return false;
        }
        if (type() == KRJSON_U16STRING && other.type() == KRJSON_U16STRING) {
            size_t a_n = 0;
            size_t b_n = 0;
            const uint16_t *a = kuikly::util::json::GetStringUtf16(value_, &a_n);
            const uint16_t *b = kuikly::util::json::GetStringUtf16(other.value_, &b_n);
            if (a_n != b_n) {
                return false;
            }
            return a_n == 0 || (a != nullptr && b != nullptr && std::memcmp(a, b, a_n * sizeof(uint16_t)) == 0);
        }
        if (type() == KRJSON_STRING && other.type() == KRJSON_STRING) {
            size_t a_n = 0;
            size_t b_n = 0;
            const char *a = kuikly::util::json::GetString(value_, &a_n);
            const char *b = kuikly::util::json::GetString(other.value_, &b_n);
            if (a_n != b_n) {
                return false;
            }
            return a_n == 0 || (a != nullptr && b != nullptr && std::memcmp(a, b, a_n) == 0);
        }
        return toString() == other.toString();
    }

    // Narrow a U16 box that is all < 0x80 without running Utf16ToUtf8.
    // Non-ASCII U16 falls back to stringValue(); UTF-8 boxes and non-strings
    // match toString() / stringValue().
    std::string toAsciiString() const {
        if (type() == KRJSON_U16STRING) {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            if (utf16 == nullptr) {
                return {};
            }
            for (size_t i = 0; i < units; ++i) {
                if (utf16[i] >= 0x80) {
                    return stringValue();
                }
            }
            std::string out(units, '\0');
            for (size_t i = 0; i < units; ++i) {
                out[i] = static_cast<char>(utf16[i]);
            }
            return out;
        }
        if (type() == KRJSON_STRING) {
            size_t size = 0;
            const char *data = kuikly::util::json::GetString(value_, &size);
            return data == nullptr ? std::string() : std::string(data, size);
        }
        return toString();
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
                // general = shortest round-trip form (scientific when shorter),
                // matching Dump()/RapidJSON and the Kotlin tokenizer's toDouble()
                // fallthrough. fixed would expand large-magnitude values past any
                // cap and yield an empty string (silent data loss).
                auto conversion = std::to_chars(result.data(), result.data() + result.size(), toDouble(),
                                                std::chars_format::general);
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
            return parsedFromJsonText()->toMap();
        }
        Map result;
        if (!isMap()) {
            return result;
        }
        const size_t count = kuikly::util::json::GetSize(value_);
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const KRJSONValue child = kuikly::util::json::ObjectValueAt(value_, i);
            if (child == KRJSON_INVALID) {
                continue;
            }
            if (kuikly::util::json::ObjectKeysAreUtf16(value_)) {
                size_t units = 0;
                const uint16_t *key16 = kuikly::util::json::ObjectKeyAtUtf16(value_, i, &units);
                if (key16 != nullptr) {
                    result.emplace(std::u16string(reinterpret_cast<const char16_t *>(key16), units),
                                   MakeBorrowed(child));
                }
            } else {
                const char *key = kuikly::util::json::ObjectKeyAt(value_, i);
                if (key != nullptr) {
                    result.emplace(kuikly::util::json::Utf8ToUtf16(key, std::strlen(key)), MakeBorrowed(child));
                }
            }
        }
        return result;
    }

    Array toArray() const {
        if (isString()) {
            return parsedFromJsonText()->toArray();
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

    // Bridge conversions live in KRRenderValue.cpp so this header stays free of
    // the ArkVM/NAPI function headers. See the container-mapping contract notes
    // on the ARRAY/OBJECT cases there.
    void ToNapiValue(const napi_env &env, napi_value *result, napi_status &status) const;
    void ToJsVmValue(JSVM_Env env, JSVM_Value *result, JSVM_Status &status) const;

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
    static KRJSONValue Build(const std::u16string &value) {
        return kuikly::util::json::NewStringUtf16(reinterpret_cast<const uint16_t *>(value.data()), value.size());
    }
    static KRJSONValue Build(const char16_t *value) {
        return value == nullptr
                   ? Build()
                   : kuikly::util::json::NewStringUtf16(
                         reinterpret_cast<const uint16_t *>(value), std::char_traits<char16_t>::length(value));
    }
    static KRJSONValue BuildUtf16FromUtf8(const char *s, size_t n) {
        const std::u16string u16 = kuikly::util::json::Utf8ToUtf16(s, n);
        return kuikly::util::json::NewStringUtf16(reinterpret_cast<const uint16_t *>(u16.data()), u16.size());
    }
    static KRJSONValue Build(const ByteArray &value) {
        if (!value || value->empty()) {
            return kuikly::util::json::NewBytes(nullptr, 0);
        }
        return kuikly::util::json::NewBytes(value->data(), value->size());
    }
    static KRJSONValue Build(const Map &value) {
        KRJSONValue object = kuikly::util::json::NewObjectUtf16();
        for (const auto &entry : value) {
            const KRJSONValue child = entry.second ? entry.second->value_ : kuikly::util::json::NewNull();
            kuikly::util::json::ObjectPutUtf16(object, reinterpret_cast<const uint16_t *>(entry.first.data()),
                                               entry.first.size(), child);
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
    // ArkTS/JSVM value -> KRJSONValue. Bodies (and the engine headers they need)
    // are in KRRenderValue.cpp.
    static KRJSONValue Build(const napi_env &env, const napi_value &value);
    static KRJSONValue Build(const JSVM_Env &env, const JSVM_Value &value);

    static KRRenderValue ChildOrEmpty(KRJSONValue child) {
        return child == KRJSON_INVALID ? KRRenderValue() : MakeBorrowed(child);
    }

    static KRRenderValue MakeParsed(const char *data, size_t length) {
        std::string error;
        KRJSONValue parsed = kuikly::util::json::Reader::Parse(data, length, &error);
        return parsed == KRJSON_INVALID ? MakeNull() : MakeOwned(parsed);
    }

    static KRRenderValue MakeParsed(const std::string &json) {
        return MakeParsed(json.data(), json.size());
    }

    static KRRenderValue MakeParsedUtf16(const uint16_t *data, size_t units) {
        std::string error;
        KRJSONValue parsed = kuikly::util::json::Reader::ParseUtf16(data, units, &error);
        return parsed == KRJSON_INVALID ? MakeNull() : MakeOwned(parsed);
    }

    KRRenderValue parsedFromJsonText() const {
        if (type() == KRJSON_U16STRING) {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            return MakeParsedUtf16(utf16, units);
        }
        size_t size = 0;
        const char *data = kuikly::util::json::GetString(value_, &size);
        return MakeParsed(data, size);
    }

    // Caps recursion when converting an ArkTS/JSVM value tree. Mirrors
    // DomBuilder::kMaxDepth so a converted tree stays safe for the recursive
    // Dump/WriteTo walk and the recursive Box destructors.
    static constexpr int kMaxBridgeDepth = 256;

    // Defined in KRRenderValue.cpp alongside the engine headers.
    static KRJSONValue FromNapi(napi_env env, napi_value value, int depth = 0);
    static KRJSONValue FromJsVm(JSVM_Env env, JSVM_Value value, int depth = 0);

    std::string stringValue() const {
        if (type() == KRJSON_U16STRING) {
            size_t units = 0;
            const uint16_t *utf16 = kuikly::util::json::GetStringUtf16(value_, &units);
            return utf16 == nullptr ? std::string()
                                    : kuikly::util::json::Utf16ToUtf8(utf16, units);
        }
        size_t size = 0;
        const char *data = kuikly::util::json::GetString(value_, &size);
        return std::string(data, size);
    }

    napi_status ToNapiBytes(napi_env env, napi_value *result) const;
    napi_status ToNapiArray(napi_env env, napi_value *result) const;
    JSVM_Status ToJsVmBytes(JSVM_Env env, JSVM_Value *result) const;

    KRJSONType type() const {
        return raw_napi_ ? KRJSON_NULL : kuikly::util::json::GetType(value_);
    }

    KRJSONValue value_ = KRJSON_INVALID;
    // TODO: not on the hot path — see class comment. Snapshot callbacks only.
    std::shared_ptr<NapiValue> raw_napi_;
};

// Reuse a U16 string box across ArkUI UTF-8 get/set. SetFromBox keeps the Kotlin
// box and does the one U16→U8 needed for ArkUI; BoxForUtf8 returns that box when
// the UTF-8 content still matches (SetContentText → onTextDidChange).
struct KRUtf16TextCache {
    KRRenderValue value;
    std::string utf8;

    void SetFromBox(const KRRenderValue &box) {
        value = box;
        utf8 = box ? box.toString() : std::string();
    }

    KRRenderValue BoxForUtf8(const std::string &now) {
        if (value && now == utf8) {
            return value;
        }
        utf8 = now;
        value = KRRenderValue::Make(kuikly::util::json::Utf8ToUtf16(utf8.data(), utf8.size()));
        return value;
    }
};

template<>
inline KRRenderValue KRRenderValue::Make<const char *>(const char *&&value) {
    return value == nullptr || value[0] == '\0' ? MakeEmptyString() : MakeOwned(Build(value));
}

#endif  // CORE_RENDER_OHOS_KRRENDERVALUE_H
