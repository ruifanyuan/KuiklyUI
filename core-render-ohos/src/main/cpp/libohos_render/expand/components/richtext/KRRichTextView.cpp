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

#include "libohos_render/expand/components/richtext/KRRichTextView.h"
#include <native_drawing/drawing_text_line.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_shader_effect.h>
#include "libohos_render/expand/components/base/KRCustomUserCallback.h"
#include "libohos_render/expand/components/richtext/KRRichTextShadow.h"

ArkUI_NodeHandle KRRichTextView::CreateNode() {
    return kuikly::util::GetNodeApi()->createNode(ARKUI_NODE_TEXT);
}

void KRRichTextView::OnDestroy() {
    kuikly::util::GetNodeApi()->resetAttribute(GetNode(), NODE_TEXT_CONTENT_WITH_STYLED_STRING);

    IKRRenderViewExport::OnDestroy();
    auto self = shared_from_this();
    KREventDispatchCenter::GetInstance().UnregisterCustomEvent(self);
    shadow_ = nullptr;
}

bool KRRichTextView::ReuseEnable() {
    return true;
}

void KRRichTextView::SetRenderViewFrame(const KRRect &frame) {
    IKRRenderViewExport::SetRenderViewFrame(frame);
}

void KRRichTextView::OnCustomEvent(ArkUI_NodeCustomEvent *event, const ArkUI_NodeCustomEventType &event_type) {
    if (event_type == ARKUI_NODE_CUSTOM_EVENT_ON_FOREGROUND_DRAW) {
        OnForegroundDraw(event);
    }
}

void KRRichTextView::DidInit() {
    IKRRenderViewExport::DidInit();
}

void KRRichTextView::SetShadow(const std::shared_ptr<IKRRenderShadowExport> &shadow) {
    shadow_ = shadow;
    KR_LOG_DEBUG<<"set shadow "<<shadow.get()<<", to view "<<this;

    auto textShadow = std::dynamic_pointer_cast<KRRichTextShadow>(shadow);
    if(textShadow && textShadow->StyledStringEnabled()){
        ArkUI_AttributeItem item;
        if(std::shared_ptr<KRParagraph> paragraph = std::dynamic_pointer_cast<KRRichTextShadow>(shadow)->GetParagraph()){
            item.object = paragraph->GetStyledString();
            kuikly::util::GetNodeApi()->setAttribute(GetNode(), NODE_TEXT_CONTENT_WITH_STYLED_STRING, &item);
            // Note:
            // The ownership of the styled string is not going to be transferred,
            // by setting the style item to the note by calling 
            // setAttribute with NODE_TEXT_CONTENT_WITH_STYLED_STRING.
            // Besides, it is not reference counted,
            // we need to make sure it is alive after setting it to the node.
            paragraph_ = paragraph;
        }
    }else {
        KREventDispatchCenter::GetInstance().RegisterCustomEvent(shared_from_this(), ARKUI_NODE_CUSTOM_EVENT_ON_FOREGROUND_DRAW);
        kuikly::util::GetNodeApi()->markDirty(GetNode(), NODE_NEED_RENDER);
    }
}

void KRRichTextView::DidMoveToParentView() {
    IKRRenderViewExport::DidMoveToParentView();
    auto self = shared_from_this();
    KREventDispatchCenter::GetInstance().RegisterCustomEvent(self, ARKUI_NODE_CUSTOM_EVENT_ON_FOREGROUND_DRAW);
}

void KRRichTextView::DidRemoveFromParentView() {
    kuikly::util::GetNodeApi()->resetAttribute(GetNode(), NODE_TEXT_CONTENT_WITH_STYLED_STRING);
    IKRRenderViewExport::DidRemoveFromParentView();
    shadow_ = nullptr;
    paragraph_ = nullptr;
    last_draw_frame_width_ = -1.0;
}

