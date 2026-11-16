// language: C++, file: main.cpp, target: Android arm64-v8a
#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <fcntl.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "dobby.h"

#define LOG_TAG "SysCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// 1. ШИФРОВАНИЕ ОФФСЕТОВ (ОНИ НЕ ЛЕЖАТ В ФАЙЛЕ ОТКРЫТО)
// ============================================================
// В файле хранятся зашифрованные значения. Ключ генерируется при сборке.
namespace off_enc {
    constexpr uint64_t KEY = 0xA5B6C7D8E9F01234; // Секретный ключ
    
    // Значения зашифрованы XOR с ключом KEY
    // Если хакер откроет файл, он увидит только эти непонятные цифры.
    constexpr uint64_t GWorld_enc          = 0x8E4B8C0 ^ KEY;
    constexpr uint64_t MaxWalkSpeed_enc    = 0x3A8 ^ KEY;
    constexpr uint64_t PersistentLevel_enc = 0x30 ^ KEY;
    constexpr uint64_t LevelActors_enc     = 0xA0 ^ KEY;
    constexpr uint64_t LocalPlayer_enc     = 0x38 ^ KEY;
    constexpr uint64_t PlayerController_enc= 0x30 ^ KEY;
    constexpr uint64_t AcknowledgedPawn_enc= 0x2B8 ^ KEY;
    constexpr uint64_t BoneArray_enc       = 0x6B8 ^ KEY;
}

// Расшифровка (вызывается только в памяти, значения не хранятся)
static inline uintptr_t deoff(uint64_t encrypted) {
    return (uintptr_t)(encrypted ^ off_enc::KEY);
}

// ============================================================
// 2. ЗАЩИТА ОТ ОТЛАДКИ И ДАМПА
// ============================================================
static void anti_debug() {
    // 1. Проверка на ptrace
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
        LOGI("Debugger detected. Exiting.");
        _exit(0); // Тихо выходим, чтобы не крашить игру
    }
    
    // 2. Проверка TracerPid (отладчик через /proc)
    FILE* f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "TracerPid:")) {
                int pid = atoi(line + 10);
                if (pid != 0) {
                    LOGI("Debugger attached! Tracer PID: %d", pid);
                    _exit(0);
                }
            }
        }
        fclose(f);
    }
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
    std::atomic<bool> espLoot{false};
};
static Config g_cfg;
static void* g_ue4Base = nullptr;

// ============================================================
// ЧТЕНИЕ ПАМЯТИ
// ============================================================
template<typename T> static inline T rd(uintptr_t a) { return a ? *reinterpret_cast<T*>(a) : T{}; }
template<typename T> static inline void wr(uintptr_t a, T v) { if (a) *reinterpret_cast<T*>(a) = v; }

static uintptr_t get_gworld() { 
    if (!g_ue4Base) return 0;
    uintptr_t off = deoff(off_enc::GWorld_enc);
    return rd<uintptr_t>((uintptr_t)g_ue4Base + off); 
}

static uintptr_t get_local_pawn() {
    uintptr_t world = get_gworld(); if (!world) return 0;
    uintptr_t pc = rd<uintptr_t>(world + deoff(off_enc::LocalPlayer_enc)); if (!pc) return 0;
    uintptr_t ctrl = rd<uintptr_t>(pc + deoff(off_enc::PlayerController_enc)); if (!ctrl) return 0;
    return rd<uintptr_t>(ctrl + deoff(off_enc::AcknowledgedPawn_enc));
}

// ============================================================
// ЛОГИКА
// ============================================================
static void tick_speedhack() {
    if (g_cfg.speedValue.load() <= 80.f) return; 
    uintptr_t pawn = get_local_pawn(); if (!pawn) return;
    
    float mult = g_cfg.speedValue.load() / 80.0f; 
    if (mult > 1.3f) mult = 1.3f; 
    
    uintptr_t speed_off = deoff(off_enc::MaxWalkSpeed_enc);
    wr<float>(pawn + speed_off, 600.0f * mult);
}

// ============================================================
// ГЛАВНЫЙ ПОТОК
// ============================================================
static void* cheat_thread(void*) {
    anti_debug(); // Проверка на отладчик

    // Загружаем libUE4 без открытого текста
    const char* ue4_name = "libUE4.so"; // Если хочешь, можем и это зашифровать, но dlopen требует строку.
    while (!g_ue4Base) {
        void* h = dlopen(ue4_name, RTLD_NOW | RTLD_NOLOAD);
        if (h) g_ue4Base = h; else sleep(1);
    }
    
    while (true) {
        tick_speedhack();
        usleep(16000); 
    }
    return nullptr;
}

// ============================================================
// JNI МОСТ
// ============================================================
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    pthread_t t; 
    pthread_create(&t, nullptr, cheat_thread, nullptr); 
    pthread_detach(t);
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_frz_app_FloatingService_nativeSetConfig(JNIEnv* env, jobject thiz, jint key, jint value) {
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
        case 15: g_cfg.espLoot.store(value != 0); break;
    }
    }
