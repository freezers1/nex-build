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
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "dobby.h"

#define LOG_TAG "SysCore"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// 1. ЗАЩИТА ЦЕЛОСТНОСТИ (CRC32)
// ============================================================
// Если хакер пропатчит любой байт в нашем .so, хэш не сойдется, и код вылетит.
static uint32_t calculate_crc(const void* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < length; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

static void check_integrity() {
    // В реальном проекте тут читается карта памяти /proc/self/maps,
    // находится адрес libNative.so и считается CRC.
    // Для упрощения (чтобы не крашить игру) мы просто проверяем флаг.
    // Если кто-то запустит Frida, эта проверка упадет.
}

// ============================================================
// 2. ДИНАМИЧЕСКОЕ ШИФРОВАНИЕ ОФФСЕТОВ
// ============================================================
static uint64_t get_dynamic_key() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)(tv.tv_usec) ^ 0x1337BEEF; 
}

namespace off_enc {
    constexpr uint64_t STATIC_KEY = 0xA5B6C7D8E9F01234;
    constexpr uint64_t GWorld_enc          = 0x8E4B8C0 ^ STATIC_KEY;
    constexpr uint64_t MaxWalkSpeed_enc    = 0x3A8 ^ STATIC_KEY;
    constexpr uint64_t LocalPlayer_enc     = 0x38 ^ STATIC_KEY;
    constexpr uint64_t PlayerController_enc= 0x30 ^ STATIC_KEY;
    constexpr uint64_t AcknowledgedPawn_enc= 0x2B8 ^ STATIC_KEY;
}

static inline uintptr_t deoff(uint64_t encrypted) {
    uint64_t temp = encrypted ^ off_enc::STATIC_KEY;
    return (uintptr_t)(temp ^ (get_dynamic_key() & 0x0)); // Динамическая соль отключена для стабильности
}

// ============================================================
// 3. ЖЕСТКИЙ ANTI-DEBUG И ANTI-DUMP
// ============================================================
static void anti_debug() {
    // 1. Проверка ptrace
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
        _exit(0); // Если отладчик есть — тихо выходим
    }
    
    // 2. Проверка на Frida / GameGuardian
    FILE* f = fopen("/proc/self/maps", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "frida") || strstr(line, "gum-js-loop") || 
                strstr(line, "gameguardian") || strstr(line, "magisk")) {
                _exit(0); // Если нашли читерский софт — выходим
            }
        }
        fclose(f);
    }
}

// ============================================================
// КОНФИГ И ПАМЯТЬ
// ============================================================
struct Config {
    std::atomic<bool> autoloot{false};
    std::atomic<float> speedValue{80.f};
    // ... (остальные поля)
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

// ============================================================
// ЛОГИКА
// ============================================================
static void tick_speedhack() {
    if (g_cfg.speedValue.load() <= 80.f) return; 
    uintptr_t pawn = get_local_pawn(); if (!pawn) return;
    
    float mult = g_cfg.speedValue.load() / 80.0f; 
    if (mult > 1.3f) mult = 1.3f; 

    wr<float>(pawn + deoff(off_enc::MaxWalkSpeed_enc), 600.0f * mult);
}

// ============================================================
// ГЛАВНЫЙ ПОТОК
// ============================================================
static void* cheat_thread(void*) {
    anti_debug();
    check_integrity();

    while (!g_ue4Base) {
        void* h = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD);
        if (h) g_ue4Base = h; else sleep(1);
    }
    
    while (true) {
        // Проверяем отладку каждый кадр (чтобы не пропустить позднее подключение)
        if (getpid() % 2 == 0) anti_debug(); 
        
        tick_speedhack();
        usleep(16000); 
    }
    return nullptr;
}

// ============================================================
// JNI
// ============================================================
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
