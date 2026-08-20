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
 * KRJSON array 之上的惰性 List，是 OHOS 平台 [JSONArray] 的底层容器之一。
 * 与 [LazyCJsonMap] / Apple [LazyNSArrayList] 同构：读时按需转换，写时物化。
 */
@OptIn(ExperimentalNativeApi::class)
internal class LazyCJsonList private constructor(
    private var native: Long,
    private var releaseToken: ValueRelease?,
    private var cleaner: Any?,
) : AbstractMutableList<Any?>() {

    private class ValueRelease(private val bits: Long) {
        private val done = AtomicInt(0)

        fun releaseOnce() {
            if (done.compareAndSet(0, 1)) {
                CJsonNative.release(bits)
            }
        }
    }

    private var containerCache: MutableMap<Int, Any?>? = null
    private var materialized: MutableList<Any?>? = null
    private var cachedSize: Int = -1

    companion object {
        fun fromOwner(bits: Long): JSONArray {
            if (bits == 0L || CJsonNative.isInvalid(bits)) {
                return JSONArray()
            }
            val held = CJsonNative.retain(bits)
            if (CJsonNative.type(held) != KRJSON_KIND_ARRAY) {
                CJsonNative.release(held)
                return JSONArray()
            }
            return JSONArray(wrap(held))
        }

        internal fun fromValue(bits: Long): JSONArray = fromOwner(bits)

        private fun wrap(held: Long): LazyCJsonList {
            val token = ValueRelease(held)
            val cleaner = createCleaner(token) { it.releaseOnce() }
            return LazyCJsonList(held, token, cleaner)
        }
    }

    override val size: Int
        get() {
            materialized?.let { return it.size }
            if (native == 0L) {
                return 0
            }
            if (cachedSize < 0) {
                cachedSize = CJsonNative.size(native)
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
        native = 0L
        cachedSize = -1
    }

    internal fun nativePrintCompactOrNull(): String? {
        if (materialized != null || containerCache != null || native == 0L) {
            return null
        }
        return CJsonNative.print(native)
    }

    private fun optAt(index: Int): Any? {
        if (native == 0L) {
            return null
        }
        val child = CJsonNative.arrayGet(native, index)
        if (CJsonNative.isInvalid(child)) {
            return null
        }
        return when (CJsonNative.type(child)) {
            KRJSON_KIND_NULL -> null
            KRJSON_KIND_BOOL -> CJsonNative.asBool(child, false)
            KRJSON_KIND_INT, KRJSON_KIND_UINT, KRJSON_KIND_DOUBLE -> numberFromKRJSON(child)
            KRJSON_KIND_STRING -> CJsonNative.asString(child)
            KRJSON_KIND_OBJECT -> cacheContainer(index, LazyCJsonMap.fromValue(child))
            KRJSON_KIND_ARRAY -> cacheContainer(index, fromValue(child))
            else -> null
        }
    }

    private fun <T> cacheContainer(index: Int, value: T): T {
        val cache = containerCache ?: mutableMapOf<Int, Any?>().also { containerCache = it }
        cache[index] = value
        return value
    }
}
