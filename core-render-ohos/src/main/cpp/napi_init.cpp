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
#include <ark_runtime/jsvm.h>
#include <arkui/native_node_napi.h>
#include <cstdint>
#include "libohos_render/expand/modules/back_press/KRBackPressModule.h"
#include "libohos_render/foundation/KRCallbackData.h"
#include "libohos_render/foundation/KRCommon.h"
#include "libohos_render/foundation/thread/KRMainThread.h"
#include "libohos_render/manager/KRArkTSManager.h"
#include "libohos_render/manager/KRRenderManager.h"
#include "libohos_render/utils/KRRenderLoger.h"
#include "libohos_render/utils/NAPIUtil.h"
#include "napi/native_api.h"

#ifndef NDEBUG
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "libohos_render/api/include/Kuikly/KRAnyData.h"
#include "libohos_render/api/src/KRAnyDataInternal.h"
#endif

//  ArkTs层页面加载事件
static napi_value OnLaunchStart(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return 0;
    }
    std::string instance_id = kuikly::util::getNApiArgsStdString(env, args[0]);
    KRRenderManager::GetInstance().OnLaunchStart(instance_id);
    return 0;
}
static napi_value UpdateConfig(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return 0;
    }
    std::string instance_id = kuikly::util::getNApiArgsStdString(env, args[0]);
    std::string config_json = kuikly::util::getNApiArgsStdString(env, args[1]);
    if (auto renderView = KRRenderManager::GetInstance().GetRenderView(instance_id)) {
        if (auto ctx = renderView->GetContext()) {
            renderView->GetContext()->Config()->Update(config_json);
        } else {
            KR_LOG_ERROR << "Config update failed, context null";
        }
    } else {
        KR_LOG_ERROR << "Config update failed, no render view";
    }
    return 0;
}

// 初始化render view
static napi_value OnInitRenderView(napi_env env, napi_callback_info info) {
    // args is page_name, page_data, width , height, config_json
    size_t argc = 8;
    napi_value args[8] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return 0;
    }
    std::string instance_id = kuikly::util::getNApiArgsStdString(env, args[0]);
    std::string page_name = kuikly::util::getNApiArgsStdString(env, args[1]);
    std::string page_data_json_str = kuikly::util::getNApiArgsStdString(env, args[2]);
    double renderViewWidth = kuikly::util::getNApiArgsDouble(env, args[3]);
    double renderViewHeight = kuikly::util::getNApiArgsDouble(env, args[4]);
    std::string config_json = kuikly::util::getNApiArgsStdString(env, args[5]);
    auto renderView = KRRenderManager::GetInstance().GetRenderView(instance_id);
    if (renderView != nullptr) {
        auto page_Data = KRRenderValue::Make(page_data_json_str == "" ? "{}" : page_data_json_str);
        auto context = std::make_shared<KRRenderContextParams>(page_name, page_Data, instance_id, config_json);
        ArkUI_ContextHandle context_handle;
        OH_ArkUI_GetContextFromNapiValue(env, args[6], &context_handle);
        NativeResourceManager *native_resources_manager = OH_ResourceManager_InitNativeResourceManager(env, args[7]);
        int64_t launch_time = KRRenderManager::GetInstance().GetLaunchStartTime(instance_id);
        renderView->Init(context, context_handle, native_resources_manager, renderViewWidth, renderViewHeight,
                         launch_time);
    } else {
        napi_throw_error(env, "-1006", "renderView is nil when get render view");
    }

    return 0;
}

// 销毁render view
static napi_value OnDestroyRenderView(napi_env env, napi_callback_info info) {
    // 1、从info中取出TS传递过来的参数放入args
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return 0;
    }
    std::string instanceId = kuikly::util::getNApiArgsStdString(env, args[0]);
    KRRenderManager::GetInstance().DestroyRenderView(instanceId);
    return 0;
}

// render view size 变化
static napi_value OnRenderViewSizeChanged(napi_env env, napi_callback_info info) {
    // 1、从info中取出TS传递过来的参数放入args
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return 0;
    }

    std::string instanceId = kuikly::util::getNApiArgsStdString(env, args[0]);
    double width = kuikly::util::getNApiArgsDouble(env, args[1]);
    double height = kuikly::util::getNApiArgsDouble(env, args[2]);
    auto renderView = KRRenderManager::GetInstance().GetRenderView(instanceId);
    if (renderView != nullptr) {
        renderView->OnRenderViewSizeChanged(width, height);
        napi_value result;
        napi_create_int32(env, 1, &result);
        return result;
    } else {
        napi_throw_error(env, "-1006", "renderView is nil when get render view");
    }
    return 0;
}

