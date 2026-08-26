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
import com.tencent.kuikly.core.directives.vforIndex
import com.tencent.kuikly.core.log.KLog
import com.tencent.kuikly.core.manager.BridgeManager
import com.tencent.kuikly.core.nvi.serialization.json.JSONArray
import com.tencent.kuikly.core.nvi.serialization.json.JSONException
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.reactive.handler.observableList
import com.tencent.kuikly.core.timer.setTimeout
import com.tencent.kuikly.core.views.List
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.core.views.compose.Button
import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.demo.pages.demo.base.NavBar

/**
 * 被测的 tokener。各平台指向该平台桥接层实际使用的实现：ohosArm64 走
 * `JSONTokenerOhos`（K/N 专项优化版），其余平台走 commonMain 的 `JSONTokener`。
 */
expect fun parseWithTokener(json: String): Any?

/**
 * Native 侧已完成 parse 后，仅计量 Kotlin 包装耗时。
 * OHOS：KRJSON owner → fromJsonOwnerAny；不支持返回 -1。
 */
expect fun measureNativeParsedWrapNanos(json: String): Long

/**
 * 结构化入桥 A/B（C++→Kotlin）：以下四个 expect 用一棵**已建好的 KRJSON owner** 为起点，
 * 排除两端共有的建树成本（`toJson`），只对比 Kotlin 收口方式。
 * - before 臂：`KRJSONDump` → Kotlin String → **Abstract** 全量 parse（分支前语义）
 * - after 臂：`fromJsonOwnerAny` → 惰性 `LazyJsonMap/List`（分支后语义）
 * 不支持的平台：[bridgeSupported] 返回 false，其余为空操作。
 */
expect fun bridgeSupported(): Boolean

/** 由 JSON 文本建一棵自持有 KRJSON owner；不支持返回 0。计时外调用。 */
expect fun bridgeBuildOwner(json: String): Long

expect fun bridgeReleaseOwner(owner: Long)

/** before 臂：owner root → 打印字符串 → Abstract parse，得到根对象。 */
expect fun bridgeBeforeArmRoot(owner: Long): Any?

/** after 臂：owner → 惰性壳包装，得到根对象。 */
expect fun bridgeAfterArmRoot(owner: Long): Any?

/** 结构化入桥消费模式（对应真实业务对入参的使用方式）。 */
enum class ConsumeMode { ROOT, READ5, WALK, PUT, TOSTRING }

/**
 * 平台桥接自检：验证该平台 callKotlin 入参的结构化转换（iOS Foundation 容器、
 * OHOS `NATIVE_JSON` cJSON 树）。返回以 `OK` / `FAIL` 开头的说明；没有原生惰性容器的
 * 平台返回 `OK (n/a)`。
 */
expect fun platformBridgeCheck(): String

// Platform-specific functions for memory measurement
expect fun collectGarbage()
expect fun getAllocatedBytes(): Long
expect fun getPlatformName(): String
expect fun getTimeNanosPlatform(): Long

/**
 * 已完成的 GC 轮次计数，取不到时返回 -1。
 *
 * 用于替代 [getAllocatedBytes] 判断分配量：后者是「当前已分配 Page 总大小」这一瞬时驻留水位，
 * 由 GC 触发时机决定，与总分配量无单调关系（详见 kn_json_perf_conversation.md §11）。
 * GC 在堆增长越过阈值时触发，故固定 workload 下 epoch 增量 ≈ 总分配量 / 目标堆大小。
 */
expect fun getGcEpoch(): Long

/** 最近一次 GC 结束后各内存池 `totalObjectsSizeBytes` 之和；不可用时返回 -1。 */
expect fun getHeapObjectBytes(): Long

/** 最近一次 GC 各内存池 `sweptCount` 之和；不可用时返回 -1。 */
expect fun getLastSweptCount(): Long

/** 调试用：dump 最近一次 GC 的池名与统计，确认定制收集器是否填充这些字段。 */
expect fun getGcPoolsDebug(): String

/**
 * 暂停 / 恢复 GC。用于把 STW 排除在解析计时之外。
 * 不支持的平台为空操作。计时迭代内暂停，迭代之间恢复，避免长时间挂起导致 OOM。
 */
expect fun setGcSuspended(suspended: Boolean)

data class TestMetrics(
    val name: String,
    val memBefore: Long,
    val memAfter: Long,
    val gcEpochDelta: Long,
    val sweptCount: Long,
    val retainedBytes: Long,
    val avgNanos: Long,
    val medianNanos: Long,
    val p99Nanos: Long,
    val throughput: Double,
    /** Native 已 parse 完成后，仅 Kotlin 包装 median；不支持为 -1。 */
    val wrapMedianNanos: Long = -1L,
    /** 对 parse 结果全量 walk k/v 的 median（惰性路径含首次物化）。 */
    val walkMedianNanos: Long = -1L
)

/**
 * Baseline JSON platform test page: auto-runs conformance + performance,
 * exposes PASS/FAIL via self-DSL [testTag], and emits machine-comparable
 * [KLog] lines under tag `JsonPlatformTest`.
 */
@Page("JsonPlatformTestPage")
internal class JsonPlatformTestPage : BasePager() {

    private var isRunning by observable(false)
    private var isRunningAll by observable(false)
    private var statusText by observable("Preparing test JSON...")
    private var resultLines by observableList<String>()
    private var memBeforeText by observable("Mem before: --")
    private var memAfterText by observable("Mem after: --")
    private var speedText by observable("Avg parse: --")
    private var tokenerText by observable("Tokener: --")
    /** Machine/E2E-readable verdict texts (paired with testTag attributes). */
    private var conformanceResultText by observable("CONFORMANCE: PENDING")
    private var perfResultText by observable("PERF: PENDING")
    private var bridgeResultText by observable("BRIDGE: PENDING")
    private var overallResultText by observable("OVERALL: PENDING")
    private val metricsList = mutableListOf<TestMetrics>()
    private var dumpedGcPools = false
    private var autoStarted = false
    private var benchBridge = false
    private var bridgeStringSink: Int = 0
    private var bridgeFinished = false
    private var conformancePassed = false
    private var perfPassed = false
    private var bridgePassed = false
    /** 收到过的页面生命周期事件名（走 UPDATE_INSTANCE 桥接路径）。 */
    private val observedPagerEvents = mutableListOf<String>()
    /** 生命周期事件数据不可读 / 不可再解析时记录原因。 */
    private val lifecycleFailures = mutableListOf<String>()
    // Retained 测量时把解析结果钉在堆对象上，防止 Release 下局部变量被优化掉导致 GC 收走树。
    private var retainedProbe: Any? = null
    private val retainedAnchor = arrayOfNulls<Any>(1)

    // Pre-built fixtures — generated once in created(), reused by every run.
    private lateinit var jsonSmall1KB: String
    private lateinit var jsonMedium10KB: String
    private lateinit var jsonLarge100KB: String
    private lateinit var jsonHuge1MB: String
    private lateinit var jsonDeepNest: String
    private lateinit var jsonArray1000: String
    private lateinit var jsonEscaped: String

    override fun created() {
        super.created()
        // 结构化入桥 A/B：aa start --ps bench bridge
        benchBridge = pageData.params.optString("bench", "") == "bridge"
        jsonSmall1KB = generateJson(1024)
        jsonMedium10KB = generateJson(10240)
        jsonLarge100KB = generateJson(102400)
        jsonHuge1MB = generateJson(1048576)
        jsonDeepNest = generateDeepNestedJson(50)
        jsonArray1000 = generateLargeArray(1000)
        jsonEscaped = generateEscapedStringJson()
        tokenerText = "Tokener: ${currentTokenerName()}"
        statusText = "Ready. Auto-run starts on appear."
        logMachine("PERF_MODE tokener=${currentTokenerName()} benchBridge=$benchBridge")
    }

