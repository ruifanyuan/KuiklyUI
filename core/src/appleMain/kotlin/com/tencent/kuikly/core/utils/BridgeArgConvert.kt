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

package com.tencent.kuikly.core.utils

import com.tencent.kuikly.core.nvi.serialization.json.JSONArray
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject
import com.tencent.kuikly.core.nvi.serialization.json.hasBinaryElement
import com.tencent.kuikly.core.nvi.serialization.json.toBridgeJSONArray
import com.tencent.kuikly.core.nvi.serialization.json.toBridgeJSONObject
import platform.Foundation.NSArray
import platform.Foundation.NSDictionary

/**
 * Apple 侧 callKotlin 入参转换，对应 OHOS 的 `KRRenderCValue.toAny()`：
 * 原生传来的 Foundation 容器包成惰性 [JSONObject] / [JSONArray]（不做整树拷贝），
 * 其余类型原样透传。由 KSP 生成的 iOS 入口在进 `BridgeManager` 之前调用。
 *
 * 二进制不受影响：`NSData` 元素所在的数组按原样透传（与 `KRConvertUtil.hr_isJsonArray`
 * 的历史判定一致），Kotlin 侧仍走 `toKotlinObject()` 拿到 `ByteArray`。
 *
 * `Map` / `List` 分支是防御性的：Foundation 容器出现在 `Any?` 位置时，是否被
 * Kotlin/Native 映射成 Kotlin 集合取决于运行时，两种形态都要能正确落地。
 */
fun Any?.toKotlinBridgeArg(): Any? {
    return when (this) {
        is JSONObject, is JSONArray -> this
        is NSDictionary -> JSONObject(this)
        is NSArray -> if (hasBinaryElement(this)) this else JSONArray(this)
        is Map<*, *> -> toBridgeJSONObject(this)
        is List<*> -> if (hasBinaryElement(this)) this else toBridgeJSONArray(this)
        else -> this
    }
}
