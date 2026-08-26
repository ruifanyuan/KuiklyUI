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

package com.tencent.kuikly.core.nvi.serialization.json

/**
 * `NATIVE_JSON` 惰性桥接自检：构造一棵自持有的 cJSON 树，模拟原生产者在调用返回后
 * 立刻释放自己的句柄，验证 Kotlin 侧仍能安全读取（共享所有权）、子对象比父对象活得久、
 * 以及物化后行为与其他平台一致。
 *
 * 返回 `OK ...` 或 `FAIL ...`，供 demo 的桥接回归页展示。放在 core 内是因为
 * [LazyCJsonMap] / [CJsonNative] 都是 internal。
 */
fun verifyOhosLazyJsonBridge(): String {
    val failures = mutableListOf<String>()
    val json = """{"i":1,"l":2147483648,"d":1.5,"s":"str","b":true,"nil":null,""" +
        """"child":{"ck":"cv","cn":7},"arr":[1,"two",{"ak":3}]}"""

    val owner = CJsonNative.ownerFromJson(json)
    if (owner == 0L) {
        return "FAIL cjson owner create failed (render .so symbols missing?)"
    }
    val wrapped = JSONObject.fromCJsonOwnerAny(owner)
    // 生产者句柄立刻释放：之后所有读取都只依赖 Kotlin 自己 retain 的那一份
    CJsonNative.release(owner)

    val obj = wrapped as? JSONObject
    if (obj == null) {
        return "FAIL object root not wrapped as JSONObject: ${wrapped?.let { it::class.simpleName }}"
    }

    expectEquals(failures, "size", obj.length(), 8)
    expectEquals(failures, "int", obj.opt("i"), 1)
    expectEquals(failures, "long", obj.opt("l"), 2147483648L)
    expectEquals(failures, "double", obj.opt("d"), 1.5)
    expectEquals(failures, "string", obj.opt("s"), "str")
    expectEquals(failures, "bool", obj.opt("b"), true)
    expectEquals(failures, "null value", obj.opt("nil"), null)
    expectEquals(failures, "has null key", obj.has("nil"), true)
    expectEquals(failures, "keys order", obj.keySet().joinToString(","), "i,l,d,s,b,nil,child,arr")

    val child = obj.optJSONObject("child")
    if (child == null) {
        failures.add("child object missing")
    } else {
        expectEquals(failures, "child field", child.optString("ck"), "cv")
        expectEquals(failures, "child identity", obj.optJSONObject("child") === child, true)
    }
    val arr = obj.optJSONArray("arr")
    if (arr == null) {
        failures.add("array missing")
    } else {
        expectEquals(failures, "array length", arr.length(), 3)
        expectEquals(failures, "array int element", arr.opt(0), 1)
        expectEquals(failures, "array nested field", arr.optJSONObject(2)?.opt("ak"), 3)
    }

    // 父对象物化（写入）后，之前取到的子对象仍然可读：子对象各自持有一份句柄
    obj.put("probe", "p")
    expectEquals(failures, "after put probe", obj.optString("probe"), "p")
    expectEquals(failures, "child alive after parent materialize", child?.optString("ck"), "cv")
    expectEquals(failures, "int alive after materialize", obj.opt("i"), 1)

    // 序列化 → 再解析：与其他平台同一套文本行为
    val text = JSONObject(obj.toString())
    expectEquals(failures, "roundtrip int", text.opt("i"), 1)
    expectEquals(failures, "roundtrip child", text.optJSONObject("child")?.optString("ck"), "cv")

    // 数组根同样走 NATIVE_JSON
    val arrayOwner = CJsonNative.ownerFromJson("""[{"k":1},2,"three"]""")
    if (arrayOwner == 0L) {
        failures.add("array owner create failed")
    } else {
        val wrappedArray = JSONObject.fromCJsonOwnerAny(arrayOwner)
        CJsonNative.release(arrayOwner)
        val rootArray = wrappedArray as? JSONArray
        if (rootArray == null) {
            failures.add("array root not wrapped as JSONArray")
        } else {
            expectEquals(failures, "array root length", rootArray.length(), 3)
            expectEquals(failures, "array root nested", rootArray.optJSONObject(0)?.opt("k"), 1)
            expectEquals(failures, "array root text", rootArray.toString(), """[{"k": 1},2,"three"]""")
        }
    }

    return if (failures.isEmpty()) {
        "OK ohos lazy cJSON bridge (shared ownership + array root)"
    } else {
        "FAIL ${failures.joinToString("; ")}"
    }
}

private fun expectEquals(failures: MutableList<String>, name: String, actual: Any?, expected: Any?) {
    if (actual != expected) {
        failures.add("$name: expected=$expected actual=$actual")
    }
}
