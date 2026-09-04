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
import com.tencent.kuikly.core.base.ViewContainer
import com.tencent.kuikly.core.datetime.DateTime
import com.tencent.kuikly.core.log.KLog
import com.tencent.kuikly.core.module.RouterModule
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.views.List
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.core.views.compose.Button
import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.demo.pages.demo.base.NavBar

private const val TAG = "JsonParsingPerf"

// 打开子页面时用到的参数键
private const val KEY_CLICK_TS = "clickTimestampMs"
private const val KEY_FIELD_COUNT = "fieldCount"
private const val KEY_TIME = "time"
private const val KEY_BACKING = "jsonBacking"
private const val KEY_DYNAMIC_PREFIX = "dyn_"
private const val BACKING_KOTLIN = "kotlin"
private const val BACKING_CPP = "cpp"

// 体积档位常量（单位：字节）
private const val BYTES_PER_KB = 1024
private const val BYTES_PER_MB = 1024 * 1024
private const val TARGET_INITIAL_BYTES = 30 * BYTES_PER_KB   // ~30 KB
private const val INC_SMALL_BYTES = 30 * BYTES_PER_KB        // +30 KB
private const val INC_MEDIUM_BYTES = 300 * BYTES_PER_KB      // +300 KB
private const val INC_LARGE_BYTES = 3 * BYTES_PER_MB         // +3 MB

// 初始校准阶段先塞入的字段数量。够多可以摊薄头部误差，太多又会拖慢首屏。
// 200 条足够覆盖 4 种类型（i % 4）的一个完整轮次，且实测样本量足以稳定平均值。
private const val CALIBRATION_FIELDS = 200

// 兜底：万一校准得到的 bytesPerField 异常（<=0），退回这个经验值。
// 实测 dyn_i k/v（Long/String/Bool/Double 交替，key 形如 dyn_12345）≈ 17 B/字段。
private const val AVG_BYTES_PER_FIELD_FALLBACK = 17.0

// 主入口页面：
//  - 页面创建及每次扩容后，预先准备两份同内容 payload：
//      * Kotlin backed：直接 `JSONObject() + put`；
//      * C++ backed：提前 `toString + JSONObject(text)` 成 KRJSON 惰性树。
//  - 按钮点击只选择已准备好的 payload 并调用 RouterModule.openPage，不把 C++ backed
//    的 JSON stringify/parse 计入跳转耗时。
//  - 按钮③/④/⑤：在 Kotlin payload 上追加约 30KB / 300KB / 3MB 后，重新准备 C++ payload。
@Page("JsonParsingPerfTestPage")
internal class JsonParsingPerfTestPage : BasePager() {

    // Kotlin-backed 版本。后续所有扩容都写入它。
    private val payloadJson: JSONObject = JSONObject()

    // C++-backed 版本。仅在 prepareCppPayload() 中构建，点击跳转时绝不 put，
    // 以保留 OHOS 上的 KRJSON lazy-native backing。
    private var cppPayloadJson: JSONObject? = null

    // 当前已经写入 payloadJson 的 dyn_i 字段的下一个索引（下一次追加时使用）。
    private var nextFieldIndex: Int = 0

    // 实测得到的每个 dyn_i 字段平均字节数（校准阶段结束后填入）。
    // 使用 Double 保留精度，避免 3 MB 这种大目标累积误差过大。
    private var bytesPerField: Double = AVG_BYTES_PER_FIELD_FALLBACK

    // UI 观察值：当前 payloadJson 中 dyn_i 字段数量
    private var fieldCount: Int by observable(0)

    // UI 观察值：当前 payloadJson.toString().length，即真实序列化字节数
    private var payloadBytes: Int by observable(0)

    // 最近一次跳转/操作的状态文本
    private var statusText: String by observable("等待操作\n点击「打开页面」以启动一次测量。")