static bool KRCaretOffsetsCallback(double offset, int32_t index, bool leadingEdge){
    return true;
}
template <class Facet> struct deletable_facet : Facet {
    template <class... Args> deletable_facet(Args &&...args) : Facet(std::forward<Args>(args)...) {}
    ~deletable_facet() {}
};
void KRRichTextView::OnForegroundDraw(ArkUI_NodeCustomEvent *event) {
    if (shadow_ == nullptr && GetFrame().width == 0) {
        KR_LOG_ERROR << "OnForegroundDraw, shadow or frame not ready, shadow:" << shadow_.get()
                     << ", frame width:" << GetFrame().width;
        return;
    }
    if (auto rootView = GetRootView().lock()) {
        if (rootView->IsPerformMainTasking()) {
            std::weak_ptr<IKRRenderViewExport> weakSelf = shared_from_this();
            KRMainThread::RunOnMainThreadForNextLoop([weakSelf] {
                if(auto strongSelf = weakSelf.lock()){
                    kuikly::util::GetNodeApi()->markDirty(strongSelf->GetNode(), NODE_NEED_RENDER);
                }
            });
#ifndef NDEBUG
            KR_LOG_ERROR << "OnForegroundDraw, IsPerformMainTasking Skip:" << shadow_.get();
#endif
            return;
        }
    }
    auto richTextShadow = reinterpret_cast<KRRichTextShadow *>(shadow_.get());
    if (richTextShadow == nullptr) {
        KR_LOG_ERROR << "OnForegroundDraw, richTextShadow null";
        return;
    }
    OH_Drawing_Typography *textTypo = richTextShadow->MainThreadTypography();
    if (textTypo == nullptr) {
        KR_LOG_ERROR << "OnForegroundDraw, textTypo null, shadow:" << richTextShadow;
        return;
    }
    double drawOffsetY = richTextShadow->DrawOffsetY();
    OH_Drawing_TextAlign textAlign = richTextShadow->TextAlign();
    auto textTypoSize = richTextShadow->MainMeasureSize();
    // 在容器前景上绘制额外图形，实现图形显示在子组件之上。
    auto *drawContext = OH_ArkUI_NodeCustomEvent_GetDrawContextInDraw(event);
    auto *drawingHandle = reinterpret_cast<OH_Drawing_Canvas *>(OH_ArkUI_DrawContext_GetCanvas(drawContext));
    auto frameWidth = GetFrame().width;
    bool needReLayout = false;
    if (last_draw_frame_width_ > 0 && fabs(last_draw_frame_width_ - frameWidth) > 0.01) {
        needReLayout = true;
    }
    if (fabs(textTypoSize.width - frameWidth) > 1 || textAlign != TEXT_ALIGN_LEFT) {
        needReLayout = true;
    }
    if (needReLayout) {
        auto dpi = KRConfig::GetDpi();
        OH_Drawing_TypographyLayout(textTypo, frameWidth * dpi);
        last_draw_frame_width_ = frameWidth;
        if (textAlign != TEXT_ALIGN_LEFT) {
            richTextShadow->ResetTextAlign();
        }
    }
    // Note: turn this on only when absolutely needed in testing build
    // KR_LOG_INFO<<"OnForegroundDraw, frameWidth:"<<frameWidth<<", shadow:"<<richTextShadow<<", node
    // handle"<<this->GetNode();
#if 0
    if (IsSelected()){
        // Draw background color before drawing text
        auto frame = GetFrame();
        OH_Drawing_Brush *backgroundBrush = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetColor(backgroundBrush, 0x33007DFF);  // ARGB format: alpha=255, RGB=0x007DFF
        OH_Drawing_Path *backgroundPath = OH_Drawing_PathCreate();
        OH_Drawing_PathMoveTo(backgroundPath, 0, 0);
        OH_Drawing_PathLineTo(backgroundPath, frameWidth * KRConfig::GetDpi(), 0);
        OH_Drawing_PathLineTo(backgroundPath, frameWidth * KRConfig::GetDpi(), frame.height * KRConfig::GetDpi());
        OH_Drawing_PathLineTo(backgroundPath, 0, frame.height * KRConfig::GetDpi());
        OH_Drawing_PathClose(backgroundPath);
        OH_Drawing_CanvasAttachBrush(drawingHandle, backgroundBrush);
        OH_Drawing_CanvasDrawPath(drawingHandle, backgroundPath);
        OH_Drawing_CanvasDetachBrush(drawingHandle);
        OH_Drawing_BrushDestroy(backgroundBrush);
        OH_Drawing_PathDestroy(backgroundPath);
    }
#endif
{
    double density = KRConfig::GetDpi();
    OH_Drawing_Brush *backgroundBrush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(backgroundBrush, 0x33007DFF);  // ARGB format: alpha=255, RGB=0x007DFF
    OH_Drawing_CanvasAttachBrush(drawingHandle, backgroundBrush);
    OH_Drawing_Path *backgroundPath = OH_Drawing_PathCreate();
    for(const KRRect &rect : selection_rects_){
        OH_Drawing_PathReset(backgroundPath);
        OH_Drawing_PathMoveTo(backgroundPath, rect.x * density, rect.y * density);
        OH_Drawing_PathLineTo(backgroundPath, (rect.x + rect.width) * density, rect.y  * density);
        OH_Drawing_PathLineTo(backgroundPath, (rect.x + rect.width) * density, (rect.y + rect.height) * density);
        OH_Drawing_PathLineTo(backgroundPath, rect.x * density, (rect.y + rect.height) * density);
        OH_Drawing_PathClose(backgroundPath);
        OH_Drawing_CanvasDrawPath(drawingHandle, backgroundPath);
    }
    OH_Drawing_CanvasDetachBrush(drawingHandle);
    OH_Drawing_BrushDestroy(backgroundBrush);
    OH_Drawing_PathDestroy(backgroundPath);
}
#if 1
//bool OH_Drawing_TypographyGetLineInfo(OH_Drawing_Typography* typography, int lineNumber, bool oneLine,
//    bool includeWhitespace, OH_Drawing_LineMetrics* drawingLineMetrics);
    {
        std::string text_content = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->GetTextContent();
        int lineCount = OH_Drawing_TypographyGetLineCount(textTypo);
        for(int i = 0; i < lineCount; ++i){
            OH_Drawing_LineMetrics metrics;
            OH_Drawing_TypographyGetLineInfo(textTypo, i, true, true, &metrics);
            if(true || lineCount > 1){
                KR_LOG_DEBUG<<"metrics "<<i+1<<"/"<<lineCount<<":"<<metrics.x<<","<<metrics.y<<","<<metrics.width<<","<<metrics.height<<", index"<<metrics.startIndex<<","<<metrics.endIndex<<", content:"<<text_content;
            }
        }
    }
    OH_Drawing_TypographyPaint(textTypo, drawingHandle, 0, -drawOffsetY);
#else
//    if(std::shared_ptr<KRParagraph> paragraph = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->GetParagraph()){
//        auto styled_str = paragraph->GetStyledString();
//        textTypo = OH_ArkUI_StyledString_CreateTypography(styled_str);
//        OH_Drawing_TypographyLayout(textTypo, frameWidth * KRConfig::GetDpi());
//        auto textWidth = OH_Drawing_TypographyGetLongestLine(textTypo);
//        last_draw_frame_width_ = frameWidth;
//        OH_Drawing_TypographyPaint(textTypo, drawingHandle, 0, -drawOffsetY);
//        return;
//    }
    auto last_typo = last_typo_;
    auto new_typo = textTypo;
    bool destroyed_old_text_lines = false;
    bool get_text_lines = false;
//    if (last_typo_ != textTypo) {
//        if (text_lines_){
//            OH_Drawing_DestroyTextLines(text_lines_);
//            destroyed_old_text_lines = false;
//        }
//        text_lines_ = OH_Drawing_TypographyGetTextLines(textTypo);
//        last_typo_ = textTypo;
//        get_text_lines = true;
//    }
    text_lines_ = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->lines();

    OH_Drawing_TextLine* targetLine = NULL;
    double paintX = 0.0;  // 记录排版绘制位置
    double paintY = 0.0;  // 记录排版绘制位置
    double lineY = paintY;  // 当前行的 Y 坐标
    double screenX = 0;
    double screenY = 0;
//    OH_Drawing_Brush *backgroundBrush = OH_Drawing_BrushCreate();
//    OH_Drawing_BrushSetColor(backgroundBrush, 0x33FF7DFF);  // ARGB format: alpha=255, RGB=0x007DFF
        
    {
        // 
        //typedef bool (*Drawing_CaretOffsetsCallback)(double offset, int32_t index, bool leadingEdge);

/**
 * @brief Enumerate caret offset and index in text lines.
 *
 * @syscap SystemCapability.Graphic.Graphic2D.NativeDrawing
 * @param line Indicates the pointer to an <b>OH_Drawing_TextLine</b> object.
 * @param callback User-defined callback functions, see <b>Drawing_CaretOffsetsCallback</b>.
 * @since 18
 */
//void OH_Drawing_TextLineEnumerateCaretOffsets(OH_Drawing_TextLine* line, Drawing_CaretOffsetsCallback callback);
        
    }
    
    std::string text_content = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->GetTextContent();
    size_t lineCount = OH_Drawing_GetDrawingArraySize(text_lines_);
    if(text_content.size() > 200){
        printf("");
    }
    for(size_t i = 0; i < lineCount; ++i){
        OH_Drawing_TextLine* line = OH_Drawing_GetTextLineByIndex(text_lines_, i);
        
        //assert(OH_Drawing_TextLineGetGlyphCount(line) > 0);
        KR_LOG_DEBUG<<"glyph count "<<OH_Drawing_TextLineGetGlyphCount(line)<<",view:"<<this<<",line:"<<line<<", last typo:"<<last_typo<<",new typo:"<<new_typo<<",destroyed_old_text_lines:"<<destroyed_old_text_lines<<", get_text_lines:"<<get_text_lines<<", at line "<<i<<"/"<<lineCount<<", saved last typo:"<<last_typo_<<",shadow:"<<shadow_.get();

        double ascent = 0;
        double descent = 0;
        double leading = 0;
        double w = OH_Drawing_TextLineGetTypographicBounds(line, &ascent, &descent, &leading);
        OH_Drawing_Rect* bounds = OH_Drawing_TextLineGetImageBounds(line);
        double lineTop = OH_Drawing_RectGetTop(bounds);
        double lineBottom = OH_Drawing_RectGetBottom(bounds);   
        double lineLeft = OH_Drawing_RectGetLeft(bounds);
        double lineRight = OH_Drawing_RectGetRight(bounds);
        
        //OH_Drawing_TextLineEnumerateCaretOffsets(line, [](double offset, int32_t index, bool leadingEdge){});
        size_t text_start = 0;
        size_t text_end;
        std::vector<double> offsets;
        
        OH_Drawing_TextLineGetTextRange(line, &text_start, &text_end);

        std::wstring_convert<deletable_facet<std::codecvt<char16_t, char, std::mbstate_t>>, char16_t> conv16, conv2;
        std::u16string start_part;
        if(text_start > 0){
            conv2.from_bytes(text_content.c_str(), text_content.c_str() + text_start);
        }
        std::u16string str16 = conv16.from_bytes(text_content.c_str() + text_start, text_content.c_str() + text_end);
        //std::u16string str16 = conv16.from_bytes(text_content);
        int codePointCount = str16.size();

        for(size_t text_index = start_part.size(); text_index < codePointCount + start_part.size(); ++text_index){
            double offset = OH_Drawing_TextLineGetOffsetForStringIndex(line, text_index);
            offsets.push_back(offset);
        }

//OH_Drawing_TextBox* OH_Drawing_TypographyGetRectsForRange(OH_Drawing_Typography* typography,
//    size_t start, size_t end, OH_Drawing_RectHeightStyle heightStyle, OH_Drawing_RectWidthStyle widthStyle);
    OH_Drawing_TextBox *text_boxes = OH_Drawing_TypographyGetRectsForRange(textTypo, text_start, text_end, RECT_HEIGHT_STYLE_TIGHT, RECT_WIDTH_STYLE_TIGHT);
    size_t rectCount = OH_Drawing_GetSizeOfTextBox(text_boxes);
        
        // Get coordinates for each rectangle
        for (int i = 0; i < rectCount; i++) {
            float left = OH_Drawing_GetLeftFromTextBox(text_boxes, i);
            float right = OH_Drawing_GetRightFromTextBox(text_boxes, i);
            float top = OH_Drawing_GetTopFromTextBox(text_boxes, i);
            float bottom = OH_Drawing_GetBottomFromTextBox(text_boxes, i); 
        }



        OH_Drawing_TextLinePaint( line, drawingHandle, 0, -drawOffsetY);
    }
    
#endif
}