static napi_value ArkTSCallNative(napi_env env, napi_callback_info info) {
    // 1、从info中取出TS传递过来的参数放入args
    size_t argc = 8;
    napi_value args[8] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return 0;
    }
    KRArkTSManager::GetInstance().HandleArkTSCallNative(env, args, argc);
    return 0;
}

static napi_value ArkTSOnSendEvent(napi_env env, napi_callback_info info) {
    // 1、从info中取出TS传递过来的参数放入args
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "ArkTSOnSendEvent napi_get_cb_info error");
        return 0;
    }

    std::string instance_id = kuikly::util::getNApiArgsStdString(env, args[0]);
    auto event = kuikly::util::getNApiArgsStdString(env, args[1]);
    // 结构化 napi 值（Record / Array）直接构建 KRJSON，字符串同样兼容
    auto data = KRRenderValue::Make(env, args[2]);
    auto renderView = KRRenderManager::GetInstance().GetRenderView(instance_id);
    if (renderView != nullptr) {
        renderView->SendEvent(event, data);
        napi_value result;
        napi_create_int32(env, 1, &result);
        return result;
    }
    return 0;
}

static napi_value ArkTSOnSendEventSync(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "ArkTSOnSendEventSync napi_get_cb_info error");
        return 0;
    }

    std::string instance_id = kuikly::util::getNApiArgsStdString(env, args[0]);
    auto event = kuikly::util::getNApiArgsStdString(env, args[1]);
    auto data = KRRenderValue::Make(env, args[2]);
    bool sync = kuikly::util::getNApiArgsBool(env, args[3]);
    auto renderView = KRRenderManager::GetInstance().GetRenderView(instance_id);
    if (renderView != nullptr) {
        renderView->SendEvent(event, data, sync);
        napi_value result;
        napi_create_int32(env, 1, &result);
        return result;
    }
    return 0;
}
static napi_value CreateNativeRoot(napi_env env, napi_callback_info info) {
    // The API OH_ArkUI_NodeContent_RegisterCallback depends on it
    auto nodeApi = kuikly::util::GetNodeApi();
    KRRenderManager::GetInstance().CreateRenderViewIfNeeded(env, info);
    return nullptr;
}

static napi_value isBackPressConsumed(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_value result;
    napi_create_int32(env, KRBackPressState::Undefined, &result);
    if (napi_ok != napi_get_cb_info(env, info, &argc, args, nullptr, nullptr)) {
        napi_throw_error(env, "-1000", "napi_get_cb_info error");
        return result;
    }
    std::string instance_id = kuikly::util::getNApiArgsStdString(env, args[0]);

    auto render_view = KRRenderManager::GetInstance().GetRenderView(instance_id);
    if (render_view != nullptr) {
        std::string back_press_module_name = kuikly::module::KRBackPressModule::MODULE_NAME;
        auto back_press_module = std::dynamic_pointer_cast<kuikly::module::KRBackPressModule>(render_view->GetModule(back_press_module_name));
        if (!back_press_module) {
            return result;
        }
        bool is_back_consumed = back_press_module->is_back_consumed.load();
        if (is_back_consumed) {
            napi_create_int32(env, KRBackPressState::Consumed, &result);
        } else {
            napi_create_int32(env, KRBackPressState::NotConsumed, &result);
        }
    }
    return result;
}

