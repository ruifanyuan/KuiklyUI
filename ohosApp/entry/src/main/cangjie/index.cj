package ohos_app_cangjie_entry

import ohos.ark_interop.JSModule
import ohos.ark_interop.JSContext
import ohos.ark_interop.JSCallInfo
import ohos.ark_interop.JSValue

func testCJ(runtime: JSContext, callInfo: JSCallInfo): JSValue {
    let result = "Hello ${callInfo[0].toString()}"
    runtime.string(result).toJSValue()
}

// =====================================================================
// MyExampleCModule 的仓颉实现 + C 互操作桥接
//
// 原来 napi_init.cpp 中的 ExampleModuleOnCallMethod 是 C 实现。这里把核心逻辑
// 用仓颉重写，并通过 C 互操作规则（见 https://docs.cangjie-lang.cn/.../FFI/cangjie-c.html）：
//   1) 仓颉侧用 @C 修饰 myExampleCModuleCallMethod，使其按 C 调用约定导出，
//      其类型即 CFunc<...>，可作为 C 侧函数指针使用；
//   2) 仓颉侧声明 C 侧的 "注册入口" RegisterCangjieMyExampleCModule（foreign），
//      它接收一个 CFunc 参数，由 napi_init.cpp 实现；
//   3) 仓颉模块在 registerModule 阶段（ArkTS 加载本 .so 时）主动调用该注册入口，
//      把 myExampleCModuleCallMethod 的函数指针交给 C 侧保存；
//   4) 之后 C 侧 ExampleModuleOnCallMethod（薄包装）调用该函数指针，完成桥接。
//
// 类型映射说明：
//   - KRRenderModuleCallbackContext / void* 等不透明指针 -> CPointer<Unit>
//   - const char*                                -> CString（入参）/ CPointer<Int8>（返回值）
//   - int                                         -> Int32
//
// 说明：本文件仅依赖仓颉 FFI 内置能力（foreign / @C / CPointer / CString），
// 不引入 std.c 等标准库子包，避免在 --dy-std 模式下要求从远程仓库拉取标准库。
// =====================================================================

// ---- 纯 FFI 的 C 内存分配接口（替代 std.c.LibC）----
foreign func malloc(size: UIntNative): CPointer<Unit>
foreign func free(ptr: CPointer<Unit>): Unit

// 把仓颉 String 拷贝为以 '\0' 结尾的 C 字符串（malloc 分配，调用方用 free 释放）。
// 不依赖 std.c.LibC，纯 FFI 手动构造（本模块字符串均为 ASCII，Rune->UInt8 安全）。
func cjMallocCString(s: String): CPointer<Int8> {
    let n = s.size
    let raw = unsafe { malloc(UIntNative(n + 1)) }
    let p = CPointer<UInt8>(raw)
    var i = 0
    for (r in s) {
        unsafe { (p + i).write(UInt8(r)) }
        i = i + 1
    }
    unsafe { (p + n).write(UInt8(0)) } // null terminator
    CPointer<Int8>(raw)
}

// 仓颉侧主动声明的"C 侧注册入口"：C 侧提供实现（见 napi_init.cpp）。
// 参数为一个 CFunc：对应 C 侧函数指针
//   char* (*)(void* moduleInstance, const char* moduleName, int sync,
//             const char* method, void* param, void* context)
foreign func RegisterCangjieMyExampleCModule(
    cb: CFunc<(CPointer<Unit>, CString, Int32, CString, CPointer<Unit>, CPointer<Unit>) -> CPointer<Int8>>
): Unit

// 仓颉侧主动声明的"C 侧回调 API"：业务可在 callMethod 内异步回写数据给 Kuikly。
foreign func KRRenderModuleDoCallback(context: CPointer<Unit>, data: CString): Unit

// MyExampleCModule 的 callMethod 实现（仓颉版）。
// 逻辑与原 C 版 ExampleModuleOnCallMethod 对齐：
//   - 若 context 非空，开线程异步回写 {"key":"value"}（此处简化为同步调用，
//     仓颉侧所有权规则与 C 不同，回调数据由 C 侧 KRRenderModuleDoCallback 内部拷贝，
//     因此回调后立即可释放）；
//   - 同步返回 "method handled." 字符串（malloc 分配），由 C 侧薄包装负责填 length / free。
@C
func myExampleCModuleCallMethod(
    moduleInstance: CPointer<Unit>,
    moduleName: CString,
    sync: Int32,
    method: CString,
    param: CPointer<Unit>,
    context: CPointer<Unit>
): CPointer<Int8> {
    // 异步回调（对齐原 C 版对 context 非空时的处理）。
    if (!context.isNull()) {
        let cbData = cjMallocCString("{\"key\":\"value\"}")
        unsafe { KRRenderModuleDoCallback(context, CString(CPointer<UInt8>(cbData))) }
        unsafe { free(CPointer<Unit>(cbData)) }
    }
    // 构造同步返回值 "method handled."。
    let methodStr = method.toString()
    let resultStr = methodStr + " handled."
    return cjMallocCString(resultStr)
}

let EXPORT_MODULE = JSModule.registerModule {
    runtime, exports => exports["testCJ"] = runtime.function(testCJ).toJSValue()
}

// 把仓颉实现的 MyExampleCModule callMethod 函数指针注册给 C 侧，
// 完成 C -> 仓颉 的桥接。myExampleCModuleCallMethod 是用 @C 修饰的
// 仓颉函数，可直接作为 CFunc 传给 foreign 函数。
// 在模块加载（本 .so 被 ArkTS 引入）时执行，早于任何 Kuikly callMethod。
let REGISTER_BRIDGE = unsafe { RegisterCangjieMyExampleCModule(myExampleCModuleCallMethod) }