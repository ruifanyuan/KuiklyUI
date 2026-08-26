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

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.convert
import kotlinx.cinterop.toKString
import ohos.KRJSONArrayGet
import ohos.KRJSONDump
import ohos.KRJSONFreeString
import ohos.KRJSONGetBool
import ohos.KRJSONGetDouble
import ohos.KRJSONGetInt
import ohos.KRJSONGetSize
import ohos.KRJSONGetString
import ohos.KRJSONGetType
import ohos.KRJSONGetUint
import ohos.KRJSONObjectGet
import ohos.KRJSONObjectKeyAt
import ohos.KRJSONObjectValueAt
import ohos.KRJSONParse
import ohos.KRJSONRelease
import ohos.KRJSONRetain
import ohos.KRJSON_INVALID
import platform.posix.size_t

/**
 * 与 `KRJSONType` 一致（cinterop 枚举 ordinal / 底层 int）。
 * NULL=0 … OBJECT=7；缺失 / 失败用 [KRJSON_INVALID]（0xFF），不要当成 JSON null。
 */
internal const val JSON_KIND_NULL = 0
internal const val JSON_KIND_BOOL = 1
internal const val JSON_KIND_INT = 2
internal const val JSON_KIND_UINT = 3
internal const val JSON_KIND_DOUBLE = 4
internal const val JSON_KIND_STRING = 5
internal const val JSON_KIND_ARRAY = 6
internal const val JSON_KIND_OBJECT = 7

/** JSON null 的 tagged word 是 0；缺失成员是 [KRJSON_INVALID]。 */
internal const val JSON_NULL_BITS = 0L

/**
 * OHOS 侧对 render .so 中 `KRJSON*` C API 的 FFI 封装。
 *
 * 每个 Long 是一个 `KRJSONValue` 位型：拷贝字本身不改变所有权，必须
 * [retain] / [release]。子 object/array 各自 retain 自己的堆盒，父壳释放后子壳仍可读
 * （与 iOS 对 `NSDictionary` / `NSArray` 的 retain 对齐）。
 */
@OptIn(ExperimentalForeignApi::class)
internal object JsonNative {

    fun isInvalid(bits: Long): Boolean = bits.toULong() == KRJSON_INVALID

    fun retain(bits: Long): Long {
        if (bits == JSON_NULL_BITS || isInvalid(bits)) {
            return bits
        }
        return KRJSONRetain(bits.toULong()).toLong()
    }

    fun release(bits: Long) {
        if (bits != JSON_NULL_BITS && !isInvalid(bits)) {
            KRJSONRelease(bits.toULong())
        }
    }

    /** 解析 JSON 文本，返回 owned 字（失败为 0）。调用方负责 [release]。 */
    fun ownerFromJson(json: String): Long {
        if (json.isEmpty()) {
            return 0L
        }
        val parsed = KRJSONParse(json, json.encodeToByteArray().size.convert<size_t>(), null)
        return if (parsed == KRJSON_INVALID) 0L else parsed.toLong()
    }

    fun type(bits: Long): Int {
        if (isInvalid(bits)) {
            return -1
        }
        return KRJSONGetType(bits.toULong()).toInt()
    }

    fun size(bits: Long): Int {
        if (bits == JSON_NULL_BITS || isInvalid(bits)) {
            return 0
        }
        return KRJSONGetSize(bits.toULong()).toInt()
    }

    fun hasKey(objectBits: Long, key: String): Boolean {
        if (objectBits == JSON_NULL_BITS || isInvalid(objectBits)) {
            return false
        }
        return !isInvalid(KRJSONObjectGet(objectBits.toULong(), key).toLong())
    }

    fun objectGet(objectBits: Long, key: String): Long {
        if (objectBits == JSON_NULL_BITS || isInvalid(objectBits)) {
            return KRJSON_INVALID.toLong()
        }
        return KRJSONObjectGet(objectBits.toULong(), key).toLong()
    }

    fun objectKeyAt(objectBits: Long, index: Int): String? {
        if (objectBits == JSON_NULL_BITS || isInvalid(objectBits) || index < 0) {
            return null
        }
        return KRJSONObjectKeyAt(objectBits.toULong(), index.convert<size_t>())?.toKString()
    }

    fun objectValueAt(objectBits: Long, index: Int): Long {
        if (objectBits == JSON_NULL_BITS || isInvalid(objectBits) || index < 0) {
            return KRJSON_INVALID.toLong()
        }
        return KRJSONObjectValueAt(objectBits.toULong(), index.convert<size_t>()).toLong()
    }

    fun arrayGet(arrayBits: Long, index: Int): Long {
        if (arrayBits == JSON_NULL_BITS || isInvalid(arrayBits) || index < 0) {
            return KRJSON_INVALID.toLong()
        }
        return KRJSONArrayGet(arrayBits.toULong(), index.convert<size_t>()).toLong()
    }

    fun asBool(bits: Long, fallback: Boolean): Boolean {
        if (isInvalid(bits)) {
            return fallback
        }
        return KRJSONGetBool(bits.toULong(), fallback)
    }

    fun asInt(bits: Long): Long {
        return KRJSONGetInt(bits.toULong(), 0)
    }

    fun asUint(bits: Long): ULong {
        return KRJSONGetUint(bits.toULong(), 0u)
    }

    fun asDouble(bits: Long, fallback: Double): Double {
        if (isInvalid(bits)) {
            return fallback
        }
        return KRJSONGetDouble(bits.toULong(), fallback)
    }

    fun asString(bits: Long): String? {
        if (isInvalid(bits) || type(bits) != JSON_KIND_STRING) {
            return null
        }
        return KRJSONGetString(bits.toULong(), null)?.toKString()
    }

    fun print(bits: Long): String? {
        if (bits == JSON_NULL_BITS || isInvalid(bits)) {
            return null
        }
        val printed = KRJSONDump(bits.toULong()) ?: return null
        return try {
            printed.toKString()
        } finally {
            KRJSONFreeString(printed)
        }
    }
}

/**
 * KRJSON 数字：INT/UINT 走整数位宽；DOUBLE 仍按 [numberFromJsonDouble] 整值落成 Int/Long。
 */
internal fun numberFromJson(bits: Long): Any? {
    return when (JsonNative.type(bits)) {
        JSON_KIND_INT -> {
            val n = JsonNative.asInt(bits)
            if (n >= Int.MIN_VALUE.toLong() && n <= Int.MAX_VALUE.toLong()) n.toInt() else n
        }
        JSON_KIND_UINT -> {
            val u = JsonNative.asUint(bits)
            if (u <= Long.MAX_VALUE.toULong()) u.toLong() else JsonNative.asDouble(bits, 0.0)
        }
        JSON_KIND_DOUBLE -> numberFromJsonDouble(JsonNative.asDouble(bits, 0.0))
        else -> null
    }
}

/**
 * 整值落成 Int/Long，带小数落成 Double（DOUBLE 标签与历史仅 double 数字语义共用）。
 */
internal fun numberFromJsonDouble(number: Double): Any {
    val asLong = number.toLong()
    if (number != asLong.toDouble()) {
        return number
    }
    return if (asLong >= Int.MIN_VALUE.toLong() && asLong <= Int.MAX_VALUE.toLong()) {
        asLong.toInt()
    } else {
        asLong
    }
}
