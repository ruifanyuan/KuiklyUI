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

#ifndef CORE_RENDER_OHOS_KRRICHTEXTVIEW_H
#define CORE_RENDER_OHOS_KRRICHTEXTVIEW_H
#include <utility>
#include <arkui/native_node.h>
#include "libohos_render/expand/components/base/KRCustomUserCallback.h"
#include "libohos_render/expand/components/richtext/KRParagraph.h"
#include "libohos_render/export/IKRRenderShadowExport.h"
#include "libohos_render/export/IKRRenderViewExport.h"
#include "libohos_render/foundation/KRPoint.h"
#include "libohos_render/view/IKRRenderView.h"

class KRLineInfo{
public:
    OH_Drawing_LineMetrics line_metrics_;
    std::vector<KRRect> rects_; // rect for each offset
};
class KRParagraphInfo{
public:
    // get selection between two points
    std::vector<KRRect> GetSelectionRects(KRPoint start, KRPoint end);
    // get selection from this point to the end
    std::vector<KRRect> GetSelectionRectsToEnd(KRPoint point);
    // get selection from the beginning to this point
    std::vector<KRRect> GetSelectionRectsUpTo(KRPoint start);
    // select all
    std::vector<KRRect> GetSelectionRectsAll();
    
    int GetOffsetAtPoint(KRPoint point);
    std::vector<KRRect> GetRectsBetweenOffsets(int start, int end);
    
    std::vector<KRLineInfo> line_info_list_;
    std::string text_content_;
};

class KRRichTextView : public IKRRenderViewExport {
 public:
    ArkUI_NodeHandle CreateNode() override;

    void DidInit() override;

    void OnDestroy() override;

    bool ReuseEnable() override;

    void SetShadow(const std::shared_ptr<IKRRenderShadowExport> &shadow) override;

    void DidMoveToParentView() override;
    void DidRemoveFromParentView() override;

    void OnCustomEvent(ArkUI_NodeCustomEvent *event, const ArkUI_NodeCustomEventType &event_type) override;

    void SetRenderViewFrame(const KRRect &frame) override;

    void ToSetProp(const std::string &prop_key, const KRAnyValue &prop_value,
                   const KRRenderCallback event_call_back = nullptr) override;
    
    void SetSelectionAll();
    void SetSelection(KRPoint start, KRPoint end);
    void SetSelectionToEnd(KRPoint point);
    void SetSelectionUpTo(KRPoint point);

    std::pair<int,int> GetTextRangeAtPoint(KRPoint point);
    KRParagraphInfo GetParagraphInfo();
    
 private:
    OH_Drawing_Typography *last_typo_ = nullptr;
    OH_Drawing_Array* text_lines_ = nullptr;
    std::vector<KRRect> selection_rects_;
    std::shared_ptr<KRParagraph> paragraph_;
    std::shared_ptr<IKRRenderShadowExport> shadow_;
    float last_draw_frame_width_ = -1.0;
    void OnForegroundDraw(ArkUI_NodeCustomEvent *event);
};

#endif  // CORE_RENDER_OHOS_KRRICHTEXTVIEW_H
