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

#include "libohos_render/context/KRRenderNativeContextHandlerManager.h"

#include "libohos_render/context/DefaultRenderNativeContextHandler.h"
#include "libohos_render/scheduler/KRContextScheduler.h"

extern CallKotlin callKotlin_;

void KRRenderNativeContextHandlerManager::SetContextHandlerCreator(const KRRenderContextHandlerCreator &creator) {
    creator_ = creator;
}

std::shared_ptr<IKRRenderNativeContextHandler> KRRenderNativeContextHandlerManager::CreateContextHandler(
    const std::shared_ptr<KRRenderContextParams> &context_params) {
    KRRenderContextHandlerCreator creator_;
    if (context_params->ExecuteMode()) {
        std::unordered_map<int, KRRenderContextHandlerCreator> context_creator_register =
            GetContextHandlerCreatorRegister();
        int param_mode = context_params->ExecuteMode()->GetMode();
        if (context_creator_register.find(param_mode) != context_creator_register.end()) {
            creator_ = context_creator_register[param_mode];  //  优先使用自定义注册的创建器
        } else if (auto native_mode = dynamic_cast<KRRenderNativeMode *>(context_params->ExecuteMode().get())) {
            auto context_handler_register = [](const std::shared_ptr<KRRenderContextParams> &context_params)
                -> std::shared_ptr<IKRRenderNativeContextHandler> {
                return std::make_shared<DefaultRenderNativeContextHandler>();
            };
            creator_ = context_handler_register;
        }
    }
    if (creator_) {
        return creator_(context_params);
    } else {
        throw std::runtime_error("Custom execute mode, contextHandler must be registered");
    }
}

void KRRenderNativeContextHandlerManager::RegisterContextHandler(
    const std::string &instanceId, const std::shared_ptr<IKRRenderNativeContextHandler> &contextHandler) {
    context_handler_map_.Set(instanceId, contextHandler);
}

void KRRenderNativeContextHandlerManager::UnregisterContextHandler(const std::string &instanceId) {
    context_handler_map_.Erase(instanceId);
}

static inline std::shared_ptr<KRRenderValue> MakeFromCValue(const KRRenderCValue &cValue) {
    if (kuikly::util::json::GetType(cValue) == KRJSON_NULL) {
        return KRRenderValue::MakeNull();  // 复用静态单例，避免堆分配
    }
    return KRRenderValue::MakeBorrowed(cValue);
}

KRRenderCValue KRRenderNativeContextHandlerManager::DispatchCallNative(
    const std::string &instanceId, int methodId, const KRRenderCValue &arg0, const KRRenderCValue &arg1,
    const KRRenderCValue &arg2, const KRRenderCValue &arg3, const KRRenderCValue &arg4, const KRRenderCValue &arg5) {
    auto handler = context_handler_map_.Get(instanceId);
    // 优化：移除冗余的 KRRenderManager::GetInstance().GetRenderView(instanceId) 检查
    // context_handler_map_ 中存在 handler 就意味着实例有效（注册/注销是配对的），
    // 额外的 GetRenderView 每次都要获取 SpinLock + unordered_map 查找，纯属浪费。
    if (!handler) {
        return kuikly::util::json::NewNull();
    }
    // 优化：cv0（原 instanceId 槽位）已被 ICallNativeCallback::OnCallNative 的接口契约声明为
    // “保留位”，实现方不得依赖其内容；此处直接传入 KRRenderValue::MakeNull() 静态单例，
    // 避免每次调用都构造一个 std::string 并分配 shared_ptr。
    // 如未来需要恢复 instanceId 传递，请先同步修改 IKRRenderNativeContextHandler.h 中
    // ICallNativeCallback::OnCallNative 的契约注释，再改本处构造逻辑，避免形成静默约定。
    auto cv0 = KRRenderValue::MakeNull();
    auto cv1 = MakeFromCValue(arg1);
    auto cv2 = MakeFromCValue(arg2);
    auto cv3 = MakeFromCValue(arg3);
    auto cv4 = MakeFromCValue(arg4);
    auto cv5 = MakeFromCValue(arg5);

    auto return_value =
        handler->OnCallNative(static_cast<KuiklyRenderNativeMethod>(methodId), cv0, cv1, cv2, cv3, cv4, cv5);
    if (return_value == nullptr || return_value->isNull()) {
        return kuikly::util::json::NewNull();
    }
    // The C result is an owned reference. Kotlin converts it immediately and
    // releases this transfer reference; containers retain their own shell ref.
    return kuikly::util::json::Retain(return_value->jsonValue());
}
