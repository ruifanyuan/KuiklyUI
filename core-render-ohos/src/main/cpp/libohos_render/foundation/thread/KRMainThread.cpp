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


#include <cassert>
#include <hilog/log.h>
#include <unistd.h>
#include <uv.h>
#include "KRDelayThread.h"
#include "libohos_render/utils/KRScopedSpinLock.h"
#include "libohos_render/foundation/thread/KRMainThread.h"

struct KRRunLoop;
static struct KRRunLoop *g_main_loop_ = nullptr;

struct KRTimerTask final{
public:
    KRTimerTask(uv_loop_t* loop, std::function<void()> task, int timeoutMS): task_(task), timeoutMS_(timeoutMS){
        uv_timer_init(loop, &timer_);
        uv_handle_set_data((uv_handle_t*)&timer_, (void*)this);
    }

    void Schedule(){
        uv_timer_start(&timer_, &cb, timeoutMS_, INT_MAX);
    }
    void Close(){
        uv_timer_stop(&timer_);
        uv_close((uv_handle_t*)&timer_, nullptr);
    }
    static void cb(uv_timer_t* handle){
        KRTimerTask* task = (KRTimerTask*)uv_handle_get_data((uv_handle_t*)handle);
        task->Close();
        task->task_();
        delete task;
    }
    uv_timer_t timer_;
    std::function<void()> task_;
    int timeoutMS_;
};

struct KRRunLoop{
public:
    static void InitMainLoop(napi_env env){
        if (g_main_loop_){
            return;
        }
        
        struct uv_loop_s *uv_loop;
        napi_get_uv_event_loop(env, &uv_loop);
        struct KRRunLoop *loop = new KRRunLoop(uv_loop);
        g_main_loop_ = loop;
    }
    
    static struct KRRunLoop *MainLoop(){
        return g_main_loop_;
    }
    
    KRRunLoop(struct  uv_loop_s *loop): loop_(loop){
        uv_async_init(loop, &async_, &cb);
        uv_handle_set_data((uv_handle_t*)&async_, this);
        tid_ = gettid();
    }
    
    void Schedule(std::function<void()> task, int delayMS){
         if (delayMS > 0){
            ScheduleTimer(task, delayMS);
         }else{
            ScheduleAsyncTask(task);
         }
    }
private:
    void ScheduleAsyncTask(std::function<void()> task){
        AppendTask(task);
        uv_async_send(&async_);
    }
    void ScheduleTimer(std::function<void()> task, int delayMS){
        if(gettid() == tid_){
            // same thread as the loop
            ScheduleTimerCurrentLoop(task, delayMS);
        }else{
            // different thread, dispatch to the same thread
            ScheduleAsyncTask([this, task, delayMS](){
                ScheduleTimerCurrentLoop(task, delayMS);
            });
        }
    }
    void ScheduleTimerCurrentLoop(std::function<void()> task, int delayMS){
        assert(gettid() == tid_);
        KRTimerTask *timerTask = new KRTimerTask(loop_, task, delayMS);
        timerTask->Schedule();
    }
    bool ProcessNextTask(){
        if(auto task = GetTask()){
            task();
            return true;
        }
        return false;
    }
    void AppendTask(std::function<void()> task){
        KRScopedSpinLock lock(&tasks_lock_);
        tasks_.push(task);
    }
    std::function<void()> GetTask(){
        KRScopedSpinLock lock(&tasks_lock_);
        if (tasks_.empty()){
            return nullptr;
        }
        auto task = tasks_.front();
        tasks_.pop();
        return task;
    }
    
    static void cb(uv_async_t* handle){
        KRRunLoop *loop = (KRRunLoop*)uv_handle_get_data((uv_handle_t*)handle);
        for(;;){
            bool hasMore = loop->ProcessNextTask();
            // strategy: process all and then exit
            if(!hasMore){
                break;
            }
        }
    }
    
    pid_t tid_;
    struct uv_loop_s *loop_;
    uv_async_t async_;
    std::queue<std::function<void()>> tasks_;
    KRSpinLock tasks_lock_;
};

KRMainThread::KRMainThread() {
    // 在这里添加构造函数代码
}

void KRMainThread::Export(napi_env env, napi_value exports) {
    if ((nullptr == env) || (nullptr == exports)) {
        return;
    }

    KRRunLoop::InitMainLoop(env);
}

void KRMainThread::RunOnMainThread(const std::function<void()> &task, int delayMilliseconds) {
    KRRunLoop::MainLoop()->Schedule(task, delayMilliseconds);
}

void KRMainThread::RunOnMainThreadForNextLoop(const std::function<void()> &task) {
    KRRunLoop::MainLoop()->Schedule(task, 0);
}