    override fun created() {
        super.created()
        // 先写入业务 payload 固定字段。点击时间和本次 backing 属于测试路由
        // envelope 元数据，不写进预先准备的两份业务 payload。
        payloadJson.put(KEY_FIELD_COUNT, 0)

        // === 校准阶段 ===
        // 先塞入 CALIBRATION_FIELDS 个 dyn_i 字段，然后测得真实的 bytesPerField，
        // 用于后续 30 KB / 300 KB / 3 MB 目标的字段数换算。
        // 头部空 payloadJson 的字节数
        val headerBytes = payloadJson.toString().length
        appendFields(CALIBRATION_FIELDS)
        val calibrationBytes = payloadJson.toString().length
        val perField = (calibrationBytes - headerBytes).toDouble() / CALIBRATION_FIELDS
        if (perField > 0) {
            bytesPerField = perField
        }

        // === 补足到默认目标体积（≈ 30 KB） ===
        val remainingBytes = (TARGET_INITIAL_BYTES - calibrationBytes).coerceAtLeast(0)
        if (remainingBytes > 0) {
            appendFieldsForBytes(remainingBytes)
        }

        refreshMetrics()
        prepareCppPayload(reason = "init")
        KLog.i(
            TAG,
            "init: fieldCount=$fieldCount, payloadBytes=$payloadBytes, " +
                "bytesPerField=$bytesPerField, headerBytes=$headerBytes"
        )
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.WHITE)
            }

            NavBar {
                attr {
                    title = "JSON 解析耗时测试"
                }
            }

            List {
                attr {
                    flex(1f)
                }

                // 说明
                View {
                    attr {
                        margin(left = 16f, top = 16f, right = 16f, bottom = 8f)
                    }
                    Text {
                        attr {
                            text(
                                "按钮①：打开已准备的 kotlin backed JSONObject，测 KRJSON 构建/透传路径。\n" +
                                    "按钮②：打开已准备的 c++ backed JSONObject，测 KRJSON retain/透传路径。\n" +
                                    "按钮③/④/⑤：扩容后会在跳转前预先构建 c++ backed 数据。\n" +
                                    "耗时输出到日志，Tag=$TAG。"
                            )
                            fontSize(13f)
                            color(Color(0xFF555555))
                        }
                    }
                }

                // 当前字段数与真实体积展示
                View {
                    attr {
                        margin(left = 16f, top = 8f, right = 16f, bottom = 8f)
                        backgroundColor(Color(0xFFF0F4FF))
                        borderRadius(8f)
                        padding(all = 12f)
                    }
                    Text {
                        attr {
                            text(
                                "当前 JSONObject 字段数：${ctx.fieldCount}\n" +
                                    "当前 JSON 体积：${formatBytes(ctx.payloadBytes)} " +
                                    "(${ctx.payloadBytes} 字节)"
                            )
                            fontSize(14f)
                            color(Color(0xFF333333))
                            testTag("json_perf_field_count_text")
                        }
                    }
                }

                ButtonRow(
                    title = "① 打开页面（kotlin backed json）",
                    bgColor = Color(0xFF007AFF),
                    testTagName = "json_perf_open_kotlin_btn",
                    onClick = { ctx.openJsonPage(cppBacked = false) }
                )

                ButtonRow(
                    title = "② 打开页面（c++ backed json）",
                    bgColor = Color(0xFF5856D6),
                    testTagName = "json_perf_open_cpp_btn",
                    onClick = { ctx.openJsonPage(cppBacked = true) }
                )

                ButtonRow(
                    title = "③ + 30 KB",
                    bgColor = Color(0xFF34C759),
                    testTagName = "json_perf_add_30kb_btn",
                    onClick = { ctx.addBytes(INC_SMALL_BYTES, "30 KB") }
                )

                ButtonRow(
                    title = "④ + 300 KB",
                    bgColor = Color(0xFFFF9500),
                    testTagName = "json_perf_add_300kb_btn",
                    onClick = { ctx.addBytes(INC_MEDIUM_BYTES, "300 KB") }
                )

                ButtonRow(
                    title = "⑤ + 3 MB",
                    bgColor = Color(0xFFFF3B30),
                    testTagName = "json_perf_add_3mb_btn",
                    onClick = { ctx.addBytes(INC_LARGE_BYTES, "3 MB") }
                )

                // 状态/结果
                View {
                    attr {
                        margin(left = 16f, top = 24f, right = 16f, bottom = 16f)
                        backgroundColor(Color(0xFFF5F5F5))
                        borderRadius(8f)
                        padding(all = 16f)
                    }
                    Text {
                        attr {
                            text(ctx.statusText)
                            fontSize(14f)
                            color(Color(0xFF222222))
                            testTag("json_perf_status_text")
                        }
                    }
                }
            }
        }
    }

    // 根据目标字节数换算需要追加多少个 dyn_i 字段，并直接 put 到 payloadJson 上。
    private fun appendFieldsForBytes(targetDeltaBytes: Int) {
        val perField = if (bytesPerField > 0) bytesPerField else AVG_BYTES_PER_FIELD_FALLBACK
        val deltaFields = (targetDeltaBytes / perField).toInt().coerceAtLeast(1)
        appendFields(deltaFields)
    }

    // 在 payloadJson 上追加 deltaFields 个 dyn_i k/v，键值类型按索引轮换。
    private fun appendFields(deltaFields: Int) {
        val start = nextFieldIndex
        val end = start + deltaFields
        for (i in start until end) {
            val key = "$KEY_DYNAMIC_PREFIX$i"
            when (i % 4) {
                0 -> payloadJson.put(key, i.toLong())
                1 -> payloadJson.put(key, "val_$i")
                2 -> payloadJson.put(key, i % 2 == 0)
                else -> payloadJson.put(key, i * 0.5)
            }
        }
        nextFieldIndex = end
        fieldCount = nextFieldIndex
        payloadJson.put(KEY_FIELD_COUNT, fieldCount)
    }

    // 刷新 UI 展示的字节数（用真实 toString().length，字段数很大时会有一次序列化开销，
    // 但避免了「估算不准」，且体积展示才是这个 demo 的关键 UX）。
    private fun refreshMetrics() {
        payloadBytes = payloadJson.toString().length
    }

    private fun addBytes(deltaBytes: Int, label: String) {
        val operationStartTs = DateTime.currentTimestamp()
        val before = fieldCount
        val appendStartTs = DateTime.currentTimestamp()
        appendFieldsForBytes(deltaBytes)
        val appendEndTs = DateTime.currentTimestamp()
        val metricsStartTs = DateTime.currentTimestamp()
        refreshMetrics()
        val metricsEndTs = DateTime.currentTimestamp()
        val cppPrepareStartTs = DateTime.currentTimestamp()
        prepareCppPayload(reason = "add_$label")
        val cppPrepareEndTs = DateTime.currentTimestamp()
        val added = fieldCount - before
        statusText = "已追加 $label（+$added 字段）\n" +
            "当前字段数 = $fieldCount\n" +
            "当前体积   = ${formatBytes(payloadBytes)} ($payloadBytes 字节)\n" +
            "c++ backed 数据已预先准备"
        KLog.i(
            TAG,
            "addBytes: label=$label, addedFields=$added, " +
            "fieldCount=$fieldCount, payloadBytes=$payloadBytes, " +
            "appendMs=${appendEndTs - appendStartTs}, " +
            "serializeForMetricsMs=${metricsEndTs - metricsStartTs}, " +
            "prepareCppPayloadMs=${cppPrepareEndTs - cppPrepareStartTs}, " +
            "totalMs=${cppPrepareEndTs - operationStartTs}"
        )
    }

    /** Prepares a native-backed copy while the user is resizing the payload. */
    private fun prepareCppPayload(reason: String) {
        val prepareStartTs = DateTime.currentTimestamp()
        val stringifyStartTs = DateTime.currentTimestamp()
        val jsonText = payloadJson.toString()
        val stringifyEndTs = DateTime.currentTimestamp()
        val parseStartTs = DateTime.currentTimestamp()
        cppPayloadJson = JSONObject(jsonText)
        val parseEndTs = DateTime.currentTimestamp()
        KLog.i(
            TAG,
            "prepareCppPayload: reason=$reason, fieldCount=$fieldCount, " +
                "payloadBytes=${jsonText.length}, stringifyMs=${stringifyEndTs - stringifyStartTs}, " +
                "parseMs=${parseEndTs - parseStartTs}, totalMs=${parseEndTs - prepareStartTs}"
        )
    }

    private fun openJsonPage(cppBacked: Boolean) {
        val backing = if (cppBacked) BACKING_CPP else BACKING_KOTLIN
        val clickTs = DateTime.currentTimestamp()
        KLog.i(
            TAG,
            "trace: stage=source_click_enter, traceId=$clickTs, " +
                "fieldCount=$fieldCount, backing=$backing"
        )

        // The C++-backed JSONObject must remain read-only to keep its lazy
        // KRJSON tree. Build a small, Kotlin-backed route envelope that owns
        // click-local test metadata and embeds the already-prepared payload.
        val pageData = JSONObject().apply {
            put(KEY_CLICK_TS, clickTs)
            put(KEY_TIME, clickTs)
            put(KEY_BACKING, backing)
            put("payload", if (cppBacked) cppPayloadJson ?: payloadJson else payloadJson)
        }
        KLog.i(
            TAG,
            "trace: stage=source_payload_ready, traceId=$clickTs, " +
                "fieldCount=$fieldCount, payloadBytes=$payloadBytes, backing=$backing, " +
                "elapsedFromClickMs=${DateTime.currentTimestamp() - clickTs}"
        )
        statusText = "已发起跳转（$backing backed）\n" +
            "fieldCount = $fieldCount\n" +
            "JSON 体积 = ${formatBytes(payloadBytes)} ($payloadBytes 字节)\n" +
            "clickTs = $clickTs\n" +
            "请在子页面查看耗时"
        val routerOpenStartTs = DateTime.currentTimestamp()
        acquireModule<RouterModule>(RouterModule.MODULE_NAME).openPage(
            "JsonParsingResultPage",
            pageData
        )
        val routerOpenEndTs = DateTime.currentTimestamp()
        KLog.i(
            TAG,
                "trace: stage=router_openPage_return, traceId=$clickTs, backing=$backing, " +
                "routerCallMs=${routerOpenEndTs - routerOpenStartTs}, " +
                "elapsedFromClickMs=${routerOpenEndTs - clickTs}"
        )
    }
}

