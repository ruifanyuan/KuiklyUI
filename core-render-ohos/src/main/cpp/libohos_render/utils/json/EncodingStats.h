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

#ifndef CORE_RENDER_OHOS_JSON_ENCODINGSTATS_H
#define CORE_RENDER_OHOS_JSON_ENCODINGSTATS_H

#include <cstddef>

namespace kuikly {
namespace util {
namespace json {

#if defined(__OHOS__)
void EncodingStatsNoteNewUtf8(size_t bytes);
void EncodingStatsNoteNewUtf16(size_t units);
void EncodingStatsNoteGetUtf8();
void EncodingStatsNoteGetUtf16();
void EncodingStatsNoteParseUtf8();
void EncodingStatsNoteParseUtf16();
void EncodingStatsNoteNapiUtf8();
void EncodingStatsNoteNapiUtf16();
void EncodingStatsNoteConvertUtf8ToUtf16(size_t bytes);
void EncodingStatsNoteConvertUtf16ToUtf8(size_t units);
void EncodingStatsNoteAsciiWiden(size_t bytes);
void EncodingStatsFlush(const char *reason);
#else
inline void EncodingStatsNoteNewUtf8(size_t) {}
inline void EncodingStatsNoteNewUtf16(size_t) {}
inline void EncodingStatsNoteGetUtf8() {}
inline void EncodingStatsNoteGetUtf16() {}
inline void EncodingStatsNoteParseUtf8() {}
inline void EncodingStatsNoteParseUtf16() {}
inline void EncodingStatsNoteNapiUtf8() {}
inline void EncodingStatsNoteNapiUtf16() {}
inline void EncodingStatsNoteConvertUtf8ToUtf16(size_t) {}
inline void EncodingStatsNoteConvertUtf16ToUtf8(size_t) {}
inline void EncodingStatsNoteAsciiWiden(size_t) {}
inline void EncodingStatsFlush(const char *) {}
#endif

}  // namespace json
}  // namespace util
}  // namespace kuikly

#endif  // CORE_RENDER_OHOS_JSON_ENCODINGSTATS_H