    /**
     * 生命周期事件走 `UPDATE_INSTANCE`：数据可能是历史 JSON 字符串，也可能是平台结构化
     * 容器包成的惰性 [JSONObject]。
     * 这里逐个事件校验「可读 + 可再序列化 + 可再解析」，任一路退化都会让 BRIDGE 判定失败。
     */
    override fun onReceivePagerEvent(pagerEvent: String, eventData: JSONObject) {
        super.onReceivePagerEvent(pagerEvent, eventData)
        observedPagerEvents.add(pagerEvent)
        try {
            val text = eventData.toString()
            if (!text.startsWith("{") || !text.endsWith("}")) {
                lifecycleFailures.add("$pagerEvent: bad serialized text=$text")
                return
            }
            val reparsed = JSONObject(text)
            if (reparsed.length() != eventData.length()) {
                lifecycleFailures.add(
                    "$pagerEvent: roundtrip length ${reparsed.length()} != ${eventData.length()}"
                )
            }
            // 迭代惰性容器的 entries，确保 key 列表与 length 自洽
            if (eventData.keySet().size != eventData.length()) {
                lifecycleFailures.add("$pagerEvent: keySet ${eventData.keySet().size} != ${eventData.length()}")
            }
        } catch (e: Throwable) {
            lifecycleFailures.add("$pagerEvent: ${e.message}")
        }
    }

    override fun pageDidAppear() {
        super.pageDidAppear()
        if (autoStarted) return
        autoStarted = true
        // Defer so the first frame with testTags is committed before heavy work.
        this.setTimeout(timeout = 50) {
            startAutoPipeline()
        }
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.WHITE)
                flexDirectionColumn()
            }

            NavBar {
                attr {
                    title = "JSON Platform Test"
                }
            }

            Text {
                attr {
                    text("JSON Platform Test")
                    fontSize(20f)
                    fontWeightBold()
                    color(Color(0xFF333333))
                    marginTop(10f)
                    marginLeft(16f)
                    testTag("json_platform_title")
                }
            }

            Text {
                attr {
                    text("Platform: ${getPlatformName()}")
                    fontSize(12f)
                    color(Color(0xFF999999))
                    marginLeft(16f)
                    marginTop(4f)
                    testTag("json_platform_name")
                }
            }

            // E2E-facing verdicts — text is the machine-readable PASS/FAIL token.
            Text {
                attr {
                    text(ctx.conformanceResultText)
                    fontSize(15f)
                    fontWeightBold()
                    color(verdictColor(ctx.conformanceResultText))
                    marginLeft(16f)
                    marginTop(10f)
                    testTag("json_platform_conformance_result")
                }
            }
            Text {
                attr {
                    text(ctx.perfResultText)
                    fontSize(15f)
                    fontWeightBold()
                    color(verdictColor(ctx.perfResultText))
                    marginLeft(16f)
                    marginTop(4f)
                    testTag("json_platform_perf_result")
                }
            }
            Text {
                attr {
                    text(ctx.bridgeResultText)
                    fontSize(15f)
                    fontWeightBold()
                    color(verdictColor(ctx.bridgeResultText))
                    marginLeft(16f)
                    marginTop(4f)
                    testTag("json_platform_bridge_result")
                }
            }
            Text {
                attr {
                    text(ctx.overallResultText)
                    fontSize(16f)
                    fontWeightBold()
                    color(verdictColor(ctx.overallResultText))
                    marginLeft(16f)
                    marginTop(4f)
                    testTag("json_platform_overall_result")
                }
            }

            Text {
                attr {
                    text(ctx.statusText)
                    fontSize(14f)
                    color(if (ctx.isRunning) Color(0xFFFF8800) else Color(0xFF666666))
                    marginLeft(16f)
                    marginTop(12f)
                    marginBottom(8f)
                    testTag("json_platform_status")
                }
            }

            // Memory summary
            View {
                attr {
                    flexDirectionRow()
                    marginLeft(16f)
                    marginTop(4f)
                }
                Text {
                    attr {
                        text(ctx.memBeforeText)
                        fontSize(13f)
                        color(Color(0xFF999999))
                        marginRight(20f)
                    }
                }
                Text {
                    attr {
                        text(ctx.memAfterText)
                        fontSize(13f)
                        color(Color(0xFF999999))
                    }
                }
            }

            // Speed summary
            View {
                attr {
                    flexDirectionRow()
                    marginLeft(16f)
                    marginTop(2f)
                }
                Text {
                    attr {
                        text(ctx.speedText)
                        fontSize(13f)
                        color(Color(0xFF999999))
                    }
                }
            }

            // Active tokener (parse 实现开关的当前状态)
            View {
                attr {
                    flexDirectionRow()
                    marginLeft(16f)
                    marginTop(2f)
                }
                Text {
                    attr {
                        text(ctx.tokenerText)
                        fontSize(13f)
                        color(Color(0xFF3F51B5))
                    }
                }
            }

            // "Run All" button (prominent, full-width)
            View {
                attr {
                    marginTop(12f)
                    marginLeft(8f)
                    marginRight(8f)
                }
                ActionButton("▶ RUN ALL TESTS", Color(0xFF4CAF50), ctx.isRunning) {
                    ctx.runAllTests()
                }
            }

            // 一键一致性用例集：对本平台所有 tokener 实现跑同一套用例
            View {
                attr {
                    marginTop(8f)
                    marginLeft(8f)
                    marginRight(8f)
                }
                ActionButton("✔ RUN CONFORMANCE SUITE", Color(0xFF7E57C2), ctx.isRunning) {
                    ctx.runConformanceSuite()
                }
            }

            // 桥接 / 生命周期回归：结构化 callKotlin 入参与事件数据
            View {
                attr {
                    marginTop(8f)
                    marginLeft(8f)
                    marginRight(8f)
                }
                ActionButton("⇄ RUN BRIDGE CHECKS", Color(0xFF00897B), ctx.isRunning) {
                    ctx.runBridgeChecks()
                }
            }

            // Individual test buttons
            View {
                attr {
                    flexDirectionRow()
                    flexWrapWrap()
                    marginTop(8f)
                    marginLeft(8f)
                    marginRight(8f)
                }

                ActionButton("1KB x100") { ctx.runTest("Small (1KB)", ctx.jsonSmall1KB, 100) }
                ActionButton("10KB x100") { ctx.runTest("Medium (10KB)", ctx.jsonMedium10KB, 100) }
                ActionButton("100KB x50") { ctx.runTest("Large (100KB)", ctx.jsonLarge100KB, 50) }
                ActionButton("1MB x25") { ctx.runTest("Huge (1MB)", ctx.jsonHuge1MB, 25) }
                ActionButton("DeepNest x200") { ctx.runTest("Deep (50lv)", ctx.jsonDeepNest, 200, "depth: 50") }
                ActionButton("Array 1000x100") { ctx.runTest("Array 1000", ctx.jsonArray1000, 100, "items: 1000") }
                ActionButton("EscapedStr x200") { ctx.runTest("Escaped Str", ctx.jsonEscaped, 200) }
                ActionButton("GC Collect") { ctx.runGC() }
                ActionButton("Switch Tokener", Color(0xFF3F51B5), ctx.isRunning) { ctx.switchTokener() }
            }

            // Results list
            List {
                attr {
                    marginTop(16f)
                    marginLeft(16f)
                    marginRight(16f)
                    flex(1f)
                }
                vforIndex({ ctx.resultLines }) { line, index, _ ->
                    Text {
                        attr {
                            text(line)
                            fontSize(13f)
                            color(if (index < 3) Color(0xFF333333) else Color(0xFF666666))
                            fontFamily("Courier")
                            marginBottom(2f)
                        }
                    }
                }
            }
        }
    }

    // ============================================================
    // Test runners
    // ============================================================

    private fun commitResult(lines: List<String>, doneStatus: String) {
        if (!isRunningAll) {
            isRunning = false
            statusText = doneStatus
            resultLines.clear()
            resultLines.addAll(lines)
        } else {
            resultLines.addAll(lines)
        }
    }

    // KLog 内部依赖 BridgeManager.currentPageId，而该值在 setTimeout 异步回调链中为空，
    // 会导致日志静默丢失。这里临时把 currentPageId 设为当前 pagerId，确保 summary 日志能打到 native。
    @Suppress("DEPRECATION")
    private fun logSummary(msg: String) {
        val lastPageId = BridgeManager.currentPageId
        BridgeManager.currentPageId = pagerId
        try {
            KLog.i("JsonPlatformTest", msg)
        } finally {
            BridgeManager.currentPageId = lastPageId
        }
    }

    /** Stable key=value lines for log scraping / cross-platform compare. */
    private fun logMachine(line: String) {
        logSummary(line)
    }

    private fun startAutoPipeline() {
        if (isRunning) return
        overallResultText = "OVERALL: RUNNING"
        conformanceResultText = "CONFORMANCE: RUNNING"
        perfResultText = "PERF: PENDING"
        bridgeResultText = "BRIDGE: RUNNING"
        statusText = "Auto: running conformance..."
        logMachine("AUTO_START platform=${getPlatformName()} tokener=${currentTokenerName()}")
        val conformanceOk = runConformanceSuite(fromAuto = true)
        val bridgeOk = runBridgeChecks(fromAuto = true)
        if (!conformanceOk || !bridgeOk) {
            perfResultText = "PERF: SKIPPED"
            overallResultText = "OVERALL: FAIL"
            logMachine("OVERALL status=FAIL reason=${if (!conformanceOk) "conformance" else "bridge"}")
            return
        }
        if (benchBridge) {
            perfResultText = "PERF: RUNNING"
            statusText = "Auto: running structured-inbound A/B..."
            runBridgeMatrix()
            return
        }
        perfResultText = "PERF: RUNNING"
        statusText = "Auto: running performance suite..."
        runAllTests(fromAuto = true)
    }

    private fun publishOverall() {
        val ok = conformancePassed && perfPassed && bridgePassed
        overallResultText = if (ok) "OVERALL: PASS" else "OVERALL: FAIL"
        logMachine(
            "OVERALL status=${if (ok) "PASS" else "FAIL"}" +
                " conformance=${if (conformancePassed) "PASS" else "FAIL"}" +
                " perf=${if (perfPassed) "PASS" else "FAIL"}" +
                " bridge=${if (bridgePassed) "PASS" else "FAIL"}"
        )
    }

    /**
     * 桥接 / 生命周期回归：验证 `createInstance`、`UPDATE_INSTANCE` 事件与平台结构化
     * 入参转换这三条路径在本平台都拿到可用的 JSON 数据。
     *
     * 覆盖点：pageData（可能来自 JSON 字符串 / `NSDictionary` / cJSON `NATIVE_JSON`）
     * 的读取、迭代、读后写物化、再序列化；生命周期事件数据的可读性；以及平台侧惰性容器
     * 自检（含二进制透传与数组根）。
     */
    private fun runBridgeChecks(fromAuto: Boolean = false): Boolean {
        if (isRunning && !fromAuto) return false
        if (!fromAuto) isRunning = true
        val lines = mutableListOf<String>()
        val failures = mutableListOf<String>()
        var passed = false
        try {
            lines.add("=== Bridge / Lifecycle Checks ===")

            // 1) createInstance 下发的 pageData 必须解析到位（平台字段由原始 JSON 派生）
            if (pageData.platform.isEmpty()) {
                failures.add("pageData.platform empty")
            }
            if (pageData.pageViewWidth <= 0f) {
                failures.add("pageData.pageViewWidth=${pageData.pageViewWidth}")
            }
            lines.add(
                "  pageData: platform=${pageData.platform} rootW=${pageData.pageViewWidth}" +
                    " density=${pageData.density}"
            )

            // 2) params 子对象：迭代 + 读后写物化 + 再序列化
            val params = pageData.params
            if (params.keySet().size != params.length()) {
                failures.add("params keySet ${params.keySet().size} != length ${params.length()}")
            }
            val paramsKeysBeforeWrite = params.length()
            params.put("__bridge_probe", 1)
            if (params.optInt("__bridge_probe", -1) != 1) {
                failures.add("params put/read failed")
            }
            if (!params.has("__bridge_probe")) {
                failures.add("params has() after put failed")
            }
            if (params.length() != paramsKeysBeforeWrite + 1) {
                failures.add("params length after put ${params.length()} != ${paramsKeysBeforeWrite + 1}")
            }
            try {
                val reparsed = JSONObject(params.toString())
                if (reparsed.length() != params.length()) {
                    failures.add("params roundtrip length ${reparsed.length()} != ${params.length()}")
                }
                if (reparsed.optInt("__bridge_probe", -1) != 1) {
                    failures.add("params roundtrip lost written key")
                }
            } catch (e: Throwable) {
                failures.add("params roundtrip: ${e.message}")
            }
            lines.add("  params: keys=${params.length()}")

            // 3) 生命周期事件（UPDATE_INSTANCE）：至少收到 viewDidAppear，且数据都可读
            if (observedPagerEvents.isEmpty()) {
                failures.add("no pager event received")
            }
            failures.addAll(lifecycleFailures)
            lines.add("  pagerEvents: ${observedPagerEvents.joinToString(",")}")

            // 4) 平台惰性容器自检（iOS Foundation / OHOS cJSON）
            val platformResult = try {
                platformBridgeCheck()
            } catch (e: Throwable) {
                "FAIL platform check threw: ${e.message}"
            }
            if (!platformResult.startsWith("OK")) {
                failures.add(platformResult)
            }
            lines.add("  platform: $platformResult")

            passed = failures.isEmpty()
            bridgePassed = passed
            bridgeResultText = if (passed) "BRIDGE: PASS" else "BRIDGE: FAIL"
            if (passed) {
                lines.add("  RESULT: PASS")
            } else {
                lines.add("  RESULT: FAIL — ${failures.size} failures")
                for (failure in failures) {
                    lines.add("  x $failure")
                }
            }
            lines.add("")
            lines.add("  -----------------")
            logMachine(
                "BRIDGE status=${if (passed) "PASS" else "FAIL"} failures=${failures.size}" +
                    " events=${observedPagerEvents.size}"
            )
            for (line in lines) {
                logSummary(line)
            }
        } catch (e: Exception) {
            bridgePassed = false
            bridgeResultText = "BRIDGE: FAIL"
            lines.add("  ERROR: ${e.message}")
            logMachine("BRIDGE status=FAIL failures=1 error=${e.message}")
        } finally {
            if (!fromAuto) {
                commitResult(lines, if (passed) "Bridge PASS" else "Bridge FAIL")
            } else {
                resultLines.addAll(lines)
            }
        }
        return passed
    }

    private fun runTest(name: String, json: String, iterations: Int, sizeExtra: String? = null) {
        if (isRunning && !isRunningAll) return
        if (!isRunningAll) isRunning = true
        val lines = mutableListOf<String>()

        try {
            val actualSize = json.length
            val sizeLine = if (sizeExtra != null) {
                "  JSON size: ${formatBytes(actualSize)} ($sizeExtra)"
            } else {
                "  JSON size: ${formatBytes(actualSize)}"
            }
            lines.add("=== Test: $name ===")
            lines.add(sizeLine)
            lines.add("  Iterations: $iterations")
            lines.add("")
            statusText = "Running: $name..."
            runBenchmark(json, iterations, lines, if (isRunningAll) name else null)
        } catch (e: Exception) {
            lines.add("  ERROR: ${e.message}")
        } finally {
            commitResult(lines, "Done: $name")
        }
    }

    private fun runGC() {
        if (isRunning && !isRunningAll) return
        if (!isRunningAll) isRunning = true
        val lines = mutableListOf<String>()
        try {
            lines.add("=== GC Collect ===")
            val memBefore = getAllocatedBytes()
            memBeforeText = "Mem before GC: ${formatBytes(memBefore.toInt())}"
            lines.add("  Before GC: ${formatBytes(memBefore.toInt())}")

            val startNanos = getTimeNanosPlatform()
            collectGarbage()
            val elapsed = getTimeNanosPlatform() - startNanos

            val memAfter = getAllocatedBytes()
            memAfterText = "Mem after GC: ${formatBytes(memAfter.toInt())}"
            lines.add("  After GC:  ${formatBytes(memAfter.toInt())}")
            lines.add("  Freed:     ${formatBytes((memBefore - memAfter).toInt())}")
            lines.add("  GC time:   ${formatNanos(elapsed)}")
        } catch (e: Exception) {
            lines.add("  ERROR: ${e.message}")
        } finally {
            commitResult(lines, "GC done")
        }
    }

    /**
     * 一键跑解析 / 序列化 / CRUD 一致性用例集。ohos 上若有多份 tokener 会对各实现
     * 各跑一遍并做逐行差分，其余平台只有一份实现，只做绝对校验。
     * @return true if conformance passed
     */
    private fun runConformanceSuite(fromAuto: Boolean = false): Boolean {
        if (isRunning && !fromAuto) return false
        if (!fromAuto) isRunning = true
        val lines = mutableListOf<String>()
        var status = "Conformance done"
        var passed = false
        try {
            val report = JsonConformance.run()
            lines.add("=== JSON Conformance Suite ===")
            lines.add("  Variants: ${report.variantNames.joinToString(" | ")}")
            lines.addAll(report.summary)
            lines.add("")
            passed = report.passed
            conformancePassed = passed
            if (passed) {
                lines.add("  RESULT: PASS — ${report.checks} checks")
                status = "Conformance PASS (${report.checks} checks)"
                conformanceResultText = "CONFORMANCE: PASS"
            } else {
                lines.add("  RESULT: FAIL — ${report.failures.size} failures / ${report.checks} checks")
                status = "Conformance FAIL (${report.failures.size})"
                conformanceResultText = "CONFORMANCE: FAIL"
                val shown = if (report.failures.size > MAX_SHOWN_FAILURES) {
                    report.failures.subList(0, MAX_SHOWN_FAILURES)
                } else {
                    report.failures
                }
                for (failure in shown) {
                    lines.add("  x $failure")
                }
                if (report.failures.size > shown.size) {
                    lines.add("  ... ${report.failures.size - shown.size} more (see log)")
                }
            }
            lines.add("")
            lines.add("  -----------------")
            logMachine(
                "CONFORMANCE status=${if (passed) "PASS" else "FAIL"}" +
                    " checks=${report.checks} failures=${report.failures.size}" +
                    " variants=${report.variantNames.joinToString(",")}"
            )
            for (line in lines) {
                logSummary(line)
            }
            // 失败详情全量进日志，UI 只展示前 N 条
            if (!report.passed) {
                for (failure in report.failures) {
                    logSummary("FAIL $failure")
                }
            }
        } catch (e: Exception) {
            lines.add("  ERROR: ${e.message}")
            status = "Conformance ERROR"
            conformancePassed = false
            conformanceResultText = "CONFORMANCE: FAIL"
            logMachine("CONFORMANCE status=FAIL checks=0 failures=1 error=${e.message}")
            logSummary("Conformance ERROR: ${e.message}")
        } finally {
            tokenerText = "Tokener: ${currentTokenerName()}"
            if (!fromAuto) {
                commitResult(lines, status)
            } else {
                resultLines.clear()
                resultLines.addAll(lines)
                statusText = status
            }
        }
        return passed
    }

    /** 切换 JSONEngine 实际使用的 tokener（仅 ohos 有两份实现，其余平台为空操作）。 */
    private fun switchTokener() {
        if (isRunning) return
        val variants = tokenerVariants()
        if (variants.size < 2) {
            tokenerText = "Tokener: ${currentTokenerName()} (only one impl)"
            return
        }
        val current = currentTokenerName()
        val next = variants[(variants.indexOfFirst { it.name == current } + 1) % variants.size]
        selectTokener(next.name)
        tokenerText = "Tokener: ${currentTokenerName()}"
        statusText = "Switched tokener to ${currentTokenerName()}"
        logSummary("Switched tokener to ${currentTokenerName()}")
    }

    private fun runBenchmark(json: String, iterations: Int, lines: MutableList<String>, metricsName: String? = null) {
        // Warm-up parse
        try { parseWithTokener(json) } catch (_: Exception) {}

        // GC before measurement（OHOS auto-run 跳过：sync GC 会弄坏后续 setTimeout 队列）
        val skipGcHelpers = isRunningAll && getPlatformName().contains("OHOS")
        if (!skipGcHelpers) {
            try { collectGarbage() } catch (_: Exception) {}
        }
        if (!dumpedGcPools) {
            dumpedGcPools = true
            val pools = getGcPoolsDebug()
            lines.add("  GC pools: $pools")
            logSummary("GC pools: $pools")
        }
        val memBefore = try { getAllocatedBytes() } catch (_: Exception) { -1L }

        // Time each iteration with GC suspended, then keep only samples the GC did not
        // enter. suspendGCToggle is best-effort: the runtime resumes suspended GC at
        // safepoints, and parsing hits safepoints constantly, so contaminated samples
        // still occur — they are excluded from statistics rather than retried.
        val rawDurations = LongArray(iterations)
        val cleanFlags = BooleanArray(iterations)
        var gcHitsDuringTimed = 0
        for (i in 0 until iterations) {
            val epochBefore = try { getGcEpoch() } catch (_: Exception) { -1L }
            val elapsed: Long
            try {
                // OHOS auto-run：跳过 GC.suspend——suspend/resume 后 setTimeout 链会断。
                if (!skipGcHelpers) {
                    setGcSuspended(true)
                }
                val startIter = getTimeNanosPlatform()
                try {
                    val result = parseWithTokener(json)
                    if (result == null) {
                        lines.add("  WARN: parsed null at iteration $i")
                    }
                } catch (e: JSONException) {
                    lines.add("  ERROR at iteration $i: ${e.message}")
                    return
                }
                elapsed = getTimeNanosPlatform() - startIter
            } finally {
                if (!skipGcHelpers) {
                    setGcSuspended(false)
                }
            }
            val epochAfter = try { getGcEpoch() } catch (_: Exception) { -1L }
            val clean = epochBefore < 0 || epochAfter <= epochBefore
            if (!clean) gcHitsDuringTimed++
            rawDurations[i] = elapsed
            cleanFlags[i] = clean
        }

        var cleanCount = 0
        for (f in cleanFlags) if (f) cleanCount++
        // Fall back to all samples if GC contaminated everything, so stats stay defined.
        val useClean = cleanCount >= 3
        val sampleCount = if (useClean) cleanCount else iterations
        val durations = LongArray(sampleCount)
        var w = 0
        for (i in 0 until iterations) {
            if (!useClean || cleanFlags[i]) {
                durations[w++] = rawDurations[i]
            }
        }
        var totalNanos = 0L
        for (d in durations) totalNanos += d

        val memAfter = try { getAllocatedBytes() } catch (_: Exception) { -1L }
        val memDelta = if (memBefore >= 0 && memAfter >= 0) memAfter - memBefore else -1
        // Iterations the GC entered; those samples are excluded from the statistics below.
        val gcEpochDelta = gcHitsDuringTimed.toLong()
        if (gcHitsDuringTimed > 0) {
            lines.add("  GC entered $gcHitsDuringTimed/$iterations iters; stats over $sampleCount clean samples")
        }
        if (!useClean) {
            lines.add("  WARN: too few clean samples ($cleanCount); stats include GC-contaminated iters")
            logSummary("WARN: $metricsName too few clean samples ($cleanCount/$iterations)")
        }

        // Untimed pass / retained：auto-run on OHOS 同样跳过（见 runBenchmark）。
        val sweptCount = if (skipGcHelpers) -1L else measureSweptChurn(json, iterations, lines)
        val retainedBytes = if (skipGcHelpers) -1L else measureRetainedBytes(json, lines)

        // Statistics
        durations.sort()
        val minNanos = durations.first()
        val maxNanos = durations.last()
        val avgNanos = totalNanos / sampleCount
        val medianNanos = medianOfSorted(durations)
        val p95Idx = ((sampleCount.toDouble() * 0.95).toLong().coerceAtMost((sampleCount - 1).toLong())).toInt()
        val p99Idx = ((sampleCount.toDouble() * 0.99).toLong().coerceAtMost((sampleCount - 1).toLong())).toInt()
        val p95Nanos = durations[p95Idx]
        val p99Nanos = durations[p99Idx]

        val seconds = totalNanos.toDouble() / 1_000_000_000.0
        val throughput = if (seconds > 0) sampleCount.toDouble() / seconds else 0.0

        // Native 已解析 → Kotlin 包装（OHOS cJSON）；其它平台 -1。
        // wrap/walk 用较少样本，避免 OHOS 预挂 timer 槽超时叠跑。
        val sideIters = when {
            json.length >= 500_000 -> minOf(iterations, 5)
            json.length >= 50_000 -> minOf(iterations, 10)
            else -> minOf(iterations, 20)
        }
        val wrapMedianNanos = measureWrapOnlyMedian(json, sideIters, lines)
        // Parse 后全量 walk k/v（惰性路径含首次物化）。
        val walkMedianNanos = measureWalkAllMedian(json, sideIters, lines)

        lines.add("  --- Parse Time (GC-free samples only: $sampleCount/$iterations) ---")
        lines.add("  Total:      ${formatNanos(totalNanos)}")
        lines.add("  Avg:        ${formatNanos(avgNanos)}")
        lines.add("  Min:        ${formatNanos(minNanos)}")
        lines.add("  Max:        ${formatNanos(maxNanos)}")
        lines.add("  Median:     ${formatNanos(medianNanos)}")
        lines.add("  P95:        ${formatNanos(p95Nanos)}")
        lines.add("  P99:        ${formatNanos(p99Nanos)}")
        lines.add("  Throughput: ${formatDouble(throughput, 1)} parses/sec")
        lines.add("  GC hits:    $gcEpochDelta (excluded from stats)")
        if (wrapMedianNanos >= 0) {
            lines.add("  Wrap-only:  ${formatNanos(wrapMedianNanos)} (after native parse)")
        }
        if (walkMedianNanos >= 0) {
            lines.add("  Walk-all:   ${formatNanos(walkMedianNanos)} (full k/v visit)")
        }
        speedText = "Avg: ${formatNanos(avgNanos)} | Median: ${formatNanos(medianNanos)} | P99: ${formatNanos(p99Nanos)}"

        if (memDelta >= 0) {
            val perParse = memDelta / iterations
            lines.add("")
            lines.add("  --- Memory (page watermark; NOT allocation volume) ---")
            lines.add("  Before:     ${formatBytes(memBefore.toInt())}")
            lines.add("  After:      ${formatBytes(memAfter.toInt())}")
            lines.add("  Delta:      ${formatBytes(memDelta.toInt())}")
            lines.add("  Per parse:  ${formatBytes(perParse.toInt())}")
            memBeforeText = "Mem before: ${formatBytes(memBefore.toInt())}"
            memAfterText = "Mem after: ${formatBytes(memAfter.toInt())}"
        }

        if (gcEpochDelta >= 0) {
            lines.add("  GC epochs:  $gcEpochDelta")
        }
        if (sweptCount >= 0) {
            lines.add("  Swept objs: $sweptCount  (${sweptCount / iterations}/parse)")
        }
        if (retainedBytes >= 0) {
            lines.add("  Retained:   ${formatBytes(retainedBytes.toInt())}")
        }

        lines.add("")
        lines.add("  -----------------")

        if (metricsName != null && isRunningAll) {
            metricsList.add(
                TestMetrics(
                    metricsName, memBefore, memAfter, gcEpochDelta,
                    sweptCount, retainedBytes,
                    avgNanos, medianNanos, p99Nanos, throughput,
                    wrapMedianNanos, walkMedianNanos
                )
            )
        }
    }

    /** Native 侧已 parse 完成后，仅计量 Kotlin 包装；不支持返回 -1。 */
    private fun measureWrapOnlyMedian(json: String, iterations: Int, lines: MutableList<String>): Long {
        val probe = try {
            measureNativeParsedWrapNanos(json)
        } catch (_: Exception) {
            -1L
        }
        if (probe < 0) {
            return -1L
        }
        val samples = LongArray(iterations)
        var ok = 0
        for (i in 0 until iterations) {
            val ns = try {
                measureNativeParsedWrapNanos(json)
            } catch (_: Exception) {
                -1L
            }
            if (ns < 0) {
                lines.add("  WARN: wrap-only failed at iteration $i")
                return -1L
            }
            samples[ok++] = ns
        }
        samples.sort()
        return medianOfSorted(samples)
    }

    /**
     * 每次：计时外 parse，再全量 walk k/v。
     * Abstract：树已物化，walk 近似纯遍历；Lazy：首次 walk 触发按需物化。
     */
    private fun measureWalkAllMedian(json: String, iterations: Int, lines: MutableList<String>): Long {
        val samples = LongArray(iterations)
        for (i in 0 until iterations) {
            val root = try {
                parseWithTokener(json)
            } catch (e: Exception) {
                lines.add("  WARN: walk prep parse failed at $i: ${e.message}")
                return -1L
            }
            val start = getTimeNanosPlatform()
            walkAllKv(root)
            samples[i] = getTimeNanosPlatform() - start
        }
        samples.sort()
        return medianOfSorted(samples)
    }

    private fun walkAllKv(value: Any?): Int {
        var visited = 0
        when (value) {
            is JSONObject -> {
                val keys = value.keys()
                while (keys.hasNext()) {
                    val k = keys.next()
                    visited += 1 + walkAllKv(value.opt(k))
                }
            }
            is JSONArray -> {
                val n = value.length()
                for (i in 0 until n) {
                    visited += 1 + walkAllKv(value.opt(i))
                }
            }
            else -> visited += 1
        }
        // 防止被优化掉
        walkSink = visited
        return visited
    }

    private var walkSink: Int = 0

    private fun medianOfSorted(sorted: LongArray): Long {
        val n = sorted.size
        if (n == 0) return -1L
        return if (n % 2 == 0) {
            (sorted[n / 2 - 1] + sorted[n / 2]) / 2
        } else {
            sorted[n / 2]
        }
    }

    // ============================================================
    // 结构化入桥 A/B（C++→Kotlin）：before=打印+Abstract 重解析，after=惰性壳
    // 起点均为已建好的 cJSON owner（建树成本两端共有，排除在计时外）。
    // ============================================================

    private fun consumeBridge(root: Any?, mode: ConsumeMode) {
        when (mode) {
            ConsumeMode.ROOT -> walkSink = if (root == null) 0 else 1
            ConsumeMode.READ5 -> readFewKv(root, 5)
            ConsumeMode.WALK -> walkAllKv(root)
            ConsumeMode.PUT -> firstPut(root)
            ConsumeMode.TOSTRING -> bridgeStringSink = root?.toString()?.length ?: 0
        }
    }

    private fun readFewKv(root: Any?, n: Int) {
        var c = 0
        when (root) {
            is JSONObject -> {
                val it = root.keys()
                while (it.hasNext() && c < n) {
                    sinkFieldValue(root.opt(it.next()))
                    c++
                }
            }
            is JSONArray -> {
                val len = root.length()
                var i = 0
                while (i < len && c < n) {
                    sinkFieldValue(root.opt(i))
                    i++; c++
                }
            }
        }
    }

    /**
     * READ5 只想量「读到该字段」的成本：标量按值 hash；容器只登记「读到壳」，
     * 绝不对容器调 `hashCode()`——那是内容哈希（数组会全表扫），会把「读 5 个字段」
     * 测成「全量遍历」，污染惰性 vs Abstract 的对比。
     */
    private fun sinkFieldValue(v: Any?) {
        walkSink += when (v) {
            null -> 1
            is JSONObject, is JSONArray -> 1
            else -> v.hashCode()
        }
    }

    private fun firstPut(root: Any?) {
        when (root) {
            is JSONObject -> root.put("__bench_probe", 1)
            is JSONArray -> root.put(1)
        }
    }

    private fun measureBridgeArm(owner: Long, before: Boolean, mode: ConsumeMode, iters: Int): Long {
        val s = LongArray(iters)
        for (i in 0 until iters) {
            val t0 = getTimeNanosPlatform()
            val root = if (before) bridgeBeforeArmRoot(owner) else bridgeAfterArmRoot(owner)
            consumeBridge(root, mode)
            s[i] = getTimeNanosPlatform() - t0
        }
        s.sort()
        return medianOfSorted(s)
    }

    private fun bridgeItersFor(json: String): Int = when {
        json.length >= 500_000 -> 3
        json.length >= 50_000 -> 8
        else -> 20
    }

    /** 单个 fixture：建 owner（计时外）→ 逐 mode 测 before/after → 记录 → 释放。 */
    private fun runBridgeFixture(name: String, json: String) {
        val owner = bridgeBuildOwner(json)
        if (owner == 0L) {
            logMachine("BRIDGE_PERF case=$name status=SKIP reason=owner_build_failed")
            return
        }
        val iters = bridgeItersFor(json)
        try {
            for (mode in ConsumeMode.values()) {
                val before = measureBridgeArm(owner, before = true, mode, iters)
                val after = measureBridgeArm(owner, before = false, mode, iters)
                val speedup = if (after > 0) before.toDouble() / after.toDouble() else -1.0
                logMachine(
                    "BRIDGE_PERF case=$name mode=$mode iters=$iters" +
                        " before_ns=$before after_ns=$after speedup=${formatDouble(speedup, 2)}"
                )
            }
        } finally {
            bridgeReleaseOwner(owner)
        }
    }

    /** 结构化入桥 A/B：OHOS 预挂 timer 逐 fixture 跑，避免重负载后 setTimeout 失效。 */
    private fun runBridgeMatrix() {
        isRunning = true
        isRunningAll = true
        bridgeFinished = false
        metricsList.clear()
        logMachine("BRIDGE_PERF_START platform=${getPlatformName()} supported=${bridgeSupported()}")
        if (!bridgeSupported()) {
            logMachine("BRIDGE_PERF_SUMMARY status=SKIP reason=unsupported")
            perfResultText = "PERF: SKIPPED (bridge n/a)"
            perfPassed = true
            isRunning = false
            isRunningAll = false
            statusText = "Structured-inbound A/B not supported on this platform."
            publishOverall()
            return
        }
        val fixtures = listOf(
            "Small_1KB" to jsonSmall1KB,
            "Medium_10KB" to jsonMedium10KB,
            "Large_100KB" to jsonLarge100KB,
            "Huge_1MB" to jsonHuge1MB,
            "Deep_50lv" to jsonDeepNest,
            "Array_1000" to jsonArray1000,
            "Escaped" to jsonEscaped
        )
        val slotMs = 12000
        logMachine("BRIDGE_PERF_ARM slots=${fixtures.size} slot_ms=$slotMs")
        for (i in fixtures.indices) {
            val index = i
            val (name, json) = fixtures[index]
            this.setTimeout(timeout = 50 + index * slotMs) {
                statusText = "Bridge A/B ${index + 1}/${fixtures.size}: $name"
                logMachine("BRIDGE_PERF_SLOT index=${index + 1}/${fixtures.size} case=$name")
                runBridgeFixture(name, json)
                if (index == fixtures.lastIndex) {
                    finishBridgeMatrix()
                }
            }
        }
        this.setTimeout(timeout = 50 + fixtures.size * slotMs + 5000) {
            if (isRunningAll || isRunning) {
                logMachine("BRIDGE_PERF_WATCHDOG")
                finishBridgeMatrix()
            }
        }
    }

    private fun finishBridgeMatrix() {
        if (bridgeFinished) {
            return
        }
        bridgeFinished = true
        logMachine("BRIDGE_PERF_SUMMARY status=PASS")
        perfPassed = true
        perfResultText = "PERF: PASS (bridge A/B)"
        isRunning = false
        isRunningAll = false
        statusText = "Structured-inbound A/B completed."
        publishOverall()
    }

    /**
     * Re-run the same iterations without timing, polling [getLastSweptCount] whenever
     * [getGcEpoch] advances. Final forced GC captures garbage left by the last iterations.
     * Returns -1 if the runtime does not expose sweep statistics.
     */
    private fun measureSweptChurn(json: String, iterations: Int, lines: MutableList<String>): Long {
        try { collectGarbage() } catch (_: Exception) {}
        var lastEpoch = try { getGcEpoch() } catch (_: Exception) { -1L }
        if (lastEpoch < 0) return -1L
        var probe = try { getLastSweptCount() } catch (_: Exception) { -1L }
        if (probe < 0) return -1L

        var totalSwept = 0L
        var missedEpochs = 0L
        for (i in 0 until iterations) {
            try {
                parseWithTokener(json)
            } catch (_: Exception) {
                return -1L
            }
            val epoch = try { getGcEpoch() } catch (_: Exception) { -1L }
            if (epoch > lastEpoch) {
                if (epoch > lastEpoch + 1) {
                    missedEpochs += epoch - lastEpoch - 1
                }
                val swept = try { getLastSweptCount() } catch (_: Exception) { -1L }
                if (swept >= 0) totalSwept += swept
                lastEpoch = epoch
            }
        }
        try { collectGarbage() } catch (_: Exception) {}
        val epoch = try { getGcEpoch() } catch (_: Exception) { -1L }
        if (epoch > lastEpoch) {
            if (epoch > lastEpoch + 1) {
                missedEpochs += epoch - lastEpoch - 1
            }
            val swept = try { getLastSweptCount() } catch (_: Exception) { -1L }
            if (swept >= 0) totalSwept += swept
        }
        if (missedEpochs > 0) {
            lines.add("  WARN: missed $missedEpochs GC epoch(s) while accumulating sweptCount")
        }
        return totalSwept
    }

    /**
     * Bracket retained size of one parsed tree using post-GC object bytes.
     * Pins the tree on [retainedProbe]/[retainedAnchor] so Release escape analysis
     * cannot drop the reference across [collectGarbage].
     */
    private fun measureRetainedBytes(json: String, lines: MutableList<String>): Long {
        try { collectGarbage() } catch (_: Exception) {}
        try { collectGarbage() } catch (_: Exception) {}
        val base = try { getHeapObjectBytes() } catch (_: Exception) { -1L }
        if (base < 0) return -1L

        val tree = try {
            parseWithTokener(json)
        } catch (e: Exception) {
            lines.add("  WARN: retained measure parse failed: ${e.message}")
            return -1L
        }
        retainedProbe = tree
        retainedAnchor[0] = tree

        try { collectGarbage() } catch (_: Exception) {}
        val withTree = try { getHeapObjectBytes() } catch (_: Exception) { -1L }
        val pinned = retainedAnchor[0]
        val probe = retainedProbe
        retainedProbe = null
        retainedAnchor[0] = null

        val delta = if (withTree >= 0) withTree - base else -1L
        val dbg = "Retained dbg: base=$base withTree=$withTree delta=$delta pinned=${pinned != null} probe=${probe != null}"
        lines.add("  $dbg")
        logSummary(dbg)
        return delta
    }

    // ============================================================
    // Auto-run all tests sequentially
    // ============================================================

    private fun runAllTests(fromAuto: Boolean = false) {
        if (isRunning && !fromAuto) return
        isRunning = true
        isRunningAll = true
        if (!fromAuto) {
            overallResultText = "OVERALL: RUNNING"
            perfResultText = "PERF: RUNNING"
        }
        val isOhos = getPlatformName().contains("OHOS")
        val allLines = mutableListOf<String>()
        allLines.add("╔══════════════════════════════════╗")
        allLines.add("║   AUTO-RUN ALL TESTS STARTED    ║")
        allLines.add("╚══════════════════════════════════╝")
        allLines.add("")
        allLines.add("  Total tests: 7")
        allLines.add("  Tokener: ${currentTokenerName()}")
        if (isOhos) {
            allLines.add("  OHOS auto: pre-armed timers + lighter iters (no post-case setTimeout)")
        } else {
            allLines.add("  Post-test cleanup: yield 500ms between cases")
        }
        allLines.add("")
        if (!fromAuto) {
            resultLines.clear()
            resultLines.addAll(allLines)
        } else {
            resultLines.addAll(allLines)
        }
        speedText = "Avg parse: --"
        metricsList.clear()
        logMachine("PERF_START cases=7 tokener=${currentTokenerName()}")

        // OHOS auto：timer 预挂 + 跳过 GC helpers；Array 已走惰性 cJSON，可用全量规模。
        val itSmall = if (isOhos) 30 else 100
        val itMedium = if (isOhos) 30 else 100
        val itLarge = if (isOhos) 15 else 50
        val itHuge = if (isOhos) 5 else 25
        val itDeep = if (isOhos) 40 else 200
        val itArray = if (isOhos) 50 else 100
        val itEscaped = if (isOhos) 40 else 200

        val queue = listOf<(String) -> Unit>(
            { label -> runTest("Small (1KB)", jsonSmall1KB, itSmall); statusText = "[1/7] $label" },
            { label -> runTest("Medium (10KB)", jsonMedium10KB, itMedium); statusText = "[2/7] $label" },
            { label -> runTest("Large (100KB)", jsonLarge100KB, itLarge); statusText = "[3/7] $label" },
            // 1MB allocates enough to trigger GC on most iterations; run enough of them
            // that the GC-free subset is still a usable sample.
            { label -> runTest("Huge (1MB)", jsonHuge1MB, itHuge); statusText = "[4/7] $label" },
            { label -> runTest("Deep (50lv)", jsonDeepNest, itDeep, "depth: 50"); statusText = "[5/7] $label" },
            { label -> runTest("Array 1000", jsonArray1000, itArray, "items: 1000"); statusText = "[6/7] $label" },
            { label -> runTest("Escaped Str", jsonEscaped, itEscaped); statusText = "[7/7] $label" },
        )

        if (isOhos) {
            armOhosAutoPerfQueue(queue)
        } else {
            executeAutoQueue(0, queue)
        }
    }

    /**
     * OHOS：在任何 PERF 解析开始前一次性注册整条 timer 链。
     * 实测在 cJSON 重负载之后再 [setTimeout]，native 回调经常不再触发，
     * UI 会永远停在「Scheduling next test...」。
     */
    private fun armOhosAutoPerfQueue(queue: List<(String) -> Unit>) {
        // wrap + walk 追加后单 case 更长；槽间距要大于最慢 case（Huge）。
        val slotMs = 12000
        statusText = "OHOS PERF: armed ${queue.size} slots @ ${slotMs}ms"
        logMachine("PERF_ARM slots=${queue.size} slot_ms=$slotMs")
        for (i in queue.indices) {
            val index = i
            this.setTimeout(timeout = 50 + index * slotMs) {
                statusText = "Running test ${index + 1}/${queue.size}..."
                logMachine("PERF_SLOT index=${index + 1}/${queue.size}")
                queue[index]("Running test ${index + 1}/${queue.size}...")
                logMachine("PERF_SLOT_DONE index=${index + 1}/${queue.size}")
                if (index == queue.lastIndex) {
                    finishAutoPerf(queue.size)
                }
            }
        }
        // 若末槽回调丢失，仍尽量收口，避免 OVERALL 永久 RUNNING。
        this.setTimeout(timeout = 50 + queue.size * slotMs + 5000) {
            if (isRunningAll) {
                logMachine("PERF_WATCHDOG metrics=${metricsList.size}/${queue.size}")
                finishAutoPerf(queue.size)
            }
        }
    }

    private fun finishAutoPerf(expectedCases: Int) {
        val summary = buildSummaryTable()
        logSummary("===== SUMMARY TABLE (lines=${summary.size}) =====")
        for (line in summary) {
            logSummary(line)
        }
        logSummary("===== END SUMMARY TABLE =====")
        for (m in metricsList) {
            val caseKey = m.name.replace(' ', '_').replace("(", "").replace(")", "")
            logMachine(
                "PERF case=$caseKey median_ns=${m.medianNanos} p99_ns=${m.p99Nanos}" +
                    " wrap_ns=${m.wrapMedianNanos} walk_ns=${m.walkMedianNanos}" +
                    " swept=${m.sweptCount} retained=${m.retainedBytes} gc_hits=${m.gcEpochDelta}"
            )
        }
        perfPassed = metricsList.size == expectedCases
        perfResultText = if (perfPassed) "PERF: PASS" else "PERF: FAIL"
        logMachine(
            "PERF_SUMMARY cases=${metricsList.size}/$expectedCases" +
                " status=${if (perfPassed) "PASS" else "FAIL"}"
        )
        resultLines.addAll(summary)
        isRunning = false
        isRunningAll = false
        statusText = "All $expectedCases tests completed!"
        publishOverall()
    }

    private fun executeAutoQueue(index: Int, queue: List<(String) -> Unit>) {
        if (index >= queue.size) {
            finishAutoPerf(queue.size)
            return
        }

        val label = "Running test ${index + 1}/${queue.size}..."
        queue[index](label)

        // After each test, yield briefly before the next case.
        // 不要在 timer 回调里同步 GC.collect() / 不要在 timed loop 里 GC.suspend：
        // OHOS K/N 上二者都会弄坏后续 setTimeout（OHOS 走 [armOhosAutoPerfQueue]）。
        statusText = "Scheduling next test..."
        this.setTimeout(timeout = 500) {
            executeAutoQueue(index + 1, queue)
        }
    }

    private fun scheduleGCCollects(times: Int, delayMs: Int, onComplete: () -> Unit) {
        if (times <= 0) {
            onComplete()
            return
        }
        statusText = "GC cleanup (${times} remaining)..."
        // 先挂 timer 再 GC，避免 GC 阻塞导致回调永不注册。
        if (times == 1) {
            this.setTimeout(timeout = delayMs) { onComplete() }
        } else {
            this.setTimeout(timeout = delayMs) {
                scheduleGCCollects(times - 1, delayMs, onComplete)
            }
        }
        try { collectGarbage() } catch (_: Exception) {}
    }

    private fun buildSummaryTable(): List<String> {
        val lines = mutableListOf<String>()
        lines.add("")
        lines.add("==================== SUMMARY TABLE ====================")
        if (metricsList.isEmpty()) {
            lines.add("  (no metrics collected — tests may have failed early)")
            lines.add("=======================================================")
            return lines
        }
        // GcHits = GC epochs that slipped into a timed iteration (0 = clean; suspend worked).
        lines.add(TableUtil.formatRow("TestName", "Swept", "Retained", "GcHits", "Median", "Wrap", "Walk", "P99"))
        lines.add("--------------------------------------------------")

        for (m in metricsList) {
            val name = m.name
            val swept = m.sweptCount.toString()
            val retained = m.retainedBytes.toString()
            val gcHits = m.gcEpochDelta.toString()
            val med = formatNanosCompact(m.medianNanos)
            val wrap = if (m.wrapMedianNanos >= 0) formatNanosCompact(m.wrapMedianNanos) else "-"
            val walk = if (m.walkMedianNanos >= 0) formatNanosCompact(m.walkMedianNanos) else "-"
            val p99 = formatNanosCompact(m.p99Nanos)
            lines.add(TableUtil.formatRow(name, swept, retained, gcHits, med, wrap, walk, p99))
        }

        lines.add("==================================================")
        return lines
    }

    private object TableUtil {
        fun formatRow(vararg cols: String): String {
            val widths = intArrayOf(17, 10, 10, 6, 9, 9, 9, 9)
            val padded = cols.mapIndexed { i, c ->
                if (i < widths.size) padRight(c, widths[i]) else c
            }
            return padded.joinToString(" | ")
        }

        private fun padRight(s: String, len: Int): String {
            var result = s
            while (result.length < len) result += " "
            return result
        }
    }

    private fun padRight(s: String, len: Int): String {
        var result = s
        while (result.length < len) result += " "
        return result
    }

    // ---- Compact formatters for table ----

    private fun formatNanosCompact(nanos: Long): String = formatNanos(nanos)
    private fun formatBytesCompact(bytes: Int): String = formatBytes(bytes)

    companion object {
        /** 一致性用例集失败详情在 UI 上的展示上限，全量走日志。 */
        private const val MAX_SHOWN_FAILURES = 30

        private fun verdictColor(text: String): Color {
            return when {
                text.endsWith("PASS") -> Color(0xFF2E7D32)
                text.endsWith("FAIL") -> Color(0xFFC62828)
                text.endsWith("RUNNING") -> Color(0xFFFF8800)
                else -> Color(0xFF666666)
            }
        }

        private fun formatNanos(nanos: Long): String {
            return when {
                nanos < 1_000 -> "${nanos}ns"
                nanos < 1_000_000 -> "${formatDouble(nanos.toDouble() / 1_000.0, 2)}us"
                nanos < 1_000_000_000 -> "${formatDouble(nanos.toDouble() / 1_000_000.0, 2)}ms"
                else -> "${formatDouble(nanos.toDouble() / 1_000_000_000.0, 3)}s"
            }
        }

        private fun formatBytes(bytes: Int): String {
            val b = if (bytes < 0) -(bytes.toLong()) else bytes.toLong()
            val sign = if (bytes < 0) "-" else ""
            return when {
                b < 1024 -> "$sign${b}B"
                b < 1024 * 1024 -> "$sign${formatDouble(b.toDouble() / 1024.0, 1)}KB"
                else -> "$sign${formatDouble(b.toDouble() / (1024.0 * 1024.0), 2)}MB"
            }
        }

        private fun formatDouble(value: Double, decimals: Int): String {
            val factor = pow10(decimals)
            val rounded = kotlin.math.round(value * factor) / factor
            val intPart = rounded.toLong()
            if (decimals == 0) return intPart.toString()
            val fracPart = kotlin.math.abs((rounded - intPart) * factor).toLong()
            var fracStr = fracPart.toString()
            while (fracStr.length < decimals) fracStr = "0$fracStr"
            return "$intPart.$fracStr"
        }

        private fun pow10(n: Int): Double {
            var result = 1.0
            repeat(n) { result *= 10.0 }
            return result
        }

        private fun generateJson(targetSize: Int): String {
            val sb = StringBuilder()
            sb.append("{")
            sb.append("\"metadata\":{")
            sb.append("\"version\":\"1.0.0\",")
            sb.append("\"generator\":\"K/N Perf Test\",")
            sb.append("\"timestamp\":${getTimeNanosPlatform() / 1_000_000},")
            sb.append("\"description\":\"Auto-generated test JSON for perf benchmarking\"")
            sb.append("},")
            sb.append("\"items\":[")

            val itemCount = maxOf(1, targetSize / 400)
            for (i in 0 until itemCount) {
                if (i > 0) sb.append(",")
                sb.append("{")
                sb.append("\"id\":$i,")
                sb.append("\"name\":\"Item number $i\",")
                sb.append("\"active\":${i % 2 == 0},")
                sb.append("\"score\":${i * 1.5},")
                sb.append("\"tags\":[\"tag_${i % 10}\",\"category_${i % 5}\"],")
                sb.append("\"metadata\":{")
                sb.append("\"created\":\"2024-0${(i % 9) + 1}-${(i % 28) + 1}T${(i % 24).toString().padStart(2, '0')}:${(i % 60).toString().padStart(2, '0')}:00Z\",")
                sb.append("\"priority\":${i % 3},")
                sb.append("\"nullable\":${if (i % 7 == 0) "null" else "\"value_$i\""}")
                sb.append("}")
                sb.append("}")
            }

            sb.append("],")
            sb.append("\"summary\":{")
            sb.append("\"totalItems\":$itemCount,")
            sb.append("\"averageScore\":${itemCount * 0.75},")
            sb.append("\"distribution\":{")
            sb.append("\"low\":${itemCount / 3},")
            sb.append("\"medium\":${itemCount / 3},")
            sb.append("\"high\":${itemCount - 2 * (itemCount / 3)}")
            sb.append("}")
            sb.append("},")
            sb.append("\"unicode_test\":\"Hello \\u4e16\\u754c \\u0393\\u03b5\\u03b9\\u03ac \\u0633\\u0644\\u0627\\u0645\"")
            sb.append("}")
            return sb.toString()
        }

        private fun generateDeepNestedJson(depth: Int): String {
            val sb = StringBuilder()
            for (i in 0 until depth) {
                for (j in 0 until i) sb.append("  ")
                sb.append("{\"level\":$i,\"name\":\"node_$i\",\"children\":")
            }
            sb.append("{\"level\":$depth,\"name\":\"leaf\",\"value\":42}")
            for (i in 0 until depth + 1) {
                sb.append("}")
            }
            return sb.toString()
        }

        private fun generateLargeArray(size: Int): String {
            val sb = StringBuilder()
            sb.append("[")
            for (i in 0 until size) {
                if (i > 0) sb.append(",")
                sb.append("{\"index\":$i,\"value\":\"item_$i\",\"flag\":${i % 2 == 0},\"count\":$i}")
            }
            sb.append("]")
            return sb.toString()
        }

        private fun generateEscapedStringJson(): String {
            val sb = StringBuilder()
            sb.append("{\"strings\":[")
            for (i in 0 until 50) {
                if (i > 0) sb.append(",")
                sb.append("\"")
                sb.append("Line $i\\n")
                sb.append("Tab\\there\\t")
                sb.append("Quote: \\\"hello\\\" ")
                sb.append("Backslash: \\\\ ")
                sb.append("Unicode: \\u0041\\u0042\\u0043 ")
                sb.append("Chinese: \\u4e2d\\u6587 ")
                sb.append("Special: !@#\$%^&*()_+-=[]{}|;:',.<>?/ ")
                sb.append("Pad_")
                for (k in 0 until (i % 20 + 5)) sb.append("x")
                sb.append("\"")
            }
            sb.append("]}")
            return sb.toString()
        }
    }
}

