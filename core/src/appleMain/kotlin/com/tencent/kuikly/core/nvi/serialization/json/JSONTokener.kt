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
import platform.Foundation.NSArray
import platform.Foundation.NSDictionary
import platform.Foundation.NSJSONSerialization
import platform.Foundation.NSString
import platform.Foundation.NSUTF8StringEncoding
import platform.Foundation.dataUsingEncoding

/**
 * Apple 实现：严格 JSON 优先走 [NSJSONSerialization]，结果包成惰性
 * [LazyNSDictionaryMap] / [LazyNSArrayList]；注释、单引号、尾部垃圾等历史写法、
 * 顶层标量（未开 AllowFragments）、以及重复 key（Foundation 可能 first-wins，
 * 与 org.json「后者覆盖」不一致）回退到 [AbstractJSONTokener]。
 *
 * 注意：`NSDictionary` 不保留 JSON 文本成员顺序，序列化 key 次序可能与文本不同。
 */
actual class JSONTokener actual constructor(json: String) : AbstractJSONTokener(json) {

    private val source = json

    @OptIn(ExperimentalForeignApi::class)
    @Throws(JSONException::class)
    actual override fun nextValue(): Any? {
        tryNative()?.let { return it }
        return super.nextValue()
    }

    @OptIn(ExperimentalForeignApi::class)
    private fun tryNative(): Any? {
        if (source.isEmpty()) {
            return null
        }
        // 重复 key / 尾逗号：org.json 语义与 NSJSONSerialization 不一致，回退宽松扫描。
        if (jsonTextHasDuplicateObjectKeys(source) || jsonTextHasTrailingComma(source)) {
            return null
        }
        val data = (source as NSString).dataUsingEncoding(NSUTF8StringEncoding) ?: return null
        // options=0：不允许顶层标量碎片，标量 / 宽松写法一律回退，保证数字选型与历史一致。
        val parsed = try {
            NSJSONSerialization.JSONObjectWithData(data, 0u, null)
        } catch (_: Throwable) {
            null
        } ?: return null
        return when (parsed) {
            is NSDictionary -> JSONObject(parsed)
            is NSArray -> JSONArray(parsed)
            else -> null
        }
    }
}

/**
 * 轻量检测 JSON 文本里是否存在同一对象层级的重复 key。
 * 只识别双引号 key 的严格写法；宽松写法本就会回退到扫描器。
 */
internal fun jsonTextHasDuplicateObjectKeys(json: String): Boolean {
    var i = 0
    val stack = ArrayList<MutableSet<String>?>(8)
    while (i < json.length) {
        when (val c = json[i]) {
            '{' -> {
                stack.add(HashSet())
                i++
            }
            '}' -> {
                if (stack.isNotEmpty()) {
                    stack.removeAt(stack.lastIndex)
                }
                i++
            }
            '[' -> {
                stack.add(null) // 数组帧：不收集 key
                i++
            }
            ']' -> {
                if (stack.isNotEmpty()) {
                    stack.removeAt(stack.lastIndex)
                }
                i++
            }
            '"' -> {
                val keyStart = i + 1
                var j = keyStart
                while (j < json.length) {
                    when (json[j]) {
                        '\\' -> j += 2
                        '"' -> break
                        else -> j++
                    }
                }
                if (j >= json.length) {
                    return false
                }
                val key = json.substring(keyStart, j)
                var k = j + 1
                while (k < json.length && json[k].isWhitespace()) {
                    k++
                }
                val keys = stack.lastOrNull()
                if (keys != null && k < json.length && json[k] == ':') {
                    if (!keys.add(key)) {
                        return true
                    }
                }
                i = j + 1
            }
            else -> i++
        }
    }
    return false
}

/**
 * 检测 `}` / `]` 前是否有尾逗号。NSJSONSerialization 在部分系统上会吞掉对象尾逗号，
 * 而宽松扫描器对 `{"a":1,}` 会抛错、对 `[1,]` 则接受——必须回退才能对齐。
 */
internal fun jsonTextHasTrailingComma(json: String): Boolean {
    var i = 0
    var inString = false
    var escape = false
    while (i < json.length) {
        val c = json[i]
        if (inString) {
            when {
                escape -> escape = false
                c == '\\' -> escape = true
                c == '"' -> inString = false
            }
            i++
            continue
        }
        when (c) {
            '"' -> {
                inString = true
                i++
            }
            ',' -> {
                var j = i + 1
                while (j < json.length && json[j].isWhitespace()) {
                    j++
                }
                if (j < json.length && (json[j] == '}' || json[j] == ']')) {
                    return true
                }
                i++
            }
            else -> i++
        }
    }
    return false
}