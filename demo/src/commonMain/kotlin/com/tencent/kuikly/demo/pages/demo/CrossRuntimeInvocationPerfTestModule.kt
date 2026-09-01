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

import com.tencent.kuikly.core.module.CallbackFn
import com.tencent.kuikly.core.module.Module
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject

/**
 * Keep-alive sync callbacks for string / JSON payload benches.
 * [setCallbackWith*] only registers. [run*] asks native to fire unique payloads,
 * time them, and report via [setStatsCallback].
 */
internal class CrossRuntimeInvocationPerfTestModule : Module() {

    override fun moduleName(): String = MODULE_NAME

    fun setStatsCallback(callbackFn: CallbackFn) {
        toNative(
            keepCallbackAlive = true,
            methodName = "SetStatsCallback",
            param = null,
            callback = callbackFn,
            syncCall = true
        )
    }

    fun setCallbackWithSmallString(callbackFn: CallbackFn) {
        registerPayloadCallback("SetCallbackWithSmallString", callbackFn)
    }

    fun setCallbackWithMedianString(callbackFn: CallbackFn) {
        registerPayloadCallback("SetCallbackWithMedianString", callbackFn)
    }

    fun setCallbackWithLongString(callbackFn: CallbackFn) {
        registerPayloadCallback("SetCallbackWithLongString", callbackFn)
    }

    fun setCallbackWithSmallJSON(callbackFn: CallbackFn) {
        registerPayloadCallback("SetCallbackWithSmallJSON", callbackFn)
    }

    fun setCallbackWithMedianJSON(callbackFn: CallbackFn) {
        registerPayloadCallback("SetCallbackWithMedianJSON", callbackFn)
    }

    fun setCallbackWithLargeJSON(callbackFn: CallbackFn) {
        registerPayloadCallback("SetCallbackWithLargeJSON", callbackFn)
    }

    fun runSmallString(count: Int) = runCase("RunSmallString", count)

    fun runMedianString(count: Int) = runCase("RunMedianString", count)

    fun runLongString(count: Int) = runCase("RunLongString", count)

    fun runSmallJSON(count: Int) = runCase("RunSmallJSON", count)

    fun runMedianJSON(count: Int) = runCase("RunMedianJSON", count)

    fun runLargeJSON(count: Int) = runCase("RunLargeJSON", count)

    private fun registerPayloadCallback(methodName: String, callbackFn: CallbackFn) {
        toNative(
            keepCallbackAlive = true,
            methodName = methodName,
            param = null,
            callback = callbackFn,
            syncCall = true
        )
    }

    private fun runCase(methodName: String, count: Int) {
        toNative(
            keepCallbackAlive = false,
            methodName = methodName,
            param = JSONObject().put("count", count).toString(),
            callback = null,
            syncCall = false
        )
    }

    companion object {
        const val MODULE_NAME = "KRCrossRuntimeInvocationPerfTestModule"
    }
}
