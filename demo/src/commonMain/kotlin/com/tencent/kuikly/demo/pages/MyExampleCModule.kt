package com.tencent.kuikly.demo.pages

import com.tencent.kuikly.core.module.Module
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject

/**
 * MyExampleCModule 的 Kotlin 封装。
 *
 * 对应鸿蒙侧原生 Module（C 薄包装 napi_init.cpp + 仓颉实现 index.cj）：
 *   - 同步调用：callMethod 同步返回 "<method> handled." 字符串；
 *   - 异步回调：传入 callback 时，仓颉实现会异步回写 {"key":"value"}。
 *
 * 注意：该原生 Module 目前仅在 OHOS 平台注册，非 OHOS 平台调用会安全降级返回 null。
 */
class MyExampleCModule : Module() {
    override fun moduleName(): String {
        return MODULE_NAME
    }

    /**
     * 同步调用原生 MyExampleCModule。
     * @param method 方法名
     * @param param  透传给原生的参数（任意可被序列化的对象）
     * @return 原生同步返回的字符串（如 "<method> handled."），未注册/降级时为 null
     */
    fun callSync(method: String, param: Any? = null): String? {
        val result = syncToNativeMethod(
            methodName = method,
            data = JSONObject().apply { put("param", param?.toString() ?: "") },
            callbackFn = null
        )
        return result.ifEmpty { null }
    }

    /**
     * 异步调用原生 MyExampleCModule，并通过 callback 接收原生回写的数据。
     * @param method   方法名
     * @param param    透传给原生的参数
     * @param callback 原生回写数据时的回调（回参类型为 JSONObject，实际为 {"key":"value"}）
     */
    fun callAsync(method: String, param: Any? = null, callback: (JSONObject?) -> Unit) {
        asyncToNativeMethod(
            methodName = method,
            data = JSONObject().apply { put("param", param?.toString() ?: "") },
            callbackFn = callback
        )
    }

    companion object {
        const val MODULE_NAME = "MyExampleCModule"
    }
}
