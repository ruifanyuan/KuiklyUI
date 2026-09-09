/*
 * Tencent is pleased to support the open source community by making KuiklyUI
 * available.
 * Copyright (C) 2026 Tencent. All rights reserved.
 * Licensed under the License of the KuiklyUI;
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * https://github.com/Tencent-TDS/KuiklyUI/blob/main/LICENSE
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.tencent.kuikly.core.module

import com.tencent.kuikly.core.nvi.serialization.json.JSONObject

/**
 * Encodes RouterModule.openPage arguments for the platform bridge.
 *
 * HarmonyOS keeps the envelope structured so the bridge can retain/build
 * KRJSONValue directly. Other platforms preserve their historical JSON-text
 * ABI. [routeStartTimestampMs] is framework-internal tracing metadata kept on
 * the envelope (never inside business [pageData]), so it does not change the
 * adapter or destination Pager pageData contract. Optional [extra] string
 * fields are written onto the same envelope (not nested in [pageData]).
 */
internal expect fun platformOpenPageParams(
    pageName: String,
    pageData: JSONObject?,
    routeStartTimestampMs: Long,
    extra: Map<String, String>?,
): Any

internal fun stringifyOpenPageParams(
    pageName: String,
    pageData: JSONObject?,
    extra: Map<String, String>? = null,
): String {
    return JSONObject().apply {
        put("pageName", pageName)
        putOpenPageExtras(extra)
        pageData?.let { put("pageData", it) }
    }.toString()
}

internal fun JSONObject.putOpenPageExtras(extra: Map<String, String>?) {
    extra?.forEach { (key, value) ->
        put(key, value)
    }
}
