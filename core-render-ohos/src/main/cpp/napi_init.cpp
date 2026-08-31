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
#include "libohos_render/foundation/KRCommon.h"
#include "libohos_render/foundation/KRCallbackData.h"
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
#include "libohos_render/api/include/Kuikly/KRJSON.h"
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
    std::string instance_id = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);
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
    std::string instance_id = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);
    auto config_json = KRRenderValue::Make(env, args[1]);
    if (auto renderView = KRRenderManager::GetInstance().GetRenderView(instance_id)) {
        if (auto ctx = renderView->GetContext()) {
            ctx->Config()->Update(config_json);
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
    auto instance_id = KRRenderValue::Make(env, args[0]);
    auto page_name = KRRenderValue::Make(env, args[1]);
    auto page_Data = KRRenderValue::Make(env, args[2]);
    double renderViewWidth = kuikly::util::getNApiArgsDouble(env, args[3]);
    double renderViewHeight = kuikly::util::getNApiArgsDouble(env, args[4]);
    auto config_json = KRRenderValue::Make(env, args[5]);
    std::string instance_id_utf8 = instance_id.toAsciiString();
    auto renderView = KRRenderManager::GetInstance().GetRenderView(instance_id_utf8);
    if (renderView != nullptr) {
        size_t page_data_units = 0;
        if (page_Data.isString()) {
            if (KRJSONGetType(page_Data.jsonValue()) == KRJSON_U16STRING) {
                KRJSONGetStringUtf16(page_Data.jsonValue(), &page_data_units);
            } else {
                KRJSONGetString(page_Data.jsonValue(), &page_data_units);
            }
        }
        if (!page_Data.isMap() && !page_Data.isArray() && (!page_Data.isString() || page_data_units == 0)) {
            page_Data = KRRenderValue::Make(KRRenderValue::Map{});
        }
        auto context = std::make_shared<KRRenderContextParams>(page_name, page_Data, instance_id, config_json);
        ArkUI_ContextHandle context_handle;
        OH_ArkUI_GetContextFromNapiValue(env, args[6], &context_handle);
        NativeResourceManager *native_resources_manager = OH_ResourceManager_InitNativeResourceManager(env, args[7]);
        int64_t launch_time = KRRenderManager::GetInstance().GetLaunchStartTime(instance_id_utf8);
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
    std::string instanceId = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);
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

    std::string instanceId = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);
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

    std::string instance_id = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);
    auto event = KRRenderValue::Make(env, args[1]);
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

    std::string instance_id = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);
    auto event = KRRenderValue::Make(env, args[1]);
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
    std::string instance_id = kuikly::util::getNApiArgsAsciiStdString(env, args[0]);

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