/** 人类可读的字节数格式化。 */
private fun formatBytes(bytes: Int): String {
    val absBytes = bytes.toLong()
    return when {
        absBytes >= BYTES_PER_MB -> {
            val mb = absBytes.toDouble() / BYTES_PER_MB
            "${((mb * 100).toInt() / 100.0)} MB"
        }
        absBytes >= BYTES_PER_KB -> {
            val kb = absBytes.toDouble() / BYTES_PER_KB
            "${((kb * 100).toInt() / 100.0)} KB"
        }
        else -> "$bytes B"
    }
}

/** 通用按钮行，避免 body 内重复模板代码。 */
private fun ViewContainer<*, *>.ButtonRow(
    title: String,
    bgColor: Color,
    testTagName: String,
    onClick: () -> Unit
) {
    View {
        attr {
            alignItemsCenter()
            marginTop(12f)
        }
        Button {
            attr {
                titleAttr {
                    text(title)
                    color(Color.WHITE)
                    fontSize(15f)
                }
                backgroundColor(bgColor)
                size(width = 300f, height = 48f)
                borderRadius(8f)
                testTag(testTagName)
            }
            event {
                click {
                    onClick()
                }
            }
        }
    }
}

/**
 * 子页面：从 pageData 中解析动态字段并展示
 *  - 展示 time 字段
 *  - 展示 fieldCount
 *  - 计算「点击 → 子页面 created 时机」的耗时，输出到 UI 与日志
 */
