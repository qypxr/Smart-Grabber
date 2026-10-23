// ============================================================================
//  YOUTUBE GRABBER  -  main.cpp
//  Интерфейс в стиле «Горизонт событий» (Dear ImGui + DirectX 11).
//  Движок загрузки: grabber_core.h (yt-dlp + FFmpeg + Deno).
//  Кодировка файла: UTF-8. Сборка: build.bat (MSVC, флаг /utf-8).
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "grabber_core.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

namespace fs = std::filesystem;

// ============================================================================
//  D3D
// ============================================================================
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND                    g_hwnd = nullptr;

bool CreateDeviceD3D(HWND hWnd); void CleanupDeviceD3D(); void CreateRenderTarget(); void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
//  Состояние интерфейса
// ============================================================================
static std::mutex g_UIMutex;                    // тосты
struct ToastNotification { std::string text; float timer; };
static std::vector<ToastNotification> g_Toasts;

static bool  g_AnimBg = true, g_ParticlesOn = true, g_ToastsOn = true, g_AutoOpen = false;
static int   g_AccentIdx = 0;
static bool  g_IsLightTheme = false;
static float g_ThemeAnim = 0.0f;
static int   g_Page = 0;                        // 0 загрузка, 1 файлы, 2 настройки, 3 помощь, 4 о программе
static float g_PageAlpha = 0.0f;
static float g_NavHover[5] = { 0, 0, 0, 0, 0 };
static float g_A = 1.0f;                        // глобальная прозрачность контента (анимация смены страниц)

struct Particle { float angle, radius, speed, size, alpha, base_radius; };
static std::vector<Particle> g_Particles;
struct BgStar { float x, y, size, phase, speed; };   // x,y нормализованы 0..1
static std::vector<BgStar> g_Stars;

static ImFont* g_FontBody = nullptr;
static ImFont* g_FontSmall = nullptr;
static ImFont* g_FontH2 = nullptr;
static ImFont* g_FontH1 = nullptr;
static ImFont* g_FontBig = nullptr;   // светлый крупный (цифры)
static ImFont* g_FontMono = nullptr;  // моно-подписи

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)); }
static ImVec4 Rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
static ImU32 Col(const ImVec4& c, float a = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a * g_A)); }
static float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Палитра «Горизонт событий»: почти-чёрный с тёплым оттенком, янтарный акцент.
// Поверхности полупрозрачные (панели не «коробки», а лёгкие стёкла поверх атмосферы).
struct Palette { ImVec4 bg, panel, panel2, solid, border, text, dim; };
static Palette g_P;
static ImVec4 ACCENT = Rgb(255, 138, 56);
static ImVec4 ACCENT_HI = Rgb(255, 186, 120);
static const ImVec4 CYAN = Rgb(120, 200, 255);
static const ImVec4 WHITE = ImVec4(1, 1, 1, 1);
static const ImVec4 INK = Rgb(16, 13, 10);

static void UpdatePalette() {
    Palette dk = { Rgb(7,8,11), ImVec4(1.f,0.96f,0.90f,0.030f), ImVec4(1.f,0.96f,0.90f,0.075f), Rgb(22,21,23), ImVec4(1.f,0.95f,0.88f,0.100f), Rgb(233,228,218), Rgb(136,131,124) };
    Palette lt = { Rgb(236,231,221), ImVec4(1.f,1.f,1.f,0.50f), ImVec4(0.10f,0.08f,0.05f,0.060f), Rgb(250,247,241), ImVec4(0.10f,0.08f,0.05f,0.150f), Rgb(26,24,22), Rgb(112,106,98) };
    g_P.bg = LerpColor(dk.bg, lt.bg, g_ThemeAnim);
    g_P.panel = LerpColor(dk.panel, lt.panel, g_ThemeAnim);
    g_P.panel2 = LerpColor(dk.panel2, lt.panel2, g_ThemeAnim);
    g_P.solid = LerpColor(dk.solid, lt.solid, g_ThemeAnim);
    g_P.border = LerpColor(dk.border, lt.border, g_ThemeAnim);
    g_P.text = LerpColor(dk.text, lt.text, g_ThemeAnim);
    g_P.dim = LerpColor(dk.dim, lt.dim, g_ThemeAnim);
}
// Акцент для текста/линий: на светлой теме затемняется, чтобы читался
static ImVec4 AccentT() { return LerpColor(ACCENT_HI, ImVec4(ACCENT.x * 0.68f, ACCENT.y * 0.68f, ACCENT.z * 0.68f, 1.f), g_ThemeAnim); }

static std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &s[0], n, NULL, NULL); return s;
}
static std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &w[0], n); return w;
}

static void AddToast(const std::string& message) { std::lock_guard<std::mutex> lock(g_UIMutex); g_Toasts.push_back({ message, 3.0f }); }

static std::string FileNameOf(const std::string& p) { size_t i = p.find_last_of("\\/"); return i == std::string::npos ? p : p.substr(i + 1); }
static std::string DirOf(const std::string& p) { size_t i = p.find_last_of("\\/"); return i == std::string::npos ? std::string() : p.substr(0, i); }
static std::string FmtSize(uint64_t b) {
    char buf[48];
    if (b < 1024) snprintf(buf, sizeof(buf), "%llu Б", (unsigned long long)b);
    else if (b < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f КБ", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f МБ", b / (1024.0 * 1024.0));
    else snprintf(buf, sizeof(buf), "%.2f ГБ", b / (1024.0 * 1024.0 * 1024.0));
    return buf;
}
static uint64_t FileSizeOf(const std::string& p) {
    std::error_code ec; auto s = std::filesystem::file_size(std::filesystem::u8path(p), ec); return ec ? 0 : (uint64_t)s;
}
static void OpenUrl(const wchar_t* url) { ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL); }

static void InitStars() {
    if (!g_Stars.empty()) return;
    // размер = «глубина»: мелких много, крупных единицы; крупные дрейфуют быстрее (параллакс)
    for (int i = 0; i < 130; ++i)
        g_Stars.push_back({ (rand() % 1000) / 1000.f, (rand() % 1000) / 1000.f, powf((rand() % 1000) / 1000.f, 2.4f) * 1.4f + 0.45f, (rand() % 628) / 100.f, (rand() % 20) / 10.f + 0.6f });
}
static void InitParticles() {
    if (!g_Particles.empty()) return;
    for (int i = 0; i < 110; ++i) {
        Particle p;
        p.angle = (rand() % 360) * 3.14159f / 180.f;
        p.base_radius = (float)(rand() % 110 + 60); p.radius = p.base_radius * ((rand() % 100) / 100.f);
        p.speed = (float)(rand() % 50 + 20) * 0.001f; p.size = (float)(rand() % 3) + 1.8f; p.alpha = (float)(rand() % 255);
        g_Particles.push_back(p);
    }
}

// ============================================================================
//  Звук: всё синтезируется при запуске (никаких файлов), играется через winmm
// ============================================================================
enum SndId { SFX_HOVER, SFX_CLICK, SFX_ON, SFX_OFF, SFX_PICK, SFX_RUN, SFX_OK, SFX_ERR, SFX_START, SFX_COUNT };
static bool g_SoundOn = true;
static std::vector<uint8_t> g_Wav[SFX_COUNT];
static std::atomic<int> g_PendingSnd(-1);        // звук из рабочего потока → проигрывается в главном цикле
static double g_SndMuteUntil = 0.0, g_LastHoverSnd = 0.0;

static const int SR = 44100;
static void SynthTone(std::vector<float>& o, float t0, float dur, float f0, float f1, float amp, float attack, float decay) {
    size_t i0 = (size_t)(t0 * SR), n = (size_t)(dur * SR); if (o.size() < i0 + n) o.resize(i0 + n, 0.f);
    double ph = 0.0;
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, k = (float)i / (float)n;
        float f = f0 + (f1 - f0) * k; ph += 6.2831853 * f / SR;
        float env = std::min(1.f, t / std::max(attack, 0.0005f)) * expf(-t * decay);
        o[i0 + i] += amp * env * (sinf((float)ph) + 0.28f * sinf((float)ph * 2.f) + 0.08f * sinf((float)ph * 3.f));
    }
}
static void SynthNoise(std::vector<float>& o, float t0, float dur, float amp, float lp, float attack, float decay) {
    size_t i0 = (size_t)(t0 * SR), n = (size_t)(dur * SR); if (o.size() < i0 + n) o.resize(i0 + n, 0.f);
    float y = 0.f; unsigned seed = 12345u + (unsigned)(t0 * 1000);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u; float r = ((seed >> 9) / 4194304.f) - 1.f;
        y += (r - y) * lp; float t = (float)i / SR;
        o[i0 + i] += amp * y * std::min(1.f, t / std::max(attack, 0.0005f)) * expf(-t * decay);
    }
}
static void SynthEcho(std::vector<float>& o, float delay, float gain, int taps) {
    size_t d = (size_t)(delay * SR); o.resize(o.size() + d * taps, 0.f);
    for (size_t i = d; i < o.size(); ++i) o[i] += o[i - d] * gain;
}
static std::vector<uint8_t> ToWav(std::vector<float>& s, float vol) {
    std::vector<uint8_t> w(44 + s.size() * 2);
    auto p32 = [&](size_t o, uint32_t v) { memcpy(&w[o], &v, 4); }; auto p16 = [&](size_t o, uint16_t v) { memcpy(&w[o], &v, 2); };
    memcpy(&w[0], "RIFF", 4); p32(4, (uint32_t)(36 + s.size() * 2)); memcpy(&w[8], "WAVEfmt ", 8);
    p32(16, 16); p16(20, 1); p16(22, 1); p32(24, SR); p32(28, SR * 2); p16(32, 2); p16(34, 16); memcpy(&w[36], "data", 4); p32(40, (uint32_t)(s.size() * 2));
    for (size_t i = 0; i < s.size(); ++i) {
        float x = tanhf(s[i] * vol * 1.2f);                              // мягкий лимитер
        float fade = std::min(1.f, (s.size() - i) / (0.01f * SR));       // без щелчка в конце
        p16(44 + i * 2, (uint16_t)(int16_t)(x * fade * 30000.f));
    }
    return w;
}
static void BuildSounds() {
    std::vector<float> s;
    s.clear(); SynthTone(s, 0, 0.035f, 3300, 3100, 0.07f, 0.002f, 90);                                                   g_Wav[SFX_HOVER] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.09f, 1500, 800, 0.30f, 0.001f, 48); SynthNoise(s, 0, 0.02f, 0.10f, 0.5f, 0.001f, 160);   g_Wav[SFX_CLICK] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.14f, 660, 660, 0.24f, 0.002f, 22); SynthTone(s, 0.06f, 0.18f, 990, 990, 0.24f, 0.002f, 20); g_Wav[SFX_ON] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.14f, 990, 990, 0.22f, 0.002f, 24); SynthTone(s, 0.06f, 0.18f, 560, 560, 0.22f, 0.002f, 20); g_Wav[SFX_OFF] = ToWav(s, 0.9f);
    s.clear(); SynthNoise(s, 0, 0.26f, 0.22f, 0.18f, 0.08f, 11); SynthTone(s, 0, 0.28f, 420, 900, 0.20f, 0.02f, 11);       g_Wav[SFX_PICK] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.42f, 110, 340, 0.34f, 0.04f, 6); SynthTone(s, 0, 0.42f, 220, 680, 0.14f, 0.04f, 6); SynthNoise(s, 0, 0.30f, 0.08f, 0.10f, 0.12f, 7); g_Wav[SFX_RUN] = ToWav(s, 0.9f);
    {   // успех: арпеджио C-E-G-C с «залом»
        s.clear(); const float nt[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
        for (int i = 0; i < 4; ++i) SynthTone(s, i * 0.09f, 1.0f, nt[i], nt[i], 0.22f, 0.004f, 4.2f);
        SynthEcho(s, 0.17f, 0.36f, 3); g_Wav[SFX_OK] = ToWav(s, 0.85f);
    }
    s.clear(); SynthTone(s, 0, 0.30f, 196, 140, 0.34f, 0.004f, 8); SynthTone(s, 0, 0.30f, 207, 148, 0.24f, 0.004f, 8); SynthTone(s, 0.17f, 0.30f, 165, 110, 0.28f, 0.004f, 8); g_Wav[SFX_ERR] = ToWav(s, 0.9f);
    {   // запуск: нарастающий гул + мерцание
        s.clear(); SynthTone(s, 0, 1.2f, 80, 330, 0.26f, 0.45f, 2.0f); SynthTone(s, 0, 1.2f, 160, 660, 0.10f, 0.50f, 2.2f);
        SynthNoise(s, 0, 1.1f, 0.06f, 0.05f, 0.5f, 2.2f);
        SynthTone(s, 0.45f, 0.9f, 1318.5f, 1318.5f, 0.07f, 0.04f, 3.5f); SynthTone(s, 0.62f, 0.9f, 1760.f, 1760.f, 0.05f, 0.04f, 3.5f);
        SynthEcho(s, 0.23f, 0.42f, 3); g_Wav[SFX_START] = ToWav(s, 0.8f);
    }
}
static void Snd(int id) {
    if (!g_SoundOn || id < 0 || id >= SFX_COUNT || g_Wav[id].empty()) return;
    double now = ImGui::GetTime();
    if (id == SFX_HOVER) { if (now < g_SndMuteUntil || now - g_LastHoverSnd < 0.07) return; g_LastHoverSnd = now; }
    else if (id == SFX_OK || id == SFX_ERR) g_SndMuteUntil = now + 1.3;
    else if (now < g_SndMuteUntil) return;
    PlaySoundW((LPCWSTR)g_Wav[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
// звук наведения (по фронту) + клика для только что созданного элемента
static bool Fx(bool pressed) {
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), prev = st->GetBool(id ^ 0x5F3759DF, false);
    if (hov && !prev) Snd(SFX_HOVER);
    st->SetBool(id ^ 0x5F3759DF, hov);
    if (pressed) Snd(SFX_CLICK);
    return pressed;
}

// ============================================================================
//  Примитивы рисования
// ============================================================================
static ImVec2 TSize(ImFont* f, const char* s) { ImGui::PushFont(f); ImVec2 v = ImGui::CalcTextSize(s); ImGui::PopFont(); return v; }
static void DText(ImDrawList* d, ImFont* f, ImVec2 p, ImU32 c, const char* s) { ImGui::PushFont(f); d->AddText(p, c, s); ImGui::PopFont(); }
static void DTextC(ImDrawList* d, ImFont* f, float cx, float y, ImU32 c, const char* s) { ImVec2 sz = TSize(f, s); DText(d, f, ImVec2(cx - sz.x * 0.5f, y), c, s); }
static void DTextR(ImDrawList* d, ImFont* f, float rx, float y, ImU32 c, const char* s) { ImVec2 sz = TSize(f, s); DText(d, f, ImVec2(rx - sz.x, y), c, s); }

static void Poly3(ImDrawList* d, ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col, float th) { ImVec2 pts[3] = { a, b, c }; d->AddPolyline(pts, 3, col, 0, th); }

static int U8Len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4; }

// Текст с разрядкой (tracking) — основа «инструментального» типографского стиля
static float TrackWidth(ImFont* f, const char* s, float track) {
    ImGui::PushFont(f); float w = 0.f;
    for (const char* p = s; *p; ) { int n = U8Len((unsigned char)*p); w += ImGui::CalcTextSize(p, p + n).x + track; p += n; }
    ImGui::PopFont(); return w > 0.f ? w - track : 0.f;
}
static float DTrack(ImDrawList* d, ImFont* f, ImVec2 p, ImU32 col, const char* s, float track) {
    ImGui::PushFont(f); float x = p.x;
    for (const char* q = s; *q; ) { int n = U8Len((unsigned char)*q); d->AddText(ImVec2(x, p.y), col, q, q + n); x += ImGui::CalcTextSize(q, q + n).x + track; q += n; }
    ImGui::PopFont(); return (x - p.x) - track;
}
static float DLabel(ImDrawList* d, ImVec2 p, ImU32 col, const char* s, float track = 1.8f) { return DTrack(d, g_FontMono, p, col, s, track); }
static void DLabelC(ImDrawList* d, float cx, float y, ImU32 col, const char* s, float track = 1.8f) { DLabel(d, ImVec2(cx - TrackWidth(g_FontMono, s, track) * 0.5f, y), col, s, track); }
static void DLabelR(ImDrawList* d, float rx, float y, ImU32 col, const char* s, float track = 1.8f) { DLabel(d, ImVec2(rx - TrackWidth(g_FontMono, s, track), y), col, s, track); }

// «01 — ЗАГОЛОВОК»: индекс акцентом, подпись приглушённо
static void Eyebrow(ImDrawList* d, ImVec2 p, const char* idx, const char* text) {
    float w = DLabel(d, p, Col(AccentT()), idx, 1.5f);
    d->AddLine(ImVec2(p.x + w + 8, p.y + 8), ImVec2(p.x + w + 20, p.y + 8), Col(g_P.dim, 0.5f), 1.f);
    DLabel(d, ImVec2(p.x + w + 28, p.y), Col(g_P.dim), text);
}

static float DrawWrapped(ImDrawList* d, ImFont* f, ImVec2 p, float maxW, ImU32 c, const std::string& text, float lineH) {
    ImGui::PushFont(f);
    std::vector<std::string> words; { std::string w; for (char ch : text) { if (ch == ' ') { words.push_back(w); w.clear(); } else w += ch; } words.push_back(w); }
    std::string cur; float y = p.y;
    for (const auto& w : words) {
        std::string t = cur.empty() ? w : cur + " " + w;
        if (!cur.empty() && ImGui::CalcTextSize(t.c_str()).x > maxW) { d->AddText(ImVec2(p.x, y), c, cur.c_str()); y += lineH; cur = w; }
        else cur = t;
    }
    if (!cur.empty()) { d->AddText(ImVec2(p.x, y), c, cur.c_str()); y += lineH; }
    ImGui::PopFont();
    return y - p.y;
}

// Угловые скобки «видоискателя»
static void Brackets(ImDrawList* d, ImVec2 a, ImVec2 b, float L, ImU32 col, float th) {
    d->AddLine(a, ImVec2(a.x + L, a.y), col, th);                 d->AddLine(a, ImVec2(a.x, a.y + L), col, th);
    d->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - L, a.y), col, th);  d->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + L), col, th);
    d->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + L, b.y), col, th);  d->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - L), col, th);
    d->AddLine(b, ImVec2(b.x - L, b.y), col, th);                 d->AddLine(b, ImVec2(b.x, b.y - L), col, th);
}

