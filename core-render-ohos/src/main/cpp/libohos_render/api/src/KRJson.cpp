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

// Thin C-ABI surface over the internal Scheme A value operations. External
// consumers (including Kotlin/Native via cinterop) use only these functions;
// the reader/builder/tests call the kuikly::util::json:: functions directly.

#include "libohos_render/api/include/Kuikly/KRJson.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "libohos_render/utils/json/KRJSONReader.h"
#include "libohos_render/utils/json/KRJSONValue.h"

namespace kjson = kuikly::util::json;

extern "C" {

KRJSONValue KRJSONRetain(KRJSONValue value) {
    return kjson::Retain(value);
}
void KRJSONRelease(KRJSONValue value) {
    kjson::Release(value);
}

KRJSONValue KRJSONParse(const char *data, size_t len, char **err) {
    if (err != nullptr) {
        *err = nullptr;
    }
    std::string message;
    KRJSONValue value = kjson::KRJSONReader::Parse(data, len, &message);
    if (value == KRJSON_INVALID && err != nullptr && !message.empty()) {
        char *buf = static_cast<char *>(std::malloc(message.size() + 1));
        if (buf != nullptr) {
            std::memcpy(buf, message.c_str(), message.size() + 1);
        }
        *err = buf;
    }
    return value;
}

char *KRJSONDump(KRJSONValue value) {
    std::string s = kjson::Dump(value);
    char *buf = static_cast<char *>(std::malloc(s.size() + 1));
    if (buf != nullptr) {
        std::memcpy(buf, s.c_str(), s.size() + 1);
    }
    return buf;
}
void KRJSONFreeString(char *str) {
    std::free(str);
}

KRJSONType KRJSONGetType(KRJSONValue value) {
    return kjson::GetType(value);
}
bool KRJSONGetBool(KRJSONValue value, bool default_value) {
    return kjson::GetBool(value, default_value);
}
int64_t KRJSONGetInt(KRJSONValue value, int64_t default_value) {
    return kjson::GetInt(value, default_value);
}
uint64_t KRJSONGetUint(KRJSONValue value, uint64_t default_value) {
    return kjson::GetUint(value, default_value);
}
double KRJSONGetDouble(KRJSONValue value, double default_value) {
    return kjson::GetDouble(value, default_value);
}
const char *KRJSONGetString(KRJSONValue value, size_t *out_len) {
    return kjson::GetString(value, out_len);
}

size_t KRJSONGetSize(KRJSONValue value) {
    return kjson::GetSize(value);
}
KRJSONValue KRJSONArrayGet(KRJSONValue array, size_t index) {
    return kjson::ArrayGet(array, index);
}
KRJSONValue KRJSONObjectGet(KRJSONValue object, const char *key) {
    return kjson::ObjectGet(object, key, key != nullptr ? std::strlen(key) : 0);
}
KRJSONValue KRJSONObjectValueAt(KRJSONValue object, size_t index) {
    return kjson::ObjectValueAt(object, index);
}
const char *KRJSONObjectKeyAt(KRJSONValue object, size_t index) {
    return kjson::ObjectKeyAt(object, index);
}
void KRJSONObjectForEach(KRJSONValue object, KRJSONObjectVisitor visitor, void *userdata) {
    kjson::ObjectForEach(object, visitor, userdata);
}

KRJSONValue KRJSONNewNull(void) {
    return kjson::NewNull();
}
KRJSONValue KRJSONNewBool(bool v) {
    return kjson::NewBool(v);
}
KRJSONValue KRJSONNewInt(int64_t v) {
    return kjson::NewInt(v);
}
KRJSONValue KRJSONNewUint(uint64_t v) {
    return kjson::NewUint(v);
}
KRJSONValue KRJSONNewDouble(double v) {
    return kjson::NewDouble(v);
}
KRJSONValue KRJSONNewString(const char *data, size_t len) {
    return kjson::NewString(data, len);
}
KRJSONValue KRJSONNewArray(void) {
    return kjson::NewArray();
}
KRJSONValue KRJSONNewObject(void) {
    return kjson::NewObject();
}
void KRJSONArrayAppend(KRJSONValue array, KRJSONValue child) {
    kjson::ArrayAppend(array, child);
}
void KRJSONObjectPut(KRJSONValue object, const char *key, size_t key_len, KRJSONValue child) {
    kjson::ObjectPut(object, key, key_len, child);
}

}  // extern "C"
