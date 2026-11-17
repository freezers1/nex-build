// language: C++, file: main.cpp, target: Android arm64-v8a
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <signal.h>
#include <sys/ptrace.h>

#define LOG_TAG "SysCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Оффсеты (зашифрованы)
namespace off_enc {
    constexpr uint64_t STATIC_KEY = 0xA5B6C7D8E9F01234;
    constexpr uint64_t GWorld_enc          = 0x8E4B8C0 ^ STATIC_KEY;
    constexpr uint64_t MaxWalkSpeed_enc    = 0x3A8 ^ STATIC_KEY;
    constexpr uint64_t LocalPlayer_enc     = 0x38 ^ STATIC_KEY;
    constexpr uint64_t PlayerController_enc= 0x30 ^ STATIC_KEY;
    constexpr uint64_t AcknowledgedPawn_enc= 0x2B8 ^ STATIC_KEY;
}

static inline uintptr_t deoff(uint64_t encrypted) {
    return (uintptr_t)(encrypted ^ off_enc::STATIC_KEY);
}

static void anti_debug() {
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) _exit(0);
}

struct Config {
    std::atomic<bool> autoloot{false};
    std::atomic<float> speedValue{80.f};
};
static Config g_cfg;
static void* g_ue4Base = nullptr;

template<typename T> static inline T rd(uintptr_t a) { return a ? *reinterpret_cast<T*>(a) : T{}; }
template<typename T> static inline void wr(uintptr_t a, T v) { if (a) *reinterpret_cast<T*>(a) = v; }

static uintptr_t get_gworld() { 
    if (!g_ue4Base) return 0;
    return rd<uintptr_t>((uintptr_t)g_ue4Base + deoff(off_enc::GWorld_enc)); 
}

static uintptr_t get_local_pawn() {
    uintptr_t world = get_gworld(); if (!world) return 0;
    uintptr_t pc = rd<uintptr_t>(world + deoff(off_enc::LocalPlayer_enc)); if (!pc) return 0;
    uintptr_t ctrl = rd<uintptr_t>(pc + deoff(off_enc::PlayerController_enc)); if (!ctrl) return 0;
    return rd<uintptr_t>(ctrl + deoff(off_enc::AcknowledgedPawn_enc));
}

static void tick_speedhack() {
    if (g_cfg.speedValue.load() <= 80.f) return; 
    uintptr_t pawn = get_local_pawn(); if (!pawn) return;
    
    float mult = g_cfg.speedValue.load() / 80.0f; 
    if (mult > 1.3f) mult = 1.3f; 

    wr<float>(pawn + deoff(off_enc::MaxWalkSpeed_enc), 600.0f * mult);
}

static void* cheat_thread(void*) {
    anti_debug();
    while (!g_ue4Base) {
        void* h = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD);
        if (h) g_ue4Base = h; else sleep(1);
    }
    while (true) {
        tick_speedhack();
        usleep(16000); 
    }
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    pthread_t t; 
    pthread_create(&t, nullptr, cheat_thread, nullptr); 
    pthread_detach(t);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_frz_app_FloatingService_nativeSetConfig(JNIEnv* env, jobject thiz, jint key, jint value) {
    if (key == 6) g_cfg.speedValue.store((float)value);
    if (key == 1) g_cfg.autoloot.store(value != 0);
}