// Панель: лёгкое стекло, тонкая рамка, блик по верхней кромке, акцентные скобки по углам
static void Panel(ImDrawList* d, ImVec2 a, ImVec2 b, bool brackets = true) {
    const float r = 4.f; float dk = 1.f - g_ThemeAnim;
    d->AddRectFilled(a, b, Col(g_P.panel), r);
    d->AddRectFilledMultiColor(ImVec2(a.x + r, a.y), ImVec2(b.x - r, a.y + 1.f), Col(WHITE, 0.20f * dk), Col(WHITE, 0.f), Col(WHITE, 0.f), Col(WHITE, 0.20f * dk));
    d->AddRect(a, b, Col(g_P.border), r, 0, 1.f);
    if (brackets) Brackets(d, ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), 9.f, Col(AccentT(), 0.65f), 1.2f);
}

static void Glow(ImDrawList* d, ImVec2 c, float radius, const ImVec4& color, float totalAlpha, int steps = 40) {
    for (int i = 0; i < steps; ++i) { float t = (float)i / steps; d->AddCircleFilled(c, radius * (1.f - t), Col(color, totalAlpha / steps * (0.6f + t)), 64); }
}

static float Hash01(int n) {
    unsigned x = (unsigned)n * 747796405u + 2891336453u; x = ((x >> ((x >> 28) + 4)) ^ x) * 277803737u; x = (x >> 22) ^ x; return x / 4294967295.f;
}


static bool PrimaryButton(const char* id, const char* label, ImVec2 pos, ImVec2 size, int state, float time, float prog = 0.f) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, size);
    if (state == 1) Fx(pressed);
    bool hov = (state == 1) && ImGui::IsItemHovered();
    ImVec2 b(pos.x + size.x, pos.y + size.y); const float r = 4.f;
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
    float h = st->GetFloat(gid, 0.f); h = Lerp(h, hov ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 12.f)); st->SetFloat(gid, h);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImU32 txt = Col(INK);
    if (state == 1) {
        for (int i = 1; i <= 12; ++i) d->AddRect(ImVec2(pos.x - i, pos.y - i), ImVec2(b.x + i, b.y + i), Col(ACCENT, 0.05f * h * (1.f - i / 13.f)), r + i);
        d->AddRectFilled(pos, b, Col(LerpColor(ACCENT, ACCENT_HI, h)), r);
        d->AddRectFilledMultiColor(ImVec2(pos.x + 1, pos.y + 1), ImVec2(b.x - 1, pos.y + size.y * 0.55f), Col(WHITE, 0.22f), Col(WHITE, 0.22f), Col(WHITE, 0.f), Col(WHITE, 0.f));
        // световая полоса пробегает при наведении
        d->PushClipRect(pos, b, true);
        float sx = pos.x - 60.f + h * (size.x + 120.f);
        d->AddQuadFilled(ImVec2(sx, pos.y), ImVec2(sx + 26, pos.y), ImVec2(sx + 6, b.y), ImVec2(sx - 20, b.y), Col(WHITE, 0.28f * sinf(h * 3.14159f)));
        d->PopClipRect();
    }
    else if (state == 2) {
        float fw = size.x * std::clamp(prog, 0.f, 1.f);
        d->AddRectFilled(pos, b, Col(ACCENT, 0.20f), r);
        d->AddRect(pos, b, Col(ACCENT, 0.55f), r);
        d->PushClipRect(pos, ImVec2(pos.x + fw, b.y), true);
        d->AddRectFilled(pos, b, Col(LerpColor(ACCENT, ACCENT_HI, 0.2f)), r);
        float off = fmodf(time * 34.f, 18.f);
        for (float x = pos.x - size.y - 18.f + off; x < b.x + 18.f; x += 18.f) d->AddLine(ImVec2(x, b.y), ImVec2(x + size.y, pos.y), Col(INK, 0.14f), 7.f);
        d->PopClipRect();
        if (fw > 4.f) { for (int i = 1; i <= 6; ++i) d->AddRectFilled(ImVec2(pos.x + fw - 1.f, pos.y), ImVec2(pos.x + fw + i * 2.f, b.y), Col(ACCENT_HI, 0.05f), 0.f); d->AddRectFilled(ImVec2(pos.x + fw - 2.f, pos.y), ImVec2(pos.x + fw, b.y), Col(WHITE, 0.75f)); }
        txt = Col(INK);
    }
    else {
        d->AddRectFilled(pos, b, Col(g_P.panel), r);
        d->AddRect(pos, b, Col(g_P.border), r);
        txt = Col(g_P.dim);
    }
    float tw = TrackWidth(g_FontH2, label, 2.2f); ImVec2 ts = TSize(g_FontH2, label);
    float tx = pos.x + (size.x - tw) * 0.5f - (state == 1 ? 10.f : 0.f);
    if (state == 2) {   // слева от кромки прогресса текст тёмный, справа — светлый
        float fw = size.x * std::clamp(prog, 0.f, 1.f);
        d->PushClipRect(pos, ImVec2(pos.x + fw, b.y), true); DTrack(d, g_FontH2, ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), Col(INK), label, 2.2f); d->PopClipRect();
        d->PushClipRect(ImVec2(pos.x + fw, pos.y), b, true); DTrack(d, g_FontH2, ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), Col(g_P.text), label, 2.2f); d->PopClipRect();
    }
    else
        DTrack(d, g_FontH2, ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), txt, label, 2.2f);
    if (state == 1) {   // стрелка
        float ax = tx + tw + 18.f + h * 5.f, ay = pos.y + size.y * 0.5f;
        d->AddLine(ImVec2(ax, ay), ImVec2(ax + 16, ay), txt, 1.8f);
        d->AddLine(ImVec2(ax + 10, ay - 5), ImVec2(ax + 16, ay), txt, 1.8f); d->AddLine(ImVec2(ax + 10, ay + 5), ImVec2(ax + 16, ay), txt, 1.8f);
    }
    return pressed && state == 1;
}

// Тонкая «призрачная» кнопка с моно-подписью
static bool GhostBtn(const char* id, const char* label, ImVec2 pos, ImVec2 size, bool active = false) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = Fx(ImGui::InvisibleButton(id, size));
    bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 b(pos.x + size.x, pos.y + size.y);
    d->AddRectFilled(pos, b, Col(hov ? g_P.panel2 : g_P.panel), 4.f);
    d->AddRect(pos, b, (hov || active) ? Col(AccentT(), 0.8f) : Col(g_P.border), 4.f, 0, 1.f);
    float tw = TrackWidth(g_FontMono, label, 1.4f); ImVec2 ts = TSize(g_FontMono, label);
    DLabel(d, ImVec2(pos.x + (size.x - tw) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), (hov || active) ? Col(AccentT()) : Col(g_P.text), label, 1.4f);
    return pressed;
}

// Текстовая вкладка с подчёркиванием
static bool TabText(const char* id, const char* label, ImVec2 pos, float w, bool active) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = Fx(ImGui::InvisibleButton(id, ImVec2(w, 26)));
    bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float tw = TrackWidth(g_FontMono, label, 1.2f); ImVec2 ts = TSize(g_FontMono, label);
    float tx = pos.x + (w - tw) * 0.5f;
    DLabel(d, ImVec2(tx, pos.y + (26 - ts.y) * 0.5f - 1.f), active ? Col(AccentT()) : (hov ? Col(g_P.text) : Col(g_P.dim)), label, 1.2f);
    if (active) d->AddRectFilled(ImVec2(tx, pos.y + 24.f), ImVec2(tx + tw, pos.y + 26.f), Col(ACCENT), 1.f);
    return pressed;
}

static bool LinkBtn(const char* id, const char* label, ImVec2 pos, ImVec2 size, const ImVec4& color) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = Fx(ImGui::InvisibleButton(id, size));
    bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 b(pos.x + size.x, pos.y + size.y);
    d->AddRectFilled(pos, b, Col(hov ? g_P.panel2 : g_P.panel), 4.f);
    d->AddRect(pos, b, Col(g_P.border), 4.f);
    d->AddRectFilled(pos, ImVec2(pos.x + 3.f, b.y), Col(color), 2.f);
    ImVec2 ts = TSize(g_FontBody, label);
    DText(d, g_FontBody, ImVec2(pos.x + 18, pos.y + (size.y - ts.y) * 0.5f), Col(g_P.text), label);
    float ax = b.x - 28.f, ay = pos.y + size.y * 0.5f + (hov ? -1.f : 0.f), axx = ax + (hov ? 2.f : 0.f);
    ImU32 ac = Col(color, hov ? 1.f : 0.7f);
    d->AddLine(ImVec2(axx - 5, ay + 5), ImVec2(axx + 5, ay - 5), ac, 1.6f);
    d->AddLine(ImVec2(axx - 1, ay - 5), ImVec2(axx + 5, ay - 5), ac, 1.6f); d->AddLine(ImVec2(axx + 5, ay - 5), ImVec2(axx + 5, ay + 1), ac, 1.6f);
    return pressed;
}

struct AccentPreset { ImVec4 a, hi; const char* name; };
static const AccentPreset g_Accents[5] = {
    { Rgb(255, 138, 56),  Rgb(255, 186, 120), "Янтарь" },
    { Rgb(86, 168, 255),  Rgb(150, 205, 255), "Лёд" },
    { Rgb(64, 208, 168),  Rgb(130, 236, 204), "Мята" },
    { Rgb(240, 98, 132),  Rgb(255, 152, 174), "Роза" },
    { Rgb(160, 140, 255), Rgb(198, 184, 255), "Сумрак" }
};

static bool Switch(const char* id, ImVec2 pos, bool* v) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pr = ImGui::InvisibleButton(id, ImVec2(52, 28)); Fx(false); if (pr) { *v = !*v; Snd(*v ? SFX_ON : SFX_OFF); }
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
    float t = st->GetFloat(gid, *v ? 1.f : 0.f); t = Lerp(t, *v ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 14.f)); st->SetFloat(gid, t);
    ImVec2 a(pos.x + 2, pos.y + 3), b(pos.x + 50, pos.y + 25);
    d->AddRectFilled(a, b, Col(ACCENT, 0.92f * t), 11.f);
    d->AddRectFilled(a, b, Col(g_P.panel2, 1.f - t), 11.f);
    d->AddRect(a, b, Col(LerpColor(g_P.dim, ACCENT, t), 0.55f + 0.45f * t), 11.f, 0, 1.2f);
    d->AddCircleFilled(ImVec2(a.x + 11.f + t * 26.f, a.y + 11.f), 7.f, Col(LerpColor(g_P.dim, INK, t)), 24);
    return pr;
}

// ============================================================================
//  Чёрная дыра: тень, фотонное кольцо, аккреционный диск и линзированные дуги
// ============================================================================
static ImVec4 DiskColor(float u) {
    const ImVec4 hot = Rgb(255, 246, 226);
    if (u < 0.30f) return LerpColor(hot, ACCENT_HI, u / 0.30f);
    return LerpColor(ACCENT_HI, ACCENT, std::min(1.f, (u - 0.30f) / 0.5f));
}

