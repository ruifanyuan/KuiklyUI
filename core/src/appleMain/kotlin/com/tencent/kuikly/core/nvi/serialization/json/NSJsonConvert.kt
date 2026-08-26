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

import com.tencent.kuikly.core.module.toByteArray
import com.tencent.kuikly.core.module.toNSData
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.pointed
import kotlinx.cinterop.value
import platform.Foundation.NSArray
import platform.Foundation.NSData
import platform.Foundation.NSDictionary
import platform.Foundation.NSMutableArray
import platform.Foundation.NSMutableDictionary
import platform.Foundation.NSNull
import platform.Foundation.NSNumber
import platform.Foundation.addObject
import platform.Foundation.setValue

/**
 * Foundation 值 ↔ Kotlin 值互转。
 *
 * Foundation → Kotlin 的数字选型必须与宽松扫描器（[AbstractJSONTokener]）一致：
 * 整数在 `Int` 范围内给 `Int`，超出给 `Long`，浮点给 `Double`。
 * `NSNumber` 用 `objCType` 区分整数与浮点（`NSJSONSerialization` 对 `0.0` 会给
 * 浮点类型的 `NSNumber`，只看数值会误判成 `Int`）。
 */

@OptIn(ExperimentalForeignApi::class)
internal fun nsNumberToKotlin(number: NSNumber): Any {
    val type = number.objCType?.pointed?.value?.toInt()?.toChar() ?: 'q'
    return when (type) {
        // BOOL 在 arm64 上是 'B'，在 x86_64 上是 'c'；JSON 里没有 byte，可直接判定为布尔
        'c', 'C', 'B' -> number.boolValue
        'f', 'd' -> number.doubleValue
        else -> {
            val double = number.doubleValue
            val long = number.longLongValue
            if (double == long.toDouble()) {
                if (long >= Int.MIN_VALUE.toLong() && long <= Int.MAX_VALUE.toLong()) {
                    long.toInt()
                } else {
                    long
                }
            } else {
                double
            }
        }
    }
}

/**
 * Foundation 值转 Kotlin 值：容器转成惰性包装的 [JSONObject] / [JSONArray]，
 * `NSNull` 转成 `null`，`NSData` 转成 `ByteArray`（二进制不能丢），
 * 其余标量转成对应 Kotlin 类型。
 */
internal fun nsValueToKotlin(value: Any?): Any? {
    if (value == null || value is NSNull) {
        return null
    }
    return when (value) {
        is JSONObject, is JSONArray -> value
        is String -> value
        is Boolean, is Int, is Long, is Double, is Float, is ByteArray -> value
        is NSNumber -> nsNumberToKotlin(value)
        is NSData -> value.toByteArray()
        is NSDictionary -> JSONObject(value)
        is NSArray -> JSONArray(value)
        is Map<*, *> -> toBridgeJSONObject(value)
        is List<*> -> toBridgeJSONArray(value)
        else -> value.toString()
    }
}

/** [NSArray] 里是否有二进制元素；有的话整个数组按原样透传，不能包成 [JSONArray]。 */
internal fun hasBinaryElement(array: NSArray): Boolean {
    val count = array.count.toInt()
    for (i in 0 until count) {
        val element = array.objectAtIndex(i.toULong())
        if (element is NSData || element is ByteArray) {
            return true
        }
    }
    return false
}

/** [List] 版本的二进制判定，用于 Foundation 数组已被映射成 Kotlin 集合的场景。 */
internal fun hasBinaryElement(list: List<*>): Boolean {
    for (element in list) {
        if (element is NSData || element is ByteArray) {
            return true
        }
    }
    return false
}

/** Kotlin [Map] 形态的桥接入参转成 [JSONObject]（值逐个按 Foundation 规则转换）。 */
internal fun toBridgeJSONObject(map: Map<*, *>): JSONObject {
    val pairs: MutableMap<String, Any?> = JSONEngine.getMutableMap()
    for ((key, value) in map) {
        pairs[key?.toString() ?: "null"] = nsValueToKotlin(value)
    }
    return JSONObject(pairs)
}

/** Kotlin [List] 形态的桥接入参转成 [JSONArray]。 */
internal fun toBridgeJSONArray(list: List<*>): JSONArray {
    val values: MutableList<Any?> = JSONEngine.getMutableList()
    for (element in list) {
        values.add(nsValueToKotlin(element))
    }
    return JSONArray(values)
}

/** Kotlin 值转 Foundation 值；`null` 用 [NSNull] 承载，因为 Foundation 容器不能存 nil。 */
internal fun kotlinValueToNS(value: Any?): Any {
    return when (value) {
        null -> NSNull.`null`()
        is JSONObject -> value.toNSDictionary()
        is JSONArray -> value.toNSArray()
        is ByteArray -> value.toNSData()
        is Map<*, *> -> {
            val dictionary = NSMutableDictionary()
            for ((key, mapValue) in value) {
                dictionary.setValue(kotlinValueToNS(mapValue), forKey = key.toString())
            }
            dictionary
        }
        is List<*> -> {
            val array = NSMutableArray()
            for (element in value) {
                array.addObject(kotlinValueToNS(element))
            }
            array
        }
        else -> value
    }
}

/** [JSONObject] 转 Foundation 字典，供结构化桥接出参使用。 */
internal fun kotlinMapToNSDictionary(map: Map<String, Any?>): NSDictionary {
    val dictionary = NSMutableDictionary()
    for ((key, value) in map) {
        dictionary.setValue(kotlinValueToNS(value), forKey = key)
    }
    return dictionary
}

/** [JSONArray] 转 Foundation 数组，供结构化桥接出参使用。 */
internal fun kotlinListToNSArray(list: List<Any?>): NSArray {
    val array = NSMutableArray()
    for (element in list) {
        array.addObject(kotlinValueToNS(element))
    }
    return array
}
