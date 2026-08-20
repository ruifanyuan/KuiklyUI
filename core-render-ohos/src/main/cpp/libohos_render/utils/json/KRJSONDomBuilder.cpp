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

#include "libohos_render/utils/json/KRJSONDomBuilder.h"

#include <utility>

#include "libohos_render/utils/json/KRJSONValue.h"

namespace kuikly {
namespace util {
namespace json {

KRJSONDomBuilder::~KRJSONDomBuilder() {
    for (auto &frame : stack_) {
        Release(frame.container);
    }
    if (root_ != KRJSON_INVALID) {
        Release(root_);
    }
}

bool KRJSONDomBuilder::AddValue(KRJSONValue owned) {
    if (stack_.empty()) {
        if (root_ != KRJSON_INVALID) {
            Release(root_);  // defensive: more than one top-level value
        }
        root_ = owned;  // transfer ownership to root_
        return true;
    }
    Frame &top = stack_.back();
    if (TagOf(top.container) == kTagArray) {
        ArrayAppend(top.container, owned);  // container retains
        Release(owned);                     // drop our ref
    } else {  // object
        if (!top.has_key) {
            Release(owned);
            return false;  // value without a preceding key
        }
        ObjectPut(top.container, top.key.data(), top.key.size(), owned);  // container retains
        Release(owned);
        top.key.clear();
        top.has_key = false;
    }
    return true;
}

bool KRJSONDomBuilder::CloseContainer() {
    KRJSONValue container = stack_.back().container;  // owned
    stack_.pop_back();
    return AddValue(container);
}

bool KRJSONDomBuilder::OnNull() {
    return AddValue(NewNull());
}
bool KRJSONDomBuilder::OnBool(bool value) {
    return AddValue(NewBool(value));
}
bool KRJSONDomBuilder::OnInt(int64_t value) {
    return AddValue(NewInt(value));
}
bool KRJSONDomBuilder::OnUint(uint64_t value) {
    return AddValue(NewUint(value));
}
bool KRJSONDomBuilder::OnDouble(double value) {
    return AddValue(NewDouble(value));
}
bool KRJSONDomBuilder::OnString(const char *data, size_t length, bool /*copy*/) {
    return AddValue(NewString(data, length));
}

bool KRJSONDomBuilder::OnStartObject() {
    if (stack_.size() >= kMaxDepth) {
        return false;  // too deep; abort parse
    }
    stack_.push_back(Frame{NewObject(), std::string(), false});
    return true;
}
bool KRJSONDomBuilder::OnKey(const char *data, size_t length, bool /*copy*/) {
    if (stack_.empty()) {
        return false;
    }
    Frame &top = stack_.back();
    top.key.assign(data, length);
    top.has_key = true;
    return true;
}
bool KRJSONDomBuilder::OnEndObject(size_t /*member_count*/) {
    return CloseContainer();
}

bool KRJSONDomBuilder::OnStartArray() {
    if (stack_.size() >= kMaxDepth) {
        return false;  // too deep; abort parse
    }
    stack_.push_back(Frame{NewArray(), std::string(), false});
    return true;
}
bool KRJSONDomBuilder::OnEndArray(size_t /*element_count*/) {
    return CloseContainer();
}

KRJSONValue KRJSONDomBuilder::TakeResult() {
    KRJSONValue result = root_;
    root_ = KRJSON_INVALID;
    return result;
}

}  // namespace json
}  // namespace util
}  // namespace kuikly