#ifndef NDEBUG
namespace {

struct KRAnyDataCApiTestResult {
    int passed = 0;
    int failed = 0;
    std::string first_failure;
};

void Expect(KRAnyDataCApiTestResult *result, bool ok, const char *name) {
    if (ok) {
        ++result->passed;
        return;
    }
    ++result->failed;
    if (result->first_failure.empty()) {
        result->first_failure = name;
    }
    KR_LOG_ERROR << "[KRAnyDataCApi] FAIL " << name;
}

KRAnyData WrapAny(KRRenderValue value) {
    auto *internal = new KRAnyDataInternal();
    internal->anyValue = std::move(value);
    return internal;
}

void TestScalars(KRAnyDataCApiTestResult *r) {
    KRAnyDataDestroy(nullptr);

    KRAnyData empty = KRAnyDataCreate();
    Expect(r, empty != nullptr, "create_empty");
    Expect(r, !KRAnyDataIsString(empty) && !KRAnyDataIsInt(empty) && !KRAnyDataIsLong(empty) &&
                  !KRAnyDataIsFloat(empty) && !KRAnyDataIsBool(empty) && !KRAnyDataIsBytes(empty) &&
                  !KRAnyDataIsArray(empty) && !KRAnyDataIsMap(empty),
           "empty_type_predicates");
    KRAnyDataDestroy(empty);

    KRAnyData i = KRAnyDataCreateInt(42);
    int32_t iv = 0;
    Expect(r, KRAnyDataIsInt(i) && KRAnyDataGetInt(i, &iv) == KRANYDATA_SUCCESS && iv == 42, "int_roundtrip");
    Expect(r, KRAnyDataGetInt(nullptr, &iv) == KRANYDATA_NULL_INPUT, "get_int_null_input");
    Expect(r, KRAnyDataGetInt(i, nullptr) == KRANYDATA_NULL_OUTPUT, "get_int_null_output");
    KRAnyDataDestroy(i);

    KRAnyData l = KRAnyDataCreateLong(1LL << 40);
    int64_t lv = 0;
    Expect(r, KRAnyDataIsLong(l) && KRAnyDataGetLong(l, &lv) == KRANYDATA_SUCCESS && lv == (1LL << 40),
           "long_roundtrip");
    KRAnyDataDestroy(l);

    KRAnyData f = KRAnyDataCreateFloat(1.5f);
    float fv = 0;
    Expect(r, KRAnyDataIsFloat(f) && KRAnyDataGetFloat(f, &fv) == KRANYDATA_SUCCESS && std::fabs(fv - 1.5f) < 1e-6f,
           "float_roundtrip");
    KRAnyDataDestroy(f);

    KRAnyData b = KRAnyDataCreateBool(true);
    bool bv = false;
    Expect(r, KRAnyDataIsBool(b) && KRAnyDataGetBool(b, &bv) == KRANYDATA_SUCCESS && bv, "bool_roundtrip");
    KRAnyDataDestroy(b);

    KRAnyData s = KRAnyDataCreateString("hello");
    const char *sv = nullptr;
    Expect(r, KRAnyDataIsString(s) && std::strcmp(KRAnyDataGetString(s), "hello") == 0, "string_get");
    Expect(r, KRAnyDataGetStr(s, &sv) == KRANYDATA_SUCCESS && sv && std::strcmp(sv, "hello") == 0, "string_get_str");
    Expect(r, KRAnyDataGetStr(s, nullptr) == KRANYDATA_NULL_OUTPUT, "get_str_null_output");
    KRAnyDataDestroy(s);

    const char bytes[] = {'a', 'b', '\0', 'c'};
    KRAnyData bin = KRAnyDataCreateBytes(bytes, 4);
    const char *bp = nullptr;
    int bsize = 0;
    Expect(r, KRAnyDataIsBytes(bin) && KRAnyDataGetBytes(bin, &bp, &bsize) == KRANYDATA_SUCCESS && bsize == 4 &&
                  bp && std::memcmp(bp, bytes, 4) == 0,
           "bytes_roundtrip");
    KRAnyDataDestroy(bin);
}

void TestArray(KRAnyDataCApiTestResult *r) {
    KRAnyData arr = KRAnyDataCreateArray(2);
    int size = -1;
    Expect(r, KRAnyDataIsArray(arr) && KRAnyDataGetArraySize(arr, &size) == KRANYDATA_SUCCESS && size == 2,
           "array_create_size");
    Expect(r, KRAnyDataGetArraySize(nullptr, &size) == KRANYDATA_NULL_INPUT, "array_size_null_input");
    Expect(r, KRAnyDataGetArraySize(arr, nullptr) == KRANYDATA_NULL_OUTPUT, "array_size_null_output");

    KRAnyData a0 = KRAnyDataCreateInt(10);
    KRAnyData a1 = KRAnyDataCreateString("x");
    Expect(r, KRAnyDataSetArrayElement(arr, a0, 0) == KRANYDATA_SUCCESS, "array_set_0");
    Expect(r, KRAnyDataSetArrayElement(arr, a1, 1) == KRANYDATA_SUCCESS, "array_set_1");
    Expect(r, KRAnyDataSetArrayElement(arr, a0, 2) == KRANYDATA_OUT_OF_INDEX, "array_set_oob");
    Expect(r, KRAnyDataSetArrayElement(arr, a0, -1) == KRANYDATA_OUT_OF_INDEX, "array_set_neg");
    Expect(r, KRAnyDataSetArrayElement(nullptr, a0, 0) == KRANYDATA_NULL_INPUT, "array_set_null_data");
    Expect(r, KRAnyDataSetArrayElement(arr, nullptr, 0) == KRANYDATA_NULL_INPUT, "array_set_null_value");

    KRAnyData got = nullptr;
    int32_t iv = 0;
    Expect(r, KRAnyDataGetArrayElement(arr, &got, 0) == KRANYDATA_SUCCESS && got && KRAnyDataIsInt(got) &&
                  KRAnyDataGetInt(got, &iv) == KRANYDATA_SUCCESS && iv == 10,
           "array_get_0");
    Expect(r, KRAnyDataGetArrayElement(arr, &got, 1) == KRANYDATA_SUCCESS && got && KRAnyDataIsString(got) &&
                  std::strcmp(KRAnyDataGetString(got), "x") == 0,
           "array_get_1");
    Expect(r, KRAnyDataGetArrayElement(arr, &got, 2) == KRANYDATA_OUT_OF_INDEX, "array_get_oob");
    Expect(r, KRAnyDataGetArrayElement(arr, nullptr, 0) == KRANYDATA_NULL_OUTPUT, "array_get_null_out");

    KRAnyData extra = KRAnyDataCreateInt(99);
    Expect(r, KRAnyDataAddArrayElement(arr, extra) == KRANYDATA_SUCCESS &&
                  KRAnyDataGetArraySize(arr, &size) == KRANYDATA_SUCCESS && size == 3,
           "array_add");
    Expect(r, KRAnyDataGetArrayElement(arr, &got, 2) == KRANYDATA_SUCCESS && KRAnyDataGetInt(got, &iv) == KRANYDATA_SUCCESS &&
                  iv == 99,
           "array_add_read");

    KRAnyData not_arr = KRAnyDataCreateInt(1);
    Expect(r, KRAnyDataGetArraySize(not_arr, &size) == KRANYDATA_SUCCESS && size == 0, "array_size_non_array");
    Expect(r, KRAnyDataGetArrayElement(not_arr, &got, 0) == KRANYDATA_OUT_OF_INDEX, "array_get_non_array");
    Expect(r, KRAnyDataSetArrayElement(not_arr, extra, 0) == KRANYDATA_TYPE_MISMATCH, "array_set_type_mismatch");
    Expect(r, KRAnyDataAddArrayElement(not_arr, extra) == KRANYDATA_TYPE_MISMATCH, "array_add_type_mismatch");

    KRRenderValue::Array shared_items;
    shared_items.emplace_back(KRRenderValue::Make(1));
    shared_items.emplace_back(KRRenderValue::Make(2));
    KRRenderValue shared_root = KRRenderValue::Make(shared_items);
    KRAnyData h1 = WrapAny(shared_root);
    KRAnyData h2 = WrapAny(shared_root);
    KRAnyData three = KRAnyDataCreateInt(3);
    Expect(r, KRAnyDataSetArrayElement(h1, three, 0) == KRANYDATA_SUCCESS, "array_cow_set");
    Expect(r, KRAnyDataGetArrayElement(h1, &got, 0) == KRANYDATA_SUCCESS && KRAnyDataGetInt(got, &iv) == KRANYDATA_SUCCESS &&
                  iv == 3,
           "array_cow_writer");
    Expect(r, KRAnyDataGetArrayElement(h2, &got, 0) == KRANYDATA_SUCCESS && KRAnyDataGetInt(got, &iv) == KRANYDATA_SUCCESS &&
                  iv == 1,
           "array_cow_reader");

    KRAnyDataDestroy(arr);
    KRAnyDataDestroy(a0);
    KRAnyDataDestroy(a1);
    KRAnyDataDestroy(extra);
    KRAnyDataDestroy(not_arr);
    KRAnyDataDestroy(h1);
    KRAnyDataDestroy(h2);
    KRAnyDataDestroy(three);
}

void TestMap(KRAnyDataCApiTestResult *r) {
    KRRenderValue::Map members;
    members["name"] = KRRenderValue::Make("kuikly");
    members["count"] = KRRenderValue::Make(static_cast<int32_t>(7));
    KRAnyData map = WrapAny(KRRenderValue::Make(std::move(members)));
    Expect(r, KRAnyDataIsMap(map), "map_is_map");

    KRAnyData child = nullptr;
    Expect(r, KRAnyDataGetMapValue(map, "name", &child) == KRANYDATA_SUCCESS && child && KRAnyDataIsString(child) &&
                  std::strcmp(KRAnyDataGetString(child), "kuikly") == 0,
           "map_get_name");
    int32_t iv = 0;
    Expect(r, KRAnyDataGetMapValue(map, "count", &child) == KRANYDATA_SUCCESS && KRAnyDataIsInt(child) &&
                  KRAnyDataGetInt(child, &iv) == KRANYDATA_SUCCESS && iv == 7,
           "map_get_count");
    Expect(r, KRAnyDataGetMapValue(map, "missing", &child) == KRANYDATA_KEY_NOT_FOUND && child == nullptr,
           "map_get_missing");
    Expect(r, KRAnyDataGetMapValue(map, nullptr, &child) == KRANYDATA_NULL_OUTPUT, "map_get_null_key");
    Expect(r, KRAnyDataGetMapValue(map, "name", nullptr) == KRANYDATA_NULL_OUTPUT, "map_get_null_out");

    struct VisitAcc {
        int count = 0;
        bool saw_name = false;
        bool saw_count = false;
    } acc;
    auto visitor = [](const char *key, KRAnyData value, void *userData) {
        auto *out = static_cast<VisitAcc *>(userData);
        ++out->count;
        if (key && std::strcmp(key, "name") == 0 && KRAnyDataIsString(value)) {
            out->saw_name = std::strcmp(KRAnyDataGetString(value), "kuikly") == 0;
        }
        if (key && std::strcmp(key, "count") == 0 && KRAnyDataIsInt(value)) {
            int32_t n = 0;
            out->saw_count = KRAnyDataGetInt(value, &n) == KRANYDATA_SUCCESS && n == 7;
        }
    };
    Expect(r, KRAnyDataVisitMap(map, visitor, &acc) == KRANYDATA_SUCCESS && acc.count == 2 && acc.saw_name &&
                  acc.saw_count,
           "map_visit");
    Expect(r, KRAnyDataVisitMap(map, nullptr, &acc) == KRANYDATA_INVALID_PARAM, "map_visit_null_visitor");

    KRAnyData not_map = KRAnyDataCreateString("nope");
    Expect(r, KRAnyDataVisitMap(not_map, visitor, &acc) == KRANYDATA_TYPE_MISMATCH, "map_visit_type_mismatch");
    Expect(r, KRAnyDataGetMapValue(not_map, "name", &child) == KRANYDATA_TYPE_MISMATCH, "map_get_type_mismatch");
    Expect(r, KRAnyDataIsMap(nullptr) == false && KRAnyDataIsArray(nullptr) == false, "null_predicates");

    KRAnyDataDestroy(map);
    KRAnyDataDestroy(not_map);
}

void RunKRAnyDataCApiTests() {
    KRAnyDataCApiTestResult result;
    TestScalars(&result);
    TestArray(&result);
    TestMap(&result);
    std::string report = "KRAnyData C API tests: passed=" + std::to_string(result.passed) +
                         " failed=" + std::to_string(result.failed);
    if (result.failed > 0) {
        report += " first=" + result.first_failure;
        KR_LOG_ERROR << report;
    } else {
        KR_LOG_INFO << report;
    }
}

}  // namespace
#endif  // NDEBUG

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
#ifndef NDEBUG
    RunKRAnyDataCApiTests();
#endif
    napi_property_descriptor desc[] = {
        //  { "add", nullptr, Add, nullptr, nullptr, nullptr, napi_default, nullptr },
        {"onRenderViewSizeChanged", nullptr, OnRenderViewSizeChanged, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"onDestroyRenderView", nullptr, OnDestroyRenderView, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"onInitRenderView", nullptr, OnInitRenderView, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"arkTSCallNative", nullptr, ArkTSCallNative, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sendEvent", nullptr, ArkTSOnSendEvent, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sendEventSync", nullptr, ArkTSOnSendEventSync, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"updateConfig", nullptr, UpdateConfig, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"OnLaunchStart", nullptr, OnLaunchStart, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"createNativeRoot", nullptr, CreateNativeRoot, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"isBackPressConsumed", nullptr, isBackPressConsumed, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    KRMainThread::Export(env, exports);                   // 缓存主线程 uv_loop / async 句柄
    KRRenderManager::GetInstance().Export(env, exports);  // 尝试注册RenderView
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "kuikly",
    .nm_priv = (static_cast<void *>(0)),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterHarmony_renderModule(void) {
    napi_module_register(&demoModule);
}
