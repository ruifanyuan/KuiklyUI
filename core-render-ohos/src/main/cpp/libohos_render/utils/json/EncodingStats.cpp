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

#include "libohos_render/utils/json/EncodingStats.h"

#include <atomic>
#include <chrono>
#include <cstdint>

#include "libohos_render/utils/KRRenderLoger.h"

namespace kuikly {
namespace util {
namespace json {

namespace {

std::atomic<uint64_t> g_utf8_new{0};
std::atomic<uint64_t> g_utf16_new{0};
std::atomic<uint64_t> g_utf8_bytes{0};
std::atomic<uint64_t> g_utf16_units{0};
std::atomic<uint64_t> g_utf8_get{0};
std::atomic<uint64_t> g_utf16_get{0};
std::atomic<uint64_t> g_utf8_parse{0};
std::atomic<uint64_t> g_utf16_parse{0};
std::atomic<uint64_t> g_napi_utf8{0};
std::atomic<uint64_t> g_napi_utf16{0};
std::atomic<uint64_t> g_last_flush_events{0};
std::atomic<int64_t> g_last_flush_ms{0};

constexpr uint64_t kFlushEvery = 256;
constexpr int64_t kFlushIntervalMs = 2000;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

uint64_t EventTotal() {
    return g_utf8_new.load(std::memory_order_relaxed) + g_utf16_new.load(std::memory_order_relaxed) +
           g_utf8_get.load(std::memory_order_relaxed) + g_utf16_get.load(std::memory_order_relaxed) +
           g_utf8_parse.load(std::memory_order_relaxed) + g_utf16_parse.load(std::memory_order_relaxed) +
           g_napi_utf8.load(std::memory_order_relaxed) + g_napi_utf16.load(std::memory_order_relaxed);
}

void Emit(const char *reason) {
    const uint64_t utf8_new = g_utf8_new.load(std::memory_order_relaxed);
    const uint64_t utf16_new = g_utf16_new.load(std::memory_order_relaxed);
    const uint64_t new_total = utf8_new + utf16_new;
    const int utf16_pct = new_total == 0 ? 0 : static_cast<int>((utf16_new * 100) / new_total);
    KR_LOG_INFO_WITH_TAG("KRJSON_ENC")
        << "reason=" << (reason != nullptr ? reason : "batch") << " new utf8=" << utf8_new << " utf16=" << utf16_new
        << " (" << utf16_pct << "% u16) bytes=" << g_utf8_bytes.load(std::memory_order_relaxed)
        << " units=" << g_utf16_units.load(std::memory_order_relaxed)
        << " get utf8=" << g_utf8_get.load(std::memory_order_relaxed)
        << " utf16=" << g_utf16_get.load(std::memory_order_relaxed)
        << " parse utf8=" << g_utf8_parse.load(std::memory_order_relaxed)
        << " utf16=" << g_utf16_parse.load(std::memory_order_relaxed)
        << " napi utf8=" << g_napi_utf8.load(std::memory_order_relaxed)
        << " utf16=" << g_napi_utf16.load(std::memory_order_relaxed);
}

void MaybeFlush() {
    const uint64_t total = EventTotal();
    const uint64_t last = g_last_flush_events.load(std::memory_order_relaxed);
    const int64_t now = NowMs();
    const int64_t last_ms = g_last_flush_ms.load(std::memory_order_relaxed);
    if (total - last < kFlushEvery && now - last_ms < kFlushIntervalMs) {
        return;
    }
    uint64_t expected = last;
    if (!g_last_flush_events.compare_exchange_strong(expected, total, std::memory_order_relaxed)) {
        return;
    }
    g_last_flush_ms.store(now, std::memory_order_relaxed);
    Emit("batch");
}

}  // namespace

void EncodingStatsNoteNewUtf8(size_t bytes) {
    g_utf8_new.fetch_add(1, std::memory_order_relaxed);
    g_utf8_bytes.fetch_add(bytes, std::memory_order_relaxed);
    MaybeFlush();
}
void EncodingStatsNoteNewUtf16(size_t units) {
    g_utf16_new.fetch_add(1, std::memory_order_relaxed);
    g_utf16_units.fetch_add(units, std::memory_order_relaxed);
    MaybeFlush();
}
void EncodingStatsNoteGetUtf8() {
    g_utf8_get.fetch_add(1, std::memory_order_relaxed);
    MaybeFlush();
}
void EncodingStatsNoteGetUtf16() {
    g_utf16_get.fetch_add(1, std::memory_order_relaxed);
    MaybeFlush();
}
void EncodingStatsNoteParseUtf8() {
    g_utf8_parse.fetch_add(1, std::memory_order_relaxed);
}
void EncodingStatsNoteParseUtf16() {
    g_utf16_parse.fetch_add(1, std::memory_order_relaxed);
}
void EncodingStatsNoteNapiUtf8() {
    g_napi_utf8.fetch_add(1, std::memory_order_relaxed);
    MaybeFlush();
}
void EncodingStatsNoteNapiUtf16() {
    g_napi_utf16.fetch_add(1, std::memory_order_relaxed);
    MaybeFlush();
}
void EncodingStatsFlush(const char *reason) {
    g_last_flush_events.store(EventTotal(), std::memory_order_relaxed);
    g_last_flush_ms.store(NowMs(), std::memory_order_relaxed);
    Emit(reason);
}

}  // namespace json
}  // namespace util
}  // namespace kuikly