@Page("JsonParsingResultPage")
internal class JsonParsingResultPage : BasePager() {

    private var summaryText: String by observable("正在解析 pageData ...")

    // 记录页面进入 created 的时间戳（即 KMP 侧首次可以拿到 pageData 的时间点）
    private var pageCreatedTs: Long = 0L
    private var clickTs: Long = 0L

    private fun trace(stage: String, nowTs: Long = DateTime.currentTimestamp(), extra: String = "") {
        val elapsedFromClick = if (clickTs > 0L) nowTs - clickTs else -1L
        val elapsedFromCreated = if (pageCreatedTs > 0L) nowTs - pageCreatedTs else -1L
        KLog.i(
            TAG,
            "trace: stage=$stage, traceId=$clickTs, " +
                "elapsedFromClickMs=$elapsedFromClick, " +
                "elapsedFromResultCreatedMs=$elapsedFromCreated" +
                if (extra.isEmpty()) "" else ", $extra"
        )
    }

    override fun created() {
        super.created()
        pageCreatedTs = DateTime.currentTimestamp()
        trace("result_created_enter")

        val paramsReadStartTs = DateTime.currentTimestamp()
        val routeParams: JSONObject = pageData.params
        clickTs = routeParams.optLong(KEY_CLICK_TS, 0L)
        val params = routeParams.optJSONObject("payload") ?: JSONObject()
        val timeField = routeParams.optLong(KEY_TIME, 0L)
        val fieldCount = params.optInt(KEY_FIELD_COUNT, 0)
        val backing = routeParams.optString(KEY_BACKING, "")
        val paramsReadEndTs = DateTime.currentTimestamp()
        val cost = if (clickTs > 0L) pageCreatedTs - clickTs else -1L
        trace(
            "result_params_read",
            paramsReadEndTs,
            "paramsReadMs=${paramsReadEndTs - paramsReadStartTs}, " +
                "fieldCount=$fieldCount, backing=$backing, pageCreatedCostMs=$cost"
        )

        // 统计 pageData.params 里实际的 key 数量（动态字段）
        val keyCountStartTs = DateTime.currentTimestamp()
        // JSONObject.length() is the key-count API. On OHOS LazyJsonMap it
        // reads KRJSON's container size directly and does not enumerate keys
        // or build the Kotlin key index.
        val totalKeys = params.length()
        val keyCountEndTs = DateTime.currentTimestamp()
        trace(
            "result_key_count_done",
            keyCountEndTs,
            "keyCountMs=${keyCountEndTs - keyCountStartTs}, actualKeys=$totalKeys"
        )

        // 真实 JSON 体积（重新 toString）
        val serializeStartTs = DateTime.currentTimestamp()
        val realBytes = params.toString().length
        val serializeEndTs = DateTime.currentTimestamp()
        trace(
            "result_serialize_done",
            serializeEndTs,
            "serializeMs=${serializeEndTs - serializeStartTs}, realJsonBytes=$realBytes"
        )

        // 抽样几个动态字段做展示
        val sampleStartTs = DateTime.currentTimestamp()
        val samples = mutableListOf<String>()
        val keyList = params.keys().asSequence().filter { it.startsWith(KEY_DYNAMIC_PREFIX) }.toList()
        val sampleKeys = if (keyList.size <= 5) keyList else keyList.take(5)
        for (k in sampleKeys) {
            samples.add("$k = ${params.opt(k)}")
        }
        val sampleEndTs = DateTime.currentTimestamp()
        trace(
            "result_samples_done",
            sampleEndTs,
            "sampleMs=${sampleEndTs - sampleStartTs}, dynamicKeyCount=${keyList.size}"
        )

        val summaryBuildStartTs = DateTime.currentTimestamp()
        val sb = StringBuilder()
        sb.appendLine("===== JSON 解析结果 =====")
        sb.appendLine("time (动态字段) = $timeField")
        sb.appendLine("clickTimestampMs = $clickTs")
        sb.appendLine("pageCreatedMs    = $pageCreatedTs")
        sb.appendLine("耗时(click → created) = $cost ms")
        sb.appendLine("jsonBacking = $backing")
        sb.appendLine("声明 fieldCount = $fieldCount")
        sb.appendLine("params 实际 key 总数 = $totalKeys")
        sb.appendLine("真实 JSON 体积 = ${formatBytes(realBytes)} ($realBytes 字节)")
        sb.appendLine("动态字段抽样：")
        if (samples.isEmpty()) {
            sb.appendLine("  <无>")
        } else {
            samples.forEach { sb.appendLine("  $it") }
        }
        val summary = sb.toString()
        val summaryBuildEndTs = DateTime.currentTimestamp()
        trace(
            "result_summary_built",
            summaryBuildEndTs,
            "summaryBuildMs=${summaryBuildEndTs - summaryBuildStartTs}, summaryChars=${summary.length}"
        )

        val stateUpdateStartTs = DateTime.currentTimestamp()
        summaryText = summary
        val stateUpdateEndTs = DateTime.currentTimestamp()
        trace(
            "result_summary_state_assigned",
            stateUpdateEndTs,
            "stateAssignMs=${stateUpdateEndTs - stateUpdateStartTs}"
        )

        // 同时输出到日志，便于对比
        KLog.i(
            TAG,
            "parsed: traceId=$clickTs, backing=$backing, fieldCount=$fieldCount, actualKeys=$totalKeys, " +
                "realJsonBytes=$realBytes, time=$timeField, clickTs=$clickTs, createdTs=$pageCreatedTs, costMs=$cost"
        )
    }

    override fun viewWillLoad() {
        super.viewWillLoad()
        trace("result_view_will_load")
    }

    override fun viewDidLoad() {
        super.viewDidLoad()
        trace("result_view_did_load")
    }

    override fun viewDidLayout() {
        super.viewDidLayout()
        trace("result_view_did_layout")
    }

    override fun pageDidAppear() {
        super.pageDidAppear()
        trace("result_page_did_appear_ui_complete")
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.WHITE)
            }

            NavBar {
                attr {
                    title = "JSON 解析结果"
                }
            }

            View {
                attr {
                    backgroundColor(Color(0xFFF5F5F5))
                    borderRadius(12f)
                    padding(16f)
                    margin(16f)
                }

                Text {
                    attr {
                        text(ctx.summaryText)
                        fontSize(13f)
                        color(Color(0xFF222222))
                        testTag("json_perf_result_text")
                    }
                }
            }
        }
    }
}
