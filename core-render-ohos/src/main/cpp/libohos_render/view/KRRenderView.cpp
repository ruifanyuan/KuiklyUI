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

#include "libohos_render/view/KRRenderView.h"

#include <algorithm>
#include <functional>
#include <vector>
#include "libohos_render/context/IKRRenderNativeContextHandler.h"
#include "libohos_render/manager/KRWeakObjectManager.h"
#include "libohos_render/manager/KRRenderManager.h"
#include "libohos_render/scheduler/IKRScheduler.h"
#include "libohos_render/scheduler/KRContextScheduler.h"
#include "libohos_render/scheduler/KRUIScheduler.h"
#include "libohos_render/utils/KRRenderLoger.h"
#include "libohos_render/utils/KRViewUtil.h"

static constexpr char PAGER_EVENT_FIRST_FRAME_PAINT[] = "pageFirstFramePaint";
static constexpr char KR_PERFORMANCE_MODULE[] = "KRPerformanceModule";
static constexpr char NOTIFY_INIT_STATE[] = "notifyInitState";

const unsigned int LOG_PRINT_DOMAIN = 0xFF01;
static std::string GetIncreaseCallbackId() {
    static int gCallbackId = 0;
    gCallbackId++;
    return NewKRRenderValue(gCallbackId)->toString();
}

KRRenderView::KRRenderView(ArkUI_NodeContentHandle handle, std::string instance_id) : IKRRenderView(), node_content_handle_((handle)) {
    unsigned long long id_value = std::atoi(instance_id.c_str());
    OH_ArkUI_NodeContent_SetUserData(handle, (void*)id_value);
    auto cb = [](ArkUI_NodeContentEvent* event){
        auto event_type = OH_ArkUI_NodeContentEvent_GetEventType(event);
        if(event_type == NODE_CONTENT_EVENT_ON_DETACH_FROM_WINDOW){
            auto handle = OH_ArkUI_NodeContentEvent_GetNodeContentHandle(event);
            void *user_data = OH_ArkUI_NodeContent_GetUserData(handle);
            unsigned long long id_value = (unsigned long long)user_data;
            std::string instance_id = std::to_string(id_value);
            if (auto instance = KRRenderManager::GetInstance().GetRenderView(instance_id)) {
                std::shared_ptr<KRRenderView> render_view = std::dynamic_pointer_cast<KRRenderView>(instance);
                if(render_view){
                    render_view->RemoveRootViewFromContentHandle(true);
                }
            }
        }
    };
    OH_ArkUI_NodeContent_RegisterCallback(handle, cb);
}

KRRenderView::~KRRenderView() {
    RemoveRootViewFromContentHandle(false);
//    if (node_content_handle_ && root_node_){
//        ArkUI_NodeContentHandle content_handle = node_content_handle_;
//        ArkUI_NodeHandle root_node = root_node_;
//
//        KRMainThread::RunOnMainThread([content_handle, root_node] {
//            OH_ArkUI_NodeContent_RemoveNode(content_handle, root_node);
//        });
//    }
    CleanupGestures();
    if(root_node_ != nullptr){
        ArkUI_NodeHandle root_node = root_node_;
        KRMainThread::RunOnMainThread([root_node] {
            kuikly::util::GetNodeApi()->disposeNode(root_node);
        });
        root_node_ = nullptr;
    }
    node_content_handle_ = nullptr;
    native_resources_manager_ = nullptr;
    method_arg_callback_map_.clear();
}

void KRRenderView::RemoveRootViewFromContentHandle(bool immediate){
    if(node_content_handle_ && root_node_){
        ArkUI_NodeContentHandle content_handle = node_content_handle_;
        ArkUI_NodeHandle root_node = root_node_;
        
        auto unregister_and_remove = [content_handle, root_node](){
            OH_ArkUI_NodeContent_RegisterCallback(content_handle, [](ArkUI_NodeContentEvent*){}); // set callback to noop
            OH_ArkUI_NodeContent_RemoveNode(content_handle, root_node);
        };
        
        if(immediate){
            unregister_and_remove();
        }else{
            KRMainThread::RunOnMainThread([unregister_and_remove] {
                unregister_and_remove();
            });
        }
        
        node_content_handle_ = nullptr;
    }
}

void KRRenderView::WillDestroy(const std::string &instanceId) {
    core_->WillDealloc(instanceId);
    // send event to call
    // delay destroy for core
}

/**
 * 发送页面事件到kotlin侧
 * @param event_name 事件名
 * @param json_data json数据字符串）
 */