void TestJsonUtf16(KRAnyDataCApiTestResult *r) {
    const uint16_t hello[] = {u'h', u'e', u'l', u'l', u'o'};
    KRJSONValue utf16 = KRJSONNewStringUtf16(hello, 5);
    size_t units = 0;
    const uint16_t *got16 = KRJSONGetStringUtf16(utf16, &units);
    Expect(r, KRJSONGetType(utf16) == KRJSON_U16STRING, "utf16_public_type");
    Expect(r, got16 != nullptr && units == 5 && std::memcmp(got16, hello, sizeof(hello)) == 0, "utf16_get");
    size_t utf8_len = 99;
    const char *utf8 = KRJSONGetString(utf16, &utf8_len);
    Expect(r, utf8 != nullptr && utf8[0] == '\0' && utf8_len == 0, "utf16_get_string_empty");
    char *dumped = KRJSONDump(utf16);
    Expect(r, dumped != nullptr && std::strcmp(dumped, "\"hello\"") == 0, "utf16_dump");
    KRJSONFreeString(dumped);
    size_t dump16_units = 0;
    uint16_t *dumped16 = KRJSONDumpUtf16(utf16, &dump16_units);
    const uint16_t hello_json[] = {u'"', u'h', u'e', u'l', u'l', u'o', u'"'};
    Expect(r,
           dumped16 != nullptr && dump16_units == 7 &&
               std::memcmp(dumped16, hello_json, sizeof(hello_json)) == 0,
           "utf16_dump_utf16");
    KRJSONFreeString(reinterpret_cast<char *>(dumped16));

    KRAnyData any16 = WrapAny(KRRenderValue::MakeBorrowed(utf16));
    KRJSONRelease(utf16);
    Expect(r, KRAnyDataIsString(any16) && std::strcmp(KRAnyDataGetString(any16), "hello") == 0, "anydata_u16_get");
    const char *any16_str = nullptr;
    Expect(r, KRAnyDataGetStr(any16, &any16_str) == KRANYDATA_SUCCESS && any16_str &&
                  std::strcmp(any16_str, "hello") == 0,
           "anydata_u16_get_str");
    KRAnyDataDestroy(any16);

    const uint16_t nihao[] = {0x4F60, 0x597D};
    KRJSONValue zh = KRJSONNewStringUtf16(nihao, 2);
    size_t zh_units = 0;
    const uint16_t *got_zh = KRJSONGetStringUtf16(zh, &zh_units);
    Expect(r, got_zh != nullptr && zh_units == 2 && std::memcmp(got_zh, nihao, sizeof(nihao)) == 0, "utf16_cjk");
    KRJSONRelease(zh);

    KRJSONValue ascii = KRJSONNewString("ascii", 5);
    Expect(r, KRJSONGetType(ascii) == KRJSON_STRING, "utf8_public_type");
    Expect(r, KRJSONGetStringUtf16(ascii, nullptr) == nullptr, "utf8_get_utf16_null");
    KRJSONRelease(ascii);

    const uint16_t json_src[] = {u'{', u'"', u'z', u'h', u'"', u':', u'"', 0x4F60, 0x597D, u'"', u'}'};
    char *parse_err = nullptr;
    KRJSONValue parsed = KRJSONParseUtf16(json_src, sizeof(json_src) / sizeof(json_src[0]), &parse_err);
    Expect(r, parsed != KRJSON_INVALID && parse_err == nullptr, "utf16_parse_ok");
    const uint16_t zh_key[] = {u'z', u'h'};
    KRJSONValue zh_val = KRJSONObjectGetUtf16(parsed, zh_key, 2);
    size_t parsed_units = 0;
    const uint16_t *parsed_zh = KRJSONGetStringUtf16(zh_val, &parsed_units);
    Expect(r, KRJSONGetType(zh_val) == KRJSON_U16STRING && parsed_zh != nullptr && parsed_units == 2 &&
                  parsed_zh[0] == 0x4F60 && parsed_zh[1] == 0x597D,
           "utf16_parse_string_leaf");
    KRJSONFreeString(parse_err);
    KRJSONRelease(parsed);
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
    members[u"name"] = KRRenderValue::Make(u"kuikly");
    members[u"count"] = KRRenderValue::Make(static_cast<int32_t>(7));
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
    TestJsonUtf16(&result);
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

// 仅当编译命令显式 -DKUIKLY_OHOS_NAPI_RECORD_BENCH=1 时编进 so。
#if defined(KUIKLY_OHOS_NAPI_RECORD_BENCH) && (KUIKLY_OHOS_NAPI_RECORD_BENCH == 1)
#include <algorithm>
#include <chrono>
#include <sstream>
#include <vector>

static int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static int64_t PercentileNs(std::vector<int64_t> samples, double p) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const size_t idx = static_cast<size_t>(p * static_cast<double>(samples.size() - 1));
    return samples[idx];
}

static napi_value CallJsonStringify(napi_env env, napi_value value) {
    napi_value global = nullptr;
    napi_value json = nullptr;
    napi_value stringify = nullptr;
    napi_value result = nullptr;
    napi_get_global(env, &global);
    napi_get_named_property(env, global, "JSON", &json);
    napi_get_named_property(env, json, "stringify", &stringify);
    napi_value argv[1] = {value};
    if (napi_call_function(env, json, stringify, 1, argv, &result) != napi_ok) {
        return nullptr;
    }
    return result;
}

