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

import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.core.annotations.Page
import com.tencent.kuikly.core.base.Color
import com.tencent.kuikly.core.base.ViewBuilder
import com.tencent.kuikly.core.base.ViewContainer
import com.tencent.kuikly.core.base.event.layoutFrameDidChange
import com.tencent.kuikly.core.directives.vif
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.views.Scroller
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View

private const val LINE_HEIGHT = 20f
private const val EXPAND_MARGIN = 72f
private const val LONG_TEXT =
    "这是一段用于演示 lineBreakMargin 的长文本。当文本超过行数限制时，最后一行右侧会预留空白，" +
        "通常在此区域放置「展开/收起」按钮。再多写一些内容以确保超过两行并被截断显示省略号。" +
        "继续补充文字以便在不同设备宽度下都能稳定触发 onLineBreakMargin 事件。"

private fun ViewContainer<*, *>.sectionTitle(title: String) {
    Text {
        attr {
            text(title)
            fontSize(15f)
            fontWeightSemiBold()
            marginTop(12f)
            marginBottom(6f)
        }
    }
}

private fun ViewContainer<*, *>.lineBreakMarginStatus(
    label: String,
    fired: () -> Boolean,
    expectFalse: Boolean = false,
) {
    Text {
        attr {
            val triggered = fired()
            val ok = if (expectFalse) !triggered else triggered
            text("$label: ${if (triggered) "已触发" else "未触发"}${if (expectFalse) "（预期未触发）" else ""}")
            fontSize(12f)
            color(if (ok) 0xFF2E7D32 else 0xFFC62828)
            marginBottom(4f)
        }
    }
}

@Page("line_break_margin")
internal class LineBreakMarginPager : BasePager() {

    /** 场景1：长文折叠 + 预留区内的展开/收起 */
    var expandedMain by observable(false)
    var showExpandInMargin by observable(false)
    var lineBreakMain by observable(false)
    var expandBtnTop by observable(0f)
    var expandBtnLeft by observable(0f)

    /** 场景2：短文，不应触发 */
    var lineBreakShort by observable(false)

    /** 场景3：较小 margin(40)，仅观察触发状态 */
    var lineBreakSmallMargin by observable(false)

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                flexDirectionColumn()
                backgroundColor(Color.WHITE)
            }
            Scroller {
                attr {
                    flex(1f)
                }
                View {
                    attr {
                        flexDirectionColumn()
                        padding(all = 16f)
                    }
                    Text {
                        attr {
                            text("lineBreakMargin 演示")
                            fontSize(18f)
                            fontWeightBold()
                            marginBottom(8f)
                        }
                    }
                    Text {
                        attr {
                            text(
                                "场景1：长文 + lines(2) + lineBreakMargin，在预留区点击展开/收起。" +
                                    "场景2：短文不应触发。场景3：margin=40 的对比。"
                            )
                            fontSize(13f)
                            color(Color.GRAY)
                            lineHeight(18f)
                            marginBottom(16f)
                        }
                    }

                    sectionTitle("场景1 · 展开 / 收起（margin ${EXPAND_MARGIN.toInt()}）")
                    View {
                        attr {
                            backgroundColor(0xFFF5F5F5)
                            padding(all = 8f)
                            marginBottom(4f)
                        }
                        Text {
                            attr {
                                fontSize(15f)
                                color(Color.BLACK)
                                lineHeight(LINE_HEIGHT)
                                if (!ctx.expandedMain) {
                                    lines(2)
                                    lineBreakMargin(EXPAND_MARGIN)
                                } else {
                                    lines(0)
                                }
                                text(LONG_TEXT)
                            }
                            event {
                                onLineBreakMargin {
                                    ctx.lineBreakMain = true
                                    if (!ctx.expandedMain) {
                                        ctx.showExpandInMargin = true
                                    }
                                }
                                layoutFrameDidChange { frame ->
                                    ctx.expandBtnTop = frame.y + frame.height - LINE_HEIGHT
                                    ctx.expandBtnLeft = frame.x + frame.width - EXPAND_MARGIN
                                }
                            }
                        }
                        vif({ ctx.showExpandInMargin && !ctx.expandedMain }) {
                            View {
                                attr {
                                    absolutePosition(top = ctx.expandBtnTop, left = ctx.expandBtnLeft)
                                    width(EXPAND_MARGIN)
                                    height(LINE_HEIGHT)
                                    backgroundColor(0xFF1976D2)
                                    borderRadius(4f)
                                    justifyContentCenter()
                                    alignItemsCenter()
                                }
                                Text {
                                    attr {
                                        text("展开")
                                        fontSize(13f)
                                        color(Color.WHITE)
                                        fontWeightBold()
                                    }
                                }
                                event {
                                    click {
                                        ctx.expandedMain = true
                                        ctx.showExpandInMargin = false
                                    }
                                }
                            }
                        }
                    }
                    vif({ ctx.expandedMain }) {
                        Text {
                            attr {
                                text("收起")
                                fontSize(14f)
                                color(0xFF007AFF)
                                fontWeightBold()
                                marginTop(4f)
                                alignSelfFlexEnd()
                            }
                            event {
                                click {
                                    ctx.expandedMain = false
                                    ctx.showExpandInMargin = false
                                    ctx.lineBreakMain = false
                                }
                            }
                        }
                    }
                    lineBreakMarginStatus("onLineBreakMargin", fired = { ctx.lineBreakMain })

                    sectionTitle("场景2 · 短文（不应触发）")
                    View {
                        attr {
                            backgroundColor(0xFFE8F4FF)
                            padding(all = 8f)
                            marginBottom(4f)
                        }
                        Text {
                            attr {
                                text("短文本 lineBreakMargin(80)")
                                fontSize(15f)
                                color(Color.BLACK)
                                lines(2)
                                lineHeight(LINE_HEIGHT)
                                lineBreakMargin(80f)
                            }
                            event {
                                onLineBreakMargin {
                                    ctx.lineBreakShort = true
                                }
                            }
                        }
                    }
                    lineBreakMarginStatus("onLineBreakMargin", fired = { ctx.lineBreakShort }, expectFalse = true)

                    sectionTitle("场景3 · margin = 40")
                    View {
                        attr {
                            backgroundColor(0xFFFFF3E0)
                            padding(all = 8f)
                            marginBottom(4f)
                        }
                        Text {
                            attr {
                                text("lineBreakMargin(40)：预留较窄，最后一行右侧空白更小。$LONG_TEXT")
                                fontSize(15f)
                                color(Color.BLACK)
                                lines(2)
                                lineHeight(LINE_HEIGHT)
                                lineBreakMargin(40f)
                            }
                            event {
                                onLineBreakMargin {
                                    ctx.lineBreakSmallMargin = true
                                }
                            }
                        }
                    }
                    lineBreakMarginStatus("onLineBreakMargin", fired = { ctx.lineBreakSmallMargin })
                }
            }
        }
    }
}
