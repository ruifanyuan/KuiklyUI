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

// 链接期桩：编成文件名 libkuikly_entry_stub.so、SONAME 为 libkuikly_entry.so
// 的空壳共享库，只为让 libohos_app_cangjie_entry.so 在链接期解析这两个符号
// 并记录 DT_NEEDED。运行时动态链接器会解析到真正的 libkuikly_entry.so，
// 这里的空实现永不执行。详见同目录 README.md。

#include "../src/main/cpp/cj_bridge.h"

void RegisterCangjieMyExampleCModule(CjMyExampleCModuleCallMethod cb) {
    (void)cb;
}

void KRDemoRenderModuleDoCallback(void *context, const char *data) {
    (void)context;
    (void)data;
}
