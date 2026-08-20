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

#include "libohos_render/utils/json/KRJSONReader.h"

#include "libohos_render/utils/json/KRJSONDomBuilder.h"
#include "rapidjson/error/en.h"
#include "rapidjson/memorystream.h"
#include "rapidjson/reader.h"

namespace kuikly {
namespace util {
namespace json {

namespace {
/**
 * Bridges RapidJSON's compile-time (static) handler concept onto our
 * runtime-polymorphic `KRJSONSaxHandler`. 32-bit signed/unsigned variants widen
 * to the 64-bit callbacks; `RawNumber` (unused here) maps to a string.
 */
struct SaxAdapter {
    explicit SaxAdapter(KRJSONSaxHandler &handler) : handler_(handler) {}

    bool Null() { return handler_.OnNull(); }
    bool Bool(bool b) { return handler_.OnBool(b); }
    bool Int(int i) { return handler_.OnInt(i); }
    bool Uint(unsigned u) { return handler_.OnUint(u); }
    bool Int64(int64_t i) { return handler_.OnInt(i); }
    bool Uint64(uint64_t u) { return handler_.OnUint(u); }
    bool Double(double d) { return handler_.OnDouble(d); }
    bool RawNumber(const char *str, rapidjson::SizeType len, bool copy) { return handler_.OnString(str, len, copy); }
    bool String(const char *str, rapidjson::SizeType len, bool copy) { return handler_.OnString(str, len, copy); }
    bool StartObject() { return handler_.OnStartObject(); }
    bool Key(const char *str, rapidjson::SizeType len, bool copy) { return handler_.OnKey(str, len, copy); }
    bool EndObject(rapidjson::SizeType member_count) { return handler_.OnEndObject(member_count); }
    bool StartArray() { return handler_.OnStartArray(); }
    bool EndArray(rapidjson::SizeType element_count) { return handler_.OnEndArray(element_count); }

 private:
    KRJSONSaxHandler &handler_;
};
}  // namespace

bool KRJSONReader::ParseSax(const char *data, size_t length, KRJSONSaxHandler &handler, std::string *error) {
    if (data == nullptr) {
        if (error != nullptr) {
            *error = "null input";
        }
        return false;
    }
    // MemoryStream reads exactly `length` bytes and yields '\0' past the end,
    // so a non-NUL-terminated caller buffer is never over-read.
    rapidjson::Reader reader;
    rapidjson::MemoryStream stream(data, length);
    SaxAdapter adapter(handler);
    rapidjson::ParseResult result = reader.Parse(stream, adapter);
    if (result.IsError()) {
        if (error != nullptr) {
            *error = std::string(rapidjson::GetParseError_En(result.Code())) + " (offset " +
                     std::to_string(result.Offset()) + ")";
        }
        return false;
    }
    return true;
}

KRJSONValue KRJSONReader::Parse(const char *data, size_t length, std::string *error) {
    KRJSONDomBuilder builder;
    if (!ParseSax(data, length, builder, error)) {
        return KRJSON_INVALID;
    }
    return builder.TakeResult();
}

}  // namespace json
}  // namespace util
}  // namespace kuikly
