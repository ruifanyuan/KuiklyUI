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
import com.tencent.kuikly.core.directives.vforIndex
import com.tencent.kuikly.core.log.KLog
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.reactive.handler.observableList
import com.tencent.kuikly.core.views.List
import com.tencent.kuikly.core.views.Scroller
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.core.base.ViewBuilder
import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.demo.pages.demo.base.NavBar

/**
 * OHOS 状态栏点击回顶（back-to-top）验证页面。
 *
 * 覆盖三种用例：
 * 1. Scroller：未注册 scrollToTop 回调 → 点状态栏应由框架默认回顶；
 * 2. Scroller：已注册 scrollToTop 回调 → 点状态栏应触发回调且不自动回顶；
 * 3. List：未注册回调 → 点状态栏应由框架默认回顶。
 */
@Page("OhosBackToTopPage")
internal class OhosBackToTopDemoPage : BasePager() {

    companion object {
        private const val TAG = "OhosBackToTopPage"
        private const val SCROLL_VIEW_HEIGHT = 260f
    }

    private val itemCount by observableList<Int>()

    private var defaultScrollerOffsetY by observable(0f)
    private var callbackScrollerOffsetY by observable(0f)
    private var listOffsetY by observable(0f)
    private var scrollToTopCallbackCount by observable(0)

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            NavBar {
                attr {
                    title = "OHOS BackToTop"
                }
            }

            // 用例 1：未注册回调，期望框架默认回顶
            Text {
                attr {
                    marginTop(10f)
                    fontSize(14f)
                    text("1) Scroller(默认回顶) offsetY=${ctx.defaultScrollerOffsetY}")
                }
            }
            Scroller {
                attr {
                    size(pagerData.pageViewWidth, SCROLL_VIEW_HEIGHT)
                }
                vforIndex({ ctx.itemCount }) { item, _, _ ->
                    ctx.demoItem(item, Color.BLUE).invoke(this)
                }
                event {
                    scroll { param ->
                        ctx.defaultScrollerOffsetY = param.offsetY
                    }
                }
            }

            // 用例 2：注册 scrollToTop 回调，期望仅回调、不自动回顶
            Text {
                attr {
                    marginTop(10f)
                    fontSize(14f)
                    text(
                        "2) Scroller(回调接管) offsetY=${ctx.callbackScrollerOffsetY}" +
                            " callbackCount=${ctx.scrollToTopCallbackCount}"
                    )
                }
            }
            Scroller {
                attr {
                    size(pagerData.pageViewWidth, SCROLL_VIEW_HEIGHT)
                }
                vforIndex({ ctx.itemCount }) { item, _, _ ->
                    ctx.demoItem(item, Color.GREEN).invoke(this)
                }
                event {
                    scroll { param ->
                        ctx.callbackScrollerOffsetY = param.offsetY
                    }
                    scrollToTop {
                        ctx.scrollToTopCallbackCount++
                        KLog.i(TAG, "scrollToTop callback fired, count=${ctx.scrollToTopCallbackCount}")
                    }
                }
            }

            // 用例 3：List（与 Scroller 同一个 native 实现），期望默认回顶
            Text {
                attr {
                    marginTop(10f)
                    fontSize(14f)
                    text("3) List(默认回顶) offsetY=${ctx.listOffsetY}")
                }
            }
            List {
                attr {
                    size(pagerData.pageViewWidth, SCROLL_VIEW_HEIGHT)
                }
                vforIndex({ ctx.itemCount }) { item, _, _ ->
                    ctx.demoItem(item, Color.YELLOW).invoke(this)
                }
                event {
                    scroll { param ->
                        ctx.listOffsetY = param.offsetY
                    }
                }
            }
        }
    }

    private fun demoItem(index: Int, color: Color): ViewBuilder {
        return {
            View {
                attr {
                    width(pagerData.pageViewWidth)
                    height(60f)
                    backgroundColor(color)
                }
                Text {
                    attr {
                        alignSelfCenter()
                        fontSize(24f)
                        text("item $index")
                    }
                }
            }
        }
    }

    override fun created() {
        repeat(20) {
            itemCount.add(it)
        }
    }
}
