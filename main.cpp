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
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "dobby.h"

#define LOG_TAG "SysCore" // Сменили имя, чтобы не палиться
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ============================================================
// 1. ЗАЩИТА ОТ РЕВЕРСА (ANTI-DEBUG & ANTI-TAMPER)
// ============================================================
static void anti_debug() {
    // Проверка на подключение отладчика через ptrace
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
        // Отладчик подключен — убиваем процесс
        kill(getpid(), SIGKILL);
    }
    
    // Проверка статуса отладки через /proc/self/status
    FILE* f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "TracerPid:") != nullptr) {
                int tracer_pid = atoi(line + 10);
                if (tracer_pid != 0) {
                    kill(getpid(), SIGKILL); // Нас кто-то слушает
                }
            }
        }
        fclose(f);
    }
}

// ============================================================
// 2. ШИФРОВАНИЕ СТРОК (XOR)
// ============================================================
// Чтобы в файле не было открытым текстом "libUE4.so"
class ObfuscatedString {
    char data[64];
    int len;
    char key;
public:
    constexpr ObfuscatedString(const char* str, char k) : key(k), len(0) {
        int i = 0;
        while (str[i] != '\0' && i < 63) {
            data[i] = str[i] ^ key;
            i++;
        }
        data[i] = '\0';
        len = i;
    }
    const char* get() const {
        static char buf[64];
        for (int i = 0; i < len; i++) {
            buf[i] = data[i] ^ key;
        }
        buf[len] = '\0';
        return buf;
    }
};

// Строки зашифрованы (ключ 0xAA)
constexpr ObfuscatedString ENC_UE4("libUE4.so", 0xAA);
constexpr ObfuscatedString ENC_EGL("libEGL.so", 0xAA);
constexpr ObfuscatedString ENC_SWAP("eglSwapBuffers", 0xAA);

// ============================================================
// ОФФСЕТЫ И КОНФИГ
// ============================================================
namespace off {
    uintptr_t GWorld          = 0x8E4B8C0; 
    uintptr_t PersistentLevel = 0x30;
    uintptr_t LevelActors     = 0xA0;
    uintptr_t LocalPlayer     = 0x38;
    uintptr_t PlayerController= 0x30;
    uintptr_t AcknowledgedPawn= 0x2B8;
    uintptr_t MaxWalkSpeed    = 0x3A8;     
    uintptr_t BoneArray       = 0x6B8; 
    uintptr_t RootComponent   = 0x1B0; 
    uintptr_t ItemId          = 0x5F0; 
    uintptr_t CurrentWeapon   = 0x8C8; 
    uintptr_t ViewMatrix      = 0x3A0; 
    uintptr_t PlayerState     = 0x2C0;
    uintptr_t TeamId          = 0x3F0;
}

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
    return (!g_ue4Base || !off::GWorld) ? 0 : rd<uintptr_t>((uintptr_t)g_ue4Base + off::GWorld); 
}
static uintptr_t get_local_pawn() {
    uintptr_t world = get_gworld(); if (!world) return 0;
    uintptr_t pc = rd<uintptr_t>(world + off::LocalPlayer); if (!pc) return 0;
    uintptr_t ctrl = rd<uintptr_t>(pc + off::PlayerController); if (!ctrl) return 0;
    return rd<uintptr_t>(ctrl + off::AcknowledgedPawn);
}

// ============================================================
// ЛОГИКА
// ============================================================
static void tick_speedhack() {
    if (g_cfg.speedValue.load() <= 80.f) return; 
    uintptr_t pawn = get_local_pawn(); if (!pawn) return;
    float mult = g_cfg.speedValue.load() / 80.0f; 
    if (mult > 1.3f) mult = 1.3f; 
    wr<float>(pawn + off::MaxWalkSpeed, 600.0f * mult);
}

static void tick_autoloot() {
    if (!g_cfg.autoloot.load()) return;
}

// ============================================================
// ESP ОТРИСОВКА (ЧЕРЕЗ EGL ХУК)
// ============================================================
typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    // Отрисовка ESP
    return orig_eglSwapBuffers(dpy, surf);
}

// ============================================================
// ГЛАВНЫЙ ПОТОК
// ============================================================
static void* cheat_thread(void*) {
    anti_debug(); // Проверка на отладку при старте

    while (!g_ue4Base) {
        // Используем зашифрованную строку
        void* h = dlopen(ENC_UE4.get(), RTLD_NOW | RTLD_NOLOAD);
        if (h) g_ue4Base = h; else sleep(1);
    }
    
    while (true) {
        tick_speedhack();
        tick_autoloot();
        usleep(16000); 
    }
    return nullptr;
}

// ============================================================
// JNI И ХУКИ
// ============================================================
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    pthread_t t; 
    pthread_create(&t, nullptr, cheat_thread, nullptr); 
    pthread_detach(t);

    void* h = dlopen(ENC_EGL.get(), RTLD_NOW);
    if (h) {
        void* target = dlsym(h, ENC_SWAP.get());
        if (target) {
            DobbyHook(target, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
        }
    }
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
