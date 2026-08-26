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

package com.tencent.kuikly.core.nvi.serialization.json

import kotlinx.cinterop.ExperimentalForeignApi
import platform.Foundation.NSArray
import platform.Foundation.NSData
import platform.Foundation.NSDictionary
import platform.Foundation.NSJSONSerialization
import platform.Foundation.NSString
import platform.Foundation.NSUTF8StringEncoding
import platform.Foundation.create
import platform.Foundation.dataUsingEncoding
import kotlin.time.TimeSource

/**
 * Apple JSON 性能探针：供 demo `JsonPlatformTestPage` 做结构化入桥 A/B
 *（NSDictionary 打成字符串再走宽松扫描 vs 惰性 Foundation 包装）。
 */
object AppleJsonPerfHooks {
    // ---- 结构化入桥 A/B：从已建好的 Foundation 容器起，排除共有 NSJSON 建树成本 ----

    private var nextOwnerId = 1L
    private val owners = HashMap<Long, Any>()

    /** 由 JSON 文本建 Foundation 容器；失败返回 0。计时外调用。 */
    @OptIn(ExperimentalForeignApi::class)
    fun bridgeBuildOwner(json: String): Long {
        val parsed = parseFoundation(json) ?: return 0L
        val id = nextOwnerId++
        owners[id] = parsed
        return id
    }

    fun bridgeReleaseOwner(owner: Long) {
        owners.remove(owner)
    }

    /**
     * before 臂（分支前语义）：Foundation 容器 → `NSJSONSerialization` 打成字符串
     * → 宽松扫描器全量 parse。覆盖旧路径「Native 打印字符串 + Kotlin 重新解析」。
     */
    @OptIn(ExperimentalForeignApi::class)
    fun bridgeBeforeArmRoot(owner: Long): Any? {
        val native = owners[owner] ?: return null
        val data = NSJSONSerialization.dataWithJSONObject(native, 0u, null) ?: return null
        val text = nsDataUtf8(data) ?: return null
        return LenientJSONTokener(text).nextValue()
    }

    /** after 臂（分支后语义）：Foundation 容器 → 惰性壳。 */
    fun bridgeAfterArmRoot(owner: Long): Any? {
        val native = owners[owner] ?: return null
        return wrapFoundation(native)
    }

    @OptIn(ExperimentalForeignApi::class)
    fun measureWrapAfterNativeParseNanos(json: String): Long {
        val parsed = parseFoundation(json) ?: return -1L
        val mark = TimeSource.Monotonic.markNow()
        val wrapped = wrapFoundation(parsed)
        val nanos = mark.elapsedNow().inWholeNanoseconds
        if (wrapped == null) {
            return -1L
        }
        sink = wrapped
        return nanos
    }

    @OptIn(ExperimentalForeignApi::class)
    private fun parseFoundation(json: String): Any? {
        if (json.isEmpty()) {
            return null
        }
        val data = (json as NSString).dataUsingEncoding(NSUTF8StringEncoding) ?: return null
        return try {
            NSJSONSerialization.JSONObjectWithData(data, 0u, null)
        } catch (_: Throwable) {
            null
        }
    }

    private fun wrapFoundation(parsed: Any): Any? {
        return when (parsed) {
            is NSDictionary -> JSONObject(parsed)
            is NSArray -> JSONArray(parsed)
            else -> null
        }
    }

    @OptIn(ExperimentalForeignApi::class)
    private fun nsDataUtf8(data: NSData): String? {
        return NSString.create(data = data, encoding = NSUTF8StringEncoding)?.toString()
    }

    @Suppress("unused")
    private var sink: Any? = null
}