void KRRichTextView::ToSetProp(const std::string &prop_key, const KRAnyValue &prop_value,
                               const KRRenderCallback event_callback) {
    if (kuikly::util::isEqual(prop_key, "click")) {
        std::weak_ptr<KRRichTextView> weakSelf = std::dynamic_pointer_cast<KRRichTextView>(shared_from_this());
        KRRenderCallback middleManCallback = [weakSelf, event_callback](KRAnyValue res) {
            auto strongSelf = weakSelf.lock();
            if(strongSelf == nullptr){
                return;
            }
            if (res->isMap()) {
                const auto oldParam = res->toMap();
                const auto x = oldParam.find("x");
                const auto y = oldParam.find("y");

                KRRenderValueMap params;
                if (x != oldParam.end()) {
                    params["x"] = x->second;
                }

                if (y != oldParam.end()) {
                    params["y"] = y->second;
                }

                const auto pageX = oldParam.find("pageX");
                const auto pageY = oldParam.find("pageY");
                if (pageX != oldParam.end()) {
                    params["pageX"] = pageX->second;
                }
                if (pageY != oldParam.end()) {
                    params["pageY"] = pageY->second;
                }

                if (auto richTextShadow = std::dynamic_pointer_cast<KRRichTextShadow>(strongSelf->shadow_)) {
                    int index = richTextShadow->SpanIndexAt(x->second->toFloat(), y->second->toFloat());
                    if (index < 0) {
                        index = 0;
                    }
                    params["index"] = NewKRRenderValue(index);
                }
                event_callback(NewKRRenderValue(params));
            } else {
                event_callback(res);
            }
        };
        IKRRenderViewExport::ToSetProp(prop_key, prop_value, middleManCallback);
    } else {
        IKRRenderViewExport::ToSetProp(prop_key, prop_value, event_callback);
    }
}

std::vector<KRRect> KRParagraphInfo::GetSelectionRectsToEnd(KRPoint point_in){
    float density = KRConfig::GetDpi();
    KRPoint point(point_in.x * KRConfig::GetDpi(), point_in.y  * density);

    int start_line_index = -1;
    int start_rect_index = -1;
    int end_line_index = -1;
    int end_rect_index = -1;
    int start_text_index = -1;
    int end_text_index = -1;
    int text_index = 0;
    for (int line_index = 0; line_index < line_info_list_.size(); ++line_index){
        const KRLineInfo &line_info = line_info_list_[line_index];
        if (start_line_index == -1){
            for(int rect_index = 0; rect_index < line_info.rects_.size(); ++rect_index){
                if(line_info.rects_[rect_index].ContainsPoint(point)){
                    start_line_index = line_index;
                    start_rect_index = rect_index;
                    start_text_index = text_index;
                }
    
                ++text_index;
            }
        } else {
            text_index += line_info.rects_.size();
        }
    }
    end_line_index = line_info_list_.size() - 1;
    end_rect_index = line_info_list_.back().rects_.size() - 1;
    end_text_index = text_index;

    KR_LOG_DEBUG_WITH_TAG("Text Selection")<<"text content:"<<text_content_<<", range:"<<start_text_index<<","<<end_text_index;

    std::vector<KRRect> selected_rect_list;
    if (start_line_index == -1 && end_line_index == -1){
        return selected_rect_list;
    }
    if(start_line_index == end_line_index){
        // same line
        const KRLineInfo &line_info = line_info_list_[start_line_index];
        const KRRect &start_rect = line_info.rects_[start_rect_index];
        const KRRect &end_rect = line_info.rects_[end_rect_index];
        KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
KR_LOG_DEBUG_WITH_TAG("Text Selection")<<"text content:"<<text_content_<<", range:"<<start_text_index<<","<<end_text_index<<", rect:"<<result.x<<","<<result.y<<","<<result.width<<","<<result.height;
        selected_rect_list.push_back(result);
    }else{
        for (int line_index = start_line_index; line_index <= end_line_index; ++line_index){
            if (line_index == start_line_index){
                const KRLineInfo &line_info = line_info_list_[line_index];
                const KRRect &start_rect = line_info.rects_[line_index];
                const KRRect &end_rect = line_info.rects_.back();
                KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
                selected_rect_list.push_back(result);
                continue;
            }
    
            if (line_index == end_line_index){
                const KRLineInfo &line_info = line_info_list_[line_index];
                const KRRect &start_rect = line_info.rects_.front();
                const KRRect &end_rect = line_info.rects_[line_index];
                KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
                selected_rect_list.push_back(result);
                continue;
            }

            const KRLineInfo &line_info = line_info_list_[line_index];
            KRRect result(line_info.line_metrics_.x / density, line_info.line_metrics_.y / density, line_info.line_metrics_.width / density, line_info.line_metrics_.height / density);
            selected_rect_list.push_back(result);
        }
    }

    KR_LOG_DEBUG<<"selected rect list size:"<<selected_rect_list.size();
    return selected_rect_list;
}

