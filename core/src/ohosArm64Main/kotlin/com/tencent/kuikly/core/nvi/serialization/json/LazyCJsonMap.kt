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

import kotlin.concurrent.AtomicInt
import kotlin.experimental.ExperimentalNativeApi
import kotlin.native.ref.createCleaner

/**
 * cJSON 树之上的惰性 Map，是 OHOS 平台 [JSONObject] 的底层容器之一。
 *
 * 生命周期：构造时 [CJsonNative.retain] 一个 shared_ptr 句柄，物化或对象回收
 * （`Cleaner`）时释放；因此原生侧即使在调用返回后销毁自己的句柄，Kotlin 侧读到的
 * node 指针依然有效。
 *
 * 子对象各自 retain 一个句柄，父对象物化 / 释放后子对象仍可安全读取。
 * 子容器的转换结果会缓存，保证多次读同一个 key 拿到同一个实例。
 */
@OptIn(ExperimentalNativeApi::class)
internal class LazyCJsonMap private constructor(
    private var ownerPtr: Long,
    private var nodePtr: Long,
    private var releaseToken: OwnerRelease?,
    /** Kept so Cleaner is not collected early. */
    private var cleaner: Any?,
) : AbstractMutableMap<String, Any?>() {

    private class OwnerRelease(private val ptr: Long) {
        private val done = AtomicInt(0)

        fun releaseOnce() {
            if (done.compareAndSet(0, 1) && ptr != 0L) {
                CJsonNative.release(ptr)
            }
        }
    }

    private var containerCache: MutableMap<String, Any?>? = null

    /** After first mutation (or forced materialize), native tree is released. */
    private var materialized: MutableMap<String, Any?>? = null

    companion object {
        /**
         * 由原生 shared_ptr 句柄构造 [JSONObject]。[ownerPtr] 的所有权仍属调用方，
         * 这里只 retain 自己的一份。
         */
        fun fromOwner(ownerPtr: Long): JSONObject {
            if (ownerPtr == 0L) {
                return JSONObject()
            }
            val held = CJsonNative.retain(ownerPtr)
            if (held == 0L) {
                return JSONObject()
            }
            val root = CJsonNative.ownerRoot(held)
            if (root == 0L) {
                CJsonNative.release(held)
                return JSONObject()
            }
            return JSONObject(wrap(held, root))
        }

        /**
         * 原生 `KRRenderValue` 的 Map 与（无二进制元素的）Array 都以 `NATIVE_JSON` 下发，
         * 因此根节点可能是 object 也可能是 array：object 走惰性包装，array 沿用
         * 「打印 + 宽松扫描」以保持与其他平台一致的数字选型。
         */
        fun fromOwnerAny(ownerPtr: Long): Any? {
            if (ownerPtr == 0L) {
                return null
            }
            val held = CJsonNative.retain(ownerPtr)
            if (held == 0L) {
                return null
            }
            val root = CJsonNative.ownerRoot(held)
            if (root == 0L) {
                CJsonNative.release(held)
                return null
            }
            if (CJsonNative.nodeKind(root) == KIND_ARRAY) {
                val printed = CJsonNative.print(root)
                CJsonNative.release(held)
                return printed?.let {
                    try {
                        JSONArray(it)
                    } catch (_: JSONException) {
                        null
                    }
                }
            }
            return JSONObject(wrap(held, root))
        }

        private fun fromNode(ownerPtr: Long, nodePtr: Long): JSONObject {
            if (ownerPtr == 0L || nodePtr == 0L) {
                return JSONObject()
            }
            val held = CJsonNative.retain(ownerPtr)
            if (held == 0L) {
                return JSONObject()
            }
            return JSONObject(wrap(held, nodePtr))
        }

        private fun wrap(held: Long, nodePtr: Long): LazyCJsonMap {
            val token = OwnerRelease(held)
            val cleaner = createCleaner(token) { it.releaseOnce() }
            return LazyCJsonMap(held, nodePtr, token, cleaner)
        }
    }

    override val size: Int
        get() {
            materialized?.let { return it.size }
            return if (nodePtr != 0L) CJsonNative.size(nodePtr) else 0
        }

    override fun containsKey(key: String): Boolean {
        materialized?.let { return it.containsKey(key) }
        return nodePtr != 0L && CJsonNative.hasKey(nodePtr, key)
    }

    override fun containsValue(value: Any?): Boolean {
        materialized?.let { return it.containsValue(value) }
        return entries.any { it.value == value }
    }

    override fun get(key: String): Any? {
        materialized?.let { return it[key] }
        if (nodePtr == 0L) {
            return null
        }
        return optFromCJson(key)
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

    private fun ensureMaterialized(): MutableMap<String, Any?> {
        materialized?.let { return it }
        val map: MutableMap<String, Any?> = JSONEngine.getMutableMap()
        if (nodePtr != 0L) {
            val n = CJsonNative.size(nodePtr)
            for (i in 0 until n) {
                val key = CJsonNative.keyAt(nodePtr, i) ?: continue
                map[key] = optFromCJson(key)
            }
        }
        releaseNativeOwnership()
        materialized = map
        return map
    }

    private fun releaseNativeOwnership() {
        nodePtr = 0L
        ownerPtr = 0L
        containerCache = null
        val token = releaseToken
        releaseToken = null
        cleaner = null
        token?.releaseOnce()
    }

    private fun optFromCJson(name: String): Any? {
        containerCache?.let {
            if (it.containsKey(name)) {
                return it[name]
            }
        }
        return when (CJsonNative.valueKind(nodePtr, name)) {
            KIND_BOOL -> CJsonNative.getBool(nodePtr, name, false)
            KIND_NUMBER -> numberFromCJson(CJsonNative.getNumber(nodePtr, name, 0.0))
            KIND_STRING -> CJsonNative.getString(nodePtr, name)
            KIND_OBJECT -> {
                val child = CJsonNative.getObjectPtr(nodePtr, name)
                if (child == 0L || ownerPtr == 0L) {
                    null
                } else {
                    cacheContainer(name, fromNode(ownerPtr, child))
                }
            }
            KIND_ARRAY -> {
                val child = CJsonNative.getArrayPtr(nodePtr, name)
                if (child == 0L) {
                    null
                } else {
                    // cJSON 不区分整数与浮点，数组转换沿用「打印 + 宽松扫描」以保持
                    // 与其他平台一致的数字选型。
                    val printed = CJsonNative.print(child)
                    if (printed == null) {
                        null
                    } else {
                        try {
                            cacheContainer(name, JSONArray(printed))
                        } catch (_: JSONException) {
                            null
                        }
                    }
                }
            }
            else -> null
        }
    }

    private fun <T> cacheContainer(key: String, value: T): T {
        val cache = containerCache ?: mutableMapOf<String, Any?>().also { containerCache = it }
        cache[key] = value
        return value
    }

    private fun numberFromCJson(number: Double): Any {
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

    /**
     * Read-only entry view over cJSON. Any structural mutation materializes.
     */
    private inner class LazyEntries : AbstractMutableSet<MutableMap.MutableEntry<String, Any?>>() {
        override val size: Int
            get() = this@LazyCJsonMap.size

        override fun add(element: MutableMap.MutableEntry<String, Any?>): Boolean {
            ensureMaterialized()[element.key] = element.value
            return true
        }

        override fun clear() {
            this@LazyCJsonMap.clear()
        }

        override fun iterator(): MutableIterator<MutableMap.MutableEntry<String, Any?>> {
            materialized?.let { return it.entries.iterator() }
            if (nodePtr == 0L) {
                return mutableListOf<MutableMap.MutableEntry<String, Any?>>().iterator()
            }
            val n = CJsonNative.size(nodePtr)
            val keys = ArrayList<String>(n)
            for (i in 0 until n) {
                CJsonNative.keyAt(nodePtr, i)?.let { keys.add(it) }
            }
            val keyIterator = keys.iterator()
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
                    this@LazyCJsonMap.remove(key)
                    lastKey = null
                }
            }
        }
    }

    private inner class LazyEntry(override val key: String) : MutableMap.MutableEntry<String, Any?> {
        override val value: Any?
            get() = this@LazyCJsonMap[key]

        override fun setValue(newValue: Any?): Any? {
            val old = this@LazyCJsonMap[key]
            this@LazyCJsonMap[key] = newValue
            return old
        }
    }
}

/** cJSON 值类型，与 `kuikly_cjson_value_kind` 的返回值一一对应。 */
private const val KIND_BOOL = 1
private const val KIND_NUMBER = 2
private const val KIND_STRING = 3
private const val KIND_OBJECT = 4
private const val KIND_ARRAY = 5