// kind: 0 — задняя половина плоского диска, 1 — передняя, 2 — верхняя линзированная дуга, 3 — нижняя (слабая)
static void DiskPass(ImDrawList* d, ImVec2 c, float R, float t, float energy, int kind, float tilt) {
    const int rings = 26, segs = 30; const float PI = 3.14159265f;
    float ct = cosf(tilt), st = sinf(tilt);
    for (int k = 0; k < rings; ++k) {
        float u01 = k / (float)(rings - 1);
        float u = 1.10f + u01 * 1.55f;
        float rx = R * u, ry; bool lower;
        switch (kind) {
        case 0: ry = rx * 0.19f; lower = false; break;
        case 1: ry = rx * 0.19f; lower = true; break;
        case 2: ry = R * (1.0f + (u - 1.0f) * 0.60f); lower = false; break;
        default: ry = R * (1.0f + (u - 1.0f) * 0.13f); lower = true; break;
        }
        float a0 = lower ? 0.f : PI;
        float speed = 1.9f / (0.35f + u01 * 1.4f) * (0.6f + 0.8f * energy);            // дифференциальное вращение
        float jit = 0.80f + 0.40f * Hash01(k * 17 + 3);
        float base = powf(1.f - u01, 1.25f) * jit * (kind == 3 ? 0.42f : (kind == 2 ? 0.95f : 1.f)) * (0.62f + 0.38f * energy);
        float th = 1.5f + (1.f - u01) * 1.4f;
        ImVec4 col = DiskColor(u01);
        ImVec2 prev(0, 0);
        for (int s = 0; s <= segs; ++s) {
            float a = a0 + PI * s / segs;
            float x = cosf(a) * rx, y = sinf(a) * ry;
            ImVec2 p(c.x + x * ct - y * st, c.y + x * st + y * ct);
            if (s > 0) {
                float am = a - PI / segs * 0.5f;
                float beam = 0.34f + 0.66f * (0.5f - 0.5f * cosf(am));                  // доплеровское усиление: левая сторона ярче
                float sw = 0.70f + 0.30f * sinf(am * 5.f + u * 6.f - t * speed);
                d->AddLine(prev, p, Col(col, base * beam * sw), th);
            }
            prev = p;
        }
    }
}

static void DrawBlackHole(ImDrawList* d, ImVec2 c, float R, float t, float energy, float glowK = 4.4f) {
    const float PI = 3.14159265f;
    float tilt = -0.20f + sinf(t * 0.25f) * 0.02f;
    Glow(d, c, R * glowK, ACCENT, (0.12f + 0.10f * energy) * (1.f - g_ThemeAnim * 0.5f), 40);
    DiskPass(d, c, R, t, energy, 0, tilt);
    DiskPass(d, c, R, t, energy, 2, tilt);
    DiskPass(d, c, R, t, energy, 3, tilt);
    d->AddCircleFilled(c, R * 1.02f, Col(Rgb(3, 3, 4)), 96);                            // тень
    for (int i = 6; i >= 1; --i) d->AddCircle(c, R * 1.02f + i * 1.5f, Col(ACCENT_HI, 0.05f * (7 - i) / 6.f), 96, 1.6f);
    for (int i = 0; i < 96; ++i) {                                                       // фотонное кольцо
        float a0 = i * 2.f * PI / 96.f, a1 = (i + 1) * 2.f * PI / 96.f, am = (a0 + a1) * 0.5f;
        float beam = 0.30f + 0.70f * (0.5f - 0.5f * cosf(am - tilt));
        d->AddLine(ImVec2(c.x + cosf(a0) * R * 1.02f, c.y + sinf(a0) * R * 1.02f), ImVec2(c.x + cosf(a1) * R * 1.02f, c.y + sinf(a1) * R * 1.02f),
            Col(Rgb(255, 240, 214), (0.40f + 0.25f * energy) * beam + 0.10f), 1.5f);
    }
    DiskPass(d, c, R, t, energy, 1, tilt);
}

static void DrawBackground(ImDrawList* d, ImVec2 wp, ImVec2 ws, float t, float dt) {
    ImVec2 e(wp.x + ws.x, wp.y + ws.y);
    d->AddRectFilled(wp, e, Col(g_P.bg));
    float dk = 1.f - g_ThemeAnim * 0.55f, night = 1.f - g_ThemeAnim;
    float tt = g_AnimBg ? t : 0.f;
    // туманность: тёплое ядро справа сверху, слабый отсвет по центру и холодная глубина слева снизу
    Glow(d, ImVec2(wp.x + ws.x * 0.80f + sinf(tt * 0.13f) * 50.f, wp.y + 150.f), 480.f, ACCENT, 0.17f * dk, 48);
    Glow(d, ImVec2(wp.x + ws.x * 0.58f, wp.y + ws.y * 0.58f + cosf(tt * 0.11f) * 30.f), 300.f, ACCENT, 0.045f * dk, 32);
    Glow(d, ImVec2(wp.x + 160.f, wp.y + ws.y - 60.f + cosf(tt * 0.17f) * 24.f), 400.f, Rgb(40, 92, 160), 0.11f * dk, 40);
    if (g_AnimBg) {
        InitStars();
        for (auto& s : g_Stars) {
            float x = fmodf(s.x + t * 0.0016f * s.size, 1.f);
            float al = (0.28f + 0.30f * sinf(t * s.speed + s.phase)) * night;
            ImVec2 p(wp.x + x * ws.x, wp.y + s.y * ws.y);
            d->AddCircleFilled(p, s.size * 0.75f, Col(Rgb(255, 244, 228), al * (0.4f + s.size * 0.35f)), 8);
            if (s.size > 1.55f) {   // редкие крупные звёзды получают крестообразный блик
                d->AddLine(ImVec2(p.x - 5, p.y), ImVec2(p.x + 5, p.y), Col(Rgb(255, 244, 228), al * 0.35f), 1.f);
                d->AddLine(ImVec2(p.x, p.y - 5), ImVec2(p.x, p.y + 5), Col(Rgb(255, 244, 228), al * 0.35f), 1.f);
            }
        }
        // редкий метеор
        static float mtimer = 4.f, mlife = -1.f; static ImVec2 mp0;
        mtimer -= dt;
        if (mlife < 0.f && mtimer < 0.f) { mlife = 0.f; mp0 = ImVec2(wp.x + (0.25f + (rand() % 600) / 1000.f) * ws.x, wp.y + (0.05f + (rand() % 300) / 1000.f) * ws.y); mtimer = 8.f + (rand() % 70) / 10.f; }
        if (mlife >= 0.f) {
            mlife += dt; float k = mlife / 0.9f;
            if (k >= 1.f) mlife = -1.f;
            else {
                ImVec2 dir(0.84f, 0.54f); float dist = k * 520.f; float fade = sinf(k * 3.14159f);
                ImVec2 head(mp0.x + dir.x * dist, mp0.y + dir.y * dist);
                for (int i = 0; i < 12; ++i) {
                    float a0 = i / 12.f, a1 = (i + 1) / 12.f;
                    d->AddLine(ImVec2(head.x - dir.x * 110.f * a0, head.y - dir.y * 110.f * a0), ImVec2(head.x - dir.x * 110.f * a1, head.y - dir.y * 110.f * a1),
                        Col(Rgb(255, 236, 214), (1.f - a0) * 0.55f * fade * night), 1.6f);
                }
            }
        }
    }
    // виньетка
    {
        ImVec4 vc = LerpColor(Rgb(0, 0, 0), Rgb(70, 48, 24), g_ThemeAnim); float va = 0.50f - 0.38f * g_ThemeAnim;
        ImU32 v0 = Col(vc, va), v1 = Col(vc, 0.f); const float V = 170.f;
        d->AddRectFilledMultiColor(wp, ImVec2(wp.x + V, e.y), v0, v1, v1, v0);
        d->AddRectFilledMultiColor(ImVec2(e.x - V, wp.y), e, v1, v0, v0, v1);
        d->AddRectFilledMultiColor(wp, ImVec2(e.x, wp.y + V * 0.7f), v0, v0, v1, v1);
        d->AddRectFilledMultiColor(ImVec2(wp.x, e.y - V * 0.7f), e, v1, v1, v0, v0);
    }
    // плёночное зерно: прячет бандинг свечения и добавляет «осязаемость»
    {
        static std::vector<ImVec2> grain; static float gtm = 0.f;
        gtm -= dt;
        if (grain.empty() || gtm <= 0.f) { grain.resize(300); for (auto& g : grain) g = ImVec2((rand() % 10000) / 10000.f, (rand() % 10000) / 10000.f); gtm = 0.08f; }
        ImU32 gc = Col(LerpColor(WHITE, Rgb(0, 0, 0), g_ThemeAnim), 0.045f + 0.03f * g_ThemeAnim);
        for (auto& g : grain) { ImVec2 p(wp.x + g.x * ws.x, wp.y + g.y * ws.y); d->AddRectFilled(p, ImVec2(p.x + 1.3f, p.y + 1.3f), gc); }
    }
}


// ============================================================================
//  Иконки боковой панели
// ============================================================================
static void DrawNavIcon(ImDrawList* d, int i, ImVec2 c, ImU32 col) {
    const float TH = 1.7f;
    if (i == 0) {          // загрузка: стрелка в лоток
        d->AddLine(ImVec2(c.x, c.y - 11), ImVec2(c.x, c.y + 3), col, TH);
        d->AddLine(ImVec2(c.x - 6, c.y - 3), ImVec2(c.x, c.y + 3), col, TH);
        d->AddLine(ImVec2(c.x + 6, c.y - 3), ImVec2(c.x, c.y + 3), col, TH);
        d->AddLine(ImVec2(c.x - 10, c.y + 8), ImVec2(c.x + 10, c.y + 8), col, TH);
        d->AddLine(ImVec2(c.x - 10, c.y + 4), ImVec2(c.x - 10, c.y + 8), col, TH);
        d->AddLine(ImVec2(c.x + 10, c.y + 4), ImVec2(c.x + 10, c.y + 8), col, TH);
    }
    else if (i == 1) {     // файлы: стопка строк
        for (int k = 0; k < 3; ++k) {
            float y = c.y - 8.f + k * 8.f;
            d->AddCircleFilled(ImVec2(c.x - 10, y), 1.6f, col, 10);
            d->AddLine(ImVec2(c.x - 5, y), ImVec2(c.x + 11, y), col, TH);
        }
    }
    else if (i == 2) {     // три регулятора
        const float ys[3] = { -7.f, 0.f, 7.f }, ks[3] = { -4.f, 5.f, -1.f };
        for (int k = 0; k < 3; ++k) {
            d->AddLine(ImVec2(c.x - 11, c.y + ys[k]), ImVec2(c.x + 11, c.y + ys[k]), col, TH);
            d->AddCircleFilled(ImVec2(c.x + ks[k], c.y + ys[k]), 3.4f, Col(g_P.bg), 16);
            d->AddCircle(ImVec2(c.x + ks[k], c.y + ys[k]), 3.4f, col, 16, TH);
        }
    }
    else if (i == 3) {     // помощь: информация
        d->AddCircle(c, 11.5f, col, 28, TH);
        d->AddCircleFilled(ImVec2(c.x, c.y - 5), 1.5f, col, 10);
        d->AddLine(ImVec2(c.x, c.y - 1), ImVec2(c.x, c.y + 6), col, TH);
    }
    else {                 // о программе: сердце
        ImVec2 pts[36];
        for (int k = 0; k < 36; ++k) {
            float t = k / 36.f * 6.2831853f, s = sinf(t);
            float x = 16.f * s * s * s, y = -(13.f * cosf(t) - 5.f * cosf(2 * t) - 2.f * cosf(3 * t) - cosf(4 * t));
            pts[k] = ImVec2(c.x + x * 0.68f, c.y + y * 0.68f + 1.f);
        }
        d->AddPolyline(pts, 36, col, ImDrawFlags_Closed, TH);
    }
}

static void DrawTitleBar(ImDrawList* d, ImVec2 wp, ImVec2 ws, float tm) {
    const float H = 44.f;
    d->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + H), Col(g_P.bg, 0.85f), Col(g_P.bg, 0.85f), Col(g_P.bg, 0.f), Col(g_P.bg, 0.f));
    d->AddLine(ImVec2(wp.x, wp.y + H), ImVec2(wp.x + ws.x, wp.y + H), Col(g_P.border), 1.f);
    // знак: затмение с орбитой
    ImVec2 lc(wp.x + 32.f, wp.y + 22.f);
    d->AddCircleFilled(lc, 7.5f, Col(ACCENT), 24);
    d->AddCircleFilled(ImVec2(lc.x + 2.6f, lc.y - 1.6f), 7.f, Col(g_P.bg), 24);
    float oa = tm * 1.1f;
    d->AddCircle(lc, 12.5f, Col(AccentT(), 0.35f), 32, 1.f);
    d->AddCircleFilled(ImVec2(lc.x + cosf(oa) * 12.5f, lc.y + sinf(oa) * 12.5f), 1.7f, Col(AccentT()), 10);
    float w1 = DLabel(d, ImVec2(wp.x + 58, wp.y + 14), Col(g_P.text), "GRABBER", 3.4f);
    DLabel(d, ImVec2(wp.x + 58 + w1 + 10, wp.y + 14), Col(g_P.dim), "SMART", 3.4f);

    float bx = wp.x + ws.x - 132.f;
    for (int i = 0; i < 3; ++i) {
        ImVec2 p(bx + i * 44.f, wp.y);
        ImGui::SetCursorScreenPos(p); char id[8]; snprintf(id, sizeof(id), "##wb%d", i);
        bool clk = Fx(ImGui::InvisibleButton(id, ImVec2(44, 44))); bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
        float k = st->GetFloat(gid, 0.f); k = Lerp(k, hov ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 14.f)); st->SetFloat(gid, k);
        ImVec2 cc(p.x + 22.f, p.y + 22.f); bool isClose = (i == 2);
        ImVec4 hue = isClose ? Rgb(232, 62, 78) : ACCENT;
        if (k > 0.01f) {   // подложка + мягкое свечение
            for (int g = 1; g <= 6; ++g) d->AddRectFilled(ImVec2(cc.x - 14 - g, cc.y - 14 - g), ImVec2(cc.x + 14 + g, cc.y + 14 + g), Col(hue, 0.03f * k * (1.f - g / 7.f)), 9.f + g);
            d->AddRectFilled(ImVec2(cc.x - 14, cc.y - 14), ImVec2(cc.x + 14, cc.y + 14), Col(hue, (isClose ? 0.85f : 0.16f) * k), 8.f);
            d->AddRect(ImVec2(cc.x - 14, cc.y - 14), ImVec2(cc.x + 14, cc.y + 14), Col(hue, 0.55f * k), 8.f, 0, 1.f);
        }
        ImU32 ic = Col(LerpColor(g_P.text, (isClose ? WHITE : AccentT()), k), 0.78f + 0.22f * k);
        const float TH = 1.5f;
        if (i == 0) {          // свернуть: линия растёт и опускается
            float hw = 5.5f + 1.5f * k, yy = cc.y + 3.f * k - 1.5f * (1.f - k);
            d->AddLine(ImVec2(cc.x - hw, yy), ImVec2(cc.x + hw, yy), ic, TH + 0.3f);
        }
        else if (i == 1) {     // развернуть / восстановить
            if (IsZoomed(g_hwnd)) {
                d->AddRect(ImVec2(cc.x - 5.f, cc.y - 2.f), ImVec2(cc.x + 3.f, cc.y + 6.f), ic, 1.5f, 0, TH);
                d->AddLine(ImVec2(cc.x - 2.f, cc.y - 5.f), ImVec2(cc.x + 6.f, cc.y - 5.f), ic, TH); d->AddLine(ImVec2(cc.x + 6.f, cc.y - 5.f), ImVec2(cc.x + 6.f, cc.y + 3.f), ic, TH);
                d->AddLine(ImVec2(cc.x - 2.f, cc.y - 5.f), ImVec2(cc.x - 2.f, cc.y - 2.f), ic, TH);
            }
            else { float e = 5.5f + 1.2f * k; d->AddRect(ImVec2(cc.x - e, cc.y - e), ImVec2(cc.x + e, cc.y + e), ic, 2.f, 0, TH); }
        }
        else {                 // закрыть: крест поворачивается на 90°
            float ang = k * 1.5708f, s = 5.8f;
            for (int q = 0; q < 2; ++q) {
                float a = 0.7854f + q * 1.5708f + ang;
                d->AddLine(ImVec2(cc.x - cosf(a) * s, cc.y - sinf(a) * s), ImVec2(cc.x + cosf(a) * s, cc.y + sinf(a) * s), ic, TH + 0.1f);
            }
        }
        if (clk) { if (i == 0) ShowWindow(g_hwnd, SW_MINIMIZE); else if (i == 1) ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); else SendMessage(g_hwnd, WM_CLOSE, 0, 0); }
    }
}