std::vector<KRRect> KRParagraphInfo::GetSelectionRectsAll(){
    float density = KRConfig::GetDpi();
    std::vector<KRRect> selected_rect_list;

    for (int line_index = 0; line_index < line_info_list_.size(); ++line_index){
        const KRLineInfo &line_info = line_info_list_[line_index];
        KRRect result(line_info.line_metrics_.x / density,
                        line_info.line_metrics_.y / density,
                        line_info.line_metrics_.width / density,
                        line_info.line_metrics_.height / density);
        selected_rect_list.push_back(result);
    }

    return selected_rect_list;
}

std::vector<KRRect> KRParagraphInfo::GetSelectionRectsUpTo(KRPoint point_in){
    float density = KRConfig::GetDpi();
    KRPoint point(point_in.x * KRConfig::GetDpi(), point_in.y  * density);

    int start_line_index = 0;
    int start_rect_index = 0;
    int end_line_index = -1;
    int end_rect_index = -1;
    int start_text_index = 0;
    int end_text_index = -1;
    int text_index = 0;
    for (int line_index = 0; line_index < line_info_list_.size(); ++line_index){
        const KRLineInfo &line_info = line_info_list_[line_index];
        for(int rect_index = 0; rect_index < line_info.rects_.size(); ++rect_index){
            if(line_info.rects_[rect_index].ContainsPoint(point)){
                end_line_index = line_index;
                end_rect_index = rect_index;
                end_text_index = text_index;
                break;
            }

            ++text_index;
        }
        if(end_line_index != -1){
            break;
        }
    }

    KR_LOG_DEBUG_WITH_TAG("Text Selection")<<"text content:"<<text_content_<<", range:"<<start_text_index<<","<<end_text_index;

    std::vector<KRRect> selected_rect_list;
    if (start_line_index == -1 && end_line_index == -1){
        return selected_rect_list;
    }
    if(start_line_index == end_line_index){
        // same line
        const KRLineInfo &line_info = line_info_list_[start_line_index];
        const KRRect &start_rect = line_info.rects_[start_rect_index];
        const KRRect &end_rect = line_info.rects_[end_rect_index];
        KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
KR_LOG_DEBUG_WITH_TAG("Text Selection")<<"text content:"<<text_content_<<", range:"<<start_text_index<<","<<end_text_index<<", rect:"<<result.x<<","<<result.y<<","<<result.width<<","<<result.height;
        selected_rect_list.push_back(result);
    }else{
        for (int line_index = start_line_index; line_index <= end_line_index; ++line_index){
            if (line_index == start_line_index){
                const KRLineInfo &line_info = line_info_list_[line_index];
                const KRRect &start_rect = line_info.rects_[line_index];
                const KRRect &end_rect = line_info.rects_.back();
                KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
                selected_rect_list.push_back(result);
                continue;
            }
    
            if (line_index == end_line_index){
                const KRLineInfo &line_info = line_info_list_[line_index];
                const KRRect &start_rect = line_info.rects_.front();
                const KRRect &end_rect = line_info.rects_[line_index];
                KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
                selected_rect_list.push_back(result);
                continue;
            }

            const KRLineInfo &line_info = line_info_list_[line_index];
            KRRect result(line_info.line_metrics_.x / density, line_info.line_metrics_.y / density, line_info.line_metrics_.width / density, line_info.line_metrics_.height / density);
            selected_rect_list.push_back(result);
        }
    }

    KR_LOG_DEBUG<<"selected rect list size:"<<selected_rect_list.size();
    return selected_rect_list;
}

