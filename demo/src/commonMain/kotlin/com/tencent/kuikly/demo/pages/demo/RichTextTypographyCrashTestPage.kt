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

package com.tencent.kuikly.demo.pages.demo

import com.tencent.kuikly.core.annotations.Page
import com.tencent.kuikly.core.base.Color
import com.tencent.kuikly.core.base.ViewBuilder
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.timer.setTimeout
import com.tencent.kuikly.core.views.List
import com.tencent.kuikly.core.views.RichText
import com.tencent.kuikly.core.views.Span
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.demo.pages.demo.base.NavBar

/**
 * Typography CFI Crash 复现测试页面
 *
 * 测试场景：
 * 1. textAlign != LEFT 时触发主线程 re-layout（与 context 线程并发）
 * 2. lineBreakMargin + GetTextLines 与 typography 生命周期竞争
 * 3. 动态改变容器宽度，触发 frameWidth != textTypoSize.width 的 re-layout
 * 4. 快速更新文本内容，加速 typography 对象的创建/销毁频率
 * 5. RichText + 居中对齐 + 多行截断组合场景
 */
@Page("RichTextTypographyCrashTest")
internal class RichTextTypographyCrashTestPage : BasePager() {

    // 动态宽度，用于触发 re-layout
    var containerWidth by observable(350f)
    // 动态文本内容，用于加速 typography 刷新
    var dynamicText by observable("初始文本内容，这是一段用于测试的文本。")
    var updateCount by observable(0)
    // 控制是否在更新
    var isUpdating by observable(false)
    // 批量测试项
    private val batchConfigs = listOf(
        Triple(1, 0, true),   // index, alignType, hasLineBreakMargin
        Triple(2, 1, false),
        Triple(3, 2, true),
        Triple(4, 0, false),
        Triple(5, 1, true),
        Triple(6, 2, false),
        Triple(7, 0, true),
        Triple(8, 1, false),
        Triple(9, 2, true),
        Triple(10, 0, false)
    )

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.WHITE)
            }
            NavBar {
                attr {
                    title = "Typography Crash Test"
                }
            }
            List {
                attr {
                    flex(1f)
                }

                // ============ 场景 1: textAlign Center + 宽度动态变化 ============
                View {
                    attr {
                        margin(16f)
                    }
                    Text {
                        attr {
                            fontSize(14f)
                            fontWeightBold()
                            color(Color(0xFF333333))
                            text("场景1: 居中对齐 + 动态宽度 (触发 re-layout)")
                        }
                    }
                }

                View {
                    attr {
                        margin(left = 16f, right = 16f, bottom = 8f)
                        width(ctx.containerWidth)
                        backgroundColor(Color(0xFFE3F2FD))
                        borderRadius(8f)
                        padding(12f)
                    }
                    Text {
                        attr {
                            fontSize(15f)
                            textAlignCenter()
                            text("居中对齐文本 - 容器宽度会动态变化，触发 OnForegroundDraw 中的 re-layout 逻辑。" +
                                "当 frameWidth != textTypoSize.width 时，系统需要用新宽度重新 Layout typography。" +
                                "如果此时 context 线程正在操作同一个 typography 对象，就会触发 CFI crash。")
                            color(Color(0xFF333333))
                        }
                    }
                }

                // ============ 场景 2: textAlign Right + 多行 ============
                View {
                    attr {
                        margin(left = 16f, right = 16f, bottom = 8f)
                        width(ctx.containerWidth)
                        backgroundColor(Color(0xFFFFF3E0))
                        borderRadius(8f)
                        padding(12f)
                    }
                    Text {
                        attr {
                            fontSize(15f)
                            textAlignRight()
                            text("右对齐文本 - 同样会触发 re-layout。右对齐在 Layout 阶段计算每行的 x 偏移 = maxWidth - lineWidth。" +
                                "如果 Layout 时用的 maxWidth 和绘制时的容器宽度不一致，对齐就会错位。")
                            color(Color(0xFF795548))
                        }
                    }
                }

                // ============ 场景 3: lineBreakMargin + GetTextLines ============
                View {
                    attr {
                        margin(16f)
                    }
                    Text {
                        attr {
                            fontSize(14f)
                            fontWeightBold()
                            color(Color(0xFF333333))
                            text("场景2: lineBreakMargin + 多行截断 (GetTextLines)")
                        }
                    }
                }

                View {
                    attr {
                        margin(left = 16f, right = 16f, bottom = 8f)
                        backgroundColor(Color(0xFFE8F5E9))
                        borderRadius(8f)
                        padding(12f)
                    }
                    Text {
                        attr {
                            fontSize(15f)
                            lines(3)
                            lineBreakMargin(80f)
                            text("lineBreakMargin 场景：这段文本设置了 lines=3 和 lineBreakMargin=80。" +
                                "绘制时会调用 OH_Drawing_TypographyGetTextLines 获取行数据进行逐行绘制。" +
                                "GetTextLines 只能安全调用一次（会 move 走内部数据），缓存的 text_lines_ " +
                                "指针如果在 DestroyCachedTextLines 后被访问就是 use-after-free。" +
                                "快速更新内容时，SetMainThreadTypography 会清掉缓存，如果此时正在迭代就会 crash。")
                            color(Color(0xFF333333))
                        }
                    }
                }

                // ============ 场景 4: RichText + 居中 + lineBreakMargin ============
                View {
                    attr {
                        margin(16f)
                    }
                    Text {
                        attr {
                            fontSize(14f)
                            fontWeightBold()
                            color(Color(0xFF333333))
                            text("场景3: RichText + 居中 + lineBreakMargin (组合触发)")
                        }
                    }
                }

                View {
                    attr {
                        margin(left = 16f, right = 16f, bottom = 8f)
                        width(ctx.containerWidth)
                        backgroundColor(Color(0xFFFCE4EC))
                        borderRadius(8f)
                        padding(12f)
                    }
                    RichText {
                        attr {
                            textAlignCenter()
                            lines(2)
                            lineBreakMargin(60f)
                        }
                        Span {
                            fontSize(16f)
                            color(Color(0xFFE91E63))
                            fontWeightBold()
                            text("【高风险组合】")
                        }
                        Span {
                            fontSize(15f)
                            color(Color(0xFF333333))
                            text("RichText 居中对齐 + lineBreakMargin + 行数限制。这个组合同时触发：" +
                                "1) re-layout（居中+宽度不一致）2) GetTextLines（lineBreakMargin 逐行绘制）" +
                                "3) typography 生命周期竞争。三者叠加最容易复现 CFI crash。")
                        }
                    }
                }

                // ============ 场景 5: 动态快速更新内容 ============
                View {
                    attr {
                        margin(16f)
                    }
                    Text {
                        attr {
                            fontSize(14f)
                            fontWeightBold()
                            color(Color(0xFF333333))
                            text("场景4: 动态高频更新文本内容 (加速 typography 更替)")
                        }
                    }
                }

                View {
                    attr {
                        margin(left = 16f, right = 16f, bottom = 8f)
                        width(ctx.containerWidth)
                        backgroundColor(Color(0xFFE8EAF6))
                        borderRadius(8f)
                        padding(12f)
                    }
                    RichText {
                        attr {
                            textAlignCenter()
                            lines(3)
                            lineBreakMargin(50f)
                        }
                        Span {
                            fontSize(14f)
                            color(Color(0xFF3F51B5))
                            fontWeightBold()
                            text("[更新#${ctx.updateCount}] ")
                        }
                        Span {
                            fontSize(14f)
                            color(Color(0xFF333333))
                            text(ctx.dynamicText)
                        }
                    }
                }

                // 控制按钮
                View {
                    attr {
                        margin(left = 16f, right = 16f, bottom = 8f)
                        flexDirectionRow()
                        justifyContentSpaceAround()
                    }
                    // 开始/停止快速更新
                    View {
                        attr {
                            backgroundColor(if (ctx.isUpdating) Color(0xFFF44336) else Color(0xFF4CAF50))
                            borderRadius(20f)
                            padding(left = 16f, right = 16f, top = 8f, bottom = 8f)
                        }
                        event {
                            click {
                                if (ctx.isUpdating) {
                                    ctx.isUpdating = false
                                } else {
                                    ctx.isUpdating = true
                                    ctx.startRapidUpdate()
                                }
                            }
                        }
                        Text {
                            attr {
                                color(Color.WHITE)
                                fontSize(14f)
                                fontWeightBold()
                                text(if (ctx.isUpdating) "⏹ 停止更新" else "▶ 开始快速更新")
                            }
                        }
                    }
                    // 切换宽度
                    View {
                        attr {
                            backgroundColor(Color(0xFF2196F3))
                            borderRadius(20f)
                            padding(left = 16f, right = 16f, top = 8f, bottom = 8f)
                        }
                        event {
                            click {
                                ctx.containerWidth = if (ctx.containerWidth > 300f) 250f else 350f
                            }
                        }
                        Text {
                            attr {
                                color(Color.WHITE)
                                fontSize(14f)
                                fontWeightBold()
                                text("↔ 切换宽度")
                            }
                        }
                    }
                }

                // ============ 场景 6: 大量 RichText 同时渲染 ============
                View {
                    attr {
                        margin(16f)
                    }
                    Text {
                        attr {
                            fontSize(14f)
                            fontWeightBold()
                            color(Color(0xFF333333))
                            text("场景5: 批量 RichText 并发测量/绘制 (压力测试)")
                        }
                    }
                }

                // 批量生成多个 RichText，模拟列表中大量富文本同时测量绘制
                for ((index, alignType, hasLBM) in ctx.batchConfigs) {
                    val alignLabel = when (alignType) { 0 -> "LEFT"; 1 -> "CENTER"; else -> "RIGHT" }
                    View {
                        attr {
                            margin(left = 16f, right = 16f, bottom = 4f)
                            backgroundColor(Color(0xFFF5F5F5))
                            borderRadius(6f)
                            padding(8f)
                        }
                        RichText {
                            attr {
                                when (alignType) {
                                    0 -> textAlignLeft()
                                    1 -> textAlignCenter()
                                    else -> textAlignRight()
                                }
                                lines(2)
                                if (hasLBM) {
                                    lineBreakMargin(40f)
                                }
                            }
                            Span {
                                fontSize(13f)
                                color(Color(0xFF9C27B0))
                                fontWeightBold()
                                text("#$index ")
                            }
                            Span {
                                fontSize(13f)
                                color(Color(0xFF555555))
                                text("批量 RichText 项 - align=$alignLabel " +
                                    "lineBreakMargin=${if (hasLBM) "40" else "无"} " +
                                    "这段文本用来模拟列表中大量富文本项同时被测量和绘制的场景，" +
                                    "增加 context 线程和主线程并发操作 typography 对象的概率。")
                            }
                        }
                    }
                }

                // 底部间距
                View {
                    attr {
                        height(60f)
                    }
                }
            }
        }
    }

    private fun startRapidUpdate() {
        if (!isUpdating) return
        updateCount++
        val texts = listOf(
            "快速更新内容A：Typography 对象正在被高频创建和销毁，context 线程不断 BuildTextTypography + Layout，主线程持续 Paint。",
            "快速更新内容B：每次文本变化都会触发新的 measure → 创建新 typography → Layout → 投递到主线程 → 替换旧 typography。",
            "快速更新内容C：旧 typography 的 shared_ptr 引用计数归零时被释放，如果主线程还在用它做 Paint 就是 use-after-free。",
            "快速更新内容D：CFI 检查发现函数指针地址非法（已被 free 后填充 poison 值），触发 SIGABRT。",
            "快速更新内容E：测试 GetTextLines 缓存在 typography 快速更替时是否被正确清理和重建。",
            "短文本F",
            "快速更新内容G：这是一段较长的文本，用于确保文本换行后的 lineBreakMargin 逻辑被触发，同时居中对齐需要 re-layout。" +
                "多行文本在容器宽度变化时，每一行的对齐偏移都需要重新计算，这增加了并发冲突的概率。"
        )
        dynamicText = texts[updateCount % texts.size]

        // 同时随机改变宽度，增加 re-layout 触发概率
        if (updateCount % 3 == 0) {
            containerWidth = 250f + (updateCount % 5) * 25f
        }

        setTimeout(50) {
            startRapidUpdate()
        }
    }
}
