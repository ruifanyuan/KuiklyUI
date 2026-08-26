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

import kotlin.system.getTimeNanos

/**
 * OHOS JSON 性能探针：供 demo `JsonPlatformTestPage` 做 A/B（宽松扫描 vs 惰性 KRJSON）。
 *
 * - [measureWrapAfterNativeParseNanos]：native `KRJSONParse` 完成后，仅计量 Kotlin 包装耗时
 */
object OhosJsonPerfHooks {
    /**
     * 先在计时外完成 [CJsonNative.ownerFromJson]，再计量
     * [JSONObject.fromCJsonOwnerAny]（惰性壳 retain），对应「Native 已解析好」后的 Kotlin 起点。
     * 失败返回 -1。
     */
    fun measureWrapAfterNativeParseNanos(json: String): Long {
        val owner = CJsonNative.ownerFromJson(json)
        if (owner == 0L) {
            return -1L
        }
        return try {
            val t0 = getTimeNanos()
            val wrapped = JSONObject.fromCJsonOwnerAny(owner)
            val t1 = getTimeNanos()
            if (wrapped == null) {
                -1L
            } else {
                // 防止被优化掉
                sink = wrapped
                t1 - t0
            }
        } finally {
            CJsonNative.release(owner)
        }
    }

    // ---- 结构化入桥 A/B（C++→Kotlin）：从已建好的 KRJSON 值起，排除共有建树成本 ----

    /** 由 JSON 文本建自持有值；失败返回 0。计时外调用。 */
    fun bridgeBuildOwner(json: String): Long = CJsonNative.ownerFromJson(json)

    fun bridgeReleaseOwner(owner: Long) = CJsonNative.release(owner)

    /**
     * before 臂（分支前语义）：值 → `KRJSONDump` 成字符串 → 宽松扫描器全量 parse。
     * 覆盖旧路径「Native 打印字符串 + Kotlin 重新解析」两段成本。
     */
    fun bridgeBeforeArmRoot(owner: Long): Any? {
        if (owner == 0L) {
            return null
        }
        val text = CJsonNative.print(owner) ?: return null
        return LenientJSONTokener(text).nextValue()
    }

    /** after 臂（分支后语义）：owner → 惰性壳。 */
    fun bridgeAfterArmRoot(owner: Long): Any? {
        if (owner == 0L) {
            return null
        }
        return JSONObject.fromCJsonOwnerAny(owner)
    }

    @Suppress("unused")
    private var sink: Any? = null
}