std::vector<KRRect> KRParagraphInfo::GetSelectionRects(KRPoint p0, KRPoint p1){
    float density = KRConfig::GetDpi();
    KRPoint start(p0.x * KRConfig::GetDpi(), p0.y  * density);
    KRPoint end(p1.x * KRConfig::GetDpi(), p1.y  * density);
    int start_line_index = -1;
    int start_rect_index = -1;
    int end_line_index = -1;
    int end_rect_index = -1;
    int start_text_index = -1;
    int end_text_index = -1;
    int text_index = 0;
    for (int line_index = 0; line_index < line_info_list_.size(); ++line_index){
        const KRLineInfo &line_info = line_info_list_[line_index];
        for(int rect_index = 0; rect_index < line_info.rects_.size(); ++rect_index){
            if(line_info.rects_[rect_index].ContainsPoint(start)){
                start_line_index = line_index;
                start_rect_index = rect_index;
                start_text_index = text_index;
            }
            if(line_info.rects_[rect_index].ContainsPoint(end)){
                end_line_index = line_index;
                end_rect_index = rect_index;
                end_text_index = text_index;
            }
            ++text_index;
        }
    }

    if(start.x <= 0 && start.y <= 0){
        start_line_index = 0;
        start_rect_index = 0;
        start_text_index = 0;
    }

    KR_LOG_DEBUG_WITH_TAG("Text Selection")<<"text content:"<<text_content_<<", range:"<<start_text_index<<","<<end_text_index;

    std::vector<KRRect> selected_rect_list;
    if (start_line_index == -1 && end_line_index == -1){
        return selected_rect_list;
    }
    if(start_line_index == end_line_index){
        // same line
        const KRLineInfo &line_info = line_info_list_[start_line_index];
        const KRRect &start_rect = line_info.rects_[start_rect_index];
        const KRRect &end_rect = line_info.rects_[end_rect_index];
        KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
KR_LOG_DEBUG_WITH_TAG("Text Selection")<<"text content:"<<text_content_<<", range:"<<start_text_index<<","<<end_text_index<<", rect:"<<result.x<<","<<result.y<<","<<result.width<<","<<result.height;
        selected_rect_list.push_back(result);
    }else{
        for (int line_index = start_line_index; line_index <= end_line_index; ++line_index){
            if (line_index == start_line_index){
                const KRLineInfo &line_info = line_info_list_[line_index];
                const KRRect &start_rect = line_info.rects_[line_index];
                const KRRect &end_rect = line_info.rects_.back();
                KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
                selected_rect_list.push_back(result);
                continue;
            }
    
            if (line_index == end_line_index){
                const KRLineInfo &line_info = line_info_list_[line_index];
                const KRRect &start_rect = line_info.rects_.front();
                const KRRect &end_rect = line_info.rects_[line_index];
                KRRect result(start_rect.x / density, start_rect.y / density, (end_rect.x - start_rect.x + end_rect.width) / density, end_rect.height / density);
                selected_rect_list.push_back(result);
                continue;
            }

            const KRLineInfo &line_info = line_info_list_[line_index];
            KRRect result(line_info.line_metrics_.x / density, line_info.line_metrics_.y / density, line_info.line_metrics_.width / density, line_info.line_metrics_.height / density);
            selected_rect_list.push_back(result);
        }
    }

    KR_LOG_DEBUG<<"selected rect list size:"<<selected_rect_list.size();
    return selected_rect_list;
}

