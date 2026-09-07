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

private enum class PayloadShape(val id: String, val label: String) {
    PLAIN_STRING("plain_string", "普通 string"),
    JSON_STRING("json_string", "JSON string"),
    KR_RECORD("kr_record", "KRRecord 对象"),
    KR_JSON_VALUE("kr_json_value", "KRJsonValue 对象"),
}

private data class PayloadScale(val suffix: String, val chars: Int, val count: Int)

private val PAYLOAD_SCALES = listOf(
    PayloadScale("128k", 128 * 1024, 50),
    PayloadScale("384k", 384 * 1024, 20),
    PayloadScale("3m", 3 * 1024 * 1024, 3),
)

private val CASE_DEFS: Map<String, Pair<PayloadShape, PayloadScale>> =
    PAYLOAD_SCALES.flatMap { scale ->
        PayloadShape.values().map { shape ->
            caseName(shape, scale) to (shape to scale)
        }
    }.toMap()

private fun caseName(shape: PayloadShape, scale: PayloadScale): String =
    "${shape.id}_${scale.suffix}"

private fun initialResultLine(shape: PayloadShape, scale: PayloadScale): String =
    "ArkTS → Kotlin｜${shape.label}｜${formatPayloadBytes(scale.chars.toLong())} × ${scale.count} 次：未执行"

@Page("CrossRuntimeInvocationPerfTestPage")
internal class CrossRuntimeInvocationPerfTestPage : BasePager() {

    private var status by observable("准备中：正在注册 ArkTS → Kotlin 回调")
    private var isTesting by observable(false)
    private var measureLine by observable(
        "Kotlin → ArkTS 同步文本测量｜$MEASURE_COUNT 次：未执行"
    )

    private var plainString128Line by observable(
        initialResultLine(PayloadShape.PLAIN_STRING, PAYLOAD_SCALES[0])
    )
    private var plainString384Line by observable(
        initialResultLine(PayloadShape.PLAIN_STRING, PAYLOAD_SCALES[1])
    )
    private var plainString3mLine by observable(
        initialResultLine(PayloadShape.PLAIN_STRING, PAYLOAD_SCALES[2])
    )
    private var jsonString128Line by observable(
        initialResultLine(PayloadShape.JSON_STRING, PAYLOAD_SCALES[0])
    )
    private var jsonString384Line by observable(
        initialResultLine(PayloadShape.JSON_STRING, PAYLOAD_SCALES[1])
    )
    private var jsonString3mLine by observable(
        initialResultLine(PayloadShape.JSON_STRING, PAYLOAD_SCALES[2])
    )
    private var krRecord128Line by observable(
        initialResultLine(PayloadShape.KR_RECORD, PAYLOAD_SCALES[0])
    )
    private var krRecord384Line by observable(
        initialResultLine(PayloadShape.KR_RECORD, PAYLOAD_SCALES[1])
    )
    private var krRecord3mLine by observable(
        initialResultLine(PayloadShape.KR_RECORD, PAYLOAD_SCALES[2])
    )
    private var krJsonValue128Line by observable(
        initialResultLine(PayloadShape.KR_JSON_VALUE, PAYLOAD_SCALES[0])
    )
    private var krJsonValue384Line by observable(
        initialResultLine(PayloadShape.KR_JSON_VALUE, PAYLOAD_SCALES[1])
    )
    private var krJsonValue3mLine by observable(
        initialResultLine(PayloadShape.KR_JSON_VALUE, PAYLOAD_SCALES[2])
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
                            "覆盖 4 种 ArkTS → Kotlin payload 形态，并对 128 KB / 384 KB / 3 MB " +
                                "三种规模分别测试。payload 在计时前生成；累计耗时包含桥接、参数转换和 Kotlin 侧消费。"
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
                ResultLine("perf_plain_string_128k") { ctx.plainString128Line }
                ResultLine("perf_plain_string_384k") { ctx.plainString384Line }
                ResultLine("perf_plain_string_3m") { ctx.plainString3mLine }
                ResultLine("perf_json_string_128k") { ctx.jsonString128Line }
                ResultLine("perf_json_string_384k") { ctx.jsonString384Line }
                ResultLine("perf_json_string_3m") { ctx.jsonString3mLine }
                ResultLine("perf_kr_record_128k") { ctx.krRecord128Line }
                ResultLine("perf_kr_record_384k") { ctx.krRecord384Line }
                ResultLine("perf_kr_record_3m") { ctx.krRecord3mLine }
                ResultLine("perf_kr_json_value_128k") { ctx.krJsonValue128Line }
                ResultLine("perf_kr_json_value_384k") { ctx.krJsonValue384Line }
                ResultLine("perf_kr_json_value_3m") { ctx.krJsonValue3mLine }
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
        status = "文本测量完成，继续执行 ArkTS → Kotlin 四类 payload 测试"
        KLog.i(TAG, measureLine)
    }

