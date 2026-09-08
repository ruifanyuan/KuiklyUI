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

import platform.Foundation.NSDictionary
import platform.Foundation.allKeys

/**
 * [NSDictionary] 之上的惰性 Map，是 Apple 平台 [JSONObject] 的底层容器之一。
 *
 * 读取时按需把 Foundation 值转成 Kotlin 值，不做整树拷贝；首次写入（或 [clear] /
 * [remove]）时才物化成 Kotlin Map 并放弃 Foundation 引用。
 *
 * 子容器（[JSONObject] / [JSONArray]）转换结果会缓存，保证多次读同一个 key 拿到
 * 同一个实例——否则「取出子对象改一下」的写法会因为每次新建包装而丢失修改。
 */
internal class LazyNSDictionaryMap(dictionary: NSDictionary) : AbstractMutableMap<String, Any?>() {

    private var dictionary: NSDictionary? = dictionary

    /** key 顺序取自 [NSDictionary.allKeys]，同一实例内稳定，保证序列化文本可复现。 */
    private var orderedKeys: List<String>? = null

    private var containerCache: MutableMap<String, Any?>? = null

    /** 物化后的 Kotlin Map；非空表示已放弃 Foundation 引用。 */
    private var materialized: MutableMap<String, Any?>? = null

    override val size: Int
        get() {
            materialized?.let { return it.size }
            return dictionary?.count?.toInt() ?: 0
        }

    override fun containsKey(key: String): Boolean {
        materialized?.let { return it.containsKey(key) }
        return dictionary?.objectForKey(key) != null
    }

    override fun containsValue(value: Any?): Boolean {
        materialized?.let { return it.containsValue(value) }
        for (key in keyOrder()) {
            if (get(key) == value) {
                return true
            }
        }
        return false
    }

    override fun get(key: String): Any? {
        materialized?.let { return it[key] }
        val dict = dictionary ?: return null
        return convert(key, dict.objectForKey(key))
    }

    override fun put(key: String, value: Any?): Any? {
        return ensureMaterialized().put(key, value)
    }

    override fun remove(key: String): Any? {
        return ensureMaterialized().remove(key)
    }

    override fun clear() {
        ensureMaterialized().clear()
    }

    override val entries: MutableSet<MutableMap.MutableEntry<String, Any?>>
        get() {
            materialized?.let { return it.entries }
            return LazyEntries()
        }

    private fun keyOrder(): List<String> {
        orderedKeys?.let { return it }
        val dict = dictionary ?: return emptyList()
        val keys = ArrayList<String>(dict.count.toInt())
        for (key in dict.allKeys) {
            keys.add(key as? String ?: key.toString())
        }
        orderedKeys = keys
        return keys
    }

    private fun convert(key: String, value: Any?): Any? {
        containerCache?.let {
            if (it.containsKey(key)) {
                return it[key]
            }
        }
        val converted = nsValueToKotlin(value)
        if (converted is JSONObject || converted is JSONArray) {
            val cache = containerCache ?: mutableMapOf<String, Any?>().also { containerCache = it }
            cache[key] = converted
        }
        return converted
    }

    private fun ensureMaterialized(): MutableMap<String, Any?> {
        materialized?.let { return it }
        val map: MutableMap<String, Any?> = JSONEngine.getMutableMap()
        val dict = dictionary
        if (dict != null) {
            for (key in keyOrder()) {
                map[key] = convert(key, dict.objectForKey(key))
            }
        }
        dictionary = null
        orderedKeys = null
        containerCache = null
        materialized = map
        return map
    }

    /**
     * Foundation 字典上的只读 entry 视图，任何结构性修改都会先物化。
     */
    private inner class LazyEntries : AbstractMutableSet<MutableMap.MutableEntry<String, Any?>>() {
        override val size: Int
            get() = this@LazyNSDictionaryMap.size

        override fun add(element: MutableMap.MutableEntry<String, Any?>): Boolean {
            ensureMaterialized()[element.key] = element.value
            return true
        }

        override fun clear() {
            this@LazyNSDictionaryMap.clear()
        }

        override fun iterator(): MutableIterator<MutableMap.MutableEntry<String, Any?>> {
            materialized?.let { return it.entries.iterator() }
            val keyIterator = keyOrder().iterator()
            return object : MutableIterator<MutableMap.MutableEntry<String, Any?>> {
                private var lastKey: String? = null

                override fun hasNext(): Boolean = keyIterator.hasNext()

                override fun next(): MutableMap.MutableEntry<String, Any?> {
                    val key = keyIterator.next()
                    lastKey = key
                    return LazyEntry(key)
                }

                override fun remove() {
                    val key = lastKey ?: throw IllegalStateException()
                    this@LazyNSDictionaryMap.remove(key)
                    lastKey = null
                }
            }
        }
    }

    private inner class LazyEntry(override val key: String) : MutableMap.MutableEntry<String, Any?> {
        override val value: Any?
            get() = this@LazyNSDictionaryMap[key]

        override fun setValue(newValue: Any?): Any? {
            val old = this@LazyNSDictionaryMap[key]
            this@LazyNSDictionaryMap[key] = newValue
            return old
        }
    }
}
