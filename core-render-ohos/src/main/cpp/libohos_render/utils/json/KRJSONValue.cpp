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

#include "libohos_render/utils/json/KRJSONValue.h"

#include <cstdlib>
#include <cstring>
#include <new>

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
}  // namespace

// ---- KRStringBox: single tail allocation ----
KRStringBox *KRStringBox::Create(const char *s, size_t n) {
    // Guard against uint32 len truncation and size_t overflow in the allocation
    // (KRJSONNewString is public C ABI and may be called with an arbitrary n).
    // A >4GiB JSON string is degenerate; clamp to empty rather than corrupt.
    if (n > UINT32_MAX || n > SIZE_MAX - sizeof(KRStringBox) - 1) {
        n = 0;
        s = nullptr;
    }
    void *mem = ::operator new(sizeof(KRStringBox) + n + 1);
    auto *box = new (mem) KRStringBox();  // rc = 1
    box->len = static_cast<uint32_t>(n);
    char *dst = reinterpret_cast<char *>(box + 1);
    if (n > 0 && s != nullptr) {
        std::memcpy(dst, s, n);
    }
    dst[n] = '\0';
    return box;
}
void KRStringBox::Free(KRStringBox *b) {
    b->~KRStringBox();  // trivial members; explicit for correctness
    ::operator delete(static_cast<void *>(b));
}

// ---- container destructors: release children (POD words don't auto-release) ----
KRArrayBox::~KRArrayBox() {
    for (KRJSONValue item : items) {
        Release(item);
    }
}
KRObjectBox::~KRObjectBox() {
    for (auto &kv : members) {
        Release(kv.second);
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
                delete static_cast<KRNumberBox *>(b);
                break;
            case kTagString:
                KRStringBox::Free(static_cast<KRStringBox *>(b));
                break;
            case kTagArray:
                delete static_cast<KRArrayBox *>(b);
                break;
            case kTagObject:
                delete static_cast<KRObjectBox *>(b);
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
KRJSONValue NewInt(int64_t x) {
    if (x >= kInt56Min && x <= kInt56Max) {
        return EncodeInt56(x);
    }
    auto *box = new KRNumberBox();
    box->bits = static_cast<uint64_t>(x);
    return EncodePtr(box, kTagInt64);
}
KRJSONValue NewUint(uint64_t x) {
    if (x <= static_cast<uint64_t>(kInt56Max)) {
        return EncodeInt56(static_cast<int64_t>(x));
    }
    auto *box = new KRNumberBox();
    box->bits = x;
    return EncodePtr(box, x <= static_cast<uint64_t>(INT64_MAX) ? kTagInt64 : kTagUint64);
}
KRJSONValue NewDouble(double d) {
    auto *box = new KRNumberBox();
    box->bits = DoubleToBits(d);
    return EncodePtr(box, kTagDouble);
}
KRJSONValue NewString(const char *s, size_t n) {
    return EncodePtr(KRStringBox::Create(s, n), kTagString);
}
KRJSONValue NewArray() {
    return EncodePtr(new KRArrayBox(), kTagArray);
}
KRJSONValue NewObject() {
    return EncodePtr(new KRObjectBox(), kTagObject);
}

void ArrayAppend(KRJSONValue array, KRJSONValue child) {
    if (TagOf(array) != kTagArray) {
        return;
    }
    static_cast<KRArrayBox *>(AsBox(array))->items.push_back(Retain(child));
}
void ObjectPut(KRJSONValue object, const char *key, size_t key_len, KRJSONValue child) {
    if (TagOf(object) != kTagObject) {
        return;
    }
    auto &members = static_cast<KRObjectBox *>(AsBox(object))->members;
    for (auto &kv : members) {
        if (kv.first.size() == key_len && std::memcmp(kv.first.data(), key, key_len) == 0) {
            Release(kv.second);
            kv.second = Retain(child);
            return;
        }
    }
    members.emplace_back(std::string(key, key_len), Retain(child));
}

// ---- accessors ----
KRJSONType GetType(KRJSONValue v) {
    switch (TagOf(v)) {
        case kTagBool:
            return KRJSON_BOOL;
        case kTagInt:
        case kTagInt64:
            return KRJSON_INT;
        case kTagUint64:
            return KRJSON_UINT;
        case kTagDouble:
            return KRJSON_DOUBLE;
        case kTagString:
            return KRJSON_STRING;
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
        case kTagInt64:
        case kTagUint64:
            return static_cast<int64_t>(static_cast<KRNumberBox *>(AsBox(v))->bits);
        case kTagDouble:
            return static_cast<int64_t>(BitsToDouble(static_cast<KRNumberBox *>(AsBox(v))->bits));
        default:
            return default_value;
    }
}
uint64_t GetUint(KRJSONValue v, uint64_t default_value) {
    switch (TagOf(v)) {
        case kTagInt:
            return static_cast<uint64_t>(DecodeInt56(v));
        case kTagInt64:
        case kTagUint64:
            return static_cast<KRNumberBox *>(AsBox(v))->bits;
        case kTagDouble:
            return static_cast<uint64_t>(BitsToDouble(static_cast<KRNumberBox *>(AsBox(v))->bits));
        default:
            return default_value;
    }
}
double GetDouble(KRJSONValue v, double default_value) {
    switch (TagOf(v)) {
        case kTagDouble:
            return BitsToDouble(static_cast<KRNumberBox *>(AsBox(v))->bits);
        case kTagInt:
            return static_cast<double>(DecodeInt56(v));
        case kTagInt64:
            return static_cast<double>(static_cast<int64_t>(static_cast<KRNumberBox *>(AsBox(v))->bits));
        case kTagUint64:
            return static_cast<double>(static_cast<KRNumberBox *>(AsBox(v))->bits);
        default:
            return default_value;
    }
}
const char *GetString(KRJSONValue v, size_t *out_len) {
    if (TagOf(v) == kTagString) {
        auto *box = static_cast<KRStringBox *>(AsBox(v));
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
size_t GetSize(KRJSONValue v) {
    switch (TagOf(v)) {
        case kTagArray:
            return static_cast<KRArrayBox *>(AsBox(v))->items.size();
        case kTagObject:
            return static_cast<KRObjectBox *>(AsBox(v))->members.size();
        default:
            return 0;
    }
}
KRJSONValue ArrayGet(KRJSONValue array, size_t index) {
    if (TagOf(array) == kTagArray) {
        auto &items = static_cast<KRArrayBox *>(AsBox(array))->items;
        if (index < items.size()) {
            return items[index];  // borrowed
        }
    }
    return KRJSON_INVALID;
}
KRJSONValue ObjectGet(KRJSONValue object, const char *key, size_t key_len) {
    if (TagOf(object) == kTagObject) {
        auto &members = static_cast<KRObjectBox *>(AsBox(object))->members;
        for (auto &kv : members) {
            if (kv.first.size() == key_len && std::memcmp(kv.first.data(), key, key_len) == 0) {
                return kv.second;  // borrowed
            }
        }
    }
    return KRJSON_INVALID;
}
KRJSONValue ObjectValueAt(KRJSONValue object, size_t index) {
    if (TagOf(object) == kTagObject) {
        auto &members = static_cast<KRObjectBox *>(AsBox(object))->members;
        if (index < members.size()) {
            return members[index].second;  // borrowed
        }
    }
    return KRJSON_INVALID;
}
const char *ObjectKeyAt(KRJSONValue object, size_t index) {
    if (TagOf(object) == kTagObject) {
        auto &members = static_cast<KRObjectBox *>(AsBox(object))->members;
        if (index < members.size()) {
            return members[index].first.c_str();
        }
    }
    return nullptr;
}
void ObjectForEach(KRJSONValue object, KRJSONObjectVisitor visitor, void *userdata) {
    if (TagOf(object) != kTagObject || visitor == nullptr) {
        return;
    }
    for (auto &kv : static_cast<KRObjectBox *>(AsBox(object))->members) {
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
        case KRJSON_UINT:
            w.Uint64(GetUint(v, 0));
            break;
        case KRJSON_DOUBLE:
            w.Double(GetDouble(v, 0));
            break;
        case KRJSON_STRING: {
            size_t len = 0;
            const char *s = GetString(v, &len);
            w.String(s, static_cast<rapidjson::SizeType>(len));
            break;
        }
        case KRJSON_ARRAY: {
            w.StartArray();
            auto &items = static_cast<KRArrayBox *>(AsBox(v))->items;
            for (KRJSONValue item : items) {
                WriteTo(item, w);
            }
            w.EndArray();
            break;
        }
        case KRJSON_OBJECT: {
            w.StartObject();
            for (auto &kv : static_cast<KRObjectBox *>(AsBox(v))->members) {
                w.Key(kv.first.data(), static_cast<rapidjson::SizeType>(kv.first.size()));
                WriteTo(kv.second, w);
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
