// language: C++, file: main.cpp, target: Android arm64-v8a
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <cmath>
#include <cstring>

#define LOG_TAG "Freezers1_Core"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// ОФФСЕТЫ (ТЕСТОВЫЕ ДЛЯ PUBG 4.7.0)
// ============================================================
namespace off {
    uintptr_t GWorld          = 0x8E4B8C0; // Если крашит, меняй на 0x8E4B8B8 или 0x8E4B8C8
    uintptr_t PersistentLevel = 0x30;
    uintptr_t LevelActors     = 0xA0;
    uintptr_t LocalPlayer     = 0x38;
    uintptr_t PlayerController= 0x30;
    uintptr_t AcknowledgedPawn= 0x2B8;
    uintptr_t MaxWalkSpeed    = 0x3A8;     
    uintptr_t BoneArray       = 0x6B8; 
}

// ============================================================
// КОНФИГ
// ============================================================
struct Config {
    std::atomic<bool> autoloot{false};
    std::atomic<bool> knifeProtection{false};
    std::atomic<float> distKnife{45.f};
    std::atomic<float> distLoot{26.f};
    std::atomic<int> pickupMethod{0};
    
    std::atomic<float> speedValue{80.f};
    std::atomic<int> speedKey{0};
    std::atomic<bool> follow{false};
    std::atomic<bool> ammoToKnife{false};

    std::atomic<bool> espSkeleton{false};
    std::atomic<bool> espLines{false};
    std::atomic<bool> espCounter{false};
    std::atomic<bool> espWarning{false};
    std::atomic<bool> espKnife{false};
};
static Config g_cfg;

static void* g_ue4Base = nullptr;

template<typename T> static inline T rd(uintptr_t a) { return a ? *reinterpret_cast<T*>(a) : T{}; }
template<typename T> static inline void wr(uintptr_t a, T v) { if (a) *reinterpret_cast<T*>(a) = v; }

static uintptr_t get_gworld() { 
    return (!g_ue4Base || !off::GWorld) ? 0 : rd<uintptr_t>((uintptr_t)g_ue4Base + off::GWorld); 
}

static uintptr_t get_local_pawn() {
    uintptr_t world = get_gworld(); if (!world) return 0;
    uintptr_t pc = rd<uintptr_t>(world + off::LocalPlayer); if (!pc) return 0;
    uintptr_t ctrl = rd<uintptr_t>(pc + off::PlayerController); if (!ctrl) return 0;
    return rd<uintptr_t>(ctrl + off::AcknowledgedPawn);
}

static void tick_speedhack() {
    if (g_cfg.speedValue.load() <= 80.f) return; 
    uintptr_t pawn = get_local_pawn();
    if (!pawn) return;
    
    float base_speed = 600.0f;
    float mult = g_cfg.speedValue.load() / 80.0f; 
    if (mult > 1.3f) mult = 1.3f; // Ограничение, чтобы не спалили

    wr<float>(pawn + off::MaxWalkSpeed, base_speed * mult);
}

static void* cheat_thread(void*) {
    while (!g_ue4Base) {
        void* h = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD);
        if (h) g_ue4Base = h; else sleep(1);
    }
    LOGI("libUE4 hooked. Base: %p", g_ue4Base);
    
    while (true) {
        tick_speedhack();
        usleep(16000);
    }
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    pthread_t t; 
    pthread_create(&t, nullptr, cheat_thread, nullptr); 
    pthread_detach(t);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_frz_app_FloatingService_nativeSetConfig(JNIEnv*, jobject, jint key, jint value) {
    switch (key) {
        case 1: g_cfg.autoloot.store(value != 0); break;
        case 2: g_cfg.knifeProtection.store(value != 0); break;
        case 3: g_cfg.distKnife.store((float)value); break;
        case 4: g_cfg.distLoot.store((float)value); break;
        case 5: g_cfg.pickupMethod.store(value); break;
        case 6: g_cfg.speedValue.store((float)value); break;
        case 7: g_cfg.speedKey.store(value); break;
        case 8: g_cfg.follow.store(value != 0); break;
        case 9: g_cfg.ammoToKnife.store(value != 0); break;
        case 10: g_cfg.espSkeleton.store(value != 0); break;
        case 11: g_cfg.espLines.store(value != 0); break;
        case 12: g_cfg.espCounter.store(value != 0); break;
        case 13: g_cfg.espWarning.store(value != 0); break;
        case 14: g_cfg.espKnife.store(value != 0); break;
    }
}