static void DrawSidebar(ImDrawList* d, ImVec2 wp, ImVec2 ws, float dt) {
    const float TB = 44.f, SB = 84.f;
    ImVec2 a(wp.x, wp.y + TB), b(wp.x + SB, wp.y + ws.y);
    d->AddRectFilledMultiColor(a, b, Col(g_P.bg, 0.78f), Col(g_P.bg, 0.45f), Col(g_P.bg, 0.45f), Col(g_P.bg, 0.78f));
    d->AddLine(ImVec2(b.x, a.y), b, Col(g_P.border), 1.f);
    const char* tips[5] = { "ЗАГРУЗКА", "ФАЙЛЫ", "НАСТРОЙКИ", "ПОМОЩЬ", "О ПРОГРАММЕ" };
    for (int i = 0; i < 5; ++i) {
        ImVec2 p(wp.x + 14.f, a.y + 22.f + i * 68.f);
        ImGui::SetCursorScreenPos(p); char id[8]; snprintf(id, sizeof(id), "##nav%d", i);
        bool nclk = ImGui::InvisibleButton(id, ImVec2(56, 56)); Fx(nclk && g_Page != i);
        if (nclk && g_Page != i) { g_Page = i; g_PageAlpha = 0.f; }
        bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        g_NavHover[i] = Lerp(g_NavHover[i], hov ? 1.f : 0.f, std::min(1.f, dt * 14.f));
        bool act = (g_Page == i);
        ImVec2 cc(p.x + 28, p.y + 28);
        if (act) {
            d->AddRectFilledMultiColor(ImVec2(wp.x, p.y), ImVec2(wp.x + SB - 1, p.y + 56), Col(ACCENT, 0.16f), Col(ACCENT, 0.f), Col(ACCENT, 0.f), Col(ACCENT, 0.16f));
            d->AddRectFilled(ImVec2(wp.x, p.y + 10), ImVec2(wp.x + 2.5f, p.y + 46), Col(ACCENT), 1.f);
            for (int k = 1; k <= 6; ++k) d->AddCircleFilled(cc, 14.f + k * 3.f, Col(ACCENT, 0.018f), 24);
        }
        else if (g_NavHover[i] > 0.01f) d->AddRectFilled(ImVec2(wp.x + 10, p.y + 4), ImVec2(wp.x + SB - 10, p.y + 52), Col(g_P.panel2, g_NavHover[i]), 4.f);
        DrawNavIcon(d, i, cc, act ? Col(AccentT()) : Col(g_P.dim, 0.75f + 0.25f * g_NavHover[i]));
        if (g_NavHover[i] > 0.02f) {   // свой tooltip: выезжает справа от иконки
            ImDrawList* fd = ImGui::GetForegroundDrawList(); float k = Smooth(g_NavHover[i]);
            std::string t = tips[i]; ImVec2 ts(TrackWidth(g_FontMono, t.c_str(), 1.6f), 14.f);
            ImVec2 ta(wp.x + SB + 6.f + (1.f - k) * -8.f, p.y + 28.f - 15.f), tb(ta.x + ts.x + 26.f, ta.y + 30.f);
            float sv = g_A; g_A = k;
            fd->AddRectFilled(ta, tb, Col(g_P.solid), 4.f); fd->AddRect(ta, tb, Col(g_P.border, 1.8f), 4.f);
            fd->AddRectFilled(ImVec2(ta.x, ta.y + 7), ImVec2(ta.x + 2.5f, tb.y - 7), Col(ACCENT), 1.f);
            std::string up = t; DLabel(fd, ImVec2(ta.x + 14, ta.y + 8), Col(g_P.text), up.c_str(), 1.6f);
            g_A = sv;
        }
    }
    DLabelC(d, wp.x + SB * 0.5f, b.y - 32.f, Col(g_P.dim, 0.7f), "V1.0", 1.5f);
}

static void DrawToasts(float dt) {
    ImDrawList* d = ImGui::GetForegroundDrawList(); ImGuiViewport* vp = ImGui::GetMainViewport();
    std::lock_guard<std::mutex> lock(g_UIMutex);
    if (!g_ToastsOn) { g_Toasts.clear(); return; }
    float y = vp->WorkPos.y + vp->WorkSize.y - 24.f; float rx = vp->WorkPos.x + vp->WorkSize.x - 24.f;
    g_A = 1.f;
    for (auto it = g_Toasts.begin(); it != g_Toasts.end(); ) {
        float age = 3.f - it->timer; if (age < 0) age = 0;
        float in = Smooth(std::min(1.f, age / 0.25f)), out = std::min(1.f, it->timer / 0.4f), al = in * out;
        ImVec2 ts = TSize(g_FontBody, it->text.c_str());
        float w = ts.x + 60.f, h = 46.f; float slide = (1.f - in) * 40.f;
        ImVec2 a(rx - w + slide, y - h), b(rx + slide, y);
        for (int i = 1; i <= 8; ++i) d->AddRectFilled(ImVec2(a.x - i, a.y - i + 3), ImVec2(b.x + i, b.y + i + 3), Col(ImVec4(0, 0, 0, 0.05f), al * (1.f - i / 9.f)), 4.f + i);
        d->AddRectFilled(a, b, Col(g_P.solid, al), 4.f);
        d->AddRect(a, b, Col(g_P.border, 1.6f * al), 4.f, 0, 1.f);
        d->AddRectFilled(a, ImVec2(a.x + 3.f, b.y), Col(ACCENT, al), 2.f);
        float prog = std::clamp(it->timer / 3.f, 0.f, 1.f);
        d->AddRectFilled(ImVec2(a.x + 3.f, b.y - 2.f), ImVec2(a.x + 3.f + (w - 3.f) * prog, b.y), Col(ACCENT, 0.7f * al), 0.f);
        ImGui::PushFont(g_FontBody); d->AddText(ImVec2(a.x + 22, a.y + (h - ts.y) * 0.5f - 1.f), Col(g_P.text, al), it->text.c_str()); ImGui::PopFont();
        y -= h + 10.f;
        it->timer -= dt;
        if (it->timer <= 0.f) it = g_Toasts.erase(it); else ++it;
    }
}

// ============================================================================
//  Заставка запуска (фейковая загрузка)
// ============================================================================
static float g_SplashT = 0.f;
static const float SPLASH_LEN = 3.3f, SPLASH_FADE = 0.7f;
static bool SplashActive() { return g_SplashT < SPLASH_LEN + SPLASH_FADE; }
static void DrawSplash(ImDrawList* d, ImVec2 wp, ImVec2 ws, float dt) {
    if (!SplashActive()) return;
    g_SplashT += dt;
    ImGuiIO& io = ImGui::GetIO();
    if (g_SplashT < SPLASH_LEN - 0.2f && g_SplashT > 0.5f && (ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter))) g_SplashT = SPLASH_LEN - 0.2f;
    float t = g_SplashT, fade = t > SPLASH_LEN ? 1.f - Smooth(std::min(1.f, (t - SPLASH_LEN) / SPLASH_FADE)) : 1.f;
    float zoom = 1.f + (1.f - fade) * 0.9f;                           // при исчезновении камера «влетает» в дыру
    float prog = Smooth(std::min(1.f, t / (SPLASH_LEN - 0.35f)));
    float fadeIn = Smooth(std::min(1.f, t / 0.6f));
    float savedA = g_A; g_A = fade;
    DrawBackground(d, wp, ws, t, dt);
    g_A = fade * fadeIn;
    ImVec2 ctr(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f - 62.f);
    float R = 58.f * zoom;
    d->PushClipRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), true);
    DrawBlackHole(d, ctr, R, t * 1.2f, 0.25f + 0.75f * prog);
    d->PopClipRect();
    float ty = ctr.y + 150.f;
    const char* name = "GRABBER"; float nw = TrackWidth(g_FontBig, name, 16.f);
    DTrack(d, g_FontBig, ImVec2(ctr.x - nw * 0.5f, ty), Col(g_P.text), name, 16.f);
    const char* sub = "S M A R T"; DLabelC(d, ctr.x, ty + 66.f, Col(AccentT()), sub, 5.f);
    // шкала
    float bw = 340.f, bx = ctr.x - bw * 0.5f, by = ty + 112.f;
    const char* stage = prog < 0.25f ? "ИНИЦИАЛИЗАЦИЯ ЯДРА" : prog < 0.50f ? "ЗАГРУЗКА ШРИФТОВ" : prog < 0.75f ? "СИНТЕЗ ЗВУКОВ" : prog < 0.97f ? "ПОДГОТОВКА ИНТЕРФЕЙСА" : "ГОТОВО";
    DLabelC(d, ctr.x, by - 28.f, Col(g_P.dim), stage, 2.4f);
    d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 2.f), Col(g_P.text, 0.14f), 1.f);
    for (int i = 0; i <= 20; ++i) { float x = bx + bw * i / 20.f; d->AddLine(ImVec2(x, by + 6.f), ImVec2(x, by + (i % 5 == 0 ? 12.f : 9.f)), Col(g_P.dim, i / 20.f <= prog ? 0.7f : 0.25f), 1.f); }
    d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * prog, by + 2.f), Col(ACCENT), 1.f);
    for (int i = 1; i <= 8; ++i) d->AddCircleFilled(ImVec2(bx + bw * prog, by + 1.f), 2.f + i * 2.4f, Col(ACCENT, 0.05f), 20);
    char pc[16]; snprintf(pc, sizeof(pc), "%d%%", (int)(prog * 100.f + 0.5f)); DLabelC(d, ctr.x, by + 22.f, Col(AccentT()), pc, 2.0f);
    if (t > 0.8f && t < SPLASH_LEN - 0.2f) DLabelC(d, ctr.x, wp.y + ws.y - 46.f, Col(g_P.dim, 0.55f + 0.2f * sinf(t * 3.f)), "НАЖМИТЕ, ЧТОБЫ ПРОПУСТИТЬ", 2.0f);
    g_A = savedA; (void)io;
}


// ============================================================================
//  Состояние приложения
// ============================================================================
enum Phase { PH_TOOLS, PH_IDLE, PH_ANALYZING, PH_READY, PH_DOWNLOADING };
static std::atomic<int>  g_Phase{ PH_TOOLS };
static std::atomic<bool> g_Cancel{ false };
static std::mutex g_M;                                   // защищает всё ниже
static std::string g_ToolStatus = "ПРОВЕРКА ИНСТРУМЕНТОВ", g_ToolErr;
static std::shared_ptr<const Grab::VideoInfo>            g_Info;
static std::shared_ptr<const std::vector<Grab::Choice>>  g_Choices;
static std::string g_InfoUrl;
static Grab::Progress g_Prog;
static int  g_Sel = 0;
static bool g_Manual = false;
static std::string g_ManV, g_ManA;
struct DoneItem { std::string path, title; uint64_t size; double sec; };
static std::vector<DoneItem> g_Done;

static fs::path g_Dir, g_DlDir;
static int   g_Fragments = 8;
static char  g_UrlBuf[2048] = "";
static float g_ProgShown = 0.f, g_Energy = 0.f;
static std::atomic<bool> g_Updating{ false };
static bool g_Accepted = false;                           // пользователь принял отказ от ответственности

struct Ctx { ImDrawList* d; ImVec2 o; ImVec2 sz; float time; float dt; };

// ============================================================================
//  Настройки (ini рядом с exe)
// ============================================================================
static fs::path SettingsPath() { return Grab::ExeDir() / L"grabber_settings.ini"; }
static void SaveSettings() {
    std::ofstream f(SettingsPath(), std::ios::binary); if (!f) return;
    f << "light=" << (g_IsLightTheme ? 1 : 0) << "\naccent=" << g_AccentIdx << "\nanimbg=" << (g_AnimBg ? 1 : 0)
        << "\nparticles=" << (g_ParticlesOn ? 1 : 0) << "\ntoasts=" << (g_ToastsOn ? 1 : 0) << "\nautoopen=" << (g_AutoOpen ? 1 : 0)
        << "\naccepted=" << (g_Accepted ? 1 : 0) << "\nsound=" << (g_SoundOn ? 1 : 0) << "\nfragments=" << g_Fragments << "\ndir=" << Grab::PathToUtf8(g_DlDir) << "\n";
}
static void LoadSettings() {
    g_DlDir = g_Dir / L"Downloads";
    std::ifstream f(SettingsPath(), std::ios::binary); std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        size_t e = line.find('='); if (e == std::string::npos) continue;
        std::string k = line.substr(0, e), val = line.substr(e + 1); int v = atoi(val.c_str());
        if (k == "light") { g_IsLightTheme = v != 0; g_ThemeAnim = v ? 1.f : 0.f; }
        else if (k == "accent") g_AccentIdx = std::clamp(v, 0, 4);
        else if (k == "animbg") g_AnimBg = v != 0;
        else if (k == "particles") g_ParticlesOn = v != 0;
        else if (k == "toasts") g_ToastsOn = v != 0;
        else if (k == "autoopen") g_AutoOpen = v != 0;
        else if (k == "accepted") g_Accepted = v != 0;
        else if (k == "sound") g_SoundOn = v != 0;
        else if (k == "fragments") g_Fragments = std::clamp(v, 1, 16);
        else if (k == "dir" && !val.empty()) g_DlDir = fs::path(Grab::Utf8ToWide(val));
    }
    ACCENT = g_Accents[g_AccentIdx].a; ACCENT_HI = g_Accents[g_AccentIdx].hi;
}

// ============================================================================
//  Вспомогательное
// ============================================================================
static std::string Clip(const std::string& s, size_t maxChars) {
    size_t chars = 0, i = 0;
    while (i < s.size() && chars < maxChars) { i += (size_t)U8Len((unsigned char)s[i]); ++chars; }
    return i >= s.size() ? s : s.substr(0, i) + "…";
}
static void RevealInExplorer(const std::string& pathUtf8) {
    std::wstring args = L"/select,\"" + Grab::Utf8ToWide(pathUtf8) + L"\"";
    ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}