void KRRichTextView::SetSelectionAll(){
    KRParagraphInfo info = GetParagraphInfo();
    selection_rects_ = info.GetSelectionRectsAll();
}
void KRRichTextView::SetSelectionToEnd(KRPoint point){
    KRParagraphInfo info = GetParagraphInfo();
    selection_rects_ = info.GetSelectionRectsToEnd(point);
}
void KRRichTextView::SetSelectionUpTo(KRPoint point){
    KRParagraphInfo info = GetParagraphInfo();
    selection_rects_ = info.GetSelectionRectsUpTo(point);
}
void KRRichTextView::SetSelection(KRPoint start, KRPoint end){
//    auto rich_text_shadow = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_);
//    OH_Drawing_Typography *text_typo = rich_text_shadow->MainThreadTypography();
//    
//    OH_Drawing_PositionAndAffinity *start_pa = OH_Drawing_TypographyGetGlyphPositionAtCoordinate(text_typo, start.x, start.y);
//    OH_Drawing_PositionAndAffinity *end_pa = OH_Drawing_TypographyGetGlyphPositionAtCoordinate(text_typo, end.x, end.y);
    KRParagraphInfo info = GetParagraphInfo();
    selection_rects_ = info.GetSelectionRects(start, end);
    
    std::pair<int,int> start_range = GetTextRangeAtPoint(start);
    std::pair<int,int> end_range = GetTextRangeAtPoint(end);
    std::pair<int,int> end_range2 = GetTextRangeAtPoint(KRPoint());
    std::pair<int,int> end_range3 = GetTextRangeAtPoint(KRPoint(120.0/KRConfig::GetDpi(), 0));
    std::pair<int,int> end_range4 = GetTextRangeAtPoint(KRPoint(129.0/KRConfig::GetDpi(), 0));
    std::pair<int,int> end_range5 = GetTextRangeAtPoint(KRPoint(130.0/KRConfig::GetDpi(), 0));

#if 0
    size_t pos = OH_Drawing_GetPositionFromPositionAndAffinity(start_pa);
    if (OH_Drawing_Range *range = OH_Drawing_TypographyGetWordBoundary(text_typo, pos)){
        size_t start = OH_Drawing_GetStartFromRange(range);
        size_t end = OH_Drawing_GetEndFromRange(range);
        start_range.first = start;
        start_range.second = end;
    }
    pos = OH_Drawing_GetPositionFromPositionAndAffinity(end_pa);
    if (OH_Drawing_Range *range = OH_Drawing_TypographyGetWordBoundary(text_typo, pos)){
        size_t start = OH_Drawing_GetStartFromRange(range);
        size_t end = OH_Drawing_GetEndFromRange(range);
        KR_LOG_DEBUG_WITH_TAG("Selection Test")<<"start:"<<start<<", end:"<<end;
        end_range.first = start;
        end_range.second = end;
    }
#endif
    KR_LOG_DEBUG_WITH_TAG("Selection Test")<<"start range:"<<start_range.first<<","<<start_range.second<<", end range:"<<end_range.first<<","<<end_range.second;
}

