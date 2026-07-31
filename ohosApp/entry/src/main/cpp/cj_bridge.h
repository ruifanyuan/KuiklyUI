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

#ifndef KUIKLY_DEMO_CJ_BRIDGE_H
#define KUIKLY_DEMO_CJ_BRIDGE_H

// libkuikly_entry.so 与 libohos_app_cangjie_entry.so 之间的桥接 ABI。
//
// 本头文件被两处包含，二者签名必须严格一致：
//   * napi_init.cpp        —— 真实实现，编入 libkuikly_entry.so；
//   * cangjie_stub/cj_bridge_stub.c —— 空实现，编成 libkuikly_entry_stub.so
//     （SONAME 仍为 libkuikly_entry.so），仅供仓颉侧链接时解析符号，
//     运行时由真实产物提供实现。
// 修改本文件中任一签名后，必须重新运行 cangjie_stub/gen_stub.sh 生成桩。

#ifdef __cplusplus
extern "C" {
#endif

// 仓颉侧 MyExampleCModule callMethod 实现的函数指针类型，
// 与 index.cj 中 @C 修饰的 myExampleCModuleCallMethod 签名对齐。
typedef char *(*CjMyExampleCModuleCallMethod)(void *moduleInstance,
                                              const char *moduleName,
                                              int sync,
                                              const char *method,
                                              void *param,
                                              void *context);

// 由仓颉侧在模块加载时调用，注册其 callMethod 实现。
void RegisterCangjieMyExampleCModule(CjMyExampleCModuleCallMethod cb);

// KRRenderModuleDoCallback 的全局导出转发 wrapper，供仓颉侧跨库调用。
void KRDemoRenderModuleDoCallback(void *context, const char *data);

#ifdef __cplusplus
}
#endif

#endif  // KUIKLY_DEMO_CJ_BRIDGE_H
