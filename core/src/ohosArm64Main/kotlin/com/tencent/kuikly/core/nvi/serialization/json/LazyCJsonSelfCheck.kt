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
 * `NATIVE_JSON` 惰性桥接自检：构造一棵自持有的 KRJSON 树，模拟原生产者在调用返回后
 * 立刻释放自己的引用，验证 Kotlin 侧仍能安全读取（节点 retain）、子对象比父对象活得久、
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
        return "FAIL KRJSON parse failed (render .so symbols missing?)"
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

    // 遍历中途 put：iterator 只握 key 快照，物化删树后继续 next / opt 不得 UAF
    val scalarOwner = CJsonNative.ownerFromJson("""{"a":1,"b":2,"c":3}""")
    if (scalarOwner == 0L) {
        failures.add("scalar owner create failed")
    } else {
        val scalarWrapped = JSONObject.fromCJsonOwner(scalarOwner)
        CJsonNative.release(scalarOwner)
        val seen = ArrayList<String>()
        for ((key, value) in scalarWrapped.nameValuePairs) {
            seen.add(key)
            if (key == "a") {
                expectEquals(failures, "entry value before mid-iter put", value, 1)
                scalarWrapped.put("z", 9)
            } else {
                // 物化后 LazyEntry.value 走 map[key]，不得再碰已释放的原生树
                expectEquals(failures, "entry value after mid-iter put ($key)", value, scalarWrapped.opt(key))
            }
        }
        expectEquals(failures, "keys snapshot during put", seen.joinToString(","), "a,b,c")
        expectEquals(failures, "put during keys() visible", scalarWrapped.opt("z"), 9)
        expectEquals(failures, "value after mid-iter put", scalarWrapped.opt("b"), 2)
    }

    // 序列化 → 再解析：与其他平台同一套文本行为
    val text = JSONObject(obj.toString())
    expectEquals(failures, "roundtrip int", text.opt("i"), 1)
    expectEquals(failures, "roundtrip child", text.optJSONObject("child")?.optString("ck"), "cv")

    // 数组根同样走惰性 List（不再 print + 宽松重解析）
    val arrayOwner = CJsonNative.ownerFromJson("""[1,"two",{"ak":3},[9]]""")
    if (arrayOwner == 0L) {
        failures.add("array owner create failed")
    } else {
        val wrappedArray = JSONObject.fromCJsonOwnerAny(arrayOwner)
        CJsonNative.release(arrayOwner)
        val rootArray = wrappedArray as? JSONArray
        if (rootArray == null) {
            failures.add("array root not wrapped as JSONArray: ${wrappedArray?.let { it::class.simpleName }}")
        } else {
            expectEquals(failures, "array root length", rootArray.length(), 4)
            expectEquals(failures, "array root int", rootArray.opt(0), 1)
            expectEquals(failures, "array root nested obj", rootArray.optJSONObject(2)?.opt("ak"), 3)
            expectEquals(failures, "array root nested arr", rootArray.optJSONArray(3)?.opt(0), 9)
            val roundtrip = JSONArray(rootArray.toString())
            expectEquals(failures, "array root roundtrip length", roundtrip.length(), 4)
            expectEquals(failures, "array root roundtrip nested", roundtrip.optJSONObject(2)?.opt("ak"), 3)
        }
    }

    return if (failures.isEmpty()) {
        "OK ohos lazy KRJSON bridge (per-node retain + array root)"
    } else {
        "FAIL ${failures.joinToString("; ")}"
    }
}

private fun expectEquals(failures: MutableList<String>, name: String, actual: Any?, expected: Any?) {
    if (actual != expected) {
        failures.add("$name: expected=$expected actual=$actual")
    }
}
