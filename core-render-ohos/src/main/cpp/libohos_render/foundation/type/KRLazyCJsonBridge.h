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

#ifndef CORE_RENDER_OHOS_KRLAZYCJSONBRIDGE_H
#define CORE_RENDER_OHOS_KRLAZYCJSONBRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared ownership of a cJSON tree between C++ KRRenderValue and Kotlin.
 *
 * Internally each opaque handle is a heap-allocated std::shared_ptr<KuiklyCJsonOwner>.
 * - create → one shared_ptr handle (held by the producer, e.g. KRRenderValue)
 * - retain → returns a new handle sharing the same owner (Kotlin / child wrappers)
 * - release → destroys that handle's shared_ptr; tree deleted when last handle is gone
 *
 * Kotlin never sees a raw cJSON* it owns: node pointers are only valid while the
 * Kotlin side still holds a retained owner handle (see LazyCJsonMap).
 *
 * NATIVE_JSON longValue is a create/retain handle.
 */

// 这些符号由 Kotlin/Native 的 libshared.so 跨 so 解析，在 -fvisibility=hidden 下必须
// 显式导出，否则加载期符号解析失败直接 SIGSEGV。与 KRRenderCValue.h 同理，这里不
// include KuiklyExport.h，保持本头自包含以便 cinterop 直接引用。
#define KR_CJSON_EXPORT __attribute__((visibility("default")))

KR_CJSON_EXPORT int64_t kuikly_cjson_owner_create(void *cjson /* cJSON* */);
/**
 * 由 JSON 文本新建一棵自持有的 cJSON 树（解析失败返回 0）。
 * 提供给 Kotlin 侧自检使用：不经过 KRRenderValue 也能构造 NATIVE_JSON 句柄，
 * 从而验证「生产者释放后 Kotlin 仍可安全读取」这一共享所有权语义。
 */
KR_CJSON_EXPORT int64_t kuikly_cjson_owner_create_from_string(const char *json);
/** Returns a new handle that shares ownership with [owner]. 0 if invalid. */
KR_CJSON_EXPORT int64_t kuikly_cjson_retain(int64_t owner);
KR_CJSON_EXPORT void kuikly_cjson_release(int64_t owner);
/** Root cJSON* as int64 (0 if owner invalid). */
KR_CJSON_EXPORT int64_t kuikly_cjson_owner_root(int64_t owner);

/** Field accessors: ptr is a cJSON* node (root or child). */
KR_CJSON_EXPORT int kuikly_cjson_has(int64_t ptr, const char *key);
KR_CJSON_EXPORT int kuikly_cjson_size(int64_t ptr);
KR_CJSON_EXPORT const char *kuikly_cjson_key_at(int64_t ptr, int index);
/** 节点自身的类型（判断 owner 根是 object 还是 array），编码同 kuikly_cjson_value_kind */
KR_CJSON_EXPORT int kuikly_cjson_node_kind(int64_t ptr);
KR_CJSON_EXPORT int kuikly_cjson_value_kind(int64_t ptr, const char *key);
KR_CJSON_EXPORT int kuikly_cjson_get_bool(int64_t ptr, const char *key, int fallback);
KR_CJSON_EXPORT double kuikly_cjson_get_number(int64_t ptr, const char *key, double fallback);
KR_CJSON_EXPORT const char *kuikly_cjson_get_string(int64_t ptr, const char *key);
KR_CJSON_EXPORT int64_t kuikly_cjson_get_object(int64_t ptr, const char *key);
KR_CJSON_EXPORT int64_t kuikly_cjson_get_array(int64_t ptr, const char *key);
/** 按下标取子节点（array / object 的 child 链均可），返回 cJSON* 或 0 */
KR_CJSON_EXPORT int64_t kuikly_cjson_item_at(int64_t ptr, int index);
/**
 * 子节点游标：把 object/array 的 child 链表暴露给 Kotlin，避免按下标反复 O(n) 重扫。
 * first_child 取第一个孩子；next 取兄弟节点；child_key 取孩子在 object 中的键（array 元素为 null）。
 */
KR_CJSON_EXPORT int64_t kuikly_cjson_first_child(int64_t ptr);
KR_CJSON_EXPORT int64_t kuikly_cjson_next(int64_t ptr);
KR_CJSON_EXPORT const char *kuikly_cjson_child_key(int64_t ptr);
/** 节点自身的标量访问（配合 item_at，用于惰性 JSONArray） */
KR_CJSON_EXPORT int kuikly_cjson_as_bool(int64_t ptr, int fallback);
KR_CJSON_EXPORT double kuikly_cjson_as_number(int64_t ptr, double fallback);
KR_CJSON_EXPORT const char *kuikly_cjson_as_string(int64_t ptr);
KR_CJSON_EXPORT char *kuikly_cjson_print(int64_t ptr);
KR_CJSON_EXPORT void kuikly_cjson_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif  // CORE_RENDER_OHOS_KRLAZYCJSONBRIDGE_H
