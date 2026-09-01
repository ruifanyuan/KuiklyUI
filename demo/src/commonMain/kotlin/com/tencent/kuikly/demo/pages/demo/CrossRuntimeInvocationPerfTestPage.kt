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

import com.tencent.kuikly.core.annotations.Page
import com.tencent.kuikly.core.base.Color
import com.tencent.kuikly.core.base.ViewBuilder
import com.tencent.kuikly.core.base.ViewConst
import com.tencent.kuikly.core.base.ViewContainer
import com.tencent.kuikly.core.datetime.DateTime
import com.tencent.kuikly.core.log.KLog
import com.tencent.kuikly.core.module.Module
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.views.Scroller
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.TextConst
import com.tencent.kuikly.core.views.compose.Button
import com.tencent.kuikly.core.views.shadow.TextShadow
import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.demo.pages.demo.base.NavBar

@Page("CrossRuntimeInvocationPerfTestPage")
internal class CrossRuntimeInvocationPerfTestPage : BasePager() {

    private var status by observable("waiting")
    private var measureLine by observable("measure: —")
    private var smallStringLine by observable("small string: —")
    private var medianStringLine by observable("median string: —")
    private var longStringLine by observable("long string: —")
    private var smallJsonLine by observable("small json: —")
    private var medianJsonLine by observable("median json: —")
    private var largeJsonLine by observable("large json: —")

    private var registered = false
    private var payloadSink = 0

    override fun createExternalModules(): Map<String, Module>? {
        val modules = super.createExternalModules()?.toMutableMap() ?: hashMapOf()
        modules[CrossRuntimeInvocationPerfTestModule.MODULE_NAME] = CrossRuntimeInvocationPerfTestModule()
        return modules
    }

    override fun viewDidLayout() {
        super.viewDidLayout()
        if (registered) {
            return
        }
        registered = true
        registerNativeCallbacks()
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.WHITE)
            }
            NavBar {
                attr {
                    title = "Cross Runtime Invocation Perf"
                }
            }
            Scroller {
                attr {
                    flex(1f)
                    padding(16f)
                }
                ResultLine("perf_status") { ctx.status }
                ResultLine("perf_measure") { ctx.measureLine }
                ResultLine("perf_small_string") { ctx.smallStringLine }
                ResultLine("perf_median_string") { ctx.medianStringLine }
                ResultLine("perf_long_string") { ctx.longStringLine }
                ResultLine("perf_small_json") { ctx.smallJsonLine }
                ResultLine("perf_median_json") { ctx.medianJsonLine }
                ResultLine("perf_large_json") { ctx.largeJsonLine }
                Button {
                    attr {
                        height(40f)
                        padding(left = 16f, right = 16f)
                        borderRadius(6f)
                        backgroundColor(Color(0xFF1677FFL))
                        highlightBackgroundColor(Color(0x33111111))
                        alignSelfFlexStart()
                        testTag("run_all")
                        titleAttr {
                            text("Run all")
                            fontSize(14f)
                            color(Color.WHITE)
                        }
                    }
                    event {
                        click {
                            ctx.runAllBenches()
                        }
                    }
                }
            }
        }
    }

    private fun runMeasureBench() {
        val shadow = TextShadow(pagerId, nativeRef, ViewConst.TYPE_RICH_TEXT)
        shadow.setProp(TextConst.FONT_SIZE, MEASURE_FONT_SIZE)
        shadow.setProp(TextConst.TEXT_USE_DP_FONT_SIZE_DIM, 1)
        val start = DateTime.currentTimestamp()
        var lastWidth = 0f
        var lastHeight = 0f
        for (i in 0 until MEASURE_COUNT) {
            shadow.setProp(TextConst.VALUE, "测$i 中文Abc🎉/${i * 17}")
            val size = shadow.calculateRenderViewSize(MEASURE_MAX_WIDTH, MEASURE_MAX_HEIGHT)
            lastWidth = size.width
            lastHeight = size.height
        }
        shadow.removeFromParentComponent()
        val cost = DateTime.currentTimestamp() - start
        measureLine = "measure: count=$MEASURE_COUNT cost_ms=$cost w=$lastWidth h=$lastHeight"
        status = "measure done"
        KLog.i(TAG, measureLine)
    }

    private fun registerNativeCallbacks() {
        val module = acquireModule<CrossRuntimeInvocationPerfTestModule>(
            CrossRuntimeInvocationPerfTestModule.MODULE_NAME
        )
        module.setStatsCallback { data ->
            applyNativeStats(data)
        }
        val sink: (JSONObject?) -> Unit = { data ->
            payloadSink += data?.length() ?: 0
        }
        module.setCallbackWithSmallString(sink)
        module.setCallbackWithMedianString(sink)
        module.setCallbackWithLongString(sink)
        module.setCallbackWithSmallJSON(sink)
        module.setCallbackWithMedianJSON(sink)
        module.setCallbackWithLargeJSON(sink)
        status = "callbacks registered"
        KLog.i(TAG, status)
    }

    private fun runAllBenches() {
        payloadSink = 0
        status = "running"
        runMeasureBench()
        val module = acquireModule<CrossRuntimeInvocationPerfTestModule>(
            CrossRuntimeInvocationPerfTestModule.MODULE_NAME
        )
        module.runSmallString(SMALL_STRING_COUNT)
        module.runMedianString(MEDIAN_STRING_COUNT)
        module.runLongString(LONG_STRING_COUNT)
        module.runSmallJSON(SMALL_JSON_COUNT)
        module.runMedianJSON(MEDIAN_JSON_COUNT)
        module.runLargeJSON(LARGE_JSON_COUNT)
        status = "all done sink=$payloadSink"
        KLog.i(TAG, status)
    }

    private fun applyNativeStats(data: JSONObject?) {
        if (data == null) {
            return
        }
        val caseName = data.optString("case", "")
        val line = "$caseName: count=${data.optInt("count")} cost_ms=${data.optLong("cost_ms")} bytes=${data.optLong("bytes")}"
        when (caseName) {
            "small_string" -> smallStringLine = line
            "median_string" -> medianStringLine = line
            "long_string" -> longStringLine = line
            "small_json" -> smallJsonLine = line
            "median_json" -> medianJsonLine = line
            "large_json" -> largeJsonLine = line
        }
        KLog.i(TAG, line)
    }

    companion object {
        private const val TAG = "CrossRuntimeInvocationPerf"
        private const val MEASURE_COUNT = 10000
        private const val MEASURE_FONT_SIZE = 16f
        private const val MEASURE_MAX_WIDTH = 320f
        private const val MEASURE_MAX_HEIGHT = 100000f
        private const val SMALL_STRING_COUNT = 2000
        private const val MEDIAN_STRING_COUNT = 200
        private const val LONG_STRING_COUNT = 50
        private const val SMALL_JSON_COUNT = 2000
        private const val MEDIAN_JSON_COUNT = 200
        private const val LARGE_JSON_COUNT = 10
    }
}

private fun ViewContainer<*, *>.ResultLine(tag: String, textProvider: () -> String) {
    Text {
        attr {
            text(textProvider())
            fontSize(14f)
            marginBottom(8f)
            testTag(tag)
        }
    }
}
