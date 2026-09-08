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

import kotlinx.cinterop.ByteVar
import kotlinx.cinterop.CPointer
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.UShortVar
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.alloc
import kotlinx.cinterop.convert
import kotlinx.cinterop.get
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.reinterpret
import kotlinx.cinterop.toKString
import kotlinx.cinterop.usePinned
import kotlinx.cinterop.value
import ohos.KRJSONArrayGet
import ohos.KRJSONDumpUtf16
import ohos.KRJSONFreeString
import ohos.KRJSONGetBool
import ohos.KRJSONGetBytes
import ohos.KRJSONGetDouble
import ohos.KRJSONGetInt
import ohos.KRJSONGetSize
import ohos.KRJSONGetString
import ohos.KRJSONGetStringUtf16
import ohos.KRJSONGetType
import ohos.KRJSONGetUint
import ohos.KRJSONObjectGet
import ohos.KRJSONObjectGetUtf16
import ohos.KRJSONObjectKeyAt
import ohos.KRJSONObjectKeyAtUtf16
import ohos.KRJSONObjectKeysAreUtf16
import ohos.KRJSONObjectValueAt
import ohos.KRJSONParseUtf16
import ohos.KRJSONRelease
import ohos.KRJSONRetain
import ohos.KRJSON_INVALID
import platform.posix.memcpy
import platform.posix.size_t
import platform.posix.size_tVar

/**
 * 与 `KRJSONType` / 存储 `kTag*` 对齐（INT 含 kTagInt/Int32/Int64；4 与 10 为空洞）。
 * 缺失 / 失败用 [KRJSON_INVALID]（0xFF），不要当成 JSON null。
 */
