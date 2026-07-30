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
import com.tencent.kuikly.core.log.KLog
import com.tencent.kuikly.core.module.Module
import com.tencent.kuikly.core.pager.IModuleCreator
import com.tencent.kuikly.core.reactive.handler.observable
import com.tencent.kuikly.core.views.List
import com.tencent.kuikly.core.views.Text
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.core.views.compose.Button
import com.tencent.kuikly.demo.pages.MyExampleCModule
import com.tencent.kuikly.demo.pages.base.BasePager
import com.tencent.kuikly.demo.pages.demo.base.NavBar

/**
 * 验证 [MyExampleCModule] 原生 Module 调用的 test page。
 *
 * MyExampleCModule 在鸿蒙侧由 C（napi_init.cpp 薄包装）+ 仓颉（index.cj 实现）桥接注册：
 *   - 同步调用：callMethod 同步返回 "<method> handled." 字符串；
 *   - 异步回调：传入 callback 时，仓颉实现会异步回写 {"key":"value"}。
 *
 * 注意：MyExampleCModule 目前仅在 OHOS 平台注册。非 OHOS 平台调用会安全降级
 *（C 侧尚未经 DemoSetCallback 收到仓颉 handler 时返回空结果），本页展示的回调/返回值为空属正常。
 */
@Page("MyExampleCModuleTestPage")
internal class MyExampleCModuleTestPage : BasePager() {
    private val testMethod = "exampleMethod"

    private var syncResult by observable("点击按钮进行同步调用")
    private var asyncResult by observable("点击按钮进行异步回调调用")
    private var isSyncing by observable(false)
    private var isAsyncCalling by observable(false)

    override fun created() {
        super.created()
        // 注册 MyExampleCModule 原生 Module 封装，便于在页面内通过 acquireModule 获取使用。
        registerModule(MyExampleCModule.MODULE_NAME, object : IModuleCreator {
            override fun createModule(): Module {
                return MyExampleCModule()
            }
        })
    }

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr { backgroundColor(Color.WHITE) }
            NavBar { attr { title = "MyExampleCModule Test" } }
            List {
                attr { flex(1f) }

                View {
                    attr {
                        margin(left = 16f, top = 16f, right = 16f, bottom = 8f)
                    }
                    Text {
                        attr {
                            text(
                                "验证 OHOS 原生 Module MyExampleCModule（C 薄包装 + 仓颉实现）。\n" +
                                    "同步调用返回 \"<method> handled.\"；\n" +
                                    "带 callback 的异步调用回写 {\"key\":\"value\"}。"
                            )
                            fontSize(14f)
                            color(Color(0xFF666666))
                        }
                    }
                }

                View {
                    attr {
                        alignItemsCenter()
                        marginTop(20f)
                    }
                    Button {
                        attr {
                            titleAttr {
                                text(if (ctx.isSyncing) "调用中..." else "同步调用 MyExampleCModule")
                                color(Color.WHITE)
                            }
                            backgroundColor(if (ctx.isSyncing) Color.GRAY else Color(0xFF007AFF))
                            size(width = 300f, height = 48f)
                            borderRadius(8f)
                        }
                        event {
                            click {
                                if (!ctx.isSyncing) {
                                    ctx.isSyncing = true
                                    val module = this@MyExampleCModuleTestPage.acquireModule<MyExampleCModule>(MyExampleCModule.MODULE_NAME)
                                    val res = module.callSync(ctx.testMethod, "hello")
                                    val resultText = res ?: "<null>（非 OHOS 平台或未注册，安全降级）"
                                    KLog.i(MyExampleCModule.MODULE_NAME, "sync call result=$resultText")
                                    ctx.syncResult = "同步返回值：\n$resultText"
                                    ctx.isSyncing = false
                                }
                            }
                        }
                    }
                }

                View {
                    attr {
                        alignItemsCenter()
                        marginTop(16f)
                    }
                    Button {
                        attr {
                            titleAttr {
                                text(if (ctx.isAsyncCalling) "调用中..." else "异步回调调用 MyExampleCModule")
                                color(Color.WHITE)
                            }
                            backgroundColor(if (ctx.isAsyncCalling) Color.GRAY else Color(0xFF34C759))
                            size(width = 300f, height = 48f)
                            borderRadius(8f)
                        }
                        event {
                            click {
                                if (!ctx.isAsyncCalling) {
                                    ctx.isAsyncCalling = true
                                    ctx.asyncResult = "等待异步回调..."
                                    val module = this@MyExampleCModuleTestPage.acquireModule<MyExampleCModule>(MyExampleCModule.MODULE_NAME)
                                    module.callAsync(ctx.testMethod, "hello") { cbRes ->
                                        val cbText = cbRes?.toString() ?: "<null>"
                                        KLog.i(MyExampleCModule.MODULE_NAME, "async callback result=$cbText")
                                        ctx.asyncResult = "异步回调值：\n$cbText"
                                        ctx.isAsyncCalling = false
                                    }
                                }
                            }
                        }
                    }
                }

                View {
                    attr {
                        margin(left = 16f, top = 24f, right = 16f, bottom = 16f)
                        backgroundColor(Color(0xFFF5F5F5))
                        borderRadius(8f)
                        padding(all = 16f)
                    }
                    Text {
                        attr {
                            text(ctx.syncResult)
                            fontSize(15f)
                            color(Color(0xFF333333))
                        }
                    }
                }

                View {
                    attr {
                        margin(left = 16f, top = 8f, right = 16f, bottom = 32f)
                        backgroundColor(Color(0xFFF5F5F5))
                        borderRadius(8f)
                        padding(all = 16f)
                    }
                    Text {
                        attr {
                            text(ctx.asyncResult)
                            fontSize(15f)
                            color(Color(0xFF333333))
                        }
                    }
                }
            }
        }
    }
}