void KRRenderView::SendEvent(std::string event_name, const std::string &json_data) {
    if (core_) {
        return core_->SendEvent(event_name, json_data);
    }
}

bool KRRenderView::syncSendEvent(const std::string &event_name) {
    // 与 ETS 侧常量保持一致：'onBackPressed'
    if (event_name == "onBackPressed") {
        return true;
    }
    return false;
}

/**
 * 获取渲染节点视图（要求在主线程调用）
 * @param tag 所在tag
 * @return 对应节点view
 */
std::shared_ptr<IKRRenderViewExport> KRRenderView::GetView(int tag) {
    if (core_) {
        return core_->GetView(tag);
    }
    return nullptr;
}

/**
 * 获取渲染节点视图（要求在主线程调用）
 * @param tag 所在tag
 * @return 对应节点view
 */
std::shared_ptr<IKRRenderModuleExport> KRRenderView::GetModule(const std::string &module_name) {
    if (core_) {
        return core_->GetModule(module_name);
    }
    return nullptr;
}

std::shared_ptr<IKRRenderModuleExport> KRRenderView::GetModuleOrCreate(const std::string &module_name) {
    if (core_) {
        return core_->GetModuleOrCreate(module_name);
    }
    return nullptr;
}

void KRRenderView::AddContentView(const std::shared_ptr<IKRRenderViewExport> contentView, int index) {
    if (root_node_ == nullptr) {
        return;
    }
    kuikly::util::GetNodeApi()->addChild(root_node_, contentView->GetNode());
    if (!is_load_finish) {  //  首帧事件
        is_load_finish = true;
        OnFirstFramePaint();
    }
}

/**
 * 添加任务到主线程队列中，注意：调用接口所在线程须是context线程
 * @param task
 */
void KRRenderView::AddTaskToMainQueueWithTask(const KRSchedulerTask &task) {
    if (core_ != nullptr) {
        core_->AddTaskToMainQueueWithTask(task);
    }
}

void KRRenderView::PerformTaskWhenMainThreadEnd(const KRSchedulerTask &task) {
    if (core_ != nullptr) {
        core_->PerformTaskWhenMainThreadEnd(task);
    }
}

bool KRRenderView::IsPerformMainTasking() {
    if (core_ != nullptr) {
        return core_->IsPerformMainTasking();
    }
    return false;
}

const ArkUI_ContextHandle &KRRenderView::GetUIContextHandle() const {
    return ui_context_handle_;
}

KRSnapshotManager *KRRenderView::GetSnapshotManager() {
    return &snapshot_manager_;
}

NativeResourceManager *KRRenderView::GetNativeResourceManager() {
    return native_resources_manager_;
}

std::shared_ptr<KRPerformanceManager> KRRenderView::GetPerformanceManager() {
    return performance_manager_;
}

std::shared_ptr<KRRenderContextParams> KRRenderView::GetContext() {
    return context_;
}

void KRRenderView::Init(std::shared_ptr<KRRenderContextParams> context, ArkUI_ContextHandle &ui_context_handle,
                        NativeResourceManager *native_resources_manager, float width, float height,
                        int64_t launch_time) {
    context_ = context;
    ui_context_handle_ = ui_context_handle;
    native_resources_manager_ = native_resources_manager;
    performance_manager_ = std::make_shared<KRPerformanceManager>(context->PageName(), context->ExecuteMode());
    performance_manager_->SetArkLaunchTime(launch_time);
    root_view_width_ = width;
    root_view_height_ = height;
    InitRender(width, height);
}

void KRRenderView::OnRenderViewSizeChanged(float width, float height) {
    KR_LOG_INFO << "KRRenderView CreateRenderNode";
    if (root_node_ == nullptr) {
        return;
    }
    if (fabs(root_view_width_ - width) >= 0.1 || fabs(root_view_height_ - height) >= 0.1) {
        root_view_width_ = width;
        root_view_height_ = height;
        KR_LOG_INFO << "KRRenderView render view size did changed";
        kuikly::util::UpdateNodeSize(root_node_, width, height);
        // 尺寸变化更新到Kotlin
        KRRenderValue::Map data;
        data["width"] = std::make_shared<KRRenderValue>(width);
        data["height"] = std::make_shared<KRRenderValue>(height);
        auto json_data = std::make_shared<KRRenderValue>(data)->toString();
        SendEvent("rootViewSizeDidChanged", json_data);
    }
}