static void OpenFolder(const fs::path& p) { std::error_code ec; fs::create_directories(p, ec); ShellExecuteW(NULL, L"open", p.c_str(), NULL, NULL, SW_SHOWNORMAL); }
static std::wstring BrowseFolder(HWND owner) {
    BROWSEINFOW bi{}; bi.hwndOwner = owner; bi.lpszTitle = L"Папка для загрузок"; bi.ulFlags = 0x0001 /*BIF_RETURNONLYDIRS*/ | 0x0040 /*BIF_NEWDIALOGSTYLE*/;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi); if (!pidl) return L"";
    wchar_t buf[MAX_PATH * 2] = {}; std::wstring r; if (SHGetPathFromIDListW(pidl, buf)) r = buf;
    CoTaskMemFree(pidl); return r;
}
// Принимает «youtu.be/xxx» без схемы; пустая строка = не ссылка
static std::string NormalizeUrl(std::string s) {
    s = Grab::Trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = Grab::Trim(s.substr(1, s.size() - 2));
    std::string l = Grab::ToLower(s);
    if (l.empty()) return "";
    if (l.rfind("http://", 0) == 0 || l.rfind("https://", 0) == 0) return s;
    if (s.find(' ') == std::string::npos && s.find('.') != std::string::npos) return "https://" + s;
    return "";
}
static bool ComposeManual(const std::vector<Grab::Fmt>& f, const std::string& V, const std::string& A, std::string& sel) {
    if (V.empty() && A.empty()) return false;
    if (V.empty()) { sel = A; return true; }
    auto it = std::find_if(f.begin(), f.end(), [&](const Grab::Fmt& x) { return x.id == V; });
    if (it != f.end() && it->hasAudio()) { sel = V; return true; }      // уже со звуком
    sel = A.empty() ? V + "+ba" : V + "+" + A; return true;
}
static const char* PhaseText(int phase, const Grab::Progress& p, bool hasFile, const std::string& tool, bool toolErr) {
    if (phase == PH_TOOLS) { static std::string s; s = toolErr ? "ОШИБКА УСТАНОВКИ" : tool; return s.c_str(); }
    if (phase == PH_ANALYZING) return "АНАЛИЗ ПОТОКОВ";
    if (phase == PH_DOWNLOADING) {
        if (p.phase == 1) return "СКЛЕЙКА В MP4";
        if (p.phase == 2) return "КОНВЕРТАЦИЯ В MP3";
        if (p.streams == 2 && p.stream == 2) return "ЗАГРУЗКА ЗВУКА";
        return p.streams == 2 ? "ЗАГРУЗКА ВИДЕО" : "ЗАГРУЗКА";
    }
    if (phase == PH_READY) return "ГОТОВ К ЗАГРУЗКЕ";
    return hasFile ? "ГОТОВ" : "ОЖИДАНИЕ ССЫЛКИ";
}

// ============================================================================
//  Рабочие потоки
// ============================================================================
static void StartTools() {
    g_Phase = PH_TOOLS;
    { std::lock_guard<std::mutex> l(g_M); g_ToolErr.clear(); g_ToolStatus = "ПРОВЕРКА ИНСТРУМЕНТОВ"; }
    std::thread([]() {
        std::string err;
        bool ok = Grab::EnsureTools(g_Dir, [](const std::string& s) { std::lock_guard<std::mutex> l(g_M); g_ToolStatus = s; }, err);
        std::lock_guard<std::mutex> l(g_M);
        if (ok) g_Phase = PH_IDLE; else { g_ToolErr = err; g_ToolStatus = "ОШИБКА"; }
        }).detach();
}

static void StartAnalyze(std::string url) {
    g_Phase = PH_ANALYZING;
    std::thread([url]() {
        Grab::VideoInfo vi; std::string err;
        if (Grab::Analyze(g_Dir, url, vi, err)) {
            auto ch = std::make_shared<std::vector<Grab::Choice>>(Grab::BuildChoices(vi.formats));
            auto in = std::make_shared<Grab::VideoInfo>(std::move(vi));
            { std::lock_guard<std::mutex> l(g_M); g_Info = in; g_Choices = ch; g_InfoUrl = url; g_Sel = 0; g_Manual = false; g_ManV.clear(); g_ManA.clear(); g_Prog = Grab::Progress(); }
            g_PendingSnd = SFX_PICK; g_Phase = PH_READY;
        }
        else {
            AddToast("Ошибка: " + Clip(err, 80)); g_PendingSnd = SFX_ERR; g_Phase = PH_IDLE;
        }
        }).detach();
}

static void StartDownload(std::string selector, bool audio, std::string url, std::string title) {
    g_Cancel = false; g_ProgShown = 0.f; g_Phase = PH_DOWNLOADING;
    { std::lock_guard<std::mutex> l(g_M); g_Prog = Grab::Progress(); }
    fs::path dl = g_DlDir; int frags = g_Fragments;
    std::thread([=]() {
        std::error_code ec; fs::create_directories(dl, ec);
        Grab::DownloadResult r = Grab::Download(g_Dir, dl, selector, audio, url, frags,
            [](const Grab::Progress& p) { std::lock_guard<std::mutex> l(g_M); g_Prog = p; }, &g_Cancel);
        if (r.ok) {
            { std::lock_guard<std::mutex> l(g_M); g_Prog.frac = 1.0; g_Prog.speed = 0; }
            std::this_thread::sleep_for(std::chrono::milliseconds(450));                  // даём шкале «добежать» до 100%
            std::string p = r.file.empty() ? std::string() : Grab::PathToUtf8(r.file);
            { std::lock_guard<std::mutex> l(g_M); g_Done.push_back({ p, title, r.size, r.seconds }); }
            AddToast("Загрузка завершена!"); g_PendingSnd = SFX_OK;
            if (g_AutoOpen) { if (!p.empty()) RevealInExplorer(p); else OpenFolder(dl); }
        }
        else if (r.cancelled) { AddToast("Загрузка отменена"); g_PendingSnd = SFX_OFF; }
        else { AddToast("Ошибка: " + Clip(r.error, 80)); g_PendingSnd = SFX_ERR; }
        g_Phase = PH_READY;
        }).detach();
}

static void StartUpdate() {
    if (g_Updating.exchange(true)) return;
    AddToast("Обновляю yt-dlp…");
    std::thread([]() {
        std::string last; bool ok = Grab::UpdateYtDlp(g_Dir, last);
        AddToast(ok ? "yt-dlp: " + Clip(last, 70) : "Не удалось обновить yt-dlp");
        g_PendingSnd = ok ? SFX_OK : SFX_ERR; g_Updating = false;
        }).detach();
}

// ============================================================================
//  Виджеты
// ============================================================================
// Поле ввода: стеклянная панель + системный InputText без собственной рамки
static bool UrlField(ImVec2 pos, float w, char* buf, size_t cap, bool disabled, const char* hint) {
    ImDrawList* d = ImGui::GetWindowDrawList(); const float h = 44.f;
    ImVec2 b(pos.x + w, pos.y + h);
    float fs = g_FontBody->LegacySize, padY = (h - fs) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 14.f, pos.y));
    ImGui::SetNextItemWidth(w - 28.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, g_P.text); ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.40f));
    ImGui::PushFont(g_FontBody, fs);
    bool enter = ImGui::InputText("##url", buf, cap, ImGuiInputTextFlags_EnterReturnsTrue | (disabled ? ImGuiInputTextFlags_ReadOnly : 0));
    bool act = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    ImGui::PopFont(); ImGui::PopStyleColor(5); ImGui::PopStyleVar(2);
    if (hov && !disabled) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    d->AddRectFilled(pos, b, Col(g_P.panel2), 4.f);
    d->AddRect(pos, b, act ? Col(ACCENT, 0.9f) : Col(g_P.border), 4.f, 0, act ? 1.4f : 1.f);
    if (act) for (int i = 1; i <= 6; ++i) d->AddRect(ImVec2(pos.x - i, pos.y - i), ImVec2(b.x + i, b.y + i), Col(ACCENT, 0.035f * (1.f - i / 7.f)), 4.f + i);
    if (buf[0] == 0 && !act) DText(d, g_FontBody, ImVec2(pos.x + 14.f, pos.y + padY), Col(g_P.dim, 0.8f), hint);
    return enter;
}

// Ползунок количества потоков: линейка с делениями на каждое значение
static void FragSlider(ImVec2 pos, float w, int* v, int lo, int hi, bool disabled) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(pos.x - 6.f, pos.y - 14.f));
    ImGui::InvisibleButton("##frag", ImVec2(w + 12.f, 28.f));
    bool act = ImGui::IsItemActive() && !disabled, hov = ImGui::IsItemHovered() && !disabled;
    if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (act) { float t = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / w, 0.f, 1.f); *v = std::clamp(lo + (int)floorf(t * (hi - lo) + 0.5f), lo, hi); }
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
    float hv = st->GetFloat(gid, 0.f); hv = Lerp(hv, (hov || act) ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 12.f)); st->SetFloat(gid, hv);
    float al = disabled ? 0.45f : 1.f, t = (float)(*v - lo) / (hi - lo), hx = pos.x + t * w;
    for (int f = lo; f <= hi; ++f) {
        float x = pos.x + (float)(f - lo) / (hi - lo) * w; bool major = (f % 4 == 0), on = x <= hx;
        d->AddLine(ImVec2(x, pos.y + 6.f), ImVec2(x, pos.y + (major ? 14.f : 10.f)), on ? Col(AccentT(), (major ? 0.8f : 0.45f) * al) : Col(g_P.dim, major ? 0.5f : 0.25f), 1.f);
    }
    d->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y), Col(g_P.text, 0.16f), 2.f);
    d->AddLine(ImVec2(pos.x, pos.y), ImVec2(hx, pos.y), Col(ACCENT, al), 2.5f);
    for (int i = 1; i <= 8; ++i) d->AddCircleFilled(ImVec2(hx, pos.y), 6.f + i * 2.2f, Col(ACCENT, 0.014f * hv * al), 24);
    d->AddRectFilled(ImVec2(hx - 2.f, pos.y - 9.f - hv * 1.5f), ImVec2(hx + 2.f, pos.y + 9.f + hv * 1.5f), Col(LerpColor(ACCENT, ACCENT_HI, hv), al), 2.f);
}

// Строка настроек: заголовок + пояснение слева, переключатель справа
static bool SettingSwitch(ImDrawList* d, const char* id, ImVec2 p, float w, const char* title, const char* desc, bool* v, bool sep = true) {
    if (sep) d->AddLine(ImVec2(p.x + 22, p.y), ImVec2(p.x + w - 22, p.y), Col(g_P.border), 1.f);
    DText(d, g_FontBody, ImVec2(p.x + 22, p.y + 8), Col(g_P.text), title);
    DText(d, g_FontSmall, ImVec2(p.x + 22, p.y + 30), Col(g_P.dim), desc);
    return Switch(id, ImVec2(p.x + w - 22 - 52, p.y + 12), v);
}

static void ApplyAccent(int i) { g_AccentIdx = std::clamp(i, 0, 4); }

