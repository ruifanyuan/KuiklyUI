/*
 * Tencent is pleased to support the open source community by making KuiklyUI
 * available.
 * Copyright (C) 2025 Tencent. All rights reserved.
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

actual object JSONEngine {

    /**
     * 文本解析走 [JSONTokener]：严格 JSON 优先 [NSJSONSerialization]（惰性 Foundation
     * 容器），宽松历史写法回退到 [AbstractJSONTokener]。
     *
     * `NSDictionary` 不保留文本成员顺序，因此 `JSONObject(jsonStr).toString()` 的 key
     * 次序在 Apple 上可能与输入不同（见 JsonConformanceSuite 对 key order 的平台开关）。
     */
    actual fun parse(jsonStr: String): Any? {
        return JSONTokener(jsonStr).nextValue()
    }

    actual fun stringify(jsonObject: JSONObject) = commonStringify(jsonObject)

    actual fun stringify(jsonArray: JSONArray) = commonStringify(jsonArray)

    internal actual fun <K, V> getMutableMap(): MutableMap<K, V> = mutableMapOf()

    internal actual fun <E> getMutableList(): MutableList<E> = mutableListOf()
}
