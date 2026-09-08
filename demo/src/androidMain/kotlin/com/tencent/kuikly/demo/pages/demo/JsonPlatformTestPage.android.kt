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

import com.tencent.kuikly.core.nvi.serialization.json.JSONTokener

private const val TOKENER_COMMON = "JSONTokener(common)"

actual fun parseWithTokener(json: String): Any? = JSONTokener(json).nextValue()

actual fun measureNativeParsedWrapNanos(json: String): Long = -1L

actual fun bridgeSupported(): Boolean = false

actual fun bridgeBuildOwner(json: String): Long = 0L

actual fun bridgeReleaseOwner(owner: Long) {}

actual fun bridgeBeforeArmRoot(owner: Long): Any? = null

actual fun bridgeAfterArmRoot(owner: Long): Any? = null

actual fun tokenerVariants(): List<TokenerVariant> = listOf(
    TokenerVariant(TOKENER_COMMON) { JSONTokener(it).nextValue() }
)

actual fun platformBridgeCheck(): String = "OK (n/a) android 无原生惰性 JSON 容器"

actual fun currentTokenerName(): String = TOKENER_COMMON

actual fun selectTokener(name: String) {
    // 只有一份实现，无可切换
}

actual fun platformPreservesJsonTextKeyOrder(): Boolean = true

actual fun collectGarbage() {
    System.gc()
}

actual fun getAllocatedBytes(): Long {
    val rt = Runtime.getRuntime()
    return rt.totalMemory() - rt.freeMemory()
}

actual fun getGcEpoch(): Long {
    // JVM 无对应的 GC 轮次计数 API
    return -1
}

actual fun getHeapObjectBytes(): Long = -1

actual fun getLastSweptCount(): Long = -1

actual fun getGcPoolsDebug(): String = "unavailable on JVM"

actual fun setGcSuspended(suspended: Boolean) {
    // JVM: no equivalent suspend toggle; timing may still include GC
}

actual fun getPlatformName(): String {
    return "JVM (Android)"
}

actual fun getTimeNanosPlatform(): Long {
    return System.nanoTime()
}