// ============================================================================
//  Страница: Загрузка
// ============================================================================
static void PageDownload(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f;
    float x0 = c.o.x + m, y0 = c.o.y + 20.f;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "ЗАГРУЗКА", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Видео в лучшем качестве");

    const float orbW = 300.f, gap = 28.f;
    float leftW = c.sz.x - m * 2 - orbW - gap; if (leftW < 400.f) leftW = 400.f;
    float rx = x0 + leftW + gap, top = y0 + 78.f, bottom = c.o.y + c.sz.y - 24.f;

    int phase = g_Phase;
    std::shared_ptr<const Grab::VideoInfo> info; std::shared_ptr<const std::vector<Grab::Choice>> choices;
    Grab::Progress pr; std::string toolStatus, toolErr, manV, manA; int sel; bool manual; size_t doneCount;
    { std::lock_guard<std::mutex> l(g_M); info = g_Info; choices = g_Choices; pr = g_Prog; toolStatus = g_ToolStatus; toolErr = g_ToolErr; sel = g_Sel; manual = g_Manual; manV = g_ManV; manA = g_ManA; doneCount = g_Done.size(); }
    const bool tools = (phase == PH_TOOLS), analyzing = (phase == PH_ANALYZING), dling = (phase == PH_DOWNLOADING);
    const bool ready = (phase == PH_READY) && info && choices;
    const bool busy = tools || analyzing || dling;
    const std::string normUrl = NormalizeUrl(g_UrlBuf);

    // ---- 01 · Ссылка ------------------------------------------------------
    ImVec2 la(x0, top), lb(x0 + leftW, top + 140);
    Panel(d, la, lb);
    Eyebrow(d, ImVec2(la.x + 22, la.y + 16), "01", "ССЫЛКА");
    if (tools) {
        float cx = (la.x + lb.x) * 0.5f;
        DLabelC(d, cx, la.y + 52, Col(AccentT()), toolErr.empty() ? "ПОДГОТОВКА ИНСТРУМЕНТОВ" : "НЕ УДАЛОСЬ УСТАНОВИТЬ ИНСТРУМЕНТЫ", 2.2f);
        if (toolErr.empty()) {
            DTextC(d, g_FontSmall, cx, la.y + 80, Col(g_P.dim), "Первый запуск: докачиваю yt-dlp, FFmpeg и Deno. Это один раз.");
            float bw = 260.f, bx = cx - bw * 0.5f, by = la.y + 112.f; float k = fmodf(c.time * 0.8f, 1.f);
            d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 2.f), Col(g_P.text, 0.14f), 1.f);
            d->AddRectFilled(ImVec2(bx + (bw - 80.f) * k, by), ImVec2(bx + (bw - 80.f) * k + 80.f, by + 2.f), Col(ACCENT), 1.f);
        }
        else {
            DTextC(d, g_FontSmall, cx, la.y + 78, Col(g_P.dim), Clip(toolErr, 90).c_str());
            if (GhostBtn("##retry", "ПОВТОРИТЬ", ImVec2(cx - 60, la.y + 98), ImVec2(120, 30))) StartTools();
        }
    }
    else {
        if (GhostBtn("##paste", "ВСТАВИТЬ", ImVec2(lb.x - 22 - 92, la.y + 10), ImVec2(92, 26)) && !busy) {
            const char* t = ImGui::GetClipboardText();
            if (t) { snprintf(g_UrlBuf, sizeof(g_UrlBuf), "%s", Grab::Trim(t).c_str()); }
        }
        float fw = leftW - 44.f - 112.f - 10.f;
        bool enter = UrlField(ImVec2(la.x + 22, la.y + 42), fw, g_UrlBuf, sizeof(g_UrlBuf), busy, "Вставьте ссылку на видео…");
        bool canGo = !busy && !normUrl.empty();
        bool go = GhostBtn("##analyze", "АНАЛИЗ", ImVec2(la.x + 22 + fw + 10, la.y + 42), ImVec2(112, 44), canGo);
        if ((go || enter) && canGo) { StartAnalyze(normUrl); }
        else if ((go || enter) && !busy) AddToast("Нужна ссылка вида https://…");

        if (info && (ready || dling)) {
            d->PushClipRect(ImVec2(la.x + 22, la.y + 92), ImVec2(lb.x - 22, lb.y), true);
            DText(d, g_FontBody, ImVec2(la.x + 22, la.y + 94), Col(g_P.text), Clip(info->title, 70).c_str());
            std::string meta = (info->uploader.empty() ? "" : Clip(info->uploader, 30) + "   ·   ") + Grab::HumanDuration(info->duration) + "   ·   МАКС. " + std::to_string(Grab::MaxHeight(info->formats)) + "P";
            DLabel(d, ImVec2(la.x + 22, la.y + 116), Col(AccentT()), meta.c_str(), 1.2f);
            d->PopClipRect();
        }
        else if (analyzing) DLabel(d, ImVec2(la.x + 22, la.y + 104), Col(AccentT(), 0.6f + 0.4f * sinf(c.time * 6.f)), "ЧИТАЮ СПИСОК ФОРМАТОВ…", 1.6f);
        else DLabel(d, ImVec2(la.x + 22, la.y + 104), Col(g_P.dim), "ССЫЛКА НА ВИДЕО  ·  ТОЛЬКО ДЛЯ МАТЕРИАЛОВ, НА КОТОРЫЕ ЕСТЬ ПРАВА", 1.4f);
    }

    // ---- 02 · Качество ------------------------------------------------------
    ImVec2 qa(x0, la.y + 154), qb(x0 + leftW, la.y + 154 + 312);
    Panel(d, qa, qb);
    Eyebrow(d, ImVec2(qa.x + 22, qa.y + 16), "02", "КАЧЕСТВО");
    std::string manSel; bool manOk = false;
    if (ready) {
        if (TabText("##tauto", "АВТО", ImVec2(qb.x - 22 - 150, qa.y + 12), 70.f, !manual) && !dling) { std::lock_guard<std::mutex> l(g_M); g_Manual = false; }
        if (TabText("##tman", "РУЧНОЙ", ImVec2(qb.x - 22 - 76, qa.y + 12), 76.f, manual) && !dling) { std::lock_guard<std::mutex> l(g_M); g_Manual = true; }
        manOk = ComposeManual(info->formats, manV, manA, manSel);
    }
    if (!ready) {
        float cx = (qa.x + qb.x) * 0.5f, cy = qa.y + 150.f;
        Brackets(d, ImVec2(cx - 70, cy - 40), ImVec2(cx + 70, cy + 40), 14.f, Col(g_P.dim, 0.5f), 1.6f);
        d->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), Col(AccentT(), 0.6f), 1.3f); d->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), Col(AccentT(), 0.6f), 1.3f);
        DTextC(d, g_FontBody, cx, cy + 56, Col(g_P.dim), "Сначала вставьте ссылку и нажмите АНАЛИЗ");
    }
    else if (!manual) {
        for (int i = 0; i < (int)choices->size() && i < 6; ++i) {
            const Grab::Choice& ch = (*choices)[(size_t)i];
            ImVec2 rp(qa.x + 12, qa.y + 44 + i * 36.f), rs(leftW - 24, 34);
            ImGui::PushID(i); ImGui::SetCursorScreenPos(rp);
            bool clk = Fx(ImGui::InvisibleButton("##q", rs)); bool hov = ImGui::IsItemHovered(); ImGui::PopID();
            if (hov && !dling) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (clk && !dling) { std::lock_guard<std::mutex> l(g_M); g_Sel = i; sel = i; }
            bool on = (sel == i);
            if (on) { d->AddRectFilledMultiColor(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(ACCENT, 0.14f), Col(ACCENT, 0.02f), Col(ACCENT, 0.02f), Col(ACCENT, 0.14f)); d->AddRectFilled(ImVec2(rp.x, rp.y + 5), ImVec2(rp.x + 2.5f, rp.y + rs.y - 5), Col(ACCENT), 1.f); }
            else if (hov && !dling) d->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(g_P.panel2), 4.f);
            ImVec2 rc(rp.x + 22, rp.y + 17);
            d->AddCircle(rc, 7.f, on ? Col(AccentT()) : Col(g_P.dim, 0.7f), 20, 1.4f);
            if (on) d->AddCircleFilled(rc, 3.4f, Col(ACCENT), 14);
            ImVec2 ts = TSize(g_FontBody, ch.title.c_str());
            DText(d, g_FontBody, ImVec2(rp.x + 42, rp.y + (rs.y - ts.y) * 0.5f), on ? Col(g_P.text) : Col(g_P.text, 0.85f), ch.title.c_str());
            if (!ch.size.empty()) DLabelR(d, rp.x + rs.x - 16, rp.y + 10, on ? Col(AccentT()) : Col(g_P.dim), ch.size.c_str(), 1.2f);
        }
        const Grab::Choice& cur = (*choices)[(size_t)std::clamp(sel, 0, (int)choices->size() - 1)];
        std::string foot = cur.audio ? "ФАЙЛ: MP3 (ЛУЧШЕЕ КАЧЕСТВО ЗВУКА)" : "ФАЙЛ: MP4  ·  ВИДЕО + ЗВУК СКЛЕИВАЮТСЯ АВТОМАТИЧЕСКИ";
        DLabel(d, ImVec2(qa.x + 22, qb.y - 30), Col(g_P.dim), foot.c_str(), 1.2f);
    }
    else {
        // ручной выбор: клик по видео-строке задаёт видео, по аудио-строке — звук
        std::vector<Grab::Fmt> fl = info->formats;
        std::stable_sort(fl.begin(), fl.end(), [](const Grab::Fmt& a, const Grab::Fmt& b) { return std::tie(a.height, a.tbr) > std::tie(b.height, b.tbr); });
        const float rowH = 26.f, listH = 8 * rowH; ImVec2 la0(qa.x + 12, qa.y + 44), la1(qb.x - 12, qa.y + 44 + listH);
        static float scroll = 0.f; float maxScroll = std::max(0.f, (float)fl.size() * rowH - listH);
        if (ImGui::IsMouseHoveringRect(la0, la1)) scroll -= ImGui::GetIO().MouseWheel * 52.f;
        scroll = std::clamp(scroll, 0.f, maxScroll);
        DLabel(d, ImVec2(la0.x + 10, la0.y - 14), Col(g_P.dim, 0.9f), "ID", 1.0f);
        d->PushClipRect(la0, la1, true);
        for (int i = 0; i < (int)fl.size(); ++i) {
            float y = la0.y + i * rowH - scroll; if (y + rowH < la0.y || y > la1.y) continue;
            const Grab::Fmt& f = fl[(size_t)i]; ImVec2 rp(la0.x, y), rs(la1.x - la0.x, rowH - 2);
            ImGui::PushID(i); ImGui::SetCursorScreenPos(rp);
            bool clk = ImGui::InvisibleButton("##f", rs); bool hov = ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(la0, la1); ImGui::PopID();
            bool isV = f.hasVideo(), isA = !isV;
            bool on = (isV && f.id == manV) || (isA && f.id == manA);
            if (clk && !dling && ImGui::IsMouseHoveringRect(la0, la1)) {
                Snd(SFX_CLICK); std::lock_guard<std::mutex> l(g_M);
                if (isV) { g_ManV = (g_ManV == f.id) ? "" : f.id; if (f.hasAudio()) g_ManA.clear(); }
                else g_ManA = (g_ManA == f.id) ? "" : f.id;
            }
            if (on) d->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(ACCENT, 0.16f), 3.f);
            else if (hov) d->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(g_P.panel2), 3.f);
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImU32 tc = on ? Col(AccentT()) : Col(g_P.text, 0.85f), dc = on ? Col(AccentT(), 0.85f) : Col(g_P.dim);
            float ty = rp.y + 5.f;
            DLabel(d, ImVec2(rp.x + 10, ty), tc, f.id.c_str(), 1.0f);
            std::string res = isV ? (f.width > 0 ? std::to_string(f.width) + "x" + std::to_string(f.height) : std::to_string(f.height) + "p") : "AUDIO";
            DLabel(d, ImVec2(rp.x + 78, ty), tc, res.c_str(), 1.0f);
            std::string fps = (isV && f.fps > 0) ? Grab::Fmtf("%.0f", f.fps) : std::string("-");
            DLabel(d, ImVec2(rp.x + 168, ty), dc, fps.c_str(), 1.0f);
            std::string codec = (isV ? f.vcodec : f.acodec); size_t dot = codec.find('.'); if (dot != std::string::npos) codec = codec.substr(0, dot);
            DLabel(d, ImVec2(rp.x + 208, ty), dc, (codec + (isV && f.hasAudio() ? " +A" : "")).c_str(), 1.0f);
            DLabel(d, ImVec2(rp.x + 298, ty), dc, f.ext.c_str(), 1.0f);
            DLabelR(d, rp.x + rs.x - 14, ty, dc, Grab::HumanSize(f.size).c_str(), 1.0f);
        }
        d->PopClipRect();
        if (maxScroll > 0.f) { float th = std::max(24.f, listH * listH / (listH + maxScroll)), ty0 = la0.y + (listH - th) * (scroll / maxScroll); d->AddRectFilled(ImVec2(la1.x + 2, ty0), ImVec2(la1.x + 4, ty0 + th), Col(AccentT(), 0.6f), 1.f); }
        std::string foot = "ВИДЕО: " + (manV.empty() ? std::string("—") : manV) + "     ЗВУК: " + (manA.empty() ? std::string("авто") : manA);
        DLabel(d, ImVec2(qa.x + 22, qb.y - 30), manOk ? Col(AccentT()) : Col(g_P.dim), foot.c_str(), 1.2f);
        if (!dling && (!manV.empty() || !manA.empty()) && GhostBtn("##manclr", "СБРОС", ImVec2(qb.x - 22 - 80, qb.y - 38), ImVec2(80, 26))) { std::lock_guard<std::mutex> l(g_M); g_ManV.clear(); g_ManA.clear(); }
    }

    // ---- Кнопка загрузки -------------------------------------------------
    ImVec2 bp(x0, qb.y + 14); float bw = dling ? leftW - 126.f : leftW; ImVec2 bsz(bw, 54);
    bool canDl = ready && (manual ? manOk : true);
    int bstate = dling ? 2 : (canDl ? 1 : 0);
    g_ProgShown = dling ? Lerp(g_ProgShown, (float)pr.frac, std::min(1.f, c.dt * 6.f)) : 0.f;
    char runLabel[48]; if (dling) snprintf(runLabel, sizeof(runLabel), "ЗАГРУЗКА  %d%%", (int)(g_ProgShown * 100.f + 0.5f));
    if (PrimaryButton("##run", dling ? runLabel : "СКАЧАТЬ", bp, bsz, bstate, c.time, g_ProgShown) && canDl) {
        std::string selector; bool audio = false;
        if (manual) selector = manSel; else { const Grab::Choice& ch = (*choices)[(size_t)std::clamp(sel, 0, (int)choices->size() - 1)]; selector = ch.selector; audio = ch.audio; }
        Snd(SFX_RUN); StartDownload(selector, audio, g_InfoUrl, info->title);
    }
    if (dling && GhostBtn("##cancel", "ОТМЕНА", ImVec2(bp.x + bw + 14, bp.y), ImVec2(112, 54))) g_Cancel = true;

    // ---- 03 · Папка -------------------------------------------------------
    ImVec2 fa(x0, bp.y + 54 + 14), fb(x0 + leftW, bottom);
    if (fb.y - fa.y >= 58.f) {
        fb.y = std::min(fb.y, fa.y + 70.f);
        Panel(d, fa, fb);
        Eyebrow(d, ImVec2(fa.x + 22, fa.y + 12), "03", "СОХРАНЯТЬ В");
        d->PushClipRect(ImVec2(fa.x + 22, fa.y), ImVec2(fb.x - 140, fb.y), true);
        DText(d, g_FontSmall, ImVec2(fa.x + 22, fa.y + 36), Col(g_P.text), Grab::PathToUtf8(g_DlDir).c_str());
        d->PopClipRect();
        if (GhostBtn("##openfolder", "ОТКРЫТЬ", ImVec2(fb.x - 22 - 104, fa.y + (fb.y - fa.y - 30) * 0.5f), ImVec2(104, 30))) OpenFolder(g_DlDir);
    }

    // ---- Правая колонка: сингулярность ------------------------------------
    g_Energy = Lerp(g_Energy, busy ? 1.f : 0.f, std::min(1.f, c.dt * 2.2f));
    ImVec2 oa(rx, top - 6), ob(rx + orbW, top + 294);
    ImVec2 ctr(oa.x + orbW * 0.5f, oa.y + 146.f);
    d->PushClipRect(ImVec2(oa.x - 14, oa.y), ImVec2(ob.x + 14, ob.y + 6), true);
    float R = 46.f + sinf(c.time * 1.3f) * 0.8f;
    DrawBlackHole(d, ctr, R, c.time, g_Energy, 3.1f);
    if (busy && g_ParticlesOn) {
        InitParticles();
        for (auto& p : g_Particles) {
            float spd = dling ? std::clamp((float)(pr.speed / 4.0e6), 0.5f, 2.2f) : 1.f;     // чем быстрее канал, тем быстрее падает материя
            p.angle += p.speed * 40.f * c.dt * spd; p.radius -= p.speed * 1100.f * c.dt * spd; p.alpha -= 110.f * c.dt;
            if (p.radius < R * 1.1f || p.alpha <= 0) { p.angle = (rand() % 360) * 3.14159f / 180.f; p.radius = p.base_radius * 0.78f + (rand() % 30); p.alpha = 255.f; }
            auto proj = [&](float ang, float rad) { return ImVec2(ctr.x + cosf(ang) * rad, ctr.y + sinf(ang) * rad * 0.28f); };
            ImVec2 pp = proj(p.angle, p.radius), tail = proj(p.angle - 0.14f, p.radius + 5.f); float al = p.alpha / 255.f;
            d->AddLine(pp, tail, Col(ACCENT_HI, al * 0.45f), 1.2f);
            d->AddCircleFilled(pp, p.size * 0.55f, Col(Rgb(255, 240, 214), al), 8);
        }
    }
    else if (!busy) g_Particles.clear();
    d->PopClipRect();

    const char* st = PhaseText(phase, pr, false, toolStatus, !toolErr.empty());
    float stw = TrackWidth(g_FontMono, st, 2.2f), scx = rx + orbW * 0.5f;
    float pulse = busy ? 0.5f + 0.5f * sinf(c.time * 7.f) : (ready ? 1.f : 0.35f + 0.25f * sinf(c.time * 1.8f));
    d->PushClipRect(ImVec2(rx - 20, top + 300), ImVec2(rx + orbW + 20, top + 345), true);
    if (stw > orbW - 30.f) { stw = orbW - 30.f; }
    d->AddCircleFilled(ImVec2(scx - stw * 0.5f - 12, top + 322), 3.f, Col(busy || ready ? ACCENT : g_P.dim, pulse), 12);
    DLabel(d, ImVec2(scx - stw * 0.5f, top + 314), busy ? Col(AccentT()) : Col(g_P.text), st, 2.2f);
    if (dling) {
        float bw2 = 220.f, bx = scx - bw2 * 0.5f, by = top + 338.f;
        d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw2, by + 2.f), Col(g_P.text, 0.16f), 1.f);
        d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw2 * g_ProgShown, by + 2.f), Col(ACCENT), 1.f);
        for (int i = 1; i <= 5; ++i) d->AddCircleFilled(ImVec2(bx + bw2 * g_ProgShown, by + 1.f), 2.f + i * 2.f, Col(ACCENT, 0.05f), 16);
    }
    d->PopClipRect();

    // ---- Телеметрия -----------------------------------------------------
    ImVec2 sa(rx, top + 350), sb(rx + orbW, top + 350 + 130);
    Panel(d, sa, sb);
    struct Row { const char* k; std::string v; } rows[4] = {
        { "СКОРОСТЬ", dling ? Grab::HumanSpeed(pr.speed) : "—" },
        { "ОСТАЛОСЬ", dling ? Grab::HumanEta(pr.eta) : "—" },
        { "СКАЧАНО", dling && pr.total > 0 ? Grab::HumanSize(pr.done) + " / " + Grab::HumanSize(pr.total) : "—" },
        { "ГОТОВО ФАЙЛОВ", Grab::Fmtf("%02d", (int)doneCount) } };
    for (int i = 0; i < 4; ++i) {
        float y = sa.y + 14 + i * 28.f;
        if (i > 0) d->AddLine(ImVec2(sa.x + 18, y - 6), ImVec2(sb.x - 18, y - 6), Col(g_P.border), 1.f);
        DLabel(d, ImVec2(sa.x + 18, y + 2), Col(g_P.dim), rows[i].k, 1.4f);
        DTextR(d, g_FontBody, sb.x - 18, y - 2, i == 3 ? Col(AccentT()) : Col(g_P.text), rows[i].v.c_str());
    }
    ImVec2 ta(rx, sb.y + 14), tb(rx + orbW, bottom);
    if (tb.y - ta.y > 84.f) {
        Panel(d, ta, tb);
        Eyebrow(d, ImVec2(ta.x + 18, ta.y + 14), "—", "БЫСТРЫЙ СТАРТ");
        DrawWrapped(d, g_FontSmall, ImVec2(ta.x + 18, ta.y + 38), orbW - 36, Col(g_P.dim), "Вставьте ссылку, нажмите АНАЛИЗ, выберите качество и СКАЧАТЬ. Видео и звук склеятся в один mp4.", 19.f);
    }
}

