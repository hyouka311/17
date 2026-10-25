// Standrise rework - ESP + look-input aimbot (arm64, libil2cpp.so)
// Offsets taken from your dump.cs. Names are obfuscated: after any game update re-check every RVA/offset.
//
// WARNING: using this violates the game's ToS and can get you banned.

#include <cstdint>
#include <cmath>
#include <atomic>
#include <dlfcn.h>
#include <link.h>
#include <string.h>
#include <vector>
#include <android/log.h>
#include "And64InlineHook.hpp"
#include "settings.h"

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

// ---------- il2cpp base ----------
static uintptr_t g_base = 0;
static int phdr_cb(struct dl_phdr_info* i, size_t, void*) {
    if (i->dlpi_name && strstr(i->dlpi_name, "libil2cpp.so")) { g_base = (uintptr_t)i->dlpi_addr; return 1; }
    return 0;
}
uintptr_t Base() { if (!g_base) dl_iterate_phdr(phdr_cb, nullptr); return g_base; }
template <typename T> static T Fn(uintptr_t rva) { return (T)(Base() + rva); }

// ---------- RVAs (dump.cs) ----------
constexpr uintptr_t RVA_Camera_get_main       = 0x2DD22B8;
constexpr uintptr_t RVA_Camera_WorldToScreen  = 0x2DD1F2C;
constexpr uintptr_t RVA_Camera_pixelWidth     = 0x2DD16B0;
constexpr uintptr_t RVA_Camera_pixelHeight    = 0x2DD16EC;
constexpr uintptr_t RVA_Transform_get_pos     = 0x2DDE140;
constexpr uintptr_t RVA_PlayerMainCam_get     = 0x10F7EF4;
constexpr uintptr_t RVA_PlayerMainCam_getPC   = 0x10F81B4;   // -> local PlayerController*
constexpr uintptr_t RVA_PM_ctor               = 0x10F88A4;
constexpr uintptr_t RVA_PM_getSet             = 0x10F8AD8;
// AimController: the two private Vector2-returning methods (look input candidates)
constexpr uintptr_t RVA_LookA                 = 0x112F138;   // CDGBEDGFEGCHBEA
constexpr uintptr_t RVA_LookB                 = 0x112F1E0;   // DFGGDGEGHFGEBDF

// ---------- field offsets (dump.cs) ----------
constexpr uintptr_t OFF_PM_PlayersDict = 0x20;
constexpr uintptr_t OFF_PC_Team        = 0x59;   // byte: 1=Tr 2=Ct
constexpr uintptr_t OFF_PC_Biped       = 0x30;
constexpr uintptr_t OFF_BM_Head        = 0x18;
constexpr uintptr_t OFF_AIM_Owner      = 0xB8;   // AimController -> PlayerController

typedef void* (*get_main_t)();
typedef Vec3  (*w2s_t)(void*, Vec3);
typedef int   (*get_int_t)(void*);
typedef Vec3  (*get_pos_t)(void*);
typedef void* (*get_ptr_t)(void*);
typedef void* (*get_static_ptr_t)();
template <typename T> static T Rd(uintptr_t addr) { return *(T*)addr; }

Settings S;

// ---------- capture PlayerManager instance ----------
static void* g_pm = nullptr;
static void* g_localPC = nullptr;
static void (*orig_pm_ctor)(void*) = nullptr;
static void* (*orig_pm_get)(void*) = nullptr;
static void hk_pm_ctor(void* self) { g_pm = self; orig_pm_ctor(self); }
static void* hk_pm_get(void* self) { g_pm = self; return orig_pm_get(self); }

// ---------- aim steering via look-input hooks ----------
static volatile float g_dx = 0, g_dy = 0;   // pixel error to the chosen target
static std::atomic<int> g_hasTarget{0};
static Vec2 (*orig_lookA)(void*, void*) = nullptr;
static Vec2 (*orig_lookB)(void*, void*) = nullptr;

static Vec2 Steer(Vec2 r, void* self, int id) {
    static int cnt[2] = {0, 0};
    if ((r.x != 0.f || r.y != 0.f) && (++cnt[id] % 120) == 0)
        __android_log_print(ANDROID_LOG_INFO, "mod", "look%c raw=(%.4f, %.4f)", id ? 'B' : 'A', r.x, r.y);
    if (!S.aim || S.lookSel != id || !g_hasTarget.load()) return r;
    if (g_localPC && Rd<void*>((uintptr_t)self + OFF_AIM_Owner) != g_localPC) return r;  // only our own controller
    float ax =  g_dx * S.aimGain * (S.invX ? -1.f : 1.f);
    float ay = -g_dy * S.aimGain * (S.invY ? -1.f : 1.f);
    if (ax >  S.aimMaxStep) ax =  S.aimMaxStep; if (ax < -S.aimMaxStep) ax = -S.aimMaxStep;
    if (ay >  S.aimMaxStep) ay =  S.aimMaxStep; if (ay < -S.aimMaxStep) ay = -S.aimMaxStep;
    r.x += ax; r.y += ay;
    return r;
}
static Vec2 hk_lookA(void* self, void* mi) { return Steer(orig_lookA(self, mi), self, 0); }
static Vec2 hk_lookB(void* self, void* mi) { return Steer(orig_lookB(self, mi), self, 1); }

