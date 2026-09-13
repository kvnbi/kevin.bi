#pragma once
#include <functional>
#include <utility>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <process.h>
    #include <windows.h>
#else
    #include <pthread.h>
#endif

namespace blitz {

class NativeThread {
    static constexpr size_t StackSize = 8 * 1024 * 1024;

public:
    template <class Function, class... Args>
    explicit NativeThread(Function&& fn, Args&&... args) {
        auto* call = new std::function<void()>(
            std::bind(std::forward<Function>(fn), std::forward<Args>(args)...));

#if defined(_WIN32)
        thread_ = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, unsigned(StackSize), &NativeThread::entry, call,
            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, StackSize);
        pthread_create(&thread_, &attr, &NativeThread::entry, call);
        pthread_attr_destroy(&attr);
#endif
    }

#if defined(_WIN32)
    void join() {
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
    }
#else
    void join() { pthread_join(thread_, nullptr); }
#endif

private:
#if defined(_WIN32)
    static unsigned __stdcall entry(void* arg) {
        auto* call = static_cast<std::function<void()>*>(arg);
        (*call)();
        delete call;
        return 0;
    }

    HANDLE thread_;
#else
    static void* entry(void* arg) {
        auto* call = static_cast<std::function<void()>*>(arg);
        (*call)();
        delete call;
        return nullptr;
    }

    pthread_t thread_;
#endif
};

}