    private fun registerNativeCallbacks() {
        val module = acquireModule<CrossRuntimeInvocationPerfTestModule>(
            CrossRuntimeInvocationPerfTestModule.MODULE_NAME
        )
        module.setStatsCallback { data ->
            applyNativeStats(data)
        }
        val jsonSink: (JSONObject?) -> Unit = { data ->
            payloadCallbackCount++
            data?.length()
        }
        for (scale in PAYLOAD_SCALES) {
            val plainCase = caseName(PayloadShape.PLAIN_STRING, scale)
            module.setCallbackWithPlainString(plainCase) { data ->
                payloadCallbackCount++
                (data as? String)?.length
            }
            module.setCallbackWithJson(caseName(PayloadShape.JSON_STRING, scale), jsonSink)
            module.setCallbackWithJson(caseName(PayloadShape.KR_RECORD, scale), jsonSink)
            module.setCallbackWithJson(caseName(PayloadShape.KR_JSON_VALUE, scale), jsonSink)
        }
        status = "就绪：12 个 payload case 已注册；点击 Run Tests 开始测试"
        KLog.i(TAG, status)
    }

    private fun runAllBenches() {
        isTesting = true
        payloadCallbackCount = 0
        completedCaseCount = 0
        status = "执行中：依次测试文本测量和 12 个 ArkTS → Kotlin payload case"
        runMeasureBench()
        val module = acquireModule<CrossRuntimeInvocationPerfTestModule>(
            CrossRuntimeInvocationPerfTestModule.MODULE_NAME
        )
        for (scale in PAYLOAD_SCALES) {
            val plainCase = caseName(PayloadShape.PLAIN_STRING, scale)
            module.runPlainString(plainCase, scale.count, scale.chars)
            module.runJsonString(caseName(PayloadShape.JSON_STRING, scale), scale.count, scale.chars)
            module.runKRRecord(caseName(PayloadShape.KR_RECORD, scale), scale.count, scale.chars)
            module.runKRJsonValue(buildKRJsonRequest(PayloadShape.KR_JSON_VALUE, scale))
        }
    }

    private fun applyNativeStats(data: JSONObject?) {
        if (data == null) {
            return
        }
        val caseName = data.optString("case", "")
        val count = data.optInt("count")
        val costMs = data.optLong("cost_ms")
        val bytes = data.optLong("bytes")
        val line =
            "${caseDescription(caseName)}\n" +
                "规模：${formatPayloadBytes(bytes / count.coerceAtLeast(1))} × $count｜" +
                "累计耗时：$costMs ms｜平均：${formatAverageMs(costMs, count)} ms/次"
        setResultLine(caseName, line)
        completedCaseCount++
        if (completedCaseCount == CASE_DEFS.size) {
            isTesting = false
            status = "全部完成：已收到 $payloadCallbackCount / $EXPECTED_CALLBACK_COUNT 次 payload 回调"
        }
        KLog.i(TAG, line)
    }

    private fun setResultLine(caseName: String, line: String) {
        when (caseName) {
            "plain_string_128k" -> plainString128Line = line
            "plain_string_384k" -> plainString384Line = line
            "plain_string_3m" -> plainString3mLine = line
            "json_string_128k" -> jsonString128Line = line
            "json_string_384k" -> jsonString384Line = line
            "json_string_3m" -> jsonString3mLine = line
            "kr_record_128k" -> krRecord128Line = line
            "kr_record_384k" -> krRecord384Line = line
            "kr_record_3m" -> krRecord3mLine = line
            "kr_json_value_128k" -> krJsonValue128Line = line
            "kr_json_value_384k" -> krJsonValue384Line = line
            "kr_json_value_3m" -> krJsonValue3mLine = line
        }
    }

    private fun caseDescription(caseName: String): String {
        val case = CASE_DEFS[caseName] ?: return "ArkTS → Kotlin｜未知 case=$caseName"
        return "ArkTS → Kotlin｜${case.first.label}｜${formatPayloadBytes(case.second.chars.toLong())}"
    }

    private fun buildKRJsonRequest(shape: PayloadShape, scale: PayloadScale): JSONObject {
        val case = caseName(shape, scale)
        val payload = buildKRJsonPayload(scale.chars)
        return JSONObject()
            .put("case", case)
            .put("count", scale.count)
            .put("payload", payload)
    }

    private fun buildKRJsonPayload(targetChars: Int): JSONObject {
        val base = JSONObject()
            .put("kind", "kr_json_value")
            .put("i", 0)
            .put("v", "")
        val padLen = (targetChars - base.toString().length).coerceAtLeast(0)
        return base.put("v", makePayloadString(padLen))
    }

    private fun makePayloadString(targetChars: Int): String {
        val token = "0123456789abcdef测Abc"
        val builder = StringBuilder(targetChars)
        while (builder.length < targetChars) {
            val remaining = targetChars - builder.length
            if (remaining >= token.length) {
                builder.append(token)
            } else {
                builder.append(token, 0, remaining)
            }
        }
        return builder.toString()
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
        private val EXPECTED_CALLBACK_COUNT =
            PAYLOAD_SCALES.sumOf { it.count } * PayloadShape.values().size
    }
}

private fun formatPayloadBytes(bytes: Long): String = when {
    bytes >= 1024L * 1024L -> "${bytes / (1024L * 1024L)} MB"
    bytes >= 1024L -> "${bytes / 1024L} KB"
    else -> "$bytes B"
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
