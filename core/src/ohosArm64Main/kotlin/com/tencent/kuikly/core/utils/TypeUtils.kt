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

package com.tencent.kuikly.core.utils

import com.tencent.kuikly.core.nvi.serialization.json.JSONArray
import com.tencent.kuikly.core.nvi.serialization.json.JSONObject
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_ARRAY
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_BOOL
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_BYTES
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_DOUBLE
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_FLOAT
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_INT
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_LONG
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_NULL
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_OBJECT
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_STRING
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_U16STRING
import com.tencent.kuikly.core.nvi.serialization.json.JSON_KIND_UINT
import com.tencent.kuikly.core.nvi.serialization.json.JsonNative
import com.tencent.kuikly.core.nvi.serialization.json.numberFromJson
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.convert
import kotlinx.cinterop.reinterpret
import kotlinx.cinterop.usePinned
import ohos.KRJSONArrayAppend
import ohos.KRJSONNewArray
import ohos.KRJSONNewBool
import ohos.KRJSONNewBytes
import ohos.KRJSONNewDouble
import ohos.KRJSONNewFloat
import ohos.KRJSONNewInt32
import ohos.KRJSONNewLong
import ohos.KRJSONNewNull
import ohos.KRJSONNewStringUtf16
import ohos.KRJSONRelease
import ohos.KRJSONObjectPutUtf16
import ohos.KRRenderCValue
import platform.posix.size_t

/**
 * Converts a Kotlin bridge argument to one owned KRJSON word.
 * The caller must release it after the native call.
 */
@OptIn(ExperimentalForeignApi::class)
fun Any?.toKRRenderCValue(): KRRenderCValue {
    return when (this) {
        null -> KRJSONNewNull()
        is Boolean -> KRJSONNewBool(this)
        is Int -> KRJSONNewInt32(this)
        is Long -> KRJSONNewLong(this)
        is Float -> KRJSONNewFloat(this)
        is Double -> KRJSONNewDouble(this)
        is String -> if (isEmpty()) {
            KRJSONNewStringUtf16(null, 0.convert<size_t>())
        } else {
            toCharArray().usePinned { pinned ->
                KRJSONNewStringUtf16(
                    pinned.addressOf(0).reinterpret(),
                    length.convert<size_t>(),
                )
            }
        }
        is ByteArray -> usePinned {
            KRJSONNewBytes(
                if (isEmpty()) null else it.addressOf(0).reinterpret(),
                size.convert<size_t>(),
            )
        }
        is Array<*> -> toNativeArray(asList())
        is List<*> -> toNativeArray(this)
        is Map<*, *> -> toNativeObject(this)
        is JSONObject -> toNativeObject(nameValuePairs)
        is JSONArray -> toNativeArray(values)
        else -> KRJSONNewNull()
    }
}

@OptIn(ExperimentalForeignApi::class)
private fun toNativeArray(values: List<*>): KRRenderCValue {
    val array = KRJSONNewArray()
    values.forEach { element ->
        val child = element.toKRRenderCValue()
        KRJSONArrayAppend(array, child)
        KRJSONRelease(child)
    }
    return array
}

@OptIn(ExperimentalForeignApi::class)
private fun toNativeObject(values: Map<*, *>): KRRenderCValue {
    val objectValue = ohos.KRJSONNewObjectUtf16()
    values.forEach { (rawKey, rawValue) ->
        val key = rawKey as? String ?: return@forEach
        val child = rawValue.toKRRenderCValue()
        if (key.isEmpty()) {
            KRJSONObjectPutUtf16(objectValue, null, 0.convert<size_t>(), child)
        } else {
            key.toCharArray().usePinned { pinned ->
                KRJSONObjectPutUtf16(
                    objectValue,
                    pinned.addressOf(0).reinterpret(),
                    key.length.convert<size_t>(),
                    child,
                )
            }
        }
        KRJSONRelease(child)
    }
    return objectValue
}

/**
 * Converts a borrowed bridge word. Container results retain their own native
 * reference; scalar and binary values are copied immediately.
 */
@OptIn(ExperimentalForeignApi::class)
fun KRRenderCValue.toAny(): Any? {
    val bits = toLong()
    return when (JsonNative.type(bits)) {
        JSON_KIND_NULL -> null
        JSON_KIND_BOOL -> JsonNative.asBool(bits, false)
        JSON_KIND_INT, JSON_KIND_UINT, JSON_KIND_DOUBLE -> numberFromJson(bits)
        JSON_KIND_LONG -> JsonNative.asInt(bits)
        JSON_KIND_FLOAT -> JsonNative.asDouble(bits, 0.0).toFloat()
        JSON_KIND_STRING, JSON_KIND_U16STRING -> JsonNative.asString(bits)
        JSON_KIND_BYTES -> JsonNative.asByteArray(bits)
        JSON_KIND_ARRAY, JSON_KIND_OBJECT -> JSONObject.fromJsonOwnerAny(bits)
        else -> null
    }
}

/** Converts and releases an owned result returned by C++. */
@OptIn(ExperimentalForeignApi::class)
fun KRRenderCValue.consumeToAny(): Any? {
    return try {
        toAny()
    } finally {
        KRJSONRelease(this)
    }
}

@OptIn(ExperimentalForeignApi::class)
fun KRRenderCValue.asString(): String = JsonNative.asString(toLong()) ?: ""