// ============================================================================
//  Страница: Файлы
// ============================================================================
static void PageFiles(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f;
    float x0 = c.o.x + m, y0 = c.o.y + 20.f, W = c.sz.x - m * 2;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "ФАЙЛЫ", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Готовые загрузки");
    std::vector<DoneItem> done; { std::lock_guard<std::mutex> l(g_M); done = g_Done; }
    ImVec2 a(x0, y0 + 78), b(x0 + W, c.o.y + c.sz.y - 24.f);
    Panel(d, a, b);
    Eyebrow(d, ImVec2(a.x + 22, a.y + 16), "01", "ЭТА СЕССИЯ");
    { char cnt[24]; snprintf(cnt, sizeof(cnt), "%02d", (int)done.size()); DLabelR(d, b.x - 22, a.y + 16, Col(g_P.dim), cnt); }
    if (done.empty()) { DTextC(d, g_FontBody, (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, Col(g_P.dim), "Здесь появятся скачанные видео"); }
    else {
        int maxRows = std::max(1, (int)((b.y - a.y - 56.f) / 58.f));
        for (int k = 0; k < maxRows && k < (int)done.size(); ++k) {
            const DoneItem& it = done[done.size() - 1 - (size_t)k];
            ImVec2 rp(a.x + 12, a.y + 48 + k * 58.f), rs(W - 24, 54);
            ImGui::PushID(k); ImGui::SetCursorScreenPos(rp);
            bool rc = Fx(ImGui::InvisibleButton("##row", rs)); bool rh = ImGui::IsItemHovered(); ImGui::PopID();
            if (rh) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); d->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(g_P.panel2), 4.f); }
            if (k > 0) d->AddLine(ImVec2(rp.x + 8, rp.y - 2), ImVec2(rp.x + rs.x - 8, rp.y - 2), Col(g_P.border), 1.f);
            const ImVec4 ok = Rgb(96, 206, 150);
            d->AddCircle(ImVec2(rp.x + 24, rp.y + 27), 11.f, Col(ok, 0.8f), 24, 1.4f);
            Poly3(d, ImVec2(rp.x + 19, rp.y + 27), ImVec2(rp.x + 23, rp.y + 31), ImVec2(rp.x + 30, rp.y + 22), Col(ok), 1.8f);
            d->PushClipRect(ImVec2(rp.x + 48, rp.y), ImVec2(rp.x + rs.x - 130, rp.y + rs.y), true);
            std::string nm = it.path.empty() ? it.title : FileNameOf(it.path);
            DText(d, g_FontBody, ImVec2(rp.x + 48, rp.y + 6), Col(g_P.text), nm.c_str());
            std::string sub = Grab::HumanSize((double)it.size) + "   ·   " + Grab::Fmtf("%.0f с", it.sec);
            DText(d, g_FontSmall, ImVec2(rp.x + 48, rp.y + 30), Col(g_P.dim), sub.c_str());
            d->PopClipRect();
            DLabelR(d, rp.x + rs.x - 14, rp.y + 20, rh ? Col(AccentT()) : Col(g_P.dim), "В ПАПКЕ", 1.4f);
            if (rc) { if (!it.path.empty()) RevealInExplorer(it.path); else OpenFolder(g_DlDir); }
        }
    }
}

// ============================================================================
//  Страница: Настройки
// ============================================================================
static void PageSettings(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f, gap = 24.f;
    float x0 = c.o.x + m, y0 = c.o.y + 20.f, W = c.sz.x - m * 2, colW = (W - gap) * 0.5f, top = y0 + 78.f;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "НАСТРОЙКИ", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Под себя");
    const bool busy = g_Phase == PH_DOWNLOADING;

    // ---- 01 · Вид -----------------------------------------------------------
    ImVec2 pa(x0, top), pb(x0 + colW, top + 46 + 4 * 62 + 78);
    Panel(d, pa, pb); Eyebrow(d, ImVec2(pa.x + 22, pa.y + 16), "01", "ВНЕШНИЙ ВИД");
    float ry = pa.y + 46;
    bool light = g_IsLightTheme; if (SettingSwitch(d, "##sw_theme", ImVec2(pa.x, ry), colW, "Светлая тема", "Тёплый светлый вариант интерфейса", &light, false)) g_IsLightTheme = light; ry += 62;
    // акцент
    d->AddLine(ImVec2(pa.x + 22, ry), ImVec2(pb.x - 22, ry), Col(g_P.border), 1.f);
    DText(d, g_FontBody, ImVec2(pa.x + 22, ry + 8), Col(g_P.text), "Цвет акцента"); DText(d, g_FontSmall, ImVec2(pa.x + 22, ry + 30), Col(g_P.dim), g_Accents[g_AccentIdx].name);
    for (int i = 0; i < 5; ++i) {
        ImVec2 cc(pb.x - 22 - 12 - (4 - i) * 34.f, ry + 31);
        ImGui::SetCursorScreenPos(ImVec2(cc.x - 14, cc.y - 14)); char id[16]; snprintf(id, sizeof(id), "##ac%d", i);
        bool clk = Fx(ImGui::InvisibleButton(id, ImVec2(28, 28))); bool hv = ImGui::IsItemHovered(); if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (clk) ApplyAccent(i);
        d->AddCircleFilled(cc, hv ? 11.f : 10.f, Col(g_Accents[i].a), 24);
        if (g_AccentIdx == i) d->AddCircle(cc, 15.f, Col(g_P.text, 0.85f), 28, 1.5f);
    }
    ry += 62;
    SettingSwitch(d, "##sw_bg", ImVec2(pa.x, ry), colW, "Анимированный фон", "Звёзды, свечение и метеоры", &g_AnimBg); ry += 62;
    SettingSwitch(d, "##sw_pt", ImVec2(pa.x, ry), colW, "Частицы у дыры", "Падающая материя во время работы", &g_ParticlesOn);

    // ---- 02 · Поведение ---------------------------------------------------
    ImVec2 qa(x0, pb.y + 16), qb(x0 + colW, std::min(c.o.y + c.sz.y - 24.f, pb.y + 16 + 46 + 3 * 62 + 6));
    Panel(d, qa, qb); Eyebrow(d, ImVec2(qa.x + 22, qa.y + 16), "02", "ПОВЕДЕНИЕ");
    ry = qa.y + 46;
    SettingSwitch(d, "##sw_snd", ImVec2(qa.x, ry), colW, "Звуки интерфейса", "Синтезируются на лету, без файлов", &g_SoundOn, false); ry += 62;
    SettingSwitch(d, "##sw_toast", ImVec2(qa.x, ry), colW, "Уведомления", "Всплывающие сообщения справа снизу", &g_ToastsOn); ry += 62;
    SettingSwitch(d, "##sw_open", ImVec2(qa.x, ry), colW, "Показать файл после загрузки", "Откроет папку с выделенным видео", &g_AutoOpen);

    // ---- 03 · Загрузка ----------------------------------------------------
    ImVec2 ra(x0 + colW + gap, top), rb(x0 + W, top + 46 + 118 + 112 + 92);
    Panel(d, ra, rb); Eyebrow(d, ImVec2(ra.x + 22, ra.y + 16), "03", "ЗАГРУЗКА");
    ry = ra.y + 46;
    DText(d, g_FontBody, ImVec2(ra.x + 22, ry + 6), Col(g_P.text), "Потоков загрузки");
    DText(d, g_FontSmall, ImVec2(ra.x + 22, ry + 28), Col(g_P.dim), "Параллельные фрагменты: быстрее, но 16 может давить на сеть");
    { char n[16]; snprintf(n, sizeof(n), "%d", g_Fragments); DTextR(d, g_FontBig, rb.x - 22, ry - 2, Col(AccentT()), n); }
    FragSlider(ImVec2(ra.x + 28, ry + 80), colW - 56, &g_Fragments, 1, 16, busy);
    DLabel(d, ImVec2(ra.x + 28, ry + 96), Col(g_P.dim, 0.8f), "1", 1.2f); DLabelR(d, rb.x - 28, ry + 96, Col(g_P.dim, 0.8f), "16", 1.2f);
    ry += 128;
    d->AddLine(ImVec2(ra.x + 22, ry), ImVec2(rb.x - 22, ry), Col(g_P.border), 1.f);
    DText(d, g_FontBody, ImVec2(ra.x + 22, ry + 8), Col(g_P.text), "Папка загрузки");
    d->PushClipRect(ImVec2(ra.x + 22, ry), ImVec2(rb.x - 22, ry + 62), true);
    DText(d, g_FontSmall, ImVec2(ra.x + 22, ry + 32), Col(g_P.dim), Grab::PathToUtf8(g_DlDir).c_str());
    d->PopClipRect();
    if (GhostBtn("##chdir", "ИЗМЕНИТЬ", ImVec2(rb.x - 22 - 104 - 8 - 104, ry + 62), ImVec2(104, 28)) && !busy) { std::wstring p = BrowseFolder(g_hwnd); if (!p.empty()) g_DlDir = fs::path(p); }
    if (GhostBtn("##opdir", "ОТКРЫТЬ", ImVec2(rb.x - 22 - 104, ry + 62), ImVec2(104, 28))) OpenFolder(g_DlDir);
    ry += 112;
    d->AddLine(ImVec2(ra.x + 22, ry), ImVec2(rb.x - 22, ry), Col(g_P.border), 1.f);
    DText(d, g_FontBody, ImVec2(ra.x + 22, ry + 8), Col(g_P.text), "Движок yt-dlp");
    DText(d, g_FontSmall, ImVec2(ra.x + 22, ry + 30), Col(g_P.dim), "Сайты часто меняются. Если стало не качать — обновите.");
    if (GhostBtn("##upd", g_Updating ? "ОБНОВЛЯЮ…" : "ОБНОВИТЬ", ImVec2(rb.x - 22 - 130, ry + 52), ImVec2(130, 28)) && !g_Updating && !busy && g_Phase != PH_TOOLS) StartUpdate();
}

// ============================================================================
//  Страница: Помощь
// ============================================================================
static void PageHelp(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f, gap = 24.f;
    float x0 = c.o.x + m, y0 = c.o.y + 20.f, W = c.sz.x - m * 2, colW = (W - gap) * 0.5f, top = y0 + 78.f, bottom = c.o.y + c.sz.y - 24.f;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "ПОМОЩЬ", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Как это работает");

    ImVec2 a(x0, top), b(x0 + colW, bottom);
    Panel(d, a, b); Eyebrow(d, ImVec2(a.x + 22, a.y + 16), "01", "ШАГИ");
    const char* steps[4] = { "Вставьте ссылку на видео в поле и нажмите АНАЛИЗ.", "Программа покажет только те качества, которые реально есть у ролика (до 4K и выше).", "Выберите пункт, либо РУЧНОЙ режим: клик по видео-строке и по аудио-строке задаёт пару потоков.", "Нажмите СКАЧАТЬ. Видео и звук качаются отдельно и склеиваются в один mp4." };
    float y = a.y + 52;
    for (int i = 0; i < 4; ++i) {
        char num[8]; snprintf(num, sizeof(num), "0%d", i + 1); DLabel(d, ImVec2(a.x + 22, y + 2), Col(AccentT()), num, 1.4f);
        y += DrawWrapped(d, g_FontBody, ImVec2(a.x + 56, y), colW - 80, Col(g_P.text), steps[i], 22.f) + 16.f;
    }

    ImVec2 ra(x0 + colW + gap, top), rb(x0 + W, top + 214);
    Panel(d, ra, rb); Eyebrow(d, ImVec2(ra.x + 22, ra.y + 16), "02", "ИНСТРУМЕНТЫ");
    std::error_code ec; struct T { const wchar_t* f; const char* n; const char* role; } ts[3] = { { L"yt-dlp.exe", "YT-DLP", "ЗАГРУЗКА" },{ L"ffmpeg.exe", "FFMPEG", "СКЛЕЙКА" },{ L"deno.exe", "DENO", "JS-ДВИЖОК ДЛЯ САЙТОВ" } };
    for (int i = 0; i < 3; ++i) {
        float yy = ra.y + 54 + i * 50.f; bool have = fs::exists(g_Dir / ts[i].f, ec) || (i == 2 && Grab::OnPath(L"deno.exe"));
        if (i > 0) d->AddLine(ImVec2(ra.x + 22, yy - 8), ImVec2(rb.x - 22, yy - 8), Col(g_P.border), 1.f);
        d->AddCircleFilled(ImVec2(ra.x + 30, yy + 14), 4.f, have ? Col(Rgb(96, 206, 150)) : Col(Rgb(232, 70, 80)), 12);
        DText(d, g_FontBody, ImVec2(ra.x + 48, yy + 2), Col(g_P.text), ts[i].n); DLabel(d, ImVec2(ra.x + 48, yy + 26), Col(g_P.dim), ts[i].role, 1.2f);
        DLabelR(d, rb.x - 22, yy + 8, have ? Col(Rgb(96, 206, 150)) : Col(Rgb(232, 70, 80)), have ? "НА МЕСТЕ" : "НЕТ", 1.4f);
    }
    ImVec2 ka(ra.x, rb.y + 16), kb(rb.x, bottom);
    if (kb.y - ka.y > 120.f) {
        Panel(d, ka, kb); Eyebrow(d, ImVec2(ka.x + 22, ka.y + 16), "03", "ЕСЛИ НЕ КАЧАЕТ");
        DrawWrapped(d, g_FontSmall, ImVec2(ka.x + 22, ka.y + 46), colW - 44, Col(g_P.dim), "Откройте НАСТРОЙКИ и обновите yt-dlp — сайты меняются очень часто. Не получается 4K — проверьте, что рядом лежит deno.exe.", 19.f);
        DrawWrapped(d, g_FontSmall, ImVec2(ka.x + 22, ka.y + 112), colW - 44, Col(g_P.dim), "Скачивайте только то, что вам разрешено сохранять: свои ролики и материалы с подходящей лицензией.", 19.f);
    }
}