void KRRenderView::InitRender(float width, float height) {
    if (root_node_ != nullptr ||  node_content_handle_ == nullptr ||
        context_ == nullptr) {
        return;
    }
    DispatchInitState(KRInitState::kStateKRRenderViewInit);
    auto start = std::chrono::steady_clock::now();

    auto nodeAPI = kuikly::util::GetNodeApi();
    root_node_ = nodeAPI->createNode(ARKUI_NODE_STACK);
    kuikly::util::UpdateNodeSize(root_node_, width, height);
    kuikly::util::UpdateNodeBackgroundColor(root_node_, 0);
     if (node_content_handle_) {
        OH_ArkUI_NodeContent_AddNode(node_content_handle_, root_node_);
    }
    auto self = shared_from_this();
    DispatchInitState(KRInitState::kStateInitCoreStart);
    core_ = std::make_shared<KRRenderCore>(self, context_);
    core_->DidInit();
    DispatchInitState(KRInitState::kStateInitCoreFinish);

    // 初始化手势识别
    InitGestures();

    // 获取操作完成后的时间点
    auto end = std::chrono::steady_clock::now();
    // 计算时间差（以毫秒为单位）
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    KR_LOG_INFO << "cost time first screen: " << duration.count();
}

/**
 * 注册参数Callback
 * @return 该Callback索引ID, 用于GetArgCallback
 */
std::string KRRenderView::GenerateArgCallbackId(const KRRenderCallback &callback, bool callback_keep_alive,
                                                bool arg_prefer_raw_napi_value) {
    auto callback_id = GetIncreaseCallbackId();
    method_arg_callback_map_[callback_id] =
        std::make_shared<KRArkTsCallbackWrapper>(callback, callback_keep_alive, arg_prefer_raw_napi_value);
    return callback_id;
}

/**
 * 根据callbackid获取Callback
 */
KRRenderCallback KRRenderView::GetArgCallback(std::string callbackId, bool &arg_prefer_raw_napi_value) {
    if (method_arg_callback_map_.find(callbackId) != method_arg_callback_map_.end()) {
        auto callback_wrapper = method_arg_callback_map_[callbackId];
        if (!callback_wrapper->IsKeepAlive()) {
            method_arg_callback_map_.erase(callbackId);
        }
        arg_prefer_raw_napi_value = callback_wrapper->ArgPrefersRawNapiValue();
        return callback_wrapper->GetCallback();
    }
    return nullptr;
}

void KRRenderView::OnFirstFramePaint() {
    DispatchInitState(KRInitState::kStateFirstFramePaint);
    SendEvent(PAGER_EVENT_FIRST_FRAME_PAINT, "{}");
}

KRPoint KRRenderView::GetRootNodePositionInWindow() const {
    if (root_node_ == nullptr) {
        return KRPoint{0.0f, 0.0f};
    }

    return kuikly::util::GetNodePositionInWindow(root_node_);
}

void KRRenderView::DispatchInitState(KRInitState state) {
    switch (state) {
    case KRInitState::kStateKRRenderViewInit:
        performance_manager_->OnKRRenderViewInit();
        break;
    case KRInitState::kStateInitCoreStart:
        performance_manager_->OnInitCoreStart();
        break;
    case KRInitState::kStateInitCoreFinish:
        performance_manager_->OnInitCoreFinish();
        break;
    case KRInitState::kStateInitContextStart:
        performance_manager_->OnInitContextStart();
        break;
    case KRInitState::kStateInitContextFinish:
        performance_manager_->OnInitContextFinish();
        break;
    case KRInitState::kStateCreateInstanceStart:
        performance_manager_->OnCreateInstanceStart();
        break;
    case KRInitState::kStateCreateInstanceFinish:
        performance_manager_->OnCreateInstanceFinish();
        break;
    case KRInitState::kStateFirstFramePaint:
       performance_manager_->OnFirstFramePaint();
    default:
        break;
    }
    // 通知ArkTS侧
    std::string instance_id = context_->InstanceId();
    KRContextScheduler::ScheduleTaskOnMainThread(false, [instance_id, state] {
        KRArkTSManager::GetInstance().CallArkTSMethod(instance_id, KRNativeCallArkTSMethod::CallModuleMethod,
            NewKRRenderValue(KR_PERFORMANCE_MODULE), NewKRRenderValue(NOTIFY_INIT_STATE),
            NewKRRenderValue(static_cast<int>(state)), nullptr, nullptr, nullptr);
    });
}

