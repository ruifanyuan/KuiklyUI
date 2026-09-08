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

package com.tencent.kuikly.demo.pages.demo

import com.tencent.kuikly.core.nvi.serialization.json.JSONException
import com.tencent.kuikly.core.nvi.serialization.json.JSONArray
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject
import com.tencent.kuikly.core.nvi.serialization.json.JSONTokener
import com.tencent.kuikly.core.nvi.serialization.json.OhosJsonPerfHooks
import com.tencent.kuikly.core.nvi.serialization.json.verifyOhosLazyJsonBridge
import kotlin.native.runtime.GC
import kotlin.native.runtime.NativeRuntimeApi
import kotlin.system.getTimeNanos

private const val TOKENER_CJSON = "JSONTokener(cJSON)"

actual fun parseWithTokener(json: String): Any? = JSONTokener(json).nextValue()

actual fun measureNativeParsedWrapNanos(json: String): Long =
    OhosJsonPerfHooks.measureWrapAfterNativeParseNanos(json)

actual fun bridgeSupported(): Boolean = true

actual fun bridgeBuildOwner(json: String): Long = OhosJsonPerfHooks.bridgeBuildOwner(json)

actual fun bridgeReleaseOwner(owner: Long) = OhosJsonPerfHooks.bridgeReleaseOwner(owner)

actual fun bridgeBeforeArmRoot(owner: Long): Any? = OhosJsonPerfHooks.bridgeBeforeArmRoot(owner)

actual fun bridgeAfterArmRoot(owner: Long): Any? = OhosJsonPerfHooks.bridgeAfterArmRoot(owner)

actual fun tokenerVariants(): List<TokenerVariant> = listOf(
    TokenerVariant(TOKENER_CJSON) { JSONTokener(it).nextValue() }
)

actual fun platformBridgeCheck(): String = verifyOhosLazyJsonBridge()

actual fun currentTokenerName(): String = TOKENER_CJSON

actual fun selectTokener(name: String) {
    // 只有一份实现，无可切换
}

actual fun platformPreservesJsonTextKeyOrder(): Boolean = true

@OptIn(NativeRuntimeApi::class)
actual fun collectGarbage() {
    GC.collect()
}

@OptIn(NativeRuntimeApi::class, ExperimentalStdlibApi::class)
actual fun getAllocatedBytes(): Long {
    // Kotlin 2.0+ 移除了 GC.allocatedBytes，改用最近一次 GC 后各池 totalObjectsSizeBytes 之和。
    val info = GC.lastGCInfo ?: return -1L
    val usage = info.memoryUsageAfter
    if (usage.isEmpty()) return -1L
    var total = 0L
    for (entry in usage.values) {
        total += entry.totalObjectsSizeBytes
    }
    return total
}

@OptIn(NativeRuntimeApi::class, ExperimentalStdlibApi::class)
actual fun getGcEpoch(): Long {
    return GC.lastGCInfo?.epoch ?: -1L
}

@OptIn(NativeRuntimeApi::class, ExperimentalStdlibApi::class)
actual fun getHeapObjectBytes(): Long {
    val info = GC.lastGCInfo ?: return -1L
    val usage = info.memoryUsageAfter
    if (usage.isEmpty()) return -1L
    var total = 0L
    for (entry in usage.values) {
        total += entry.totalObjectsSizeBytes
    }
    return total
}

@OptIn(NativeRuntimeApi::class, ExperimentalStdlibApi::class)
actual fun getLastSweptCount(): Long {
    val info = GC.lastGCInfo ?: return -1L
    val sweep = info.sweepStatistics
    if (sweep.isEmpty()) return -1L
    var total = 0L
    for (entry in sweep.values) {
        total += entry.sweptCount
    }
    return total
}

@OptIn(NativeRuntimeApi::class, ExperimentalStdlibApi::class)
actual fun getGcPoolsDebug(): String {
    val info = GC.lastGCInfo ?: return "lastGCInfo=null"
    val mem = info.memoryUsageAfter.entries.joinToString { "${it.key}=${it.value.totalObjectsSizeBytes}" }
    val sweep = info.sweepStatistics.entries.joinToString {
        "${it.key}:swept=${it.value.sweptCount},kept=${it.value.keptCount}"
    }
    return "epoch=${info.epoch} memAfter=[$mem] sweep=[$sweep]"
}

@OptIn(NativeRuntimeApi::class)
actual fun setGcSuspended(suspended: Boolean) {
    // Kotlin 2.0+ 用 suspend()/resume() 替代 suspendGCToggle。
    if (suspended) {
        GC.suspend()
    } else {
        GC.resume()
    }
}

actual fun getPlatformName(): String {
    return "K/N OHOS (HarmonyOS)"
}

actual fun getTimeNanosPlatform(): Long {
    // markNow().elapsedNow() 每次都是新 mark，差值接近 0；用 monotonic 绝对纳秒。
    return getTimeNanos()
}