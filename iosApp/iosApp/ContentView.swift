/*
 * Tencent is pleased to support the open source community by making KuiklyUI
 * available.
 * Copyright (C) 2025 Tencent. All rights reserved.
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

import SwiftUI
import Foundation

struct ContentView: View {
    var body: some View {
        let env = ProcessInfo.processInfo.environment
        let args = ProcessInfo.processInfo.arguments
        let pageName = launchValue("pageName", env: env, args: args) ?? "router"
        return KuiklyRenderViewPage(pageName: pageName, data: [:]).ignoresSafeArea()
    }
}

/// `SIMCTL_CHILD_KUIKLY_PAGE_NAME` / `--pageName`，与鸿蒙 `aa start --ps` 对齐。
private func launchValue(_ name: String, env: [String: String], args: [String]) -> String? {
    if let i = args.firstIndex(of: "--" + name), i + 1 < args.count {
        let value = args[i + 1]
        if !value.isEmpty { return value }
    }
    let envKey = name == "pageName" ? "KUIKLY_PAGE_NAME" : "KUIKLY_" + name.uppercased()
    if let value = env[envKey], !value.isEmpty {
        return value
    }
    return nil
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}