// ============================================================================
//  Отказ от ответственности, контакты, адреса для пожертвований
//  (меняйте здесь)
// ============================================================================
static const char* kDisclaimer =
"Это бесплатная программа-загрузчик: оболочка над открытыми инструментами yt-dlp и FFmpeg. "
"Она предназначена для сохранения материалов, на которые у вас есть права или разрешение: собственных видео, "
"работ под свободными лицензиями (например, Creative Commons), контента, который правообладатель разрешил сохранять. "
"Вы сами отвечаете за то, что скачиваете, и за соблюдение законов вашей страны и правил сайтов. "
"Автор не хранит и не видит ваши ссылки и файлы, не связан с YouTube и Google и не отвечает за использование программы. "
"Программа предоставляется «как есть», без каких-либо гарантий.";
static const char* kWalletNames[2] = { "USDT · TON", "Bitcoin · BTC" };
static const char* kWallets[2] = { "UQCr0pbOI2BPRzTuZleyp0YBEHGvE-FCsSJLTiHZzy75Tj_4", "bc1qeet3kscyqq9dhvlw6e4j4ztaxmg3hh7wdxhqp0" };
static const ImVec4 kWalletDots[2] = { Rgb(38, 161, 123), Rgb(247, 147, 26) };

// ============================================================================
//  Страница: О программе
// ============================================================================
static void PageAbout(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f, gap = 24.f;
    float x0 = c.o.x + m, y0 = c.o.y + 20.f, W = c.sz.x - m * 2, top = y0 + 78.f, bottom = c.o.y + c.sz.y - 24.f;
    float leftW = W * 0.58f - gap * 0.5f, rightW = W - leftW - gap;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "О ПРОГРАММЕ", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Прежде чем пользоваться");

    // ---- 01 · Отказ от ответственности ------------------------------------
    ImVec2 a(x0, top), b(x0 + leftW, top + 280);
    Panel(d, a, b); Eyebrow(d, ImVec2(a.x + 22, a.y + 16), "01", "ОТКАЗ ОТ ОТВЕТСТВЕННОСТИ");
    DrawWrapped(d, g_FontSmall, ImVec2(a.x + 22, a.y + 48), leftW - 44, Col(g_P.text, 0.92f), kDisclaimer, 19.f);

    // ---- 02 · Компоненты --------------------------------------------------
    ImVec2 ra(x0 + leftW + gap, top), rb(x0 + W, top + 280);
    Panel(d, ra, rb); Eyebrow(d, ImVec2(ra.x + 22, ra.y + 16), "02", "КОМПОНЕНТЫ");
    struct Comp { const char* n; const char* l; } comps[5] = { { "yt-dlp", "UNLICENSE" },{ "FFmpeg", "GPL · ОТДЕЛЬНО" },{ "Deno", "MIT" },{ "Dear ImGui", "MIT" },{ "nlohmann/json", "MIT" } };
    for (int i = 0; i < 5; ++i) {
        float yy = ra.y + 48 + i * 36.f;
        if (i > 0) d->AddLine(ImVec2(ra.x + 22, yy - 4), ImVec2(rb.x - 22, yy - 4), Col(g_P.border), 1.f);
        DText(d, g_FontBody, ImVec2(ra.x + 22, yy + 2), Col(g_P.text), comps[i].n);
        DLabelR(d, rb.x - 22, yy + 8, Col(g_P.dim), comps[i].l, 1.2f);
    }
    DLabel(d, ImVec2(ra.x + 22, rb.y - 28), Col(g_P.dim, 0.85f), "ТЕКСТЫ ЛИЦЕНЗИЙ — В ФАЙЛЕ LICENSES.TXT", 1.0f);

    // ---- 03 · Поддержать --------------------------------------------------
    ImVec2 da(x0, top + 296), db(x0 + W, std::min(bottom, top + 296 + 232));
    if (db.y - da.y >= 150.f) {
        Panel(d, da, db); Eyebrow(d, ImVec2(da.x + 22, da.y + 16), "03", "ПОДДЕРЖАТЬ ПРОЕКТ");
        DrawWrapped(d, g_FontSmall, ImVec2(da.x + 22, da.y + 44), W - 44, Col(g_P.dim), "Пожертвования добровольны. Они не дают никаких привилегий, дополнительных функций или приоритетной поддержки.", 19.f);
        for (int i = 0; i < 2; ++i) {
            float ry = da.y + 76 + i * 38.f;
            d->AddCircleFilled(ImVec2(da.x + 30, ry + 15), 4.f, Col(kWalletDots[i]), 16);
            d->AddCircle(ImVec2(da.x + 30, ry + 15), 8.f, Col(kWalletDots[i], 0.4f), 20, 1.f);
            DText(d, g_FontBody, ImVec2(da.x + 46, ry + 5), Col(g_P.text), kWalletNames[i]);
            float chipX = db.x - 22 - 118;
            d->PushClipRect(ImVec2(da.x + 170, ry), ImVec2(chipX - 10, ry + 32), true);
            DLabel(d, ImVec2(da.x + 170, ry + 8), Col(g_P.dim), kWallets[i], 0.6f);
            d->PopClipRect();
            char id[16]; snprintf(id, sizeof(id), "##cp%d", i);
            if (GhostBtn(id, "КОПИРОВАТЬ", ImVec2(chipX, ry + 2), ImVec2(118, 28))) { ImGui::SetClipboardText(kWallets[i]); AddToast("Адрес скопирован"); }
        }
        if (db.y - da.y >= 190.f && LinkBtn("##tg", "Telegram  @qypxr1", ImVec2(da.x + 22, da.y + 76 + 2 * 38.f + 8), ImVec2(320, 44), Rgb(70, 200, 230))) OpenUrl(L"https://t.me/qypxr1");
    }
    (void)rightW;
}

// ============================================================================
//  Окно согласия при первом запуске
// ============================================================================
static bool SplashActive();
static void DrawConsent(ImDrawList* d, ImVec2 wp, ImVec2 ws, float dt) {
    if (g_Accepted || SplashActive()) return;
    static float al = 0.f; al = std::min(1.f, al + dt * 4.f);
    float sv = g_A; g_A = Smooth(al);
    d->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), Col(Rgb(0, 0, 0), 0.74f));
    float pw = std::min(660.f, ws.x - 80.f), ph = 410.f;
    ImVec2 a(wp.x + (ws.x - pw) * 0.5f, wp.y + (ws.y - ph) * 0.5f), b(a.x + pw, a.y + ph);
    d->AddRectFilled(a, b, Col(g_P.solid), 4.f);
    Panel(d, a, b);
    Eyebrow(d, ImVec2(a.x + 28, a.y + 24), "—", "ПЕРЕД НАЧАЛОМ");
    DText(d, g_FontH2, ImVec2(a.x + 28, a.y + 50), Col(g_P.text), "Прочитайте и подтвердите");
    DrawWrapped(d, g_FontSmall, ImVec2(a.x + 28, a.y + 96), pw - 56, Col(g_P.text, 0.92f), kDisclaimer, 19.f);
    float by = b.y - 28.f - 54.f;
    if (PrimaryButton("##accept", "ПРИНИМАЮ", ImVec2(a.x + 28, by), ImVec2(pw - 56 - 126, 54), 1, (float)ImGui::GetTime())) { g_Accepted = true; SaveSettings(); Snd(SFX_OK); }
    if (GhostBtn("##decline", "ВЫЙТИ", ImVec2(b.x - 28 - 112, by), ImVec2(112, 54))) SendMessageW(g_hwnd, WM_CLOSE, 0, 0);
    g_A = sv;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fL; const D3D_FEATURE_LEVEL fLA[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fLA, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fL, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget(); return true;
}
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) { g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView); pBackBuffer->Release(); }
}
void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }



// ============================================================================
//  main
// ============================================================================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    SetCurrentProcessExplicitAppUserModelID(L"GRABBER.SMART.1");
    OleInitialize(nullptr);
    Grab::InitEnv();
    g_Dir = Grab::ExeDir();
    LoadSettings(); srand((unsigned int)time(0));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"GRABBER_Engine", nullptr };
    ::RegisterClassExW(&wc); DWORD dwStyle = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    HWND hwnd = ::CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"SMART GRABBER", dwStyle, 100, 100, 1040, 780, nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;
    MARGINS margins = { 1, 1, 1, 1 }; DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT); ::UpdateWindow(hwnd);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    BuildSounds();

    // шрифты Windows. С Dear ImGui 1.92+ глифы (кириллица, «→—…·») подгружаются сами по мере надобности
    auto LoadF = [&](const char* path, float size, ImFont* fb) -> ImFont* {
        std::error_code ec; if (!fs::exists(path, ec)) return fb;
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, size); return f ? f : fb;
        };
    g_FontBody = LoadF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, nullptr);
    if (!g_FontBody) { g_FontBody = io.Fonts->AddFontDefault(); }
    g_FontSmall = LoadF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, g_FontBody);
    g_FontH2 = LoadF("C:\\Windows\\Fonts\\segoeui.ttf", 22.0f, g_FontBody);
    g_FontH1 = LoadF("C:\\Windows\\Fonts\\segoeuil.ttf", 36.0f, g_FontH2);   // Segoe UI Light
    g_FontBig = LoadF("C:\\Windows\\Fonts\\segoeuil.ttf", 50.0f, g_FontH1);
    g_FontMono = LoadF("C:\\Windows\\Fonts\\consola.ttf", 14.0f, g_FontSmall);  // Consolas

    ImGui::StyleColorsDark(); ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f; style.WindowPadding = ImVec2(0.0f, 0.0f); style.WindowBorderSize = 0.0f;
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    StartTools();   // докачать yt-dlp / ffmpeg / deno, если их нет (пока идёт заставка)

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessage(&msg); if (msg.message == WM_QUIT) done = true; }
        if (done) break;

        float dt = io.DeltaTime; if (dt <= 0.f || dt > 0.1f) dt = 1.f / 60.f;
        float sp = dt * 2.5f;
        if (g_IsLightTheme && g_ThemeAnim < 1.f) g_ThemeAnim += sp;
        if (!g_IsLightTheme && g_ThemeAnim > 0.f) g_ThemeAnim -= sp;
        g_ThemeAnim = std::clamp(g_ThemeAnim, 0.f, 1.f);
        g_PageAlpha = std::min(1.f, g_PageAlpha + dt * 6.f);
        UpdatePalette();
        { float k = std::min(1.f, dt * 8.f); ACCENT = LerpColor(ACCENT, g_Accents[g_AccentIdx].a, k); ACCENT_HI = LerpColor(ACCENT_HI, g_Accents[g_AccentIdx].hi, k); }

        style.Colors[ImGuiCol_WindowBg] = g_P.bg; style.Colors[ImGuiCol_Text] = g_P.text;
        style.Colors[ImGuiCol_PopupBg] = g_P.panel2; style.Colors[ImGuiCol_Border] = g_P.border;

        { int ps = g_PendingSnd.exchange(-1); if (ps >= 0) { Snd(ps); if (ps == SFX_OK && ::GetForegroundWindow() != g_hwnd) { FLASHWINFO fi = { sizeof(fi), g_hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0 }; ::FlashWindowEx(&fi); } } }
        { static bool started = false; if (!started) { started = true; Snd(SFX_START); } }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGuiViewport* vp = ImGui::GetMainViewport(); ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("MainWorkspace", nullptr, wf);

        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 wp = vp->WorkPos, ws = vp->WorkSize; float time = (float)ImGui::GetTime();
        g_A = 1.f;
        DrawBackground(d, wp, ws, time, dt);

        const float TB = 44.f, SB = 84.f;
        Ctx cx{ d, ImVec2(wp.x + SB, wp.y + TB), ImVec2(ws.x - SB, ws.y - TB), time, dt };
        g_A = Smooth(g_PageAlpha);
        const bool dis = SplashActive() || !g_Accepted;   // пока идёт заставка или не принято согласие, страницы и сайдбар не реагируют на мышь
        const ImVec2 savedMouse = io.MousePos; if (dis) io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        d->PushClipRect(cx.o, ImVec2(cx.o.x + cx.sz.x, cx.o.y + cx.sz.y), true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(g_A, 0.02f));
        if (g_Page == 0) PageDownload(cx); else if (g_Page == 1) PageFiles(cx); else if (g_Page == 2) PageSettings(cx); else if (g_Page == 3) PageHelp(cx); else PageAbout(cx);
        ImGui::PopStyleVar();
        d->PopClipRect();
        g_A = 1.f;

        DrawSidebar(d, wp, ws, dt);
        if (dis) io.MousePos = savedMouse;
        DrawSplash(d, wp, ws, dt);
        DrawConsent(d, wp, ws, dt);
        DrawTitleBar(d, wp, ws, time);
        d->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), Col(g_P.border), 0.f, 0, 1.f);

        ImGui::End();
        DrawToasts(dt);

        ImGui::Render();
        ImVec4 c_bg = g_P.bg; const float clr[4] = { c_bg.x, c_bg.y, c_bg.z, 1.f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr); g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); g_pSwapChain->Present(1, 0);
    }

    g_Cancel = true;   // активная загрузка завершится вместе с программой (процессы yt-dlp/ffmpeg убиваются вместе с окном)
    SaveSettings();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    OleUninitialize();
    return 0;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)lParam; mm->ptMinTrackSize.x = 940; mm->ptMinTrackSize.y = 720; return 0; }
    case WM_NCCALCSIZE: if (wParam == TRUE) return 0; break;
    case WM_NCHITTEST: {
        LRESULT hit = ::DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit == HTNOWHERE || hit == HTCLIENT) {
            POINT pt; pt.x = (int)(short)LOWORD(lParam); pt.y = (int)(short)HIWORD(lParam);
            RECT rc; GetWindowRect(hWnd, &rc);
            int x = pt.x - rc.left, y = pt.y - rc.top, width = rc.right - rc.left, height = rc.bottom - rc.top;
            int border = 8, title_height = 44, buttons_width = 132;
            if (!IsZoomed(hWnd)) {
                if (x < border && y < border) return HTTOPLEFT;
                if (x > width - border && y < border) return HTTOPRIGHT;
                if (x < border && y > height - border) return HTBOTTOMLEFT;
                if (x > width - border && y > height - border) return HTBOTTOMRIGHT;
                if (x < border) return HTLEFT; if (x > width - border) return HTRIGHT;
                if (y < border) return HTTOP; if (y > height - border) return HTBOTTOM;
            }
            if (y < title_height && x < width - buttons_width) return HTCAPTION;
        }
        return hit;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (g_pd3dDevice != nullptr) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); }
        return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}