internal const val JSON_KIND_NULL = 0
internal const val JSON_KIND_BOOL = 1
internal const val JSON_KIND_INT = 2
internal const val JSON_KIND_DOUBLE = 3
internal const val JSON_KIND_UINT = 5
internal const val JSON_KIND_STRING = 6
internal const val JSON_KIND_ARRAY = 7
internal const val JSON_KIND_OBJECT = 8
internal const val JSON_KIND_BYTES = 9
internal const val JSON_KIND_FLOAT = 11
internal const val JSON_KIND_LONG = 12
internal const val JSON_KIND_U16STRING = 13

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
        val parsed = json.toCharArray().usePinned { pinned ->
            KRJSONParseUtf16(
                pinned.addressOf(0).reinterpret(),
                json.length.convert<size_t>(),
                null,
            )
        }
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
        return !isInvalid(objectGet(objectBits, key))
    }

    fun objectGet(objectBits: Long, key: String): Long {
        if (objectBits == JSON_NULL_BITS || isInvalid(objectBits)) {
            return KRJSON_INVALID.toLong()
        }
        if (KRJSONObjectKeysAreUtf16(objectBits.toULong())) {
            if (key.isEmpty()) {
                return KRJSONObjectGetUtf16(objectBits.toULong(), null, 0.convert<size_t>()).toLong()
            }
            return key.toCharArray().usePinned { pinned ->
                KRJSONObjectGetUtf16(
                    objectBits.toULong(),
                    pinned.addressOf(0).reinterpret(),
                    key.length.convert<size_t>(),
                ).toLong()
            }
        }
        return KRJSONObjectGet(objectBits.toULong(), key).toLong()
    }

    fun objectKeyAt(objectBits: Long, index: Int): String? {
        if (objectBits == JSON_NULL_BITS || isInvalid(objectBits) || index < 0) {
            return null
        }
        if (KRJSONObjectKeysAreUtf16(objectBits.toULong())) {
            return memScoped {
                val units = alloc<size_tVar>()
                val ptr = KRJSONObjectKeyAtUtf16(
                    objectBits.toULong(),
                    index.convert<size_t>(),
                    units.ptr,
                ) ?: return@memScoped null
                stringFromUtf16Chars(ptr, units.value.toInt())
            }
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
        return memScoped {
            val len = alloc<size_tVar>()
            when (type(bits)) {
                JSON_KIND_U16STRING -> {
                    val ptr = KRJSONGetStringUtf16(bits.toULong(), len.ptr) ?: return@memScoped null
                    stringFromUtf16Chars(ptr, len.value.toInt())
                }
                JSON_KIND_STRING -> {
                    val ptr = KRJSONGetString(bits.toULong(), len.ptr) ?: return@memScoped null
                    stringFromUtf8Chars(ptr, len.value.toInt())
                }
                else -> null
            }
        }
    }

    /** UTF-16 unit 数即 Kotlin `Char` 数：定长 `CharArray` + memcpy，避免 `toKStringFromUtf16` 扫 NUL。 */
    private fun stringFromUtf16Chars(ptr: CPointer<UShortVar>, units: Int): String {
        if (units <= 0) {
            return ""
        }
        val chars = CharArray(units)
        chars.usePinned { pinned ->
            memcpy(pinned.addressOf(0), ptr, (units * 2).convert<size_t>())
        }
        return chars.concatToString()
    }

    /**
     * UTF-8 `out_len` 是字节数，UTF-16 单元数 ≤ 字节数。定长 `CharArray` 解码，避免 `toKString` 扫 NUL。
     */
    private fun stringFromUtf8Chars(ptr: CPointer<ByteVar>, bytes: Int): String {
        if (bytes <= 0) {
            return ""
        }
        val chars = CharArray(bytes)
        val n = decodeUtf8Into(ptr, bytes, chars)
        return if (n == bytes) chars.concatToString() else chars.concatToString(0, n)
    }

    private fun decodeUtf8Into(src: CPointer<ByteVar>, byteLen: Int, dst: CharArray): Int {
        var i = 0
        var o = 0
        while (i < byteLen) {
            val c0 = src[i].toInt() and 0xff
            when {
                c0 < 0x80 -> {
                    dst[o++] = c0.toChar()
                    i++
                }
                c0 and 0xe0 == 0xc0 && i + 1 < byteLen -> {
                    dst[o++] = (((c0 and 0x1f) shl 6) or (src[i + 1].toInt() and 0x3f)).toChar()
                    i += 2
                }
                c0 and 0xf0 == 0xe0 && i + 2 < byteLen -> {
                    val c1 = src[i + 1].toInt() and 0x3f
                    val c2 = src[i + 2].toInt() and 0x3f
                    dst[o++] = (((c0 and 0x0f) shl 12) or (c1 shl 6) or c2).toChar()
                    i += 3
                }
                c0 and 0xf8 == 0xf0 && i + 3 < byteLen -> {
                    val c1 = src[i + 1].toInt() and 0x3f
                    val c2 = src[i + 2].toInt() and 0x3f
                    val c3 = src[i + 3].toInt() and 0x3f
                    val cp = ((c0 and 0x07) shl 18) or (c1 shl 12) or (c2 shl 6) or c3
                    val u = cp - 0x10000
                    dst[o++] = ((u shr 10) + 0xD800).toChar()
                    dst[o++] = ((u and 0x3FF) + 0xDC00).toChar()
                    i += 4
                }
                else -> {
                    dst[o++] = '\uFFFD'
                    i++
                }
            }
        }
        return o
    }

    fun asByteArray(bits: Long): ByteArray {
        if (isInvalid(bits) || type(bits) != JSON_KIND_BYTES) {
            return ByteArray(0)
        }
        val size = KRJSONGetSize(bits.toULong()).toInt()
        if (size <= 0) {
            return ByteArray(0)
        }
        val source = KRJSONGetBytes(bits.toULong(), null) ?: return ByteArray(0)
        return ByteArray(size).also { bytes ->
            bytes.usePinned { pinned ->
            memcpy(pinned.addressOf(0), source, size.convert<size_t>())
            }
        }
    }

    fun print(bits: Long): String? {
        if (bits == JSON_NULL_BITS || isInvalid(bits)) {
            return null
        }
        return memScoped {
            val units = alloc<size_tVar>()
            val printed = KRJSONDumpUtf16(bits.toULong(), units.ptr) ?: return@memScoped null
            try {
                stringFromUtf16Chars(printed, units.value.toInt())
            } finally {
                KRJSONFreeString(printed.reinterpret())
            }
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
