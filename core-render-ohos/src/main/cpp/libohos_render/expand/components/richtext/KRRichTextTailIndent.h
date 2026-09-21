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

#ifndef CORE_RENDER_OHOS_KRRICHTEXTTAILINDENT_H
#define CORE_RENDER_OHOS_KRRICHTEXTTAILINDENT_H

#include <deviceinfo.h>
#include <native_drawing/drawing_error_code.h>
#include <native_drawing/drawing_text_typography.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
extern OH_Drawing_ErrorCode OH_Drawing_SetTypographyStyleAttributeDoubleArray(
    OH_Drawing_TypographyStyle *style, OH_Drawing_TypographyStyleAttributeId id, double *arrayValue,
    size_t arrayLength) __attribute__((weak));
#ifdef __cplusplus
}
#endif

constexpr int kKRLineBreakMarginMinApiLevel = 18;
constexpr int kKRTextTailIndentApiLevel = 26;

inline bool KRLineBreakMarginFeatureAvailable() {
    return OH_GetSdkApiVersion() >= kKRLineBreakMarginMinApiLevel;
}

inline bool KRTextTailIndentApiAvailable() {
    return &OH_Drawing_SetTypographyStyleAttributeDoubleArray != nullptr &&
           OH_GetSdkApiVersion() >= kKRTextTailIndentApiLevel;
}

inline bool KRApplyLineBreakTailIndent(OH_Drawing_TypographyStyle *style, int number_of_lines, float margin_px) {
    if (style == nullptr || number_of_lines <= 0 || margin_px <= 0 || !KRTextTailIndentApiAvailable()) {
        return false;
    }
    std::vector<double> indents(static_cast<size_t>(number_of_lines), 0.0);
    indents.back() = static_cast<double>(margin_px);
    return OH_Drawing_SetTypographyStyleAttributeDoubleArray(style, TYPOGRAPHY_STYLE_ATTR_DA_LINE_TAIL_INDENT,
                                                             indents.data(), indents.size()) == OH_DRAWING_SUCCESS;
}

#endif  // CORE_RENDER_OHOS_KRRICHTEXTTAILINDENT_H
