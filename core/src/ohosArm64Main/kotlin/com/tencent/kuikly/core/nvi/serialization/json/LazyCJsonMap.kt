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

    /**
     * key → child cJSON* 一次性索引：cJSON 是链表，按 key 查是 O(n)；建一次索引后
     * 后续 get/containsKey 均摊 O(1)，把「逐字段读」从 O(n²) 降到 O(n)。值仍惰性转换。
     */
    private var keyIndex: HashMap<String, Long>? = null

    /**
     * 与 [keyIndex] 同一次链表扫描得到的 key 顺序（去重后保留首个）。
     * `entries`/`keys` 迭代器只握这份 String 列表，不握 cJSON*，避免遍历中途物化 UAF。
     */
    private var orderedKeys: List<String>? = null

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
         * 原生 `KRRenderValue` 的 Map 与（无二进制元素的）Array 都以 `NATIVE_JSON` 下发：
         * object → [LazyCJsonMap]，array → [LazyCJsonList]，与 Apple 惰性路径对齐。
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
            return when (CJsonNative.nodeKind(root)) {
                CJSON_KIND_ARRAY -> {
                    // fromOwner 会再 retain；释放本次句柄，只留 list 持有的那份。
                    try {
                        LazyCJsonList.fromOwner(held)
                    } finally {
                        CJsonNative.release(held)
                    }
                }
                CJSON_KIND_OBJECT -> JSONObject(wrap(held, root))
                else -> {
                    CJsonNative.release(held)
                    null
                }
            }
        }

        internal fun fromNode(ownerPtr: Long, nodePtr: Long): JSONObject {
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
            keyIndex?.let { return it.size }
            return if (nodePtr != 0L) CJsonNative.size(nodePtr) else 0
        }

    override fun containsKey(key: String): Boolean {
        materialized?.let { return it.containsKey(key) }
        if (nodePtr == 0L) {
            return false
        }
        return ensureKeyIndex().containsKey(key)
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

    /**
     * 遍历一次 child 链表建立 key → child 指针索引，并缓存 key 顺序。
     * cJSON 允许重复键，这里保留首个，与 cJSON 按 key 查找（返回首个匹配）语义一致。
     */
    private fun ensureKeyIndex(): HashMap<String, Long> {
        keyIndex?.let { return it }
        val index = HashMap<String, Long>()
        val keys = ArrayList<String>()
        if (nodePtr != 0L) {
            var child = CJsonNative.firstChild(nodePtr)
            while (child != 0L) {
                val key = CJsonNative.childKey(child)
                if (key != null && !index.containsKey(key)) {
                    index[key] = child
                    keys.add(key)
                }
                child = CJsonNative.nextSibling(child)
            }
        }
        keyIndex = index
        orderedKeys = keys
        return index
    }

    private fun keyOrder(): List<String> {
        orderedKeys?.let { return it }
        ensureKeyIndex()
        return orderedKeys ?: emptyList()
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
            var child = CJsonNative.firstChild(nodePtr)
            while (child != 0L) {
                val key = CJsonNative.childKey(child)
                if (key != null && !map.containsKey(key)) {
                    map[key] = cachedOrConvert(key, child)
                }
                child = CJsonNative.nextSibling(child)
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
        keyIndex = null
        orderedKeys = null
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
        val child = ensureKeyIndex()[name] ?: return null
        return convertChild(name, child)
    }

    /**
     * 复用已派生（可能已被就地修改）的子容器实例；否则由 child 指针新转换。
     * 保证「先取子对象改它，再从父读」看到的是同一实例（mutation 可见性）。
     */
    private fun cachedOrConvert(name: String, child: Long): Any? {
        containerCache?.let {
            if (it.containsKey(name)) {
                return it[name]
            }
        }
        return convertChild(name, child)
    }

    /** 由 child cJSON* 直接转换（node_kind + as_*），object/array 结果缓存复用同一实例。 */
    private fun convertChild(name: String, child: Long): Any? {
        return when (CJsonNative.nodeKind(child)) {
            CJSON_KIND_BOOL -> CJsonNative.asBool(child, false)
            CJSON_KIND_NUMBER -> numberFromCJson(CJsonNative.asNumber(child, 0.0))
            CJSON_KIND_STRING -> CJsonNative.asString(child)
            CJSON_KIND_OBJECT -> {
                if (ownerPtr == 0L) null else cacheContainer(name, fromNode(ownerPtr, child))
            }
            CJSON_KIND_ARRAY -> {
                if (ownerPtr == 0L) null else cacheContainer(name, LazyCJsonList.fromNode(ownerPtr, child))
            }
            else -> null
        }
    }

    private fun <T> cacheContainer(key: String, value: T): T {
        val cache = containerCache ?: mutableMapOf<String, Any?>().also { containerCache = it }
        cache[key] = value
        return value
    }

    /**
     * 出桥快路径：未物化、未派生过子容器且原生树仍在时，直接 `cJSON_PrintUnformatted`。
     * 派生过子容器可能被就地改写，cJSON 树会过期，故保守回退 [commonStringify]。
     */
    internal fun nativePrintCompactOrNull(): String? {
        if (materialized != null || containerCache != null || nodePtr == 0L) {
            return null
        }
        return CJsonNative.print(nodePtr)
    }

    /**
     * 惰性 entry 视图：迭代器握 key 快照，任何结构性修改都会先物化。
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
            // 先拷 key 列表再迭代（与 Apple LazyNSDictionaryMap 对齐）：iterator / Entry
            // 不握 cJSON*，中途 put 物化删树也不会 UAF。取值走 map[key]（keyIndex / 物化 Map）。
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
                    this@LazyCJsonMap.remove(key)
                    lastKey = null
                }
            }
        }
    }

    private inner class LazyEntry(
        override val key: String,
    ) : MutableMap.MutableEntry<String, Any?> {
        override val value: Any?
            get() = this@LazyCJsonMap[key]

        override fun setValue(newValue: Any?): Any? {
            val old = this@LazyCJsonMap[key]
            this@LazyCJsonMap[key] = newValue
            return old
        }
    }
}
