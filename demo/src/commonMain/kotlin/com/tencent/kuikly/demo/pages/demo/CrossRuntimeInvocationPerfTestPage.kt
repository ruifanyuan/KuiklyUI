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

    private var status by observable("准备中：正在注册 ArkTS → Kotlin 回调")
    private var isTesting by observable(false)
    private var measureLine by observable(
        "Kotlin → ArkTS 同步文本测量｜10,000 次：未执行"
    )
    private var smallStringLine by observable(
        "ArkTS → Kotlin｜字符串｜1 KB × 50 次：未执行"
    )
    private var medianStringLine by observable(
        "ArkTS → Kotlin｜字符串｜30 KB × 50 次：未执行"
    )
    private var longStringLine by observable(
        "ArkTS → Kotlin｜字符串｜300 KB × 50 次：未执行"
    )
    private var smallJsonLine by observable(
        "ArkTS → Kotlin｜JSON 文本｜1 KB × 50 次：未执行"
    )
    private var medianJsonLine by observable(
        "ArkTS → Kotlin｜JSON 文本｜30 KB × 50 次：未执行"
    )
    private var largeJsonLine by observable(
        "ArkTS → Kotlin｜JSON 文本｜3 MB × 3 次：未执行"
    )

    private var registered = false
    private var payloadCallbackCount = 0
    private var completedCaseCount = 0

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
                Text {
                    attr {
                        text(
                            "ArkTS 通过 keep-alive callback 将 payload 传到 Kotlin。" +
                                "payload 会在计时前预生成；累计耗时包含桥接、参数转换和 Kotlin 侧消费。"
                        )
                        fontSize(13f)
                        color(Color(0xFF666666L))
                        marginBottom(12f)
                    }
                }
                Button {
                    attr {
                        height(60f)
                        marginBottom(16f)
                        borderRadius(6f)
                        backgroundColor(
                            if (ctx.isTesting) Color(0xFFB8B8B8L) else Color(0xFF1677FFL)
                        )
                        if (!ctx.isTesting) {
                            highlightBackgroundColor(Color(0x33111111))
                        }
                        alignSelfStretch()
                        testTag("run_all")
                        titleAttr {
                            text(if (ctx.isTesting) "Testing…" else "Run Tests")
                            fontSize(16f)
                            color(Color.WHITE)
                        }
                    }
                    event {
                        click {
                            if (!ctx.isTesting) {
                                ctx.runAllBenches()
                            }
                        }
                    }
                }
                ResultLine("perf_status") { ctx.status }
                ResultLine("perf_measure") { ctx.measureLine }
                ResultLine("perf_small_string") { ctx.smallStringLine }
                ResultLine("perf_median_string") { ctx.medianStringLine }
                ResultLine("perf_long_string") { ctx.longStringLine }
                ResultLine("perf_small_json") { ctx.smallJsonLine }
                ResultLine("perf_median_json") { ctx.medianJsonLine }
                ResultLine("perf_large_json") { ctx.largeJsonLine }
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
        measureLine =
            "Kotlin → ArkTS 同步文本测量｜$MEASURE_COUNT 次\n" +
                "累计耗时：$cost ms｜平均：${formatAverageMs(cost, MEASURE_COUNT)} ms/次"
        status = "文本测量完成，继续执行 ArkTS → Kotlin callback 测试"
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
            payloadCallbackCount++
            data?.length()
        }
        module.setCallbackWithSmallString(sink)
        module.setCallbackWithMedianString(sink)
        module.setCallbackWithLongString(sink)
        module.setCallbackWithSmallJSON(sink)
        module.setCallbackWithMedianJSON(sink)
        module.setCallbackWithLargeJSON(sink)
        status = "就绪：payload 会在首次执行时预生成；点击 Run all 开始测试"
        KLog.i(TAG, status)
    }

    private fun runAllBenches() {
        isTesting = true
        payloadCallbackCount = 0
        completedCaseCount = 0
        status = "执行中：依次测试文本测量、字符串回调和 JSON 文本回调"
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
    }

    private fun applyNativeStats(data: JSONObject?) {
        if (data == null) {
            return
        }
        val caseName = data.optString("case", "")
        val count = data.optInt("count")
        val costMs = data.optLong("cost_ms")
        val line =
            "${caseDescription(caseName)}\n" +
                "次数：$count｜累计耗时：$costMs ms｜平均：${formatAverageMs(costMs, count)} ms/次"
        when (caseName) {
            "small_string" -> smallStringLine = line
            "median_string" -> medianStringLine = line
            "long_string" -> longStringLine = line
            "small_json" -> smallJsonLine = line
            "median_json" -> medianJsonLine = line
            "large_json" -> largeJsonLine = line
        }
        completedCaseCount++
        if (completedCaseCount == CALLBACK_CASE_COUNT) {
            isTesting = false
            status = "全部完成：已收到 $payloadCallbackCount / $EXPECTED_CALLBACK_COUNT 次 payload 回调"
        }
        KLog.i(TAG, line)
    }

    private fun caseDescription(caseName: String): String = when (caseName) {
        "small_string" -> "ArkTS → Kotlin｜字符串｜1 KB"
        "median_string" -> "ArkTS → Kotlin｜字符串｜30 KB"
        "long_string" -> "ArkTS → Kotlin｜字符串｜300 KB"
        "small_json" -> "ArkTS → Kotlin｜JSON 文本｜1 KB"
        "median_json" -> "ArkTS → Kotlin｜JSON 文本｜30 KB"
        "large_json" -> "ArkTS → Kotlin｜JSON 文本｜3 MB"
        else -> "ArkTS → Kotlin｜未知 case=$caseName"
    }

    private fun formatAverageMs(totalMs: Long, count: Int): String {
        val hundredths = (totalMs * 100 + count.coerceAtLeast(1) / 2) / count.coerceAtLeast(1)
        return "${hundredths / 100}.${(hundredths % 100).toString().padStart(2, '0')}"
    }

    companion object {
        private const val TAG = "CrossRuntimeInvocationPerf"
        private const val MEASURE_COUNT = 10000
        private const val MEASURE_FONT_SIZE = 16f
        private const val MEASURE_MAX_WIDTH = 320f
        private const val MEASURE_MAX_HEIGHT = 100000f
        private const val SMALL_STRING_COUNT = 50
        private const val MEDIAN_STRING_COUNT = 50
        private const val LONG_STRING_COUNT = 50
        private const val SMALL_JSON_COUNT = 50
        private const val MEDIAN_JSON_COUNT = 50
        private const val LARGE_JSON_COUNT = 3
        private const val CALLBACK_CASE_COUNT = 6
        private const val EXPECTED_CALLBACK_COUNT =
            SMALL_STRING_COUNT + MEDIAN_STRING_COUNT + LONG_STRING_COUNT +
                SMALL_JSON_COUNT + MEDIAN_JSON_COUNT + LARGE_JSON_COUNT
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
