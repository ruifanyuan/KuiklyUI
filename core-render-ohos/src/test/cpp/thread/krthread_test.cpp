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

#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;

namespace {

void Check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Wait(std::future<void> &future) {
    Check(future.wait_for(5s) == std::future_status::ready, "task did not complete within 5s");
    future.get();
}

// Parks the worker after A's function body, while its capture is being destroyed.
// This makes the A -> S -> B test independent of mutex scheduling fairness.
struct CaptureBarrier {
    std::promise<void> &entered;
    std::shared_future<void> release;
    ~CaptureBarrier() {
        entered.set_value();
        release.wait();
    }
};

void TestTaskBoundary() {
    std::promise<void> gateEntered, gateRelease, captureEntered, captureRelease, completed;
    auto gateReady = gateEntered.get_future();
    auto gate = gateRelease.get_future().share();
    auto captureReady = captureEntered.get_future();
    auto release = captureRelease.get_future().share();
    auto done = completed.get_future();
    std::vector<int> order;
    std::atomic<bool> ranInline{false};
    const auto caller = std::this_thread::get_id();
    KRThread thread("boundary");
    thread.DispatchAsync([&gateEntered, gate] {
        gateEntered.set_value();
        gate.wait();
    });
    Wait(gateReady);
    auto capture = std::shared_ptr<CaptureBarrier>(new CaptureBarrier{captureEntered, release});
    thread.DispatchAsync([&order, capture] { order.push_back(1); });
    capture.reset();
    thread.DispatchAsync([&order, &completed] {
        order.push_back(3);
        completed.set_value();
    });
    gateRelease.set_value();
    Wait(captureReady);

    thread.DirectRunOnCurThread([&] {
        ranInline.store(std::this_thread::get_id() == caller);
        order.push_back(2);
    });
    captureRelease.set_value();
    Wait(done);
    Check(ranInline.load(), "completed task still holds execution lock while its capture is destroyed");
    Check(order == std::vector<int>({1, 2, 3}), "synchronous task did not run between A and B");
}

void TestFifoAndNestedCalls() {
    std::promise<void> gateEntered, gateRelease, completed;
    auto gateReady = gateEntered.get_future();
    auto gate = gateRelease.get_future().share();
    auto done = completed.get_future();
    std::vector<int> order;
    KRThread thread("fifo");
    thread.DispatchAsync([&gateEntered, gate] {
        gateEntered.set_value();
        gate.wait();
    });
    Wait(gateReady);
    thread.DispatchAsync([&] {
        order.push_back(1);
        thread.DirectRunOnCurThread([&] { order.push_back(2); });
        thread.DispatchAsync([&] {
            order.push_back(4);
            completed.set_value();
        });
    });
    thread.DispatchAsync([&] { order.push_back(3); });
    gateRelease.set_value();
    Wait(done);
    Check(order == std::vector<int>({1, 2, 3, 4}), "FIFO or nested worker execution changed");
}

void TestFastFailWhileAwaitingMain() {
    std::promise<void> entered, releasePromise, completed;
    auto ready = entered.get_future();
    auto release = releasePromise.get_future().share();
    auto done = completed.get_future();
    std::atomic<bool> ran{false};
    KRThread thread("fastfail");
    thread.DispatchAsync([&] {
        KRThread::WorkerAwaitingMainTaskGuard guard(&thread);
        entered.set_value();
        release.wait();
    });
    Wait(ready);
    const auto start = std::chrono::steady_clock::now();
    thread.DirectRunOnCurThread([&] {
        ran.store(true);
        completed.set_value();
    });
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const bool ranBeforeRelease = ran.load();
    releasePromise.set_value();
    Wait(done);
    Check(!ranBeforeRelease, "task ran while worker was awaiting main");
    Check(elapsed < 50ms, "awaiting-main flag did not bypass the 100ms retry window");
    Check(!thread.IsWorkerAwaitingMainTask(), "awaiting-main flag was not cleared");
}

void TestTimeoutFallback() {
    std::promise<void> entered, releasePromise, completed;
    auto ready = entered.get_future();
    auto release = releasePromise.get_future().share();
    auto done = completed.get_future();
    std::atomic<bool> ran{false};
    std::thread::id executionThread;
    KRThread thread("timeout");
    thread.DispatchAsync([&] {
        entered.set_value();
        release.wait();
    });
    Wait(ready);
    const auto caller = std::this_thread::get_id();
    const auto start = std::chrono::steady_clock::now();
    thread.DirectRunOnCurThread([&] {
        executionThread = std::this_thread::get_id();
        ran.store(true);
        completed.set_value();
    });
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const bool ranBeforeRelease = ran.load();
    releasePromise.set_value();
    Wait(done);
    Check(!ranBeforeRelease, "timeout path bypassed the execution lock");
    Check(elapsed >= 100ms, "100ms timeout semantics changed");
    Check(executionThread != caller, "timeout task was not dispatched to worker");
}

void TestMutualExclusionAndTimers() {
    constexpr int kAsyncTasks = 500;
    constexpr int kCallerThreads = 4;
    constexpr int kDirectTasks = 100;
    std::atomic<int> active{0}, completedCount{0};
    std::atomic<bool> overlap{false};
    auto task = [&] {
        if (active.fetch_add(1) != 0) {
            overlap.store(true);
        }
        std::this_thread::yield();
        completedCount.fetch_add(1);
        active.fetch_sub(1);
    };
    std::promise<void> completed;
    auto done = completed.get_future();
    KRThread thread("exclusive");
    for (int i = 0; i < kAsyncTasks; ++i) {
        thread.DispatchAsync(task);
    }
    std::vector<std::thread> callers;
    for (int i = 0; i < kCallerThreads; ++i) {
        callers.emplace_back([&] {
            for (int j = 0; j < kDirectTasks; ++j) {
                thread.DirectRunOnCurThread(task);
            }
        });
    }
    for (auto &caller : callers) {
        caller.join();
    }
    thread.DispatchAsync([&] {
        task();
        thread.DispatchAsync([&] {
            task();
            completed.set_value();
        }, 1);
    });
    Wait(done);
    Check(!overlap.load(), "worker, direct task or timer executed concurrently");
    Check(completedCount.load() == kAsyncTasks + kCallerThreads * kDirectTasks + 2,
          "task was lost or executed twice");
}

}  // namespace

int main() {
    try {
        TestTaskBoundary();
        std::cout << "PASS: task-boundary interleaving\n";
        TestFifoAndNestedCalls();
        std::cout << "PASS: FIFO and nested worker calls\n";
        TestFastFailWhileAwaitingMain();
        std::cout << "PASS: awaiting-main fast-fail\n";
        TestTimeoutFallback();
        std::cout << "PASS: 100ms timeout fallback\n";
        TestMutualExclusionAndTimers();
        std::cout << "PASS: mutual exclusion and timers\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