void InstallHooks() {
    A64HookFunction((void*)(Base() + RVA_PM_ctor),   (void*)hk_pm_ctor, (void**)&orig_pm_ctor);
    A64HookFunction((void*)(Base() + RVA_PM_getSet), (void*)hk_pm_get,  (void**)&orig_pm_get);
    A64HookFunction((void*)(Base() + RVA_LookA),     (void*)hk_lookA,   (void**)&orig_lookA);
    A64HookFunction((void*)(Base() + RVA_LookB),     (void*)hk_lookB,   (void**)&orig_lookB);
    __android_log_print(ANDROID_LOG_INFO, "mod", "il2cpp hooks installed, base=%p", (void*)Base());
}

// ---------- enumerate players ----------
// Dictionary<int,PlayerController>: entries array at +0x18, count at +0x20, Entry = 0x18 bytes, value at +0x10
static std::vector<void*> GetPlayers() {
    std::vector<void*> out;
    void* pm = g_pm;
    if (!pm) return out;
    uintptr_t dict = Rd<uintptr_t>((uintptr_t)pm + OFF_PM_PlayersDict);
    if (!dict) return out;
    uintptr_t entries = Rd<uintptr_t>(dict + 0x18);
    int count         = Rd<int>(dict + 0x20);
    if (!entries || count <= 0 || count > 64) return out;
    for (int i = 0; i < count; i++) {
        uintptr_t e = entries + 0x20 + i * 0x18;
        void* pc = (void*)Rd<uintptr_t>(e + 0x10);
        if (pc) out.push_back(pc);
    }
    return out;
}

static bool W2S(void* cam, Vec3 w, Vec2& s, int sw, int sh) {
    Vec3 r = Fn<w2s_t>(RVA_Camera_WorldToScreen)(cam, w);
    if (r.z <= 0.f) return false;
    s = { r.x, sh - r.y };
    return s.x > -sw && s.x < sw * 2 && s.y > -sh && s.y < sh * 2;
}

struct Draw {
    void (*line)(Vec2, Vec2);
    void (*box)(Vec2 top, Vec2 bottom);
};

// Called every frame from the render hook.
void OnFrame(const Draw& d) {
    void* cam = Fn<get_main_t>(RVA_Camera_get_main)();
    if (!cam) { g_hasTarget = 0; return; }
    int sw = Fn<get_int_t>(RVA_Camera_pixelWidth)(cam);
    int sh = Fn<get_int_t>(RVA_Camera_pixelHeight)(cam);

    void* localPC = nullptr;
    if (void* pmc = Fn<get_static_ptr_t>(RVA_PlayerMainCam_get)())
        localPC = Fn<get_ptr_t>(RVA_PlayerMainCam_getPC)(pmc);
    g_localPC = localPC;
    uint8_t myTeam = localPC ? Rd<uint8_t>((uintptr_t)localPC + OFF_PC_Team) : 0;

    Vec2 centre = { sw / 2.f, sh / 2.f };
    float bestD = S.aimFov; bool found = false; float bdx = 0, bdy = 0;

    if (S.esp || S.aim) {
        for (void* pc : GetPlayers()) {
            if (pc == localPC) continue;
            uint8_t team = Rd<uint8_t>((uintptr_t)pc + OFF_PC_Team);
            if (S.skipTeam && team == myTeam) continue;
            void* biped = (void*)Rd<uintptr_t>((uintptr_t)pc + OFF_PC_Biped);
            if (!biped) continue;
            void* head = (void*)Rd<uintptr_t>((uintptr_t)biped + OFF_BM_Head);
            if (!head) continue;

            Vec3 hp = Fn<get_pos_t>(RVA_Transform_get_pos)(head);
            Vec3 fp = { hp.x, hp.y - 1.7f, hp.z };
            Vec3 tp = { hp.x, hp.y + 0.25f, hp.z };
            Vec2 sHead, sFeet, sTop;
            if (!W2S(cam, hp, sHead, sw, sh) || !W2S(cam, fp, sFeet, sw, sh) || !W2S(cam, tp, sTop, sw, sh)) continue;

            if (S.esp) {
                if (S.espBox)  d.box(sTop, sFeet);
                if (S.espLine) d.line({ centre.x, 0 }, sFeet);
            }
            float dx = sHead.x - centre.x, dy = sHead.y - centre.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < bestD) { bestD = dist; bdx = dx; bdy = dy; found = true; }
        }
    }
    g_dx = bdx; g_dy = bdy;
    g_hasTarget = (S.aim && found) ? 1 : 0;
}
