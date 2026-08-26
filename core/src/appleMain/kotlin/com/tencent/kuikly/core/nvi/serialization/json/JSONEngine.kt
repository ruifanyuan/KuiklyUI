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
     * 文本解析走宽松扫描器 [JSONTokener]，不用 `NSJSONSerialization`：
     * 后者返回 `NSDictionary`，其 `allKeys` 顺序由哈希决定，会改变
     * `JSONObject(jsonStr).toString()` 的 key 顺序——序列化文本是对端可观测行为
     * （见 JsonConformanceSuite 的 key order 用例），且没有任何 `NSJSONReadingOptions`
     * 能保留成员顺序。
     *
     * Foundation 容器只用于原生桥接入参（此时数据本身就是 `NSDictionary`，不存在
     * 文本顺序），见 [LazyNSDictionaryMap] 与 `Any?.toKotlinBridgeArg()`。
     */
    actual fun parse(jsonStr: String): Any? {
        return JSONTokener(jsonStr).nextValue()
    }

    actual fun stringify(jsonObject: JSONObject) = commonStringify(jsonObject)

    actual fun stringify(jsonArray: JSONArray) = commonStringify(jsonArray)

    internal actual fun <K, V> getMutableMap(): MutableMap<K, V> = mutableMapOf()

    internal actual fun <E> getMutableList(): MutableList<E> = mutableListOf()
}
