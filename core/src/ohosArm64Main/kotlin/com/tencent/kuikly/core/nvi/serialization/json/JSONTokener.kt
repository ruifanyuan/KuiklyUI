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
 * OHOS 实现：字符串解析用宽松扫描器。
 *
 * 不走 cJSON：cJSON 的 number 只有一个 `double`，无法区分 `1000` 与 `1e3`
 * （宽松扫描器分别给 `Int` 与 `Double`），用它解析字符串会改变既有的数字选型。
 * cJSON 只用于原生已经构造好的树（`NATIVE_JSON`，见 [LazyCJsonMap]），那条路径上
 * 原本就没有文本可参照。
 */
actual class JSONTokener actual constructor(json: String) : AbstractJSONTokener(json)