std::pair<int,int> KRRichTextView::GetTextRangeAtPoint(KRPoint point){
    std::pair<int,int> result_range;
#if 0
    std::pair<int,int> result_range;
    auto richTextShadow = reinterpret_cast<KRRichTextShadow *>(shadow_.get());
    OH_Drawing_Typography *textTypo = richTextShadow->MainThreadTypography();
    
    auto offset = richTextShadow->DrawOffsetY();
    OH_Drawing_PositionAndAffinity *pos_and_affinity = OH_Drawing_TypographyGetGlyphPositionAtCoordinateWithCluster(textTypo, point.x * KRConfig::GetDpi(), point.y * KRConfig::GetDpi() );
    size_t pos = OH_Drawing_GetPositionFromPositionAndAffinity(pos_and_affinity);
    if (OH_Drawing_Range *range = OH_Drawing_TypographyGetWordBoundary(textTypo, pos)){
        size_t start = OH_Drawing_GetStartFromRange(range);
        size_t end = OH_Drawing_GetEndFromRange(range);
        KR_LOG_DEBUG_WITH_TAG("Selection Test")<<"start:"<<start<<", end:"<<end;
        result_range.first = start;
        result_range.second = end;
    }
    
    OH_Drawing_TextBox* textBox = OH_Drawing_TypographyGetRectsForRange(
        textTypo,
        result_range.first,      // Start position
        result_range.second,         // End position
        RECT_HEIGHT_STYLE_TIGHT,  // Height style
        RECT_WIDTH_STYLE_TIGHT    // Width style
    );
    // Get number of rectangles (text may span multiple lines)
    size_t rectCount = OH_Drawing_GetSizeOfTextBox(textBox);
    
    // Get coordinates for each rectangle
    for (int i = 0; i < rectCount; i++) {
        float left = OH_Drawing_GetLeftFromTextBox(textBox, i);
        float right = OH_Drawing_GetRightFromTextBox(textBox, i);
        float top = OH_Drawing_GetTopFromTextBox(textBox, i);
        float bottom = OH_Drawing_GetBottomFromTextBox(textBox, i);
        
        KR_LOG_DEBUG_WITH_TAG("Selection Test")<<"Point:"<<point.x* KRConfig::GetDpi()<<","<<point.y* KRConfig::GetDpi()<<", range:"<<result_range.first<<","<<result_range.second<<", rect "<<i+1<<"/"<<rectCount<<" left:"<<left<<",right:"<<right<<",top:"<<top<<",bottom:"<<bottom;
    }

///////////////////////////////// plan b /////////////////////////////////////////////////////////

#else
    OH_Drawing_Typography *textTypo = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->MainThreadTypography();

    {
        
        std::string text_content = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->GetTextContent();
        int lineCount = OH_Drawing_TypographyGetLineCount(textTypo);
        for(int i = 0; i < lineCount; ++i){
            OH_Drawing_LineMetrics metrics;
            OH_Drawing_TypographyGetLineInfo(textTypo, i, true, true, &metrics);

//            OH_Drawing_TextBox* OH_Drawing_TypographyGetRectsForRange(OH_Drawing_Typography* typography,
//    size_t start, size_t end, OH_Drawing_RectHeightStyle heightStyle, OH_Drawing_RectWidthStyle widthStyle);
            for(int index = metrics.startIndex; index < metrics.endIndex; ++index){
                OH_Drawing_TextBox* boxes = OH_Drawing_TypographyGetRectsForRange(textTypo, index, index + 1, RECT_HEIGHT_STYLE_TIGHT, RECT_WIDTH_STYLE_TIGHT);
                size_t rectCount = OH_Drawing_GetSizeOfTextBox(boxes);
                for(int i = 0; i < rectCount; ++i){
                    float left = OH_Drawing_GetLeftFromTextBox(boxes, i);
                    float right = OH_Drawing_GetRightFromTextBox(boxes, i);
                    float top = OH_Drawing_GetTopFromTextBox(boxes, i);
                    float bottom = OH_Drawing_GetBottomFromTextBox(boxes, i);
    
                    // Calculate width and height
                    float width = right - left;
                    float height = bottom - top;
                }
                OH_Drawing_TypographyDestroyTextBox(boxes);
            }

            if(true || lineCount > 1){
                KR_LOG_DEBUG<<"metrics "<<i+1<<"/"<<lineCount<<":"<<metrics.x<<","<<metrics.y<<","<<metrics.width<<","<<metrics.height<<", index"<<metrics.startIndex<<","<<metrics.endIndex<<", content:"<<text_content;
            }
        }
    }

    //OH_Drawing_ArrayDestroy(textLines);
    kuikly::util::GetNodeApi()->markDirty(GetNode(), NODE_NEED_RENDER);
#endif
    return result_range;
}

KRParagraphInfo KRRichTextView::GetParagraphInfo(){
    KRParagraphInfo paragraph_info;
    OH_Drawing_Typography *textTypo = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->MainThreadTypography();
    if (textTypo == nullptr){
        return paragraph_info;
    }

    std::string text_content = std::dynamic_pointer_cast<KRRichTextShadow>(shadow_)->GetTextContent();
    paragraph_info.text_content_ = text_content;
    int lineCount = OH_Drawing_TypographyGetLineCount(textTypo);
    for(int i = 0; i < lineCount; ++i){
        KRLineInfo line_info;
        OH_Drawing_TypographyGetLineInfo(textTypo, i, true, true, &line_info.line_metrics_);

        for(int index = line_info.line_metrics_.startIndex; index < line_info.line_metrics_.endIndex; ++index){
            OH_Drawing_TextBox* boxes = OH_Drawing_TypographyGetRectsForRange(textTypo, index, index + 1, RECT_HEIGHT_STYLE_TIGHT, RECT_WIDTH_STYLE_TIGHT);
            size_t count = OH_Drawing_GetSizeOfTextBox(boxes);
            assert(count == 1);
            for(int i = 0; i < count; ++i){
                float left = OH_Drawing_GetLeftFromTextBox(boxes, i);
                float right = OH_Drawing_GetRightFromTextBox(boxes, i);
                float top = OH_Drawing_GetTopFromTextBox(boxes, i);
                float bottom = OH_Drawing_GetBottomFromTextBox(boxes, i);

                // Calculate width and height
                float width = right - left;
                float height = bottom - top;
                
                line_info.rects_.emplace_back(KRRect(left, top, width, height));
            }
            OH_Drawing_TypographyDestroyTextBox(boxes);
        }
        paragraph_info.line_info_list_.emplace_back(line_info);

//        if(true || lineCount > 1){
//            KR_LOG_DEBUG<<"metrics "<<i+1<<"/"<<lineCount<<":"<<line_info.line_metrics_.x<<","<<line_info.line_metrics_.y<<","<<line_info.line_metrics_.width<<","<<line_info.line_metrics_.height<<", index"<<line_info.line_metrics_.startIndex<<","<<line_info.line_metrics_.endIndex<<", content:"<<text_content;
//        }
    }

    return paragraph_info;
}