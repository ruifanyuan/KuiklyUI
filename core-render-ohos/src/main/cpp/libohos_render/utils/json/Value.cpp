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

#include "libohos_render/utils/json/Value.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "libohos_render/utils/json/EncodingStats.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace kuikly {
namespace util {
namespace json {

namespace {
constexpr int64_t kInt56Min = -(int64_t{1} << 55);
constexpr int64_t kInt56Max = (int64_t{1} << 55) - 1;

double BitsToDouble(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}
uint64_t DoubleToBits(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
}

void AppendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}
}  // namespace

std::string Utf16ToUtf8(const uint16_t *s, size_t n) {
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n;) {
        uint32_t w = s[i++];
        if (w >= 0xD800u && w <= 0xDBFFu) {
            if (i < n && s[i] >= 0xDC00u && s[i] <= 0xDFFFu) {
                uint32_t lo = s[i++];
                AppendUtf8(out, 0x10000u + ((w - 0xD800u) << 10) + (lo - 0xDC00u));
            } else {
                AppendUtf8(out, 0xFFFDu);
            }
        } else if (w >= 0xDC00u && w <= 0xDFFFu) {
            AppendUtf8(out, 0xFFFDu);
        } else {
            AppendUtf8(out, w);
        }
    }
    return out;
}

std::u16string Utf8ToUtf16(const char *s, size_t n) {
    std::u16string out;
    if (s == nullptr || n == 0) {
        return out;
    }
    out.reserve(n);
    const auto *p = reinterpret_cast<const unsigned char *>(s);
    size_t i = 0;
    while (i < n) {
        uint32_t cp = p[i++];
        if (cp < 0x80u) {
            // ASCII
        } else if ((cp & 0xE0u) == 0xC0u && i < n) {
            cp = ((cp & 0x1Fu) << 6) | (p[i++] & 0x3Fu);
        } else if ((cp & 0xF0u) == 0xE0u && i + 1 < n) {
            cp = ((cp & 0x0Fu) << 12) | ((p[i] & 0x3Fu) << 6) | (p[i + 1] & 0x3Fu);
            i += 2;
        } else if ((cp & 0xF8u) == 0xF0u && i + 2 < n) {
            cp = ((cp & 0x07u) << 18) | ((p[i] & 0x3Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) |
                 (p[i + 2] & 0x3Fu);
            i += 3;
        } else {
            cp = 0xFFFDu;
        }
        if (cp >= 0x10000u) {
            cp -= 0x10000u;
            out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

// ---- StringBox / U16StringBox: single tail allocation ----
StringBox *StringBox::Create(const char *s, size_t n) {
    // Guard against uint32 len truncation and size_t overflow in the allocation
    // (KRJSONNewString is public C ABI and may be called with an arbitrary n).
    // A >4GiB JSON string is degenerate; clamp to empty rather than corrupt.
    if (n > UINT32_MAX || n > SIZE_MAX - sizeof(StringBox) - 1) {
        n = 0;
        s = nullptr;
    }
    void *mem = ::operator new(sizeof(StringBox) + n + 1);
    auto *box = new (mem) StringBox();  // rc = 1
    box->len = static_cast<uint32_t>(n);
    char *dst = reinterpret_cast<char *>(box + 1);
    if (n > 0 && s != nullptr) {
        std::memcpy(dst, s, n);
    }
    dst[n] = '\0';
    return box;
}
void StringBox::Free(StringBox *b) {
    b->~StringBox();
    ::operator delete(static_cast<void *>(b));
}

U16StringBox *U16StringBox::Create(const uint16_t *s, size_t n) {
    if (n > UINT32_MAX || n > (SIZE_MAX - sizeof(U16StringBox)) / sizeof(uint16_t) - 1) {
        n = 0;
        s = nullptr;
    }
    void *mem = ::operator new(sizeof(U16StringBox) + (n + 1) * sizeof(uint16_t));
    auto *box = new (mem) U16StringBox();
    box->len = static_cast<uint32_t>(n);
    uint16_t *dst = box->data();
    if (n > 0 && s != nullptr) {
        std::memcpy(dst, s, n * sizeof(uint16_t));
    }
    dst[n] = 0;
    return box;
}
void U16StringBox::Free(U16StringBox *b) {
    b->~U16StringBox();
    ::operator delete(static_cast<void *>(b));
}

// ---- container destructors: release children (POD words don't auto-release) ----
ArrayBox::~ArrayBox() {
    for (KRJSONValue item : items) {
        Release(item);
    }
}
ObjectBox::~ObjectBox() {
    if (keys_utf16) {
        for (auto &kv : utf16) {
            Release(kv.second);
        }
        utf16.~Utf16Members();
    } else {
        for (auto &kv : utf8) {
            Release(kv.second);
        }
        utf8.~Utf8Members();
    }
}

// ---- lifetime ----
KRJSONValue Retain(KRJSONValue v) {
    if (auto *b = AsBox(v)) {
        b->rc.fetch_add(1, std::memory_order_relaxed);
    }
    return v;
}
void Release(KRJSONValue v) {
    auto *b = AsBox(v);
    if (b == nullptr) {
        return;
    }
    if (b->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        switch (TagOf(v)) {
            case kTagDouble:
            case kTagInt64:
            case kTagUint64:
            case kTagFloat:
            case kTagLong:
                delete static_cast<NumberBox *>(b);
                break;
            case kTagString:
                StringBox::Free(static_cast<StringBox *>(b));
                break;
            case kTagU16String:
                U16StringBox::Free(static_cast<U16StringBox *>(b));
                break;
            case kTagArray:
                delete static_cast<ArrayBox *>(b);
                break;
            case kTagObject:
                delete static_cast<ObjectBox *>(b);
                break;
            case kTagBytes:
                delete static_cast<BytesBox *>(b);
                break;
            default:
                break;
        }
    }
}

// ---- constructors ----
KRJSONValue NewNull() {
    return static_cast<KRJSONValue>(kTagNull);
}
KRJSONValue NewBool(bool b) {
    return (static_cast<uint64_t>(b ? 1 : 0) << 8) | kTagBool;
}
KRJSONValue NewInt32(int32_t x) {
    return (static_cast<uint64_t>(static_cast<int64_t>(x)) << 8) | kTagInt32;
}
KRJSONValue NewInt(int64_t x) {
    if (x >= kInt56Min && x <= kInt56Max) {
        return EncodeInt56(x);
    }
    auto *box = new NumberBox();
    box->bits = static_cast<uint64_t>(x);
    return EncodePtr(box, kTagInt64);
}
KRJSONValue NewLong(int64_t x) {
    auto *box = new NumberBox();
    box->bits = static_cast<uint64_t>(x);
    return EncodePtr(box, kTagLong);
}
KRJSONValue NewUint(uint64_t x) {
    if (x <= static_cast<uint64_t>(kInt56Max)) {
        return EncodeInt56(static_cast<int64_t>(x));
    }
    auto *box = new NumberBox();
    box->bits = x;
    return EncodePtr(box, x <= static_cast<uint64_t>(INT64_MAX) ? kTagInt64 : kTagUint64);
}
KRJSONValue NewDouble(double d) {
    auto *box = new NumberBox();
    box->bits = DoubleToBits(d);
    return EncodePtr(box, kTagDouble);
}
KRJSONValue NewFloat(float f) {
    auto *box = new NumberBox();
    double d = static_cast<double>(f);
    box->bits = DoubleToBits(d);
    return EncodePtr(box, kTagFloat);
}
KRJSONValue NewString(const char *s, size_t n) {
    EncodingStatsNoteNewUtf8(n);
    return EncodePtr(StringBox::Create(s, n), kTagString);
}
KRJSONValue NewStringUtf16(const uint16_t *s, size_t n) {
    EncodingStatsNoteNewUtf16(n);
    return EncodePtr(U16StringBox::Create(s, n), kTagU16String);
}
KRJSONValue NewBytes(const uint8_t *data, size_t n) {
    auto *box = new BytesBox();
    if (data != nullptr && n > 0) {
        box->data.assign(data, data + n);
    }
    return EncodePtr(box, kTagBytes);
}
KRJSONValue NewArray() {
    return EncodePtr(new ArrayBox(), kTagArray);
}
KRJSONValue NewObject() {
    return EncodePtr(new ObjectBox(), kTagObject);
}
KRJSONValue NewObjectUtf16() {
    return EncodePtr(new ObjectBox(ObjectBox::Utf16Keys{}), kTagObject);
}

void ArrayAppend(KRJSONValue array, KRJSONValue child) {
    if (TagOf(array) != kTagArray) {
        return;
    }
    static_cast<ArrayBox *>(AsBox(array))->items.push_back(Retain(child));
}
void ArraySet(KRJSONValue array, size_t index, KRJSONValue child) {
    if (TagOf(array) != kTagArray) {
        return;
    }
    auto &items = static_cast<ArrayBox *>(AsBox(array))->items;
    if (index >= items.size()) {
        return;
    }
    Release(items[index]);
    items[index] = Retain(child);
}
void ObjectPut(KRJSONValue object, const char *key, size_t key_len, KRJSONValue child) {
    if (TagOf(object) != kTagObject || key == nullptr) {
        return;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (box->keys_utf16) {
        assert(false && "ObjectPut: UTF-8 key on UTF-16-key object");
        return;
    }
    auto &members = box->utf8;
    for (auto &kv : members) {
        if (kv.first.size() == key_len && std::memcmp(kv.first.data(), key, key_len) == 0) {
            Release(kv.second);
            kv.second = Retain(child);
            return;
        }
    }
    members.emplace_back(std::string(key, key_len), Retain(child));
}

void ObjectPutUtf16(KRJSONValue object, const uint16_t *key, size_t units, KRJSONValue child) {
    if (TagOf(object) != kTagObject || (key == nullptr && units != 0)) {
        return;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (!box->keys_utf16) {
        assert(false && "ObjectPutUtf16: UTF-16 key on UTF-8-key object");
        return;
    }
    const size_t nbytes = units * sizeof(uint16_t);
    auto &members = box->utf16;
    for (auto &kv : members) {
        if (kv.first.size() == units &&
            (units == 0 || std::memcmp(kv.first.data(), key, nbytes) == 0)) {
            Release(kv.second);
            kv.second = Retain(child);
            return;
        }
    }
    members.emplace_back(
        units == 0 ? std::u16string()
                   : std::u16string(reinterpret_cast<const char16_t *>(key), units),
        Retain(child));
}

// ---- accessors ----
KRJSONType GetType(KRJSONValue v) {
    switch (TagOf(v)) {
        case kTagBool:
            return KRJSON_BOOL;
        case kTagInt:
        case kTagInt64:
            return KRJSON_INT;
        case kTagInt32:
            return KRJSON_INT;
        case kTagLong:
            return KRJSON_LONG;
        case kTagUint64:
            return KRJSON_UINT;
        case kTagDouble:
            return KRJSON_DOUBLE;
        case kTagFloat:
            return KRJSON_FLOAT;
        case kTagString:
            return KRJSON_STRING;
        case kTagU16String:
            return KRJSON_U16STRING;
        case kTagBytes:
            return KRJSON_BYTES;
        case kTagArray:
            return KRJSON_ARRAY;
        case kTagObject:
            return KRJSON_OBJECT;
        case kTagNull:
        default:
            return KRJSON_NULL;
    }
}
bool GetBool(KRJSONValue v, bool default_value) {
    return TagOf(v) == kTagBool ? ((v >> 8) & 1u) != 0 : default_value;
}
int64_t GetInt(KRJSONValue v, int64_t default_value) {
    switch (TagOf(v)) {
        case kTagInt:
            return DecodeInt56(v);
        case kTagInt32:
            return static_cast<int32_t>(DecodeInt56(v));
        case kTagInt64:
        case kTagUint64:
        case kTagLong:
            return static_cast<int64_t>(static_cast<NumberBox *>(AsBox(v))->bits);
        case kTagDouble:
        case kTagFloat:
            return static_cast<int64_t>(BitsToDouble(static_cast<NumberBox *>(AsBox(v))->bits));
        default:
            return default_value;
    }
}
uint64_t GetUint(KRJSONValue v, uint64_t default_value) {
    switch (TagOf(v)) {
        case kTagInt:
            return static_cast<uint64_t>(DecodeInt56(v));
        case kTagInt32:
            return static_cast<uint64_t>(static_cast<int32_t>(DecodeInt56(v)));
        case kTagInt64:
        case kTagUint64:
        case kTagLong:
            return static_cast<NumberBox *>(AsBox(v))->bits;
        case kTagDouble:
        case kTagFloat:
            return static_cast<uint64_t>(BitsToDouble(static_cast<NumberBox *>(AsBox(v))->bits));
        default:
            return default_value;
    }
}
double GetDouble(KRJSONValue v, double default_value) {
    switch (TagOf(v)) {
        case kTagDouble:
        case kTagFloat:
            return BitsToDouble(static_cast<NumberBox *>(AsBox(v))->bits);
        case kTagInt:
            return static_cast<double>(DecodeInt56(v));
        case kTagInt32:
            return static_cast<double>(static_cast<int32_t>(DecodeInt56(v)));
        case kTagInt64:
        case kTagLong:
            return static_cast<double>(static_cast<int64_t>(static_cast<NumberBox *>(AsBox(v))->bits));
        case kTagUint64:
            return static_cast<double>(static_cast<NumberBox *>(AsBox(v))->bits);
        default:
            return default_value;
    }
}
const char *GetString(KRJSONValue v, size_t *out_len) {
    if (TagOf(v) == kTagString) {
        EncodingStatsNoteGetUtf8();
        auto *box = static_cast<StringBox *>(AsBox(v));
        if (out_len != nullptr) {
            *out_len = box->len;
        }
        return box->data();
    }
    if (out_len != nullptr) {
        *out_len = 0;
    }
    return "";
}
const uint16_t *GetStringUtf16(KRJSONValue v, size_t *out_units) {
    if (TagOf(v) == kTagU16String) {
        EncodingStatsNoteGetUtf16();
        auto *box = static_cast<U16StringBox *>(AsBox(v));
        if (out_units != nullptr) {
            *out_units = box->len;
        }
        return box->data();
    }
    if (out_units != nullptr) {
        *out_units = 0;
    }
    return nullptr;
}
const uint8_t *GetBytes(KRJSONValue v, size_t *out_len) {
    if (TagOf(v) == kTagBytes) {
        const auto &data = static_cast<BytesBox *>(AsBox(v))->data;
        if (out_len != nullptr) {
            *out_len = data.size();
        }
        return data.empty() ? nullptr : data.data();
    }
    if (out_len != nullptr) {
        *out_len = 0;
    }
    return nullptr;
}
size_t GetSize(KRJSONValue v) {
    switch (TagOf(v)) {
        case kTagArray:
            return static_cast<ArrayBox *>(AsBox(v))->items.size();
        case kTagObject: {
            auto *box = static_cast<ObjectBox *>(AsBox(v));
            return box->keys_utf16 ? box->utf16.size() : box->utf8.size();
        }
        case kTagBytes:
            return static_cast<BytesBox *>(AsBox(v))->data.size();
        default:
            return 0;
    }
}
KRJSONValue ArrayGet(KRJSONValue array, size_t index) {
    if (TagOf(array) == kTagArray) {
        auto &items = static_cast<ArrayBox *>(AsBox(array))->items;
        if (index < items.size()) {
            return items[index];  // borrowed
        }
    }
    return KRJSON_INVALID;
}
KRJSONValue ObjectGet(KRJSONValue object, const char *key, size_t key_len) {
    if (TagOf(object) != kTagObject || key == nullptr) {
        return KRJSON_INVALID;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (box->keys_utf16) {
        assert(false && "ObjectGet: UTF-8 key on UTF-16-key object");
        return KRJSON_INVALID;
    }
    for (auto &kv : box->utf8) {
        if (kv.first.size() == key_len && std::memcmp(kv.first.data(), key, key_len) == 0) {
            return kv.second;
        }
    }
    return KRJSON_INVALID;
}
KRJSONValue ObjectGetUtf16(KRJSONValue object, const uint16_t *key, size_t units) {
    if (TagOf(object) != kTagObject || (key == nullptr && units != 0)) {
        return KRJSON_INVALID;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (!box->keys_utf16) {
        assert(false && "ObjectGetUtf16: UTF-16 key on UTF-8-key object");
        return KRJSON_INVALID;
    }
    const size_t nbytes = units * sizeof(uint16_t);
    for (auto &kv : box->utf16) {
        if (kv.first.size() == units &&
            (units == 0 || std::memcmp(kv.first.data(), key, nbytes) == 0)) {
            return kv.second;
        }
    }
    return KRJSON_INVALID;
}
bool ObjectKeysAreUtf16(KRJSONValue object) {
    return TagOf(object) == kTagObject && static_cast<ObjectBox *>(AsBox(object))->keys_utf16;
}
KRJSONValue ObjectValueAt(KRJSONValue object, size_t index) {
    if (TagOf(object) != kTagObject) {
        return KRJSON_INVALID;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (box->keys_utf16) {
        if (index < box->utf16.size()) {
            return box->utf16[index].second;
        }
        return KRJSON_INVALID;
    }
    if (index < box->utf8.size()) {
        return box->utf8[index].second;
    }
    return KRJSON_INVALID;
}
const char *ObjectKeyAt(KRJSONValue object, size_t index) {
    if (TagOf(object) != kTagObject) {
        return nullptr;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (box->keys_utf16) {
        assert(false && "ObjectKeyAt: UTF-8 key on UTF-16-key object");
        return nullptr;
    }
    if (index < box->utf8.size()) {
        return box->utf8[index].first.c_str();
    }
    return nullptr;
}
const uint16_t *ObjectKeyAtUtf16(KRJSONValue object, size_t index, size_t *out_units) {
    if (TagOf(object) != kTagObject) {
        if (out_units != nullptr) {
            *out_units = 0;
        }
        return nullptr;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (!box->keys_utf16) {
        assert(false && "ObjectKeyAtUtf16: UTF-16 key on UTF-8-key object");
        if (out_units != nullptr) {
            *out_units = 0;
        }
        return nullptr;
    }
    if (index >= box->utf16.size()) {
        if (out_units != nullptr) {
            *out_units = 0;
        }
        return nullptr;
    }
    auto &key = box->utf16[index].first;
    if (out_units != nullptr) {
        *out_units = key.size();
    }
    return reinterpret_cast<const uint16_t *>(key.c_str());
}
void ObjectForEach(KRJSONValue object, KRJSONObjectVisitor visitor, void *userdata) {
    if (TagOf(object) != kTagObject || visitor == nullptr) {
        return;
    }
    auto *box = static_cast<ObjectBox *>(AsBox(object));
    if (box->keys_utf16) {
        for (auto &kv : box->utf16) {
            const std::string utf8 = Utf16ToUtf8(
                reinterpret_cast<const uint16_t *>(kv.first.data()), kv.first.size());
            if (!visitor(utf8.data(), utf8.size(), kv.second, userdata)) {
                break;
            }
        }
        return;
    }
    for (auto &kv : box->utf8) {
        if (!visitor(kv.first.data(), kv.first.size(), kv.second, userdata)) {
            break;
        }
    }
}

// ---- serialize ----
namespace {
void WriteTo(KRJSONValue v, rapidjson::Writer<rapidjson::StringBuffer> &w) {
    switch (GetType(v)) {
        case KRJSON_NULL:
            w.Null();
            break;
        case KRJSON_BOOL:
            w.Bool(GetBool(v, false));
            break;
        case KRJSON_INT:
            w.Int64(GetInt(v, 0));
            break;
        case KRJSON_LONG:
            w.Int64(GetInt(v, 0));
            break;
        case KRJSON_UINT:
            w.Uint64(GetUint(v, 0));
            break;
        case KRJSON_DOUBLE:
            w.Double(GetDouble(v, 0));
            break;
        case KRJSON_FLOAT:
            w.Double(GetDouble(v, 0));
            break;
        case KRJSON_STRING: {
            size_t len = 0;
            const char *s = GetString(v, &len);
            w.String(s, static_cast<rapidjson::SizeType>(len));
            break;
        }
        case KRJSON_U16STRING: {
            size_t units = 0;
            const uint16_t *s = GetStringUtf16(v, &units);
            const std::string utf8 = Utf16ToUtf8(s, units);
            w.String(utf8.data(), static_cast<rapidjson::SizeType>(utf8.size()));
            break;
        }
        case KRJSON_BYTES:
            // Binary values only exist on the bridge path and have no JSON
            // text representation. Match the historical fallback to null.
            w.Null();
            break;
        case KRJSON_ARRAY: {
            w.StartArray();
            auto &items = static_cast<ArrayBox *>(AsBox(v))->items;
            for (KRJSONValue item : items) {
                WriteTo(item, w);
            }
            w.EndArray();
            break;
        }
        case KRJSON_OBJECT: {
            w.StartObject();
            auto *box = static_cast<ObjectBox *>(AsBox(v));
            if (box->keys_utf16) {
                for (auto &kv : box->utf16) {
                    const std::string utf8 = Utf16ToUtf8(
                        reinterpret_cast<const uint16_t *>(kv.first.data()), kv.first.size());
                    w.Key(utf8.data(), static_cast<rapidjson::SizeType>(utf8.size()));
                    WriteTo(kv.second, w);
                }
            } else {
                for (auto &kv : box->utf8) {
                    w.Key(kv.first.data(), static_cast<rapidjson::SizeType>(kv.first.size()));
                    WriteTo(kv.second, w);
                }
            }
            w.EndObject();
            break;
        }
    }
}
}  // namespace

std::string Dump(KRJSONValue v) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    WriteTo(v, writer);
    return std::string(buffer.GetString(), buffer.GetSize());
}

}  // namespace json
}  // namespace util
}  // namespace kuikly
