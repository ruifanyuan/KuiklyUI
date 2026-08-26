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

import com.tencent.kuikly.core.module.toNSData
import com.tencent.kuikly.core.utils.toKotlinBridgeArg
import kotlinx.cinterop.BetaInteropApi
import kotlinx.cinterop.ExperimentalForeignApi
import platform.Foundation.NSMutableArray
import platform.Foundation.NSNumber
import platform.Foundation.addObject
import platform.Foundation.numberWithInt

/**
 * Foundation 惰性桥接自检：走 `toKotlinBridgeArg()` 这条真实的 callKotlin 入参路径，
 * 验证 `NSDictionary` / `NSArray` 被包成惰性 [JSONObject] / [JSONArray]、数字选型正确、
 * 读后可写（物化）、以及含 `NSData` 的数组按原样透传（二进制不丢）。
 *
 * 返回 `OK ...` 或 `FAIL ...`，供 demo 的桥接回归页展示。放在 core 内是因为
 * [toNSDictionary] / [LazyNSDictionaryMap] 都是 internal。
 */
@OptIn(ExperimentalForeignApi::class, BetaInteropApi::class)
fun verifyAppleLazyJsonBridge(): String {
    val failures = mutableListOf<String>()
    val source = JSONObject(
        """{"i":1,"l":2147483648,"d":1.5,"s":"str","b":true,"nil":null,""" +
            """"child":{"ck":"cv"},"arr":[1,"two",{"ak":3}]}"""
    )

    // Kotlin JSONObject → NSDictionary → 桥接入参转换（原生侧递给 Kotlin 的形态）
    val bridged = source.toNSDictionary().toKotlinBridgeArg() as? JSONObject
    if (bridged == null) {
        return "FAIL NSDictionary not wrapped as JSONObject"
    }

    expectEquals(failures, "size", bridged.length(), 8)
    expectEquals(failures, "int", bridged.opt("i"), 1)
    expectEquals(failures, "long", bridged.opt("l"), 2147483648L)
    expectEquals(failures, "double", bridged.opt("d"), 1.5)
    expectEquals(failures, "string", bridged.opt("s"), "str")
    expectEquals(failures, "bool", bridged.opt("b"), true)
    expectEquals(failures, "null value", bridged.opt("nil"), null)
    expectEquals(failures, "has null key", bridged.has("nil"), true)

    val child = bridged.optJSONObject("child")
    if (child == null) {
        failures.add("child object missing")
    } else {
        expectEquals(failures, "child field", child.optString("ck"), "cv")
        expectEquals(failures, "child identity", bridged.optJSONObject("child") === child, true)
    }
    val arr = bridged.optJSONArray("arr")
    if (arr == null) {
        failures.add("array missing")
    } else {
        expectEquals(failures, "array length", arr.length(), 3)
        expectEquals(failures, "array int element", arr.opt(0), 1)
        expectEquals(failures, "array nested field", arr.optJSONObject(2)?.opt("ak"), 3)
    }

    // 读后写：惰性字典物化，已取出的子对象仍然有效
    bridged.put("probe", "p")
    expectEquals(failures, "after put probe", bridged.optString("probe"), "p")
    expectEquals(failures, "child alive after parent materialize", child?.optString("ck"), "cv")
    expectEquals(failures, "int alive after materialize", bridged.opt("i"), 1)

    // 顶层 NSArray：包成 JSONArray
    val topArray = source.optJSONArray("arr")?.toNSArray()?.toKotlinBridgeArg() as? JSONArray
    if (topArray == null) {
        failures.add("NSArray root not wrapped as JSONArray")
    } else {
        expectEquals(failures, "array root length", topArray.length(), 3)
        expectEquals(failures, "array root nested", topArray.optJSONObject(2)?.opt("ak"), 3)
    }

    // 含二进制元素的数组保持原样透传：不能被包成 JSONArray（否则 ByteArray 语义丢失）
    val binaryArray = NSMutableArray()
    binaryArray.addObject(NSNumber.numberWithInt(1))
    binaryArray.addObject(byteArrayOf(1, 2, 3).toNSData())
    val binaryConverted = binaryArray.toKotlinBridgeArg()
    if (binaryConverted is JSONArray) {
        failures.add("array with NSData must pass through, got JSONArray")
    }

    return if (failures.isEmpty()) {
        "OK apple lazy Foundation bridge (dict/array + binary pass-through)"
    } else {
        "FAIL ${failures.joinToString("; ")}"
    }
}

private fun expectEquals(failures: MutableList<String>, name: String, actual: Any?, expected: Any?) {
    if (actual != expected) {
        failures.add("$name: expected=$expected actual=$actual")
    }
}