void KRRenderView::InitGestures() {
    if (root_node_ == nullptr) {
        return;
    }
    
    KREnsureMainThread();
    auto gesture_api = kuikly::util::GetGestureApi();
    
    // 创建手势组
    gesture_group_ = gesture_api->createGroupGesture(ArkUI_GroupGestureMode::PARALLEL_GROUP);
    
    // 创建 Long Press 手势识别器（1个手指，不重复，持续250ms）
    long_press_gesture_recognizer_ = gesture_api->createLongPressGesture(1, false, 250);
    void* long_press_user_data = KRWeakObjectManagerRegisterWeakObject(std::dynamic_pointer_cast<KRRenderView>(shared_from_this()));
    gesture_api->setGestureEventTarget(
        long_press_gesture_recognizer_,
        GESTURE_EVENT_ACTION_ACCEPT | GESTURE_EVENT_ACTION_UPDATE | GESTURE_EVENT_ACTION_END | GESTURE_EVENT_ACTION_CANCEL,
        long_press_user_data,
        OnLongPressGestureEvent);
    gesture_api->addChildGesture(gesture_group_, long_press_gesture_recognizer_);
    
    // 创建 Pan 手势识别器（1个手指，所有方向，最小距离5px）
    pan_gesture_recognizer_ = gesture_api->createPanGesture(1, GESTURE_DIRECTION_ALL, 5);
    void* pan_user_data = KRWeakObjectManagerRegisterWeakObject(std::dynamic_pointer_cast<KRRenderView>(shared_from_this()));
    gesture_api->setGestureEventTarget(
        pan_gesture_recognizer_,
        GESTURE_EVENT_ACTION_ACCEPT | GESTURE_EVENT_ACTION_UPDATE | GESTURE_EVENT_ACTION_END | GESTURE_EVENT_ACTION_CANCEL,
        pan_user_data,
        OnPanGestureEvent);
    gesture_api->addChildGesture(gesture_group_, pan_gesture_recognizer_);
    
    // 将手势组添加到根节点
    gesture_api->addGestureToNode(root_node_, gesture_group_, ArkUI_GesturePriority::NORMAL, static_cast<ArkUI_GestureMask>(0));
}

void KRRenderView::CleanupGestures() {
    if (root_node_ == nullptr) {
        return;
    }
    
    KREnsureMainThread();
    auto gesture_api = kuikly::util::GetGestureApi();
    
    if (gesture_group_ != nullptr) {
        // 从节点移除手势组
        if (root_node_ != nullptr) {
            gesture_api->removeGestureFromNode(root_node_, gesture_group_);
        }
        
        // 从组中移除并释放手势识别器
        if (long_press_gesture_recognizer_ != nullptr) {
            gesture_api->removeChildGesture(gesture_group_, long_press_gesture_recognizer_);
            gesture_api->dispose(long_press_gesture_recognizer_);
            long_press_gesture_recognizer_ = nullptr;
        }
        
        if (pan_gesture_recognizer_ != nullptr) {
            gesture_api->removeChildGesture(gesture_group_, pan_gesture_recognizer_);
            gesture_api->dispose(pan_gesture_recognizer_);
            pan_gesture_recognizer_ = nullptr;
        }
        
        gesture_api->dispose(gesture_group_);
        gesture_group_ = nullptr;
    }
    
    is_long_press_triggered_ = false;
}

void KRRenderView::OnLongPressGestureEvent(ArkUI_GestureEvent *event, void *extraParams) {
    auto weak_view = KRWeakObjectManagerGetWeakObject<KRRenderView>(extraParams);
    if (auto strong_view = weak_view.lock()) {
        auto action_type = kuikly::util::GetArkUIGestureActionType(event);
        
        if (action_type == GESTURE_EVENT_ACTION_ACCEPT) {
            // Long Press 触发，标记状态以启用 Pan 手势处理
            strong_view->is_long_press_triggered_ = true;
            
            // 记录第一个点（选中区域的起始点）
            auto gesture_event_data = std::make_shared<KRGestureEventData>(event);
            strong_view->selection_point1_ = gesture_event_data->gesture_event_window_point_;
            strong_view->has_selection_start_point_ = true;
            
            // 调用 OnLongpressed 回调
            strong_view->OnLongpressed(gesture_event_data);
        } else if (action_type == GESTURE_EVENT_ACTION_END || action_type == GESTURE_EVENT_ACTION_CANCEL) {
            // Long Press 结束，清理选中区域状态
            auto gesture_event_data = std::make_shared<KRGestureEventData>(event);
            strong_view->OnLongpressed(gesture_event_data);
            strong_view->is_long_press_triggered_ = false;
            strong_view->has_selection_start_point_ = false;
        }
    }
}

