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
import com.tencent.kuikly.core.module.Module
import com.tencent.kuikly.core.pager.Pager
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.demo.pages.base.BridgeModule

/**
 * 自研 DSL 示例页：黄色背景，页面正中放一个 120x120 的红色按钮。
 * 点击按钮通过 BridgeModule.requestOrientation 在横屏 / 竖屏之间切换。
 */
@Page("OrientationButtonDemo")
internal class OrientationButtonDemo : Pager() {

    override fun createExternalModules(): Map<String, Module>? {
        return hashMapOf(BridgeModule.MODULE_NAME to BridgeModule())
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.YELLOW)
                allCenter()
            }
            View {
                attr {
                    size(120f, 120f)
                    backgroundColor(Color.RED)
                    borderRadius(12f)
                    allCenter()
                }
                event {
                    click {
                        val pageData = ctx.pageData
                        val isLandscape = pageData.pageViewWidth > pageData.pageViewHeight
                        val bridgeModule = ctx.acquireModule<BridgeModule>(BridgeModule.MODULE_NAME)
                        val target = if (isLandscape) "portrait" else "landscape"
                        if (isLandscape) {
                            bridgeModule.requestPortrait()
                        } else {
                            bridgeModule.requestLandscape()
                        }
                        println(
                            "[BD_OrientationButtonDemo] click size=" +
                                "${pageData.pageViewWidth}x${pageData.pageViewHeight} " +
                                "isLandscape=$isLandscape request=$target",
                        )
                    }
                }
                Text {
                    attr {
                        text("旋转")
                        color(Color.WHITE)
                        fontSize(18f)
                    }
                }
            }
        }
    }
}
