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

#include "libohos_render/foundation/thread/KRThread.h"

#include <algorithm>
#include <future>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr int kSamples = 40;
constexpr int kTasksPerBatch = 32;

struct Sample {
    double waitMs;
    int completedAtDirect;
    bool inlineExecution;
};

void Wait(std::future<void> &future) {
    if (future.wait_for(5s) != std::future_status::ready) {
        throw std::runtime_error("benchmark timed out after 5s");
    }
    future.get();
}

Sample Measure(KRThread &thread) {
    std::promise<void> gateEntered, gateRelease, batchStarted, batchCompleted, directCompleted;
    auto gateReady = gateEntered.get_future();
    auto release = gateRelease.get_future().share();
    auto started = batchStarted.get_future();
    auto completed = batchCompleted.get_future();
    auto direct = directCompleted.get_future();
    std::atomic<int> completedCount{0};
    Sample sample{};
    const auto caller = std::this_thread::get_id();
    std::chrono::steady_clock::time_point begin;

    // Queue the whole batch while the worker is parked in a preceding callback.
    thread.DispatchAsync([&gateEntered, release] {
        gateEntered.set_value();
        release.wait();
    });
    Wait(gateReady);
    for (int i = 0; i < kTasksPerBatch; ++i) {
        thread.DispatchAsync([&, i] {
            if (i == 0) {
                batchStarted.set_value();
            }
            std::this_thread::sleep_for(2ms);
            completedCount.fetch_add(1);
            if (i == kTasksPerBatch - 1) {
                batchCompleted.set_value();
            }
        });
    }
    gateRelease.set_value();
    Wait(started);
    begin = std::chrono::steady_clock::now();
    thread.DirectRunOnCurThread([&] {
        sample.waitMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - begin).count();
        sample.completedAtDirect = completedCount.load();
        sample.inlineExecution = std::this_thread::get_id() == caller;
        directCompleted.set_value();
    });
    Wait(direct);
    Wait(completed);
    return sample;
}

}  // namespace

int main() {
    try {
        KRThread thread("benchmark");
        std::vector<double> waits;
        int interleaved = 0;
        int fallbacks = 0;
        // Warm up thread scheduling and the libuv event loop.
        Measure(thread);
        for (int i = 0; i < kSamples; ++i) {
            const auto sample = Measure(thread);
            waits.push_back(sample.waitMs);
            interleaved += sample.completedAtDirect < kTasksPerBatch;
            fallbacks += !sample.inlineExecution;
            std::cout << "sample=" << i << " wait_ms=" << sample.waitMs
                      << " batch_completed=" << sample.completedAtDirect
                      << " inline=" << sample.inlineExecution << '\n';
        }
        std::sort(waits.begin(), waits.end());
        std::cout << std::fixed << std::setprecision(3)
                  << "SUMMARY samples=" << kSamples << " batch_tasks=" << kTasksPerBatch
                  << " task_sleep_ms=2"
                  << " min_ms=" << waits.front()
                  << " p50_ms=" << waits[kSamples / 2 - 1]
                  << " p95_ms=" << waits[(kSamples * 95 + 99) / 100 - 1]
                  << " max_ms=" << waits.back()
                  << " interleaved=" << interleaved << " fallbacks=" << fallbacks << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
