/*
 * Tencent is pleased to support the open source community by making KuiklyUI
 * available.
 * Copyright (C) 2025 Tencent. All rights reserved.
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

package com.tencent.kuikly.demo.pages

import com.tencent.kuikly.core.annotations.Page
import com.tencent.kuikly.core.base.Color
import com.tencent.kuikly.core.base.ViewBuilder
import com.tencent.kuikly.core.datetime.DateTime
import com.tencent.kuikly.core.log.KLog
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.timer.setTimeout
import com.tencent.kuikly.core.views.List
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.demo.pages.base.BasePager
import kotlinx.cinterop.AutofreeScope
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.CValues
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.UShortVar
import kotlinx.cinterop.cstr
import kotlinx.cinterop.utf16
import kotlinx.cinterop.memScoped

/**
 * CStringUtf16BenchmarkPage
 *
 * 目的：量化对比把 Kotlin String 转成 C 字符串（const char* / wchar_t*）的三种方式的开销：
 *   1) [String.cstr]        —— UTF-16 -> UTF-8 编码，产生中间 ByteArray，再 memcpy 进 native 内存
 *   2) [String.utf16]       —— 标准库 UTF-16（wchar_t*），先 toCharArray() 再 putCharArray，2 次拷贝 + 1 次 CharArray 分配
 *   3) [String.nativeUtf16] —— 本文实现的零额外堆分配 UTF-16：直接把 String 的 UTF-16 码元写进 native 内存，1 次拷贝
 *
 * 注意：本页位于 ohosArm64Main 源集，因为 [kotlinx.cinterop]（cstr / utf16 / memScoped / CPointer）
 * 仅在 native 目标可用，不能在 commonMain 编译。
 *
 * 设计要点：
 * 1) 被测操作是 `memScoped { s.xxx.ptr }` 本身（alloc + place 含编码/拷贝），无需调用真实 C 函数即可体现差异。
 * 2) 结果通过 `sink` 字段消费，防止 Kotlin/Native 把"未被使用的 ptr"整个优化掉。
 * 3) 计时用 [DateTime.nanoTime]（纳秒单调时钟）。
 * 4) 三种字符串规模（短 ASCII / 中英文混合 / 长含 emoji），让 UTF-8 编码代价在不同场景下的差异可见。
 * 5) 每种组合跑 [ITER] 次取总耗时，避免单次噪声。
 */

@OptIn(ExperimentalForeignApi::class)
@Page("CStringUtf16BenchmarkPage")
internal class CStringUtf16BenchmarkPage : BasePager() {

    // ----- 结果展示（observable） -----
    private var resultText by observable("点击“Run Benchmark”开始")

    // 消费指针，避免被编译器优化掉整个 memScoped 块
    @Suppress("unused")
    private var sink: CPointer<*>? = null

    companion object {
        private const val ITER = 20000
        private const val TAG = "CStringUtf16Bench"
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            View {
                attr {
                    flex(1f)
                    backgroundColor(Color(0xFFF2F3F5))
                    padding(16f)
                    flexDirectionColumn()
                }

                Text {
                    attr {
                        fontSize(20f)
                        fontWeightBold()
                        marginBottom(12f)
                        text("C String (UTF-16) Benchmark")
                    }
                }

                Text {
                    attr {
                        fontSize(13f)
                        text(ctx.resultText)
                    }
                }

                View {
                    attr {
                        marginTop(16f)
                        padding(12f)
                        backgroundColor(Color(0xFF07C160))
                        borderRadius(8f)
                    }
                    event {
                        click {
                            ctx.runBenchmark()
                        }
                    }
                    Text {
                        attr {
                            fontSize(16f)
                            color(Color(0xFFFFFFFF))
                            text("Run Benchmark")
                        }
                    }
                }
            }
        }
    }

    // ===== 零额外堆分配的 UTF-16 C 字符串（被测方案 3） =====
    // 直接持有 String，在 place() 里把 UTF-16 码元写入 native 内存，无 toCharArray、无 UTF-8 编码。
    private class Str16CValues(val s: String) : CValues<UShortVar>() {
        override val size: Int get() = 2 * (s.length + 1)
        override val align: Int get() = 2
        override fun place(placement: CPointer<UShortVar>): CPointer<UShortVar> {
            for (i in 0 until s.length) {
                (placement + i)!!.pointed.value = s[i].code.toUShort()
            }
            (placement + s.length)!!.pointed.value = 0u
            return placement
        }
    }

    private fun String.nativeUtf16(scope: AutofreeScope): CPointer<UShortVar> =
        Str16CValues(this).getPointer(scope)

    // ===== 基准测试主体 =====

    private fun runBenchmark() {
        resultText = "Running..."
        // 延后到下一帧执行，让 UI 先刷新 "Running..."
        setTimeout(timeout = 50) {
            val short = "hi"
            val medium = buildString { repeat(40) { append("中文ABC$it ") } }      // 中英混合
            val long = buildString {
                repeat(200) { append("KuiklyUI 跨平台 🚀 字符串基准 $it ") }
            }                                                                       // 长 + emoji

            val sb = StringBuilder()
            sb.appendLine("iterations per case: $ITER")
            sb.appendLine("========================================")

            sb.appendLine("SHORT (ascii \"hi\"):")
            sb.appendLine("  cstr       : ${bench { memScoped { sink = "hi".cstr.ptr } }} ms")
            sb.appendLine("  utf16      : ${bench { memScoped { sink = short.utf16.ptr } }} ms")
            sb.appendLine("  nativeUtf16: ${bench { memScoped { sink = short.nativeUtf16(this) } }} ms")

            sb.appendLine("----------------------------------------")
            sb.appendLine("MEDIUM (中文+ascii ~${medium.length} chars):")
            sb.appendLine("  cstr       : ${bench { memScoped { sink = medium.cstr.ptr } }} ms")
            sb.appendLine("  utf16      : ${bench { memScoped { sink = medium.utf16.ptr } }} ms")
            sb.appendLine("  nativeUtf16: ${bench { memScoped { sink = medium.nativeUtf16(this) } }} ms")

            sb.appendLine("----------------------------------------")
            sb.appendLine("LONG (含emoji ~${long.length} chars):")
            sb.appendLine("  cstr       : ${bench { memScoped { sink = long.cstr.ptr } }} ms")
            sb.appendLine("  utf16      : ${bench { memScoped { sink = long.utf16.ptr } }} ms")
            sb.appendLine("  nativeUtf16: ${bench { memScoped { sink = long.nativeUtf16(this) } }} ms")

            val out = sb.toString()
            KLog.i(TAG, out)
            resultText = out
        }
    }

    /**
     * 执行 [block] [ITER] 次并统计总耗时（毫秒）。
     * 外层没有 memScoped，block 内部自行创建 scope —— 测的是
     * "每次都 alloc 一块新 native 内存 + 写入 + 释放" 的真实循环成本。
     */
    private inline fun bench(crossinline block: () -> Unit): String {
        // 热身，避免首次执行的缓存冷启动干扰
        repeat(200) { block() }
        val start = DateTime.nanoTime()
        repeat(ITER) { block() }
        val end = DateTime.nanoTime()
        val ms = (end - start) / 1_000_000.0
        return format3(ms)
    }

    // Kotlin/Native 没有 String.format，手动保留 3 位小数
    private fun format3(v: Double): String {
        val scaled = (v * 1000 + 0.5).toLong()
        val intPart = scaled / 1000
        val fracPart = scaled % 1000
        return "$intPart.${if (fracPart < 10) "00" else if (fracPart < 100) "0" else ""}$fracPart"
    }
}
