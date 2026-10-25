// Loader: waits for libil2cpp.so, hooks eglSwapBuffers, draws ImGui + calls OnFrame().
#include <thread>
#include <chrono>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "And64InlineHook.hpp"
#include "settings.h"
#include "touch.h"
#include <float.h>

struct Vec2 { float x, y; };
struct Draw { void (*line)(Vec2, Vec2); void (*box)(Vec2, Vec2); };
void OnFrame(const Draw& d);
void InstallHooks();
uintptr_t Base();

static void DLine(Vec2 a, Vec2 b) {
    ImGui::GetBackgroundDrawList()->AddLine({a.x, a.y}, {b.x, b.y}, IM_COL32(255, 60, 60, 255), 1.5f);
}
static void DBox(Vec2 top, Vec2 bot) {
    float h = bot.y - top.y, w = h * 0.45f;
    ImGui::GetBackgroundDrawList()->AddRect({top.x - w / 2, top.y}, {top.x + w / 2, bot.y},
                                            IM_COL32(60, 255, 60, 255), 0, 0, 1.5f);
}


static bool g_open = false;
static void DrawMenu(float W, float H) {
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##toggle", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button(g_open ? " X " : "MENU")) g_open = !g_open;
    ImGui::End();
    if (!g_open) return;

    ImGui::SetNextWindowSize(ImVec2(W * 0.40f, H * 0.75f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, H * 0.12f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Standrise", &g_open);
    ImGui::Checkbox("WH (ESP)", &S.esp);
    ImGui::Indent();
    ImGui::Checkbox("Box", &S.espBox);
    ImGui::Checkbox("Lines", &S.espLine);
    ImGui::Checkbox("Skip team", &S.skipTeam);
    ImGui::Unindent();
    ImGui::Separator();
    ImGui::Checkbox("Aimbot (experimental)", &S.aim);
    ImGui::SliderFloat("FOV px", &S.aimFov, 30.f, 800.f);
    ImGui::SliderFloat("Speed", &S.aimGain, 0.0005f, 1.f, "%.4f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Max step", &S.aimMaxStep, 0.1f, 20.f);
    ImGui::Checkbox("Invert X", &S.invX);
    ImGui::SameLine();
    ImGui::Checkbox("Invert Y", &S.invY);
    ImGui::Text("Look hook:");
    ImGui::SameLine();
    ImGui::RadioButton("A", &S.lookSel, 0);
    ImGui::SameLine();
    ImGui::RadioButton("B", &S.lookSel, 1);
    ImGui::End();
}

static void DrawFov(float W, float H) {
    if (S.aim)
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(W / 2, H / 2), S.aimFov, IM_COL32(255, 255, 255, 120), 64, 1.5f);
}

static void FeedInput(float W, float H) {
    ImGuiIO& io = ImGui::GetIO();
    float x = 0, y = 0;
    bool d = TouchGet(W, H, x, y);
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, d);
}

static EGLBoolean (*orig_swap)(EGLDisplay, EGLSurface) = nullptr;
static bool g_init = false;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static int g_inits = 0;

static EGLBoolean hook_swap(EGLDisplay dpy, EGLSurface surf) {
    static int frames = 0;
    frames++;
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    EGLContext ctx = eglGetCurrentContext();
    if (frames % 300 == 1)
        __android_log_print(ANDROID_LOG_INFO, "mod", "swap frame %d surf=%dx%d ctx=%p inited=%d", frames, w, h, (void*)ctx, (int)g_init);

    // only the big game surface (ignore tiny / offscreen surfaces and context-less calls)
    if (w >= 600 && h >= 300 && ctx != EGL_NO_CONTEXT) {
        if (!g_init || (ctx != g_ctx && g_inits < 5)) {
            // NOTE: the old ImGui context is intentionally leaked (deleting its GL objects in another context is unsafe)
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().FontGlobalScale = h / 540.f;   // readable on phone screens
            ImGui_ImplOpenGL3_Init("#version 300 es");
            g_init = true; g_ctx = ctx; g_inits++;
            __android_log_print(ANDROID_LOG_INFO, "mod", "imgui init #%d on ctx=%p surf=%dx%d", g_inits, (void*)ctx, w, h);
        }
        if (ctx == g_ctx) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)w, (float)h);
            io.DeltaTime = 1.0f / 60.0f;
            FeedInput((float)w, (float)h);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            DrawMenu((float)w, (float)h);          // menu first: always visible even if game logic fails
            DrawFov((float)w, (float)h);
            if (S.esp || S.aim) { Draw d{DLine, DBox}; OnFrame(d); }
            ImGui::Render();
            ImDrawData* dd = ImGui::GetDrawData();
            if (frames % 300 == 1)
                __android_log_print(ANDROID_LOG_INFO, "mod", "imgui verts=%d cmdlists=%d", dd->TotalVtxCount, dd->CmdListsCount);
            ImGui_ImplOpenGL3_RenderDrawData(dd);
        }
    }
    return orig_swap(dpy, surf);
}

static void worker() {
    __android_log_print(ANDROID_LOG_INFO, "mod", "worker: waiting for libil2cpp.so");
    while (!Base())   // dl_iterate_phdr sees every loaded library regardless of linker namespace
        std::this_thread::sleep_for(std::chrono::seconds(1));
    __android_log_print(ANDROID_LOG_INFO, "mod", "worker: il2cpp found at 0x%lx, waiting 3s", (unsigned long)Base());
    std::this_thread::sleep_for(std::chrono::seconds(3));
    InstallHooks();
    TouchStart();
    void* swap = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    if (!swap) { void* h = dlopen("libEGL.so", RTLD_NOW); if (h) swap = dlsym(h, "eglSwapBuffers"); }
    __android_log_print(ANDROID_LOG_INFO, "mod", "worker: eglSwapBuffers at %p", swap);
    if (swap) A64HookFunction(swap, (void*)hook_swap, (void**)&orig_swap);
    __android_log_print(ANDROID_LOG_INFO, "mod", "worker: eglSwapBuffers hooked: %p", swap);
}

__attribute__((constructor)) static void entry() {
    __android_log_print(ANDROID_LOG_INFO, "mod", "libcheat loaded"); std::thread(worker).detach(); }
