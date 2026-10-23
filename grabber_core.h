// ============================================================================
//  grabber_core.h  -  движок загрузчика (БЕЗ интерфейса)
//
//  Что здесь:
//    * разбор списка форматов (yt-dlp -J) и построение пунктов выбора качества
//    * разбор строк прогресса yt-dlp
//    * запуск yt-dlp / ffmpeg / curl скрытыми процессами (с отменой)
//    * автоустановка yt-dlp.exe, ffmpeg.exe, deno.exe рядом с программой
//
//  Интерфейс (main.cpp) только вызывает функции отсюда из рабочих потоков.
//  Блок между PURE-BEGIN и PURE-END не зависит от Windows и проверяется тестом.
// ============================================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "json.hpp"

namespace Grab {
namespace fs = std::filesystem;
using nlohmann::json;

// ==== PURE-BEGIN ============================================================

inline std::string Fmtf(const char* f, ...) {
    va_list a;
    va_start(a, f);
    va_list b;
    va_copy(b, a);
    int n = vsnprintf(nullptr, 0, f, a);
    va_end(a);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) vsnprintf(s.data(), s.size() + 1, f, b);
    va_end(b);
    return s;
}

inline std::string Trim(std::string s) {
    auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && sp((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && sp((unsigned char)s[i])) ++i;
    return s.substr(i);
}

inline std::string ToLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline std::string HumanSize(double b) {
    if (b <= 0) return "?";
    const char* u[] = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    int i = 0;
    while (b >= 1024 && i < 4) { b /= 1024; ++i; }
    return Fmtf(i == 0 ? "%.0f %s" : "%.1f %s", b, u[i]);
}

inline std::string HumanSpeed(double bps) {
    if (bps <= 0) return "—";
    return HumanSize(bps) + "/с";
}

inline std::string HumanDuration(double sec) {
    long s = (long)sec;
    if (s <= 0) return "?";
    return s >= 3600 ? Fmtf("%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60)
                     : Fmtf("%ld:%02ld", s / 60, s % 60);
}

inline std::string HumanEta(double sec) {
    if (sec <= 0) return "—";
    return HumanDuration(sec);
}

// ---- форматы ---------------------------------------------------------------
struct Fmt {
    std::string id, ext, vcodec, acodec, note;
    int height = 0, width = 0;
    double fps = 0, tbr = 0, size = 0;
    bool hasVideo() const { return !vcodec.empty() && vcodec != "none"; }
    bool hasAudio() const { return !acodec.empty() && acodec != "none"; }
};

struct VideoInfo {
    std::string title, uploader;
    double duration = 0;
    std::vector<Fmt> formats;
};

inline std::string GetStr(const json& j, const char* k) {
    auto it = j.find(k);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}
inline double GetNum(const json& j, const char* k) {
    auto it = j.find(k);
    return (it != j.end() && it->is_number()) ? it->get<double>() : 0.0;
}

inline std::optional<VideoInfo> ParseInfo(const std::string& raw) {
    size_t p = raw.find('{');
    if (p == std::string::npos) return std::nullopt;
    json j = json::parse(raw.begin() + (std::ptrdiff_t)p, raw.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("formats") || !j["formats"].is_array())
        return std::nullopt;

    VideoInfo vi;
    vi.title = GetStr(j, "title");
    vi.uploader = GetStr(j, "uploader");
    vi.duration = GetNum(j, "duration");
    for (const auto& f : j["formats"]) {
        if (!f.is_object()) continue;
        Fmt x;
        x.id = GetStr(f, "format_id");
        x.ext = GetStr(f, "ext");
        x.vcodec = GetStr(f, "vcodec");
        x.acodec = GetStr(f, "acodec");
        x.note = GetStr(f, "format_note");
        x.height = (int)GetNum(f, "height");
        x.width = (int)GetNum(f, "width");
        x.fps = GetNum(f, "fps");
        x.tbr = GetNum(f, "tbr");
        x.size = GetNum(f, "filesize");
        if (x.size <= 0) x.size = GetNum(f, "filesize_approx");
        if (x.id.empty() || x.ext == "mhtml") continue;               // storyboard
        if (x.vcodec == "none" && x.acodec == "none") continue;
        vi.formats.push_back(std::move(x));
    }
    if (vi.formats.empty()) return std::nullopt;
    return vi;
}

inline int MaxHeight(const std::vector<Fmt>& f) {
    int m = 0;
    for (auto& x : f) if (x.hasVideo()) m = std::max(m, x.height);
    return m;
}

inline int BestFpsAt(const std::vector<Fmt>& f, int h) {
    double m = 0;
    for (auto& x : f) if (x.hasVideo() && x.height == h) m = std::max(m, x.fps);
    return (int)(m + 0.5);
}

// Грубая оценка размера: лучшее видео не выше maxH (0 = без ограничения) + лучшее аудио.
inline double EstimateSize(const std::vector<Fmt>& f, int maxH) {
    const Fmt* bv = nullptr;
    const Fmt* ba = nullptr;
    for (auto& x : f) {
        if (x.hasVideo()) {
            if (maxH > 0 && x.height > maxH) continue;
            if (!bv || std::tie(x.height, x.fps, x.tbr) > std::tie(bv->height, bv->fps, bv->tbr)) bv = &x;
        } else if (x.hasAudio()) {
            if (!ba || x.tbr > ba->tbr) ba = &x;
        }
    }
    if (!bv || bv->size <= 0) return 0;
    double total = bv->size;
    if (!bv->hasAudio() && ba) total += ba->size;
    return total;
}

inline double BestAudioSize(const std::vector<Fmt>& f) {
    const Fmt* ba = nullptr;
    for (auto& x : f) if (!x.hasVideo() && x.hasAudio() && (!ba || x.tbr > ba->tbr)) ba = &x;
    return ba ? ba->size : 0;
}

inline std::string QualityName(int h) {
    if (h >= 4320) return "8K";
    if (h >= 2160) return "4K";
    if (h >= 1440) return "2K";
    if (h >= 1080) return "Full HD";
    if (h >= 720) return "HD";
    return "";
}

// ---- пункты выбора качества ----------------------------------------------------
struct Choice {
    std::string title;     // «Максимальное · 4K 2160p60»
    std::string size;      // «~1.4 ГБ» или пусто
    std::string selector;  // выражение для yt-dlp -f
    bool audio = false;
    int height = 0;
};

inline std::vector<Choice> BuildChoices(const std::vector<Fmt>& f) {
    std::vector<Choice> out;
    int maxH = MaxHeight(f);
    auto sz = [&](int limit) {
        double s = EstimateSize(f, limit);
        return s > 0 ? "~" + HumanSize(s) : std::string();
    };
    if (maxH > 0) {
        int fps = BestFpsAt(f, maxH);
        std::string q = QualityName(maxH);
        std::string title = "Максимальное  ·  " + (q.empty() ? std::string() : q + "  ") + std::to_string(maxH) + "p" +
                            (fps > 30 ? std::to_string(fps) : std::string());
        out.push_back({title, sz(0), "bv*+ba/b", false, maxH});
        int prevEff = maxH;
        for (int tier : {2160, 1440, 1080, 720, 480}) {
            if (tier >= maxH) continue;
            int eff = 0;   // реальная высота лучшего видео не выше tier
            for (auto& x : f) if (x.hasVideo() && x.height <= tier) eff = std::max(eff, x.height);
            if (eff == 0 || eff >= prevEff) continue;   // такого уровня в ролике нет - пункт ничего не меняет
            prevEff = eff;
            std::string tq = QualityName(eff);
            std::string t = std::to_string(eff) + "p" + (tq.empty() ? std::string() : "  ·  " + tq);
            std::string sel = "bv*[height<=" + std::to_string(eff) + "]+ba/b[height<=" + std::to_string(eff) + "]";
            out.push_back({t, sz(eff), sel, false, eff});
        }
    } else {
        out.push_back({"Лучшее доступное", "", "bv*+ba/b", false, 0});
    }
    double as = BestAudioSize(f);
    out.push_back({"Только аудио  ·  MP3", as > 0 ? "~" + HumanSize(as) : std::string(), "ba/b", true, 0});
    return out;
}

// Ручной ввод ID: "137", "137+140", "bv*+ba". false - такого ID нет.
inline bool ManualSelector(const std::vector<Fmt>& f, const std::string& input, std::string& sel) {
    std::string s = Trim(input);
    if (s.empty()) return false;
    if (s.find_first_of("+/,") != std::string::npos) { sel = s; return true; }
    auto it = std::find_if(f.begin(), f.end(), [&](const Fmt& x) { return x.id == s; });
    if (it == f.end()) return false;
    sel = (it->hasVideo() && !it->hasAudio()) ? s + "+ba" : s;   // видео без звука - добавить лучшее аудио
    return true;
}

// ---- прогресс загрузки ----------------------------------------------------------
struct ProgLine {
    bool ok = false;
    std::string status;
    double downloaded = 0, total = 0, totalEst = 0, speed = 0, eta = 0;
    int fragIdx = 0, fragCount = 0;
};

inline double NumOrZero(const std::string& s) {
    if (s.empty() || s == "None" || s == "NA") return 0;
    char* e = nullptr;
    double v = strtod(s.c_str(), &e);
    return (e == s.c_str() || !std::isfinite(v)) ? 0 : v;
}

// Строка вида: GRAB|downloading|1234|5678|None|91234.5|12|3|40   (см. --progress-template)
inline ProgLine ParseProgLine(const std::string& line) {
    ProgLine p;
    if (line.rfind("GRAB|", 0) != 0) return p;
    std::vector<std::string> t;
    size_t s = 5;
    for (;;) {
        size_t e = line.find('|', s);
        if (e == std::string::npos) { t.push_back(line.substr(s)); break; }
        t.push_back(line.substr(s, e - s));
        s = e + 1;
    }
    if (t.size() < 8) return p;
    p.status = t[0];
    p.downloaded = NumOrZero(t[1]);
    p.total = NumOrZero(t[2]);
    p.totalEst = NumOrZero(t[3]);
    p.speed = NumOrZero(t[4]);
    p.eta = NumOrZero(t[5]);
    p.fragIdx = (int)NumOrZero(t[6]);
    p.fragCount = (int)NumOrZero(t[7]);
    p.ok = true;
    return p;
}

struct Progress {
    int phase = 0;            // 0 загрузка, 1 склейка, 2 конвертация
    int stream = 0;           // номер текущего потока (с 1)
    int streams = 1;          // всего потоков (видео + аудио = 2)
    double frac = 0;          // общий прогресс 0..1
    double speed = 0;         // байт/с
    double eta = 0;           // секунд до конца текущего потока
    double done = 0, total = 0;
    bool already = false;     // файл уже был скачан раньше
};

// Собирает общий прогресс из строк вывода yt-dlp (видео и аудио качаются двумя потоками).
struct Tracker {
    Progress P;
    double prevDone = 0, prevTotal = 0, curDone = 0, curTotal = 0;

    explicit Tracker(int streams = 1) { P.streams = streams < 1 ? 1 : streams; }

    static double Base(int streams, int stream) {
        if (streams == 2) return stream <= 1 ? 0.0 : 0.92;   // видео весит почти всё
        return (double)(std::max(stream, 1) - 1) / streams;
    }
    static double Weight(int streams, int stream) {
        if (streams == 2) return stream <= 1 ? 0.92 : 0.08;
        return 1.0 / streams;
    }

    // true - строка что-то изменила в прогрессе
    bool Feed(const std::string& line) {
        if (line.rfind("GRAB|", 0) == 0) {
            ProgLine pl = ParseProgLine(line);
            if (!pl.ok) return false;
            if (P.stream == 0) P.stream = 1;
            double tot = pl.total > 0 ? pl.total : pl.totalEst;
            curDone = pl.downloaded;
            if (tot > 0) curTotal = tot;
            double f = 0;
            if (pl.status == "finished") f = 1;
            else if (curTotal > 0) f = curDone / curTotal;
            else if (pl.fragCount > 0) f = (double)pl.fragIdx / pl.fragCount;
            f = std::clamp(f, 0.0, 1.0);
            P.phase = 0;
            P.speed = pl.speed;
            P.eta = pl.eta;
            P.done = prevDone + curDone;
            P.total = prevTotal + curTotal;
            double v = Base(P.streams, P.stream) + Weight(P.streams, P.stream) * f;
            P.frac = std::max(P.frac, std::clamp(v, 0.0, 1.0));
            return true;
        }
        if (line.find("[download] Destination:") != std::string::npos) {
            if (P.stream > 0) {
                prevDone += curDone;
                prevTotal += curTotal > 0 ? curTotal : curDone;
                curDone = curTotal = 0;
            }
            ++P.stream;
            if (P.stream > P.streams) P.streams = P.stream;
            return true;
        }
        if (line.find("[Merger]") != std::string::npos || line.find("[VideoRemuxer]") != std::string::npos ||
            line.find("[Fixup") != std::string::npos) {
            P.phase = 1;
            P.speed = 0;
            P.frac = std::max(P.frac, 0.99);
            return true;
        }
        if (line.find("[ExtractAudio]") != std::string::npos) {
            P.phase = 2;
            P.speed = 0;
            P.frac = std::max(P.frac, 0.99);
            return true;
        }
        if (line.find("has already been downloaded") != std::string::npos) {
            P.already = true;
            P.frac = 1;
            return true;
        }
        return false;
    }
};

// ==== PURE-END ==============================================================

// ----------------------------------------------------------------------------
//  Строки UTF-8 <-> UTF-16
// ----------------------------------------------------------------------------
inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::string PathToUtf8(const fs::path& p) { return WideToUtf8(p.wstring()); }

// Дочерние процессы (yt-dlp на Python) должны писать в UTF-8, иначе кириллица в названиях ломается.
inline void InitEnv() {
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
}

// ----------------------------------------------------------------------------
//  Запуск процессов
// ----------------------------------------------------------------------------
// Экранирование аргумента по правилам CommandLineToArgvW.
inline std::wstring Quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (size_t i = 0;; ++i) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { ++i; ++bs; }
        if (i == a.size()) { r.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(a[i]); }
    }
    r.push_back(L'"');
    return r;
}

inline std::wstring JoinCmd(const fs::path& exe, const std::vector<std::wstring>& args) {
    std::wstring c = Quote(exe.wstring());
    for (auto& a : args) { c += L' '; c += Quote(a); }
    return c;
}

inline fs::path ExeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return fs::path(std::wstring(buf, n)).parent_path();
}

inline fs::path SysDir() {
    wchar_t b[MAX_PATH];
    UINT n = GetSystemDirectoryW(b, MAX_PATH);
    return fs::path(std::wstring(b, n));
}

using LineFn = std::function<void(const std::string&)>;
using TickFn = std::function<void()>;
using StatusFn = std::function<void(const std::string&)>;
using ProgressFn = std::function<void(const Progress&)>;

struct RunResult {
    bool started = false;
    bool cancelled = false;
    DWORD exitCode = 1;
};

// Скрытый запуск. stdout+stderr читаются построчно (\n и \r - разделители).
// cancel=true убивает процесс вместе со всеми потомками (ffmpeg и т.д.) через Job Object.
inline RunResult RunProc(std::wstring cmd, const LineFn& onLine, const std::atomic<bool>* cancel,
                         const TickFn& onTick = nullptr) {
    RunResult r;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return r;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : nullptr;
    PROCESS_INFORMATION pi{};

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }

    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        if (job) CloseHandle(job);
        return r;
    }
    r.started = true;
    bool inJob = job && AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);

    std::string acc;
    auto flush = [&](bool final) {
        size_t s = 0;
        for (size_t i = 0; i < acc.size(); ++i) {
            if (acc[i] == '\n' || acc[i] == '\r') {
                if (i > s && onLine) onLine(acc.substr(s, i - s));
                s = i + 1;
            }
        }
        acc.erase(0, s);
        if (final && !acc.empty()) {
            if (onLine) onLine(acc);
            acc.clear();
        }
    };
    auto killIt = [&] {
        if (inJob) TerminateJobObject(job, 1);
        else TerminateProcess(pi.hProcess, 1);
        r.cancelled = true;
    };

    char buf[16384];
    for (;;) {
        if (cancel && cancel->load()) { killIt(); break; }
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;   // труба закрыта - процесс завершился
        if (avail > 0) {
            DWORD n = 0, want = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
            if (!ReadFile(rd, buf, want, &n, nullptr) || n == 0) break;
            acc.append(buf, n);
            flush(false);
            continue;
        }
        if (onTick) onTick();
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            for (;;) {   // забрать остаток вывода
                DWORD a = 0;
                if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &a, nullptr) || a == 0) break;
                DWORD n = 0, want = a < sizeof(buf) ? a : (DWORD)sizeof(buf);
                if (!ReadFile(rd, buf, want, &n, nullptr) || n == 0) break;
                acc.append(buf, n);
                flush(false);
            }
            break;
        }
    }
    flush(true);
    if (!r.cancelled) WaitForSingleObject(pi.hProcess, 10000);
    else WaitForSingleObject(pi.hProcess, 3000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = code;
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    return r;
}

// ----------------------------------------------------------------------------
//  Автоустановка инструментов
// ----------------------------------------------------------------------------
inline std::wstring PsQuote(const std::wstring& s) {
    std::wstring r = L"'";
    for (wchar_t c : s) { if (c == L'\'') r += L"''"; else r += c; }
    r += L"'";
    return r;
}

inline fs::path PowerShellExe() { return SysDir() / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe"; }

inline bool DownloadFile(const std::wstring& url, const fs::path& dest, const std::string& label,
                         const StatusFn& status, const std::atomic<bool>* cancel = nullptr) {
    std::error_code ec;
    fs::path part = dest;
    part += L".part";
    fs::remove(part, ec);

    auto tick = [&] {
        std::error_code e2;
        uintmax_t sz = fs::exists(part, e2) ? fs::file_size(part, e2) : 0;
        if (status) status(label + "  ·  " + HumanSize((double)sz));
    };
    if (status) status(label);

    // 1) curl.exe (есть в Windows 10 1803+)
    RunResult rr = RunProc(JoinCmd(SysDir() / L"curl.exe",
                                   {L"-L", L"--fail", L"-sS", L"--retry", L"3", L"-o", part.wstring(), url}),
                           nullptr, cancel, tick);
    bool ok = rr.started && !rr.cancelled && rr.exitCode == 0 && fs::exists(part, ec);
    // 2) запасной вариант - PowerShell
    if (!ok && !rr.cancelled) {
        fs::remove(part, ec);
        std::wstring script =
            L"$ProgressPreference='SilentlyContinue'; "
            L"[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
            L"Invoke-WebRequest -UseBasicParsing -Uri " + PsQuote(url) + L" -OutFile " + PsQuote(part.wstring());
        rr = RunProc(JoinCmd(PowerShellExe(), {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-Command", script}),
                     nullptr, cancel, tick);
        ok = rr.started && !rr.cancelled && rr.exitCode == 0 && fs::exists(part, ec);
    }
    if (!ok) {
        fs::remove(part, ec);
        return false;
    }
    fs::remove(dest, ec);
    fs::rename(part, dest, ec);
    return !ec;
}

inline bool ExtractZip(const fs::path& zip, const fs::path& outDir) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    RunResult rr = RunProc(JoinCmd(SysDir() / L"tar.exe", {L"-xf", zip.wstring(), L"-C", outDir.wstring()}), nullptr, nullptr);
    if (rr.started && rr.exitCode == 0) return true;
    std::wstring script = L"Expand-Archive -LiteralPath " + PsQuote(zip.wstring()) + L" -DestinationPath " +
                          PsQuote(outDir.wstring()) + L" -Force";
    rr = RunProc(JoinCmd(PowerShellExe(), {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-Command", script}), nullptr, nullptr);
    return rr.started && rr.exitCode == 0;
}

inline std::optional<fs::path> FindFile(const fs::path& root, const std::wstring& name) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && _wcsicmp(it->path().filename().c_str(), name.c_str()) == 0)
            return it->path();
    }
    return std::nullopt;
}

inline bool InstallFromZip(const wchar_t* url, const fs::path& dir, const std::vector<std::wstring>& names,
                           const std::string& label, const StatusFn& status) {
    std::error_code ec;
    fs::path tmp = dir / L"_tmp_download";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    fs::path zip = tmp / L"package.zip";
    bool ok = DownloadFile(url, zip, "СКАЧИВАНИЕ " + label, status);
    if (ok) {
        if (status) status("РАСПАКОВКА " + label);
        ok = ExtractZip(zip, tmp / L"x");
    }
    if (ok) {
        for (auto& n : names) {
            auto f = FindFile(tmp / L"x", n);
            if (!f) { ok = false; break; }
            fs::copy_file(*f, dir / n, fs::copy_options::overwrite_existing, ec);
            if (ec) { ok = false; break; }
        }
    }
    fs::remove_all(tmp, ec);
    return ok;
}

inline bool OnPath(const wchar_t* exe) { return SearchPathW(nullptr, exe, nullptr, 0, nullptr, nullptr) != 0; }

static const wchar_t* kYtDlpUrl = L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
static const wchar_t* kFfmpegUrl =
    L"https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";
static const wchar_t* kDenoUrl =
    L"https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip";

inline bool ToolsPresent(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / L"yt-dlp.exe", ec) && fs::exists(dir / L"ffmpeg.exe", ec) &&
           (fs::exists(dir / L"deno.exe", ec) || OnPath(L"deno.exe"));
}

// Докачать недостающее. false + err - критическая ошибка (нет yt-dlp или ffmpeg).
inline bool EnsureTools(const fs::path& dir, const StatusFn& status, std::string& err) {
    std::error_code ec;
    if (!fs::exists(dir / L"yt-dlp.exe", ec)) {
        if (!DownloadFile(kYtDlpUrl, dir / L"yt-dlp.exe", "СКАЧИВАНИЕ YT-DLP", status)) {
            err = "Не удалось скачать yt-dlp.exe. Проверьте интернет или положите файл рядом с программой.";
            return false;
        }
    }
    if (!fs::exists(dir / L"ffmpeg.exe", ec)) {
        if (!InstallFromZip(kFfmpegUrl, dir, {L"ffmpeg.exe", L"ffprobe.exe"}, "FFMPEG", status)) {
            err = "Не удалось установить FFmpeg. Положите ffmpeg.exe и ffprobe.exe рядом с программой.";
            return false;
        }
    }
    // Deno нужен yt-dlp для YouTube. Без него часть форматов (в том числе 4K) может быть недоступна.
    if (!fs::exists(dir / L"deno.exe", ec) && !OnPath(L"deno.exe")) {
        InstallFromZip(kDenoUrl, dir, {L"deno.exe"}, "DENO", status);   // не критично
    }
    if (status) status("ГОТОВО");
    return true;
}

// ----------------------------------------------------------------------------
//  Анализ ролика и загрузка
// ----------------------------------------------------------------------------
inline std::vector<std::wstring> JsRuntimeArgs(const fs::path& dir) {
    std::error_code ec;
    fs::path deno = dir / L"deno.exe";
    if (fs::exists(deno, ec)) return {L"--js-runtimes", L"deno:" + deno.wstring()};
    return {};
}

inline std::string CleanError(std::string e) {
    e = Trim(e);
    if (e.rfind("ERROR:", 0) == 0) e = Trim(e.substr(6));
    return e;
}

inline bool Analyze(const fs::path& dir, const std::string& url, VideoInfo& out, std::string& err) {
    std::vector<std::wstring> a = {L"--no-playlist", L"--no-warnings", L"--encoding", L"utf-8", L"-J"};
    for (auto& s : JsRuntimeArgs(dir)) a.push_back(s);
    a.push_back(Utf8ToWide(url));

    std::string jsonLine, lastErr;
    RunResult rr = RunProc(JoinCmd(dir / L"yt-dlp.exe", a),
                           [&](const std::string& l) {
                               if (!l.empty() && l[0] == '{') { if (l.size() > jsonLine.size()) jsonLine = l; }
                               else if (l.rfind("ERROR", 0) == 0) lastErr = l;
                           },
                           nullptr);
    if (!rr.started) { err = "Не удалось запустить yt-dlp.exe"; return false; }
    auto info = ParseInfo(jsonLine);
    if (!info) {
        err = lastErr.empty() ? "Не удалось получить форматы видео. Проверьте ссылку." : CleanError(lastErr);
        return false;
    }
    out = std::move(*info);
    return true;
}

struct DownloadResult {
    bool ok = false, cancelled = false;
    DWORD code = 0;
    std::string error;
    fs::path file;          // итоговый файл (если удалось определить)
    uint64_t size = 0;
    double seconds = 0;
};

inline DownloadResult Download(const fs::path& dir, const fs::path& dlDir, const std::string& selector, bool audioOnly,
                               const std::string& url, int fragments, const ProgressFn& onProgress,
                               const std::atomic<bool>* cancel) {
    DownloadResult res;
    auto t0 = std::chrono::steady_clock::now();
    auto fileT0 = fs::file_time_type::clock::now();
    Tracker tr((!audioOnly && selector.find('+') != std::string::npos) ? 2 : 1);

    std::vector<std::wstring> a = {
        L"--no-playlist",
        L"--concurrent-fragments", std::to_wstring(std::clamp(fragments, 1, 64)),
        L"--windows-filenames",
        L"--no-mtime",
        L"--encoding", L"utf-8",
        L"--newline",
        L"--progress-template",
        L"download:GRAB|%(progress.status)s|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|"
        L"%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s|"
        L"%(progress.fragment_index)s|%(progress.fragment_count)s",
        L"--ffmpeg-location", dir.wstring(),
        L"-P", dlDir.wstring(),
        L"-o", L"%(title).150B.%(ext)s",
        L"-f", Utf8ToWide(selector),
    };
    if (audioOnly) {
        for (auto s : {L"-x", L"--audio-format", L"mp3", L"--audio-quality", L"0"}) a.push_back(s);
    } else {
        // Из равных по разрешению и fps предпочитаем VP9 (поддерживается шире, чем AV1).
        for (auto s : {L"-S", L"res,fps,vcodec:vp9", L"--merge-output-format", L"mp4"}) a.push_back(s);
    }
    for (auto& s : JsRuntimeArgs(dir)) a.push_back(s);
    a.push_back(Utf8ToWide(url));

    std::string lastErr;
    RunResult rr = RunProc(JoinCmd(dir / L"yt-dlp.exe", a),
                           [&](const std::string& l) {
                               if (l.rfind("ERROR", 0) == 0) lastErr = l;
                               if (tr.Feed(l) && onProgress) onProgress(tr.P);
                           },
                           cancel);

    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    res.code = rr.exitCode;
    if (!rr.started) { res.error = "Не удалось запустить yt-dlp.exe"; return res; }
    if (rr.cancelled) { res.cancelled = true; return res; }
    if (rr.exitCode != 0) {
        res.error = lastErr.empty() ? Fmtf("yt-dlp завершился с кодом %lu", (unsigned long)rr.exitCode) : CleanError(lastErr);
        return res;
    }
    res.ok = true;

    // Итоговый файл: самый свежий в папке (при --no-mtime время файла = время загрузки).
    std::error_code ec;
    fs::file_time_type best{};
    for (fs::directory_iterator it(dlDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::wstring ext = it->path().extension().wstring();
        if (_wcsicmp(ext.c_str(), L".part") == 0 || _wcsicmp(ext.c_str(), L".ytdl") == 0 ||
            _wcsicmp(ext.c_str(), L".temp") == 0)
            continue;
        auto t = it->last_write_time(ec);
        if (ec) continue;
        if (t >= fileT0 - std::chrono::seconds(3) && (res.file.empty() || t > best)) { best = t; res.file = it->path(); }
    }
    if (!res.file.empty()) {
        auto sz = fs::file_size(res.file, ec);
        res.size = ec ? 0 : (uint64_t)sz;
    }
    return res;
}

// Обновление yt-dlp.exe самим yt-dlp. Возвращает последнюю строку вывода.
inline bool UpdateYtDlp(const fs::path& dir, std::string& lastLine) {
    RunResult rr = RunProc(JoinCmd(dir / L"yt-dlp.exe", {L"-U"}),
                           [&](const std::string& l) { if (!Trim(l).empty()) lastLine = Trim(l); }, nullptr);
    return rr.started && rr.exitCode == 0;
}

}  // namespace Grab