void KRRenderView::OnPanGestureEvent(ArkUI_GestureEvent *event, void *extraParams) {
    auto weak_view = KRWeakObjectManagerGetWeakObject<KRRenderView>(extraParams);
    if (auto strong_view = weak_view.lock()) {
        // 只有在 Long Press 触发后才处理 Pan 手势
        if (strong_view->is_long_press_triggered_) {
            auto gesture_event_data = std::make_shared<KRGestureEventData>(event);
            auto action_type = kuikly::util::GetArkUIGestureActionType(event);
            
            if (action_type == GESTURE_EVENT_ACTION_UPDATE) {
                // Pan Update 时更新第二个点并调用 FindNodesInSelectionArea
                if (strong_view->has_selection_start_point_) {
                    strong_view->selection_point2_ = gesture_event_data->gesture_event_window_point_;
                    
                    // 调用 FindNodesInSelectionArea 找出选中区域内的节点
                    std::vector<int> selected_nodes = strong_view->FindNodesInSelectionArea(
                        strong_view->selection_point1_, 
                        strong_view->selection_point2_
                    );
                    
                    // 可以在这里处理选中的节点，例如发送事件
                    KR_LOG_DEBUG << "Ruifan Selected nodes count: " << selected_nodes.size();
                    for (int tag : selected_nodes) {
                        KR_LOG_DEBUG << "Ruifan Selected node tag: " << tag;
                    }
                }
            } else if (action_type == GESTURE_EVENT_ACTION_END || action_type == GESTURE_EVENT_ACTION_CANCEL) {
                // Pan 结束或取消时清理状态
                strong_view->is_long_press_triggered_ = false;
                strong_view->has_selection_start_point_ = false;
            }
            
            strong_view->OnPan(gesture_event_data);
        }
    }
}

void KRRenderView::OnLongpressed(const std::shared_ptr<KRGestureEventData> &gesture_event_data) {
    if (!gesture_event_data) {
        return;
    }
    
    // 构建事件数据
    KRRenderValue::Map data;
    data["x"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_point_.x);
    data["y"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_point_.y);
    data["pageX"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_window_point_.x);
    data["pageY"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_window_point_.y);
    data["state"] = std::make_shared<KRRenderValue>(kuikly::util::GetArkUIGestureActionState(gesture_event_data->gesture_event_));
    
    auto json_data = std::make_shared<KRRenderValue>(data)->toString();
    //SendEvent("onLongpressed", json_data);
    KR_LOG_DEBUG<<"Ruifan OnLongpressed:"<<json_data;
}

void KRRenderView::OnPan(const std::shared_ptr<KRGestureEventData> &gesture_event_data) {
    if (!gesture_event_data) {
        return;
    }
    
    // 构建事件数据
    KRRenderValue::Map data;
    data["x"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_point_.x);
    data["y"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_point_.y);
    data["pageX"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_window_point_.x);
    data["pageY"] = std::make_shared<KRRenderValue>(gesture_event_data->gesture_event_window_point_.y);
    data["state"] = std::make_shared<KRRenderValue>(kuikly::util::GetArkUIGestureActionState(gesture_event_data->gesture_event_));
    
    auto json_data = std::make_shared<KRRenderValue>(data)->toString();
    //SendEvent("onPan", json_data);
    KR_LOG_DEBUG<<"Ruifan OnPan:"<<json_data;
}

// 辅助函数：获取节点的大小（考虑 translate 变换）
static KRRect GetNodeBounds(ArkUI_NodeHandle node) {
    if (!node) {
        return KRRect(0, 0, 0, 0);
    }
    
    // 获取考虑了 translate 的位置
    ArkUI_IntOffset positionWithTranslate;
    int32_t ret = OH_ArkUI_NodeUtils_GetPositionWithTranslateInWindow(node, &positionWithTranslate);
    if (ret != ARKUI_ERROR_CODE_NO_ERROR) {
        KR_LOG_ERROR << "Failed to get node position with translate, error code: " << ret;
        return KRRect(0, 0, 0, 0);
    }
    
    // 获取布局大小
    ArkUI_IntSize layoutSize;
    ret = OH_ArkUI_NodeUtils_GetLayoutSize(node, &layoutSize);
    if (ret != ARKUI_ERROR_CODE_NO_ERROR) {
        KR_LOG_ERROR << "Failed to get node layout size, error code: " << ret;
        return KRRect(0, 0, 0, 0);
    }
    
    // 转换为 vp 单位（与 GetNodePositionInWindow 保持一致）
    double dpi = KRConfig::GetDpi();
    float x = static_cast<float>(positionWithTranslate.x / dpi);
    float y = static_cast<float>(positionWithTranslate.y / dpi);
    float width = static_cast<float>(layoutSize.width / dpi);
    float height = static_cast<float>(layoutSize.height / dpi);
    
    return KRRect(x, y, width, height);
}

// 辅助函数：判断矩形是否与选中区域相交
static bool IsRectInSelectionArea(const KRRect &rect, const KRPoint &point1, const KRPoint &point2) {
    // 计算选中区域的边界
    float min_x = 0;// std::min(point1.x, point2.x);
    float max_x = 1000;// std::max(point1.x, point2.x);
    float min_y = std::min(point1.y, point2.y) /  KRConfig::GetDpi();
    float max_y = std::max(point1.y, point2.y) /  KRConfig::GetDpi();
    
    // 判断矩形是否与选中区域相交
    // 矩形在选中区域内：矩形的任何部分在选中区域内
    bool intersects = !(rect.x + rect.width < min_x || rect.x > max_x ||
                        rect.y + rect.height < min_y || rect.y > max_y);
    
    return intersects;
}

void KRRenderView::IterateOverChildren(ArkUI_NodeHandle node,const KRPoint &point1, const KRPoint &point2, int depth){
    KRRect bounds = GetNodeBounds(node);
    if (!IsRectInSelectionArea(bounds, point1, point2)) {
        return;
    }
    
    auto view = core_->GetView(node);
    std::string spacing(depth * 4, ' ');
    KR_LOG_DEBUG<<spacing<<"Ruifan node selected:"<<node<< ", type:"<<OH_ArkUI_NodeUtils_GetNodeType(node)<<", view name:"<<(view ? view->GetViewName() : "");
    if(view){
        if(view->GetViewName() == "KRGradientRichTextView" || view->GetViewName() == "KRRichTextView"){
            view->SetSelected(true);
        }
    }
    ++depth;
    for (int i = 0; i < kuikly::util::GetNodeApi()->getTotalChildCount(node); ++i){
        ArkUI_NodeHandle child = kuikly::util::GetNodeApi()->getChildAt(node, i);
        // 获取节点边界
        KRRect bounds = GetNodeBounds(child);
        
        // 判断节点是否在选中区域内
        if (IsRectInSelectionArea(bounds, point1, point2)) {
//            int cnt = kuikly::util::GetNodeApi()->getTotalChildCount(child);
//            if (cnt > 0){
                IterateOverChildren(child, point1, point2, depth);
//            }
        }
    }
}

std::vector<int> KRRenderView::FindNodesInSelectionArea(const KRPoint &point1, const KRPoint &point2) {
    std::vector<int> result;
    
    if (!core_ || root_node_ == nullptr) {
        return result;
    }
    
    KREnsureMainThread();
    
    // 通过 core_ 获取所有视图并检查它们是否在选中区域内
    // 由于无法直接访问 view_registry_，我们通过尝试获取视图的方式
    // 这是一个折中方案：遍历一个合理的 tag 范围
    
    // 更实用的方法：遍历一个合理的 tag 范围（例如 1-10000）
    // 对于实际使用，建议添加一个 GetAllViewTags 方法到 IKRRenderLayer
    const int MAX_TAG_TO_CHECK = 10000;
    KR_LOG_DEBUG<<"Ruifan node selected begin";
    IterateOverChildren(root_node_, point1, point2, 0);
    KR_LOG_DEBUG<<"Ruifan node selected end";
//    for (int tag = 1; tag <= MAX_TAG_TO_CHECK; tag++) {
//        auto view = core_->GetView(tag);
//        if (view == nullptr) {
//            continue;
//        }
//        
//        ArkUI_NodeHandle node = view->GetNode();
//        if (node == nullptr) {
//            continue;
//        }
//        
//        // 获取节点边界
//        KRRect bounds = GetNodeBounds(node);
//        
//        // 判断节点是否在选中区域内
//        if (IsRectInSelectionArea(bounds, point1, point2)) {
//            result.push_back(tag);
//        }
//    }
    
    return result;
}