// 对比 sendEvent 两条入桥：KRRecord → FromNapi 建 KRRenderValue，vs JSON.stringify → std::string。
static napi_value BenchNapiRecordConvert(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || args[0] == nullptr) {
        napi_throw_error(env, "-1000", "benchNapiRecordConvert args error");
        return nullptr;
    }
    int32_t iterations = 80;
    if (argc >= 2 && args[1] != nullptr) {
        napi_get_value_int32(env, args[1], &iterations);
    }
    if (iterations < 8) {
        iterations = 8;
    }

    const int warmup = 8;
    volatile size_t sink = 0;
    for (int i = 0; i < warmup; ++i) {
        auto parsed = KRRenderValue::Make(env, args[0]);
        sink ^= parsed.size();
        napi_value js_str = CallJsonStringify(env, args[0]);
        std::string cpp_str;
        if (js_str != nullptr) {
            kuikly::util::GetNApiArgsStdString(env, js_str, cpp_str);
            sink ^= cpp_str.size();
            sink ^= KRRenderValue::Make(cpp_str).size();
            sink ^= KRRenderValue::Parse(cpp_str).size();
        }
    }

    napi_value sized = CallJsonStringify(env, args[0]);
    std::string sized_str;
    if (sized != nullptr) {
        kuikly::util::GetNApiArgsStdString(env, sized, sized_str);
    }
    const size_t json_bytes = sized_str.size();

    std::vector<int64_t> from_napi;
    std::vector<int64_t> stringify;
    std::vector<int64_t> to_string;
    std::vector<int64_t> combined;
    std::vector<int64_t> make_string;
    std::vector<int64_t> parse_object;
    std::vector<int64_t> stringify_make;
    std::vector<int64_t> stringify_parse;
    from_napi.reserve(static_cast<size_t>(iterations));
    stringify.reserve(static_cast<size_t>(iterations));
    to_string.reserve(static_cast<size_t>(iterations));
    combined.reserve(static_cast<size_t>(iterations));
    make_string.reserve(static_cast<size_t>(iterations));
    parse_object.reserve(static_cast<size_t>(iterations));
    stringify_make.reserve(static_cast<size_t>(iterations));
    stringify_parse.reserve(static_cast<size_t>(iterations));

    for (int i = 0; i < iterations; ++i) {
        const int64_t t0 = NowNs();
        auto parsed = KRRenderValue::Make(env, args[0]);
        const int64_t t1 = NowNs();
        sink ^= parsed.size();
        from_napi.push_back(t1 - t0);
    }
    for (int i = 0; i < iterations; ++i) {
        const int64_t t0 = NowNs();
        napi_value js_str = CallJsonStringify(env, args[0]);
        const int64_t t1 = NowNs();
        std::string cpp_str;
        const int64_t t2 = NowNs();
        if (js_str != nullptr) {
            kuikly::util::GetNApiArgsStdString(env, js_str, cpp_str);
        }
        const int64_t t3 = NowNs();
        auto as_string = KRRenderValue::Make(cpp_str);
        const int64_t t4 = NowNs();
        auto as_object = KRRenderValue::Parse(cpp_str);
        const int64_t t5 = NowNs();
        sink ^= cpp_str.size() ^ as_string.size() ^ as_object.size();
        stringify.push_back(t1 - t0);
        to_string.push_back(t3 - t2);
        combined.push_back(t3 - t0);
        make_string.push_back(t4 - t3);
        parse_object.push_back(t5 - t4);
        stringify_make.push_back(t4 - t0);
        stringify_parse.push_back(t5 - t0);
    }

    std::ostringstream out;
    out << "json_bytes=" << json_bytes << " iters=" << iterations << " sink=" << sink
        << " from_napi_p50_ns=" << PercentileNs(from_napi, 0.5)
        << " stringify_to_string_p50_ns=" << PercentileNs(combined, 0.5)
        << " make_string_p50_ns=" << PercentileNs(make_string, 0.5)
        << " parse_object_p50_ns=" << PercentileNs(parse_object, 0.5)
        << " stringify_make_p50_ns=" << PercentileNs(stringify_make, 0.5)
        << " stringify_parse_p50_ns=" << PercentileNs(stringify_parse, 0.5);
    KR_LOG_INFO_WITH_TAG("NAPI_RECORD_BENCH") << out.str();

    napi_value result = nullptr;
    napi_create_string_utf8(env, out.str().c_str(), out.str().size(), &result);
    return result;
}
#endif  // KUIKLY_OHOS_NAPI_RECORD_BENCH == 1

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
#if defined(KUIKLY_OHOS_NAPI_RECORD_BENCH) && (KUIKLY_OHOS_NAPI_RECORD_BENCH == 1)
        {"benchNapiRecordConvert", nullptr, BenchNapiRecordConvert, nullptr, nullptr, nullptr, napi_default, nullptr},
#endif
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
