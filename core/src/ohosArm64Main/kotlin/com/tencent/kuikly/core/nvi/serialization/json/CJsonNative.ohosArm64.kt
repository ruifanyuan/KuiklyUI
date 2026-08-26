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
import kotlinx.cinterop.toKString
import ohos.kuikly_cjson_as_bool
import ohos.kuikly_cjson_as_number
import ohos.kuikly_cjson_as_string
import ohos.kuikly_cjson_child_key
import ohos.kuikly_cjson_first_child
import ohos.kuikly_cjson_next
import ohos.kuikly_cjson_free_string
import ohos.kuikly_cjson_get_array
import ohos.kuikly_cjson_get_bool
import ohos.kuikly_cjson_get_number
import ohos.kuikly_cjson_get_object
import ohos.kuikly_cjson_get_string
import ohos.kuikly_cjson_has
import ohos.kuikly_cjson_item_at
import ohos.kuikly_cjson_key_at
import ohos.kuikly_cjson_node_kind
import ohos.kuikly_cjson_owner_create_from_string
import ohos.kuikly_cjson_owner_root
import ohos.kuikly_cjson_print
import ohos.kuikly_cjson_release
import ohos.kuikly_cjson_retain
import ohos.kuikly_cjson_size
import ohos.kuikly_cjson_value_kind

/** cJSON 值类型，与 `kuikly_cjson_value_kind` / `kuikly_cjson_node_kind` 返回值一致。 */
internal const val CJSON_KIND_BOOL = 1
internal const val CJSON_KIND_NUMBER = 2
internal const val CJSON_KIND_STRING = 3
internal const val CJSON_KIND_OBJECT = 4
internal const val CJSON_KIND_ARRAY = 5

/**
 * OHOS 侧对 render .so 中 `KRLazyCJsonBridge` 的 FFI 封装。
 *
 * 每个 Long 句柄都是一个堆上的 `std::shared_ptr`：[retain] 返回共享同一棵树的新句柄，
 * [release] 释放该句柄；最后一个句柄消失时 cJSON 树才被删除。Kotlin 侧持有句柄期间，
 * 由它取出的 node 指针才是安全的。
 */
@OptIn(ExperimentalForeignApi::class)
internal object CJsonNative {
    /** New handle sharing ownership with [ownerPtr], or 0. */
    fun retain(ownerPtr: Long): Long {
        if (ownerPtr == 0L) {
            return 0L
        }
        return kuikly_cjson_retain(ownerPtr)
    }

    fun release(ownerPtr: Long) {
        if (ownerPtr != 0L) {
            kuikly_cjson_release(ownerPtr)
        }
    }

    /** 由 JSON 文本新建一棵自持有的树，返回新句柄（调用方负责 [release]）。 */
    fun ownerFromJson(json: String): Long {
        return kuikly_cjson_owner_create_from_string(json)
    }

    fun ownerRoot(ownerPtr: Long): Long {
        if (ownerPtr == 0L) {
            return 0L
        }
        return kuikly_cjson_owner_root(ownerPtr)
    }

    fun hasKey(ptr: Long, key: String): Boolean {
        if (ptr == 0L) {
            return false
        }
        return kuikly_cjson_has(ptr, key) != 0
    }

    fun size(ptr: Long): Int {
        if (ptr == 0L) {
            return 0
        }
        return kuikly_cjson_size(ptr)
    }

    fun keyAt(ptr: Long, index: Int): String? {
        if (ptr == 0L) {
            return null
        }
        return kuikly_cjson_key_at(ptr, index)?.toKString()
    }

    /** 节点自身类型，用于判断 owner 根是 object（[CJSON_KIND_OBJECT]）还是 array（[CJSON_KIND_ARRAY]）。 */
    fun nodeKind(ptr: Long): Int {
        if (ptr == 0L) {
            return 0
        }
        return kuikly_cjson_node_kind(ptr)
    }

    fun valueKind(ptr: Long, key: String): Int {
        if (ptr == 0L) {
            return 0
        }
        return kuikly_cjson_value_kind(ptr, key)
    }

    fun getBool(ptr: Long, key: String, fallback: Boolean): Boolean {
        if (ptr == 0L) {
            return fallback
        }
        return kuikly_cjson_get_bool(ptr, key, if (fallback) 1 else 0) != 0
    }

    fun getNumber(ptr: Long, key: String, fallback: Double): Double {
        if (ptr == 0L) {
            return fallback
        }
        return kuikly_cjson_get_number(ptr, key, fallback)
    }

    fun getString(ptr: Long, key: String): String? {
        if (ptr == 0L) {
            return null
        }
        return kuikly_cjson_get_string(ptr, key)?.toKString()
    }

    fun getObjectPtr(ptr: Long, key: String): Long {
        if (ptr == 0L) {
            return 0L
        }
        return kuikly_cjson_get_object(ptr, key)
    }

    fun getArrayPtr(ptr: Long, key: String): Long {
        if (ptr == 0L) {
            return 0L
        }
        return kuikly_cjson_get_array(ptr, key)
    }

    fun itemAt(ptr: Long, index: Int): Long {
        if (ptr == 0L || index < 0) {
            return 0L
        }
        return kuikly_cjson_item_at(ptr, index)
    }

    /** 子节点游标：first_child → next 顺序遍历，childKey 取 object 元素的键。 */
    fun firstChild(ptr: Long): Long {
        if (ptr == 0L) {
            return 0L
        }
        return kuikly_cjson_first_child(ptr)
    }

    fun nextSibling(ptr: Long): Long {
        if (ptr == 0L) {
            return 0L
        }
        return kuikly_cjson_next(ptr)
    }

    fun childKey(ptr: Long): String? {
        if (ptr == 0L) {
            return null
        }
        return kuikly_cjson_child_key(ptr)?.toKString()
    }

    fun asBool(ptr: Long, fallback: Boolean): Boolean {
        if (ptr == 0L) {
            return fallback
        }
        return kuikly_cjson_as_bool(ptr, if (fallback) 1 else 0) != 0
    }

    fun asNumber(ptr: Long, fallback: Double): Double {
        if (ptr == 0L) {
            return fallback
        }
        return kuikly_cjson_as_number(ptr, fallback)
    }

    fun asString(ptr: Long): String? {
        if (ptr == 0L) {
            return null
        }
        return kuikly_cjson_as_string(ptr)?.toKString()
    }

    fun print(ptr: Long): String? {
        if (ptr == 0L) {
            return null
        }
        val printed = kuikly_cjson_print(ptr) ?: return null
        return try {
            printed.toKString()
        } finally {
            kuikly_cjson_free_string(printed)
        }
    }
}

/**
 * cJSON 只有 double：整值落成 Int/Long，带小数落成 Double。
 * 与 [LazyCJsonMap] / [LazyCJsonList] 共用，保证对象字段与数组元素数字选型一致。
 */
internal fun numberFromCJson(number: Double): Any {
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
