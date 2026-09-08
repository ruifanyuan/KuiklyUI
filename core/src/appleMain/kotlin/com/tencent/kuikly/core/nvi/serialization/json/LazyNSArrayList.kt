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

import platform.Foundation.NSArray

/**
 * [NSArray] 之上的惰性 List，是 Apple 平台 [JSONArray] 的底层容器之一。
 * 与 [LazyNSDictionaryMap] 同构：读时按需转换、写时物化成 Kotlin List。
 */
internal class LazyNSArrayList(array: NSArray) : AbstractMutableList<Any?>() {

    private var array: NSArray? = array

    private var containerCache: MutableMap<Int, Any?>? = null

    private var materialized: MutableList<Any?>? = null

    override val size: Int
        get() {
            materialized?.let { return it.size }
            return array?.count?.toInt() ?: 0
        }

    override fun get(index: Int): Any? {
        materialized?.let { return it[index] }
        val source = array ?: throw IndexOutOfBoundsException("index: $index, size: 0")
        val count = source.count.toInt()
        if (index < 0 || index >= count) {
            throw IndexOutOfBoundsException("index: $index, size: $count")
        }
        containerCache?.let {
            if (it.containsKey(index)) {
                return it[index]
            }
        }
        val converted = nsValueToKotlin(source.objectAtIndex(index.toULong()))
        if (converted is JSONObject || converted is JSONArray) {
            val cache = containerCache ?: mutableMapOf<Int, Any?>().also { containerCache = it }
            cache[index] = converted
        }
        return converted
    }

    override fun add(index: Int, element: Any?) {
        ensureMaterialized().add(index, element)
    }

    override fun removeAt(index: Int): Any? {
        return ensureMaterialized().removeAt(index)
    }

    override fun set(index: Int, element: Any?): Any? {
        return ensureMaterialized().set(index, element)
    }

    private fun ensureMaterialized(): MutableList<Any?> {
        materialized?.let { return it }
        val list: MutableList<Any?> = JSONEngine.getMutableList()
        val source = array
        if (source != null) {
            val count = source.count.toInt()
            for (i in 0 until count) {
                list.add(get(i))
            }
        }
        array = null
        containerCache = null
        materialized = list
        return list
    }
}
