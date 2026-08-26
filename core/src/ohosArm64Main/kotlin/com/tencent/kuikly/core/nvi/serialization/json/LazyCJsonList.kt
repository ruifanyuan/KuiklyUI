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
 * cJSON 数组之上的惰性 List，是 OHOS 平台 [JSONArray] 的底层容器之一。
 * 与 [LazyCJsonMap] / Apple [LazyNSArrayList] 同构：读时按需转换，写时物化。
 */
@OptIn(ExperimentalNativeApi::class)
internal class LazyCJsonList private constructor(
    private var ownerPtr: Long,
    private var nodePtr: Long,
    private var releaseToken: OwnerRelease?,
    private var cleaner: Any?,
) : AbstractMutableList<Any?>() {

    private class OwnerRelease(private val ptr: Long) {
        private val done = AtomicInt(0)

        fun releaseOnce() {
            if (done.compareAndSet(0, 1) && ptr != 0L) {
                CJsonNative.release(ptr)
            }
        }
    }

    private var containerCache: MutableMap<Int, Any?>? = null
    private var materialized: MutableList<Any?>? = null

    /** 顺序游标：cJSON 是链表，缓存「上次访问的下标 → child*」让顺序遍历均摊 O(1)。 */
    private var cursorIndex: Int = -1
    private var cursorNode: Long = 0L

    /** cJSON_GetArraySize 是 O(n)，缓存一次避免每次 get(i) 都重扫成 O(n²)。 */
    private var cachedSize: Int = -1

    companion object {
        fun fromOwner(ownerPtr: Long): JSONArray {
            if (ownerPtr == 0L) {
                return JSONArray()
            }
            val held = CJsonNative.retain(ownerPtr)
            if (held == 0L) {
                return JSONArray()
            }
            val root = CJsonNative.ownerRoot(held)
            if (root == 0L || CJsonNative.nodeKind(root) != CJSON_KIND_ARRAY) {
                CJsonNative.release(held)
                return JSONArray()
            }
            return JSONArray(wrap(held, root))
        }

        fun fromNode(ownerPtr: Long, nodePtr: Long): JSONArray {
            if (ownerPtr == 0L || nodePtr == 0L) {
                return JSONArray()
            }
            val held = CJsonNative.retain(ownerPtr)
            if (held == 0L) {
                return JSONArray()
            }
            return JSONArray(wrap(held, nodePtr))
        }

        private fun wrap(held: Long, nodePtr: Long): LazyCJsonList {
            val token = OwnerRelease(held)
            val cleaner = createCleaner(token) { it.releaseOnce() }
            return LazyCJsonList(held, nodePtr, token, cleaner)
        }
    }

    override val size: Int
        get() {
            materialized?.let { return it.size }
            if (nodePtr == 0L) {
                return 0
            }
            if (cachedSize < 0) {
                cachedSize = CJsonNative.size(nodePtr)
            }
            return cachedSize
        }

    override fun get(index: Int): Any? {
        materialized?.let { return it[index] }
        val count = size
        if (index < 0 || index >= count) {
            throw IndexOutOfBoundsException("index: $index, size: $count")
        }
        containerCache?.let {
            if (it.containsKey(index)) {
                return it[index]
            }
        }
        return optAt(index)
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
        val n = size
        for (i in 0 until n) {
            list.add(get(i))
        }
        releaseNative()
        containerCache = null
        materialized = list
        return list
    }

    private fun releaseNative() {
        releaseToken?.releaseOnce()
        releaseToken = null
        cleaner = null
        ownerPtr = 0L
        nodePtr = 0L
        cursorIndex = -1
        cursorNode = 0L
        cachedSize = -1
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

    /** 取第 index 个 child*：顺序访问走兄弟指针 O(1)，随机访问回退 item_at O(n)。 */
    private fun childPtrAt(index: Int): Long {
        if (nodePtr == 0L) {
            return 0L
        }
        val node = when {
            cursorNode != 0L && index == cursorIndex -> cursorNode
            cursorNode != 0L && index == cursorIndex + 1 -> CJsonNative.nextSibling(cursorNode)
            index == 0 -> CJsonNative.firstChild(nodePtr)
            else -> CJsonNative.itemAt(nodePtr, index)
        }
        cursorIndex = index
        cursorNode = node
        return node
    }

    private fun optAt(index: Int): Any? {
        if (nodePtr == 0L) {
            return null
        }
        val child = childPtrAt(index)
        if (child == 0L) {
            return null
        }
        return when (CJsonNative.nodeKind(child)) {
            0 -> null
            CJSON_KIND_BOOL -> CJsonNative.asBool(child, false)
            CJSON_KIND_NUMBER -> numberFromCJson(CJsonNative.asNumber(child, 0.0))
            CJSON_KIND_STRING -> CJsonNative.asString(child)
            CJSON_KIND_OBJECT -> {
                if (ownerPtr == 0L) {
                    null
                } else {
                    cacheContainer(index, LazyCJsonMap.fromNode(ownerPtr, child))
                }
            }
            CJSON_KIND_ARRAY -> {
                if (ownerPtr == 0L) {
                    null
                } else {
                    cacheContainer(index, fromNode(ownerPtr, child))
                }
            }
            else -> null
        }
    }

    private fun <T> cacheContainer(index: Int, value: T): T {
        val cache = containerCache ?: mutableMapOf<Int, Any?>().also { containerCache = it }
        cache[index] = value
        return value
    }
}
