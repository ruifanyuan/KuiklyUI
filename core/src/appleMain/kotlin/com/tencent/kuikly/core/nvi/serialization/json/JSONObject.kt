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

import platform.Foundation.NSDictionary

/**
 * Apple 实现：底层容器可以是 Kotlin Map（代码构造 / 字符串解析）或 Foundation 字典
 * （原生桥接入参，见 [LazyNSDictionaryMap]）。
 */
actual class JSONObject internal actual constructor(
    nameValuePairs: MutableMap<String, Any?>
) : AbstractJSONObject(nameValuePairs) {

    actual constructor() : this(JSONEngine.getMutableMap())

    @Throws(JSONException::class)
    actual constructor(json: String) : this(requireJSONObjectPairs(JSONEngine.parse(json)))

    @Throws(JSONException::class)
    actual constructor(jsonTokener: JSONTokener) : this(requireJSONObjectPairs(jsonTokener.nextValue()))

    /** 包装 Foundation 字典，读取时按需转换，不做整树拷贝。 */
    internal constructor(dictionary: NSDictionary) : this(LazyNSDictionaryMap(dictionary))

    /** 转成 Foundation 字典，供结构化桥接出参使用。 */
    internal fun toNSDictionary(): NSDictionary = kotlinMapToNSDictionary(nameValuePairs)

    actual companion object {
        actual fun quote(data: String?): String = quoteJSONString(data)
    }
}
