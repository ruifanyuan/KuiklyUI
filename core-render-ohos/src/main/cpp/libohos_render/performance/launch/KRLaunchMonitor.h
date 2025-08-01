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

#ifndef CORE_RENDER_OHOS_KRLAUNCHMONITOR_H
#define CORE_RENDER_OHOS_KRLAUNCHMONITOR_H

#include "libohos_render/expand/modules/performance/KRPageCreateTrace.h"
#include "libohos_render/performance/KRMonitor.h"
#include "libohos_render/performance/launch/KRLaunchData.h"

class KRLaunchMonitor : public KRMonitor {
 public:
    KRLaunchMonitor() = default;
    void OnKRRenderViewInit() override;
    void OnInitCoreStart() override;
    void OnInitCoreFinish() override;
    void OnInitContextStart() override;
    void OnInitContextFinish() override;
    void OnCreateInstanceStart() override;
    void OnCreateInstanceFinish() override;
    void OnFirstFramePaint() override;
    void OnPageCreateFinish(KRPageCreateTrace &trace);
    std::string GetMonitorData() override;
    void SetArkLaunchTime(int64_t timestamp) override;
    static const char kMonitorName[];

 private:
    int64_t CurrentTimeMillis();
    int64_t event_time_stamps_[kEventCont] = {0};  //  记录启动各步骤时间戳
};
#endif  // CORE_RENDER_OHOS_KRLAUNCHMONITOR_H