// ============================================================
// ActionButton reusable component
// ============================================================

internal class ActionButtonView(
    private val label: String,
    private val bgColor: Color = Color(0xFF2196F3),
    private val disabled: Boolean = false,
    private val onClick: () -> Unit
) : com.tencent.kuikly.core.base.ComposeView<com.tencent.kuikly.core.base.ComposeAttr, com.tencent.kuikly.core.base.ComposeEvent>() {

    override fun createEvent() = com.tencent.kuikly.core.base.ComposeEvent()
    override fun createAttr() = com.tencent.kuikly.core.base.ComposeAttr()

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            Button {
                attr {
                    height(36f)
                    borderRadius(18f)
                    margin(4f)
                    paddingLeft(14f)
                    paddingRight(14f)
                    backgroundColor(if (ctx.disabled) Color(0xFFBDBDBD) else ctx.bgColor)
                    titleAttr {
                        text(ctx.label)
                        fontSize(13f)
                        color(Color.WHITE)
                    }
                }
                event {
                    click { if (!ctx.disabled) ctx.onClick() }
                }
            }
        }
    }
}

private fun com.tencent.kuikly.core.base.ViewContainer<*, *>.ActionButton(
    label: String,
    bgColor: Color = Color(0xFF2196F3),
    disabled: Boolean = false,
    onClick: () -> Unit
) {
    addChild(ActionButtonView(label, bgColor, disabled, onClick)) {}
}