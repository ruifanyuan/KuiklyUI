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

#ifndef CORE_RENDER_OHOS_KRJSONREADER_H
#define CORE_RENDER_OHOS_KRJSONREADER_H

#include <cstddef>
#include <string>

#include "libohos_render/api/include/Kuikly/KRJson.h"
#include "libohos_render/utils/json/KRJSONSaxHandler.h"

namespace kuikly {
namespace util {
namespace json {

/**
 * JSON parser backed by RapidJSON's SAX reader.
 *
 * `ParseSax` scans the input once and forwards every token to `handler` — no
 * tree is built, so peak memory is bounded by the handler. `Parse` layers a
 * `KRJSONDomBuilder` on top to produce a reference-counted value tree.
 */
class KRJSONReader {
 public:
    /**
     * Streaming parse. Returns true on success; on failure returns false and,
     * if `error` is non-null, fills it with a message (including byte offset).
     * The `data`/`length` buffer is read length-bounded (via MemoryStream);
     * NUL-termination is NOT required.
     */
    static bool ParseSax(const char *data, size_t length, KRJSONSaxHandler &handler, std::string *error = nullptr);

    static bool ParseSax(const std::string &json, KRJSONSaxHandler &handler, std::string *error = nullptr) {
        return ParseSax(json.data(), json.size(), handler, error);
    }

    /** Parse into an OWNED value tree; KRJSON_INVALID on error (see `error`). */
    static KRJSONValue Parse(const char *data, size_t length, std::string *error = nullptr);

    static KRJSONValue Parse(const std::string &json, std::string *error = nullptr) {
        return Parse(json.data(), json.size(), error);
    }
};

}  // namespace json
}  // namespace util
}  // namespace kuikly

#endif  // CORE_RENDER_OHOS_KRJSONREADER_H
