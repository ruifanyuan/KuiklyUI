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

/**
 * OHOS 实现：底层容器为 Kotlin List。cJSON 数组不直接惰性包装——cJSON 不区分整数与
 * 浮点，若逐个节点转换会与其他平台的数字选型不一致，故由 [LazyCJsonMap] 打印后交给
 * 宽松扫描器还原（见 `LazyCJsonMap.optFromCJson`）。
 */
actual class JSONArray internal actual constructor(
    values: MutableList<Any?>
) : AbstractJSONArray(values) {

    actual constructor() : this(JSONEngine.getMutableList())

    @Throws(JSONException::class)
    actual constructor(json: String) : this(requireJSONArrayValues(JSONEngine.parse(json)))

    @Throws(JSONException::class)
    actual constructor(jsonTokener: JSONTokener) : this(requireJSONArrayValues(jsonTokener.nextValue()))
}
