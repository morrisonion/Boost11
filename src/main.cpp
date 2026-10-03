#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <powrprof.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>
#include <string>
#include <vector>
#include "resource.h"

using namespace Gdiplus;

static const wchar_t* kGuidBalanced = L"381b4222-f694-41f0-9685-ff5bb260df2e";
static const wchar_t* kGuidHigh     = L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
static const wchar_t* kGuidUltimate = L"e9a42b02-d5df-448d-aa00-03f14749eb61";

enum Method { M_NONE = 0, M_TRAY = 2, M_SHORTCUT = 3 };

enum {
    WM_TRAYICON      = WM_APP + 1,
    WM_APP_SETUP     = WM_APP + 2,
    WM_APP_WIZCLOSED = WM_APP + 3
};

static const int WIZ_W = 520, WIZ_H = 470;

static const Color C_BG    (255, 28, 28, 28);
static const Color C_CARD  (255, 43, 43, 43);
static const Color C_CARDH (255, 52, 52, 52);
static const Color C_INPUT (255, 30, 30, 30);
static const Color C_BORDER(255, 62, 62, 62);
static const Color C_TEXT  (255, 255, 255, 255);
static const Color C_SUB   (255, 158, 158, 158);
static const Color C_ACCENT(255, 96, 205, 255);
static const Color C_ACCENTH(255, 130, 217, 255);
static const Color C_ERR   (255, 255, 153, 164);
static const Color C_OK    (255, 108, 203, 95);
static const Color C_POPBG (255, 32, 32, 32);

struct Config {
    int  method = M_NONE;
    bool autostart = true;
    wchar_t lang[16] = L"en";
    wchar_t guidBal[40] = {};
    wchar_t guidHigh[40] = {};
};

static HINSTANCE  g_inst;
static ULONG_PTR  g_gdip;
static Config     g_cfg;
static HWND       g_main, g_wiz;
static Bitmap*    g_img[4];
static bool       g_quitAfterOverlay;
static UINT       g_msgTaskbar;

static inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float Smooth(float t)  { t = Clamp01(t); return t * t * (3 - 2 * t); }

static Color Lerp(Color a, Color b, float t) {
    return Color((BYTE)(a.GetA() + (b.GetA() - a.GetA()) * t),
                 (BYTE)(a.GetR() + (b.GetR() - a.GetR()) * t),
                 (BYTE)(a.GetG() + (b.GetG() - a.GetG()) * t),
                 (BYTE)(a.GetB() + (b.GetB() - a.GetB()) * t));
}
static Color WithA(Color c, float k) {
    return Color((BYTE)(c.GetA() * Clamp01(k)), c.GetR(), c.GetG(), c.GetB());
}

struct Sm {
    float v = 0, t = 0;
    bool Tick() {
        if (v == t) return false;
        v += (t - v) * 0.24f;
        if (fabsf(t - v) < 0.003f) v = t;
        return true;
    }
    void Snap() { v = t; }
};

static void DwmSet(HWND h, DWORD attr, DWORD val) { DwmSetWindowAttribute(h, attr, &val, sizeof(val)); }
enum { ATTR_DARK = 20, ATTR_CORNER = 33, ATTR_BORDER = 34, ATTR_CAPTION = 35, ATTR_TEXT = 36 };

static float WinScale(HWND h) { UINT d = GetDpiForWindow(h); return d ? d / 96.f : 1.f; }

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_OEMCP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}
static std::wstring ExePath() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH); return p;
}

static Bitmap* LoadPng(int id) {
    HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return nullptr;
    HGLOBAL h = LoadResource(g_inst, r);
    DWORD sz = SizeofResource(g_inst, r);
    void* p = LockResource(h);
    IStream* s = SHCreateMemStream((const BYTE*)p, sz);
    if (!s) return nullptr;
    Bitmap* src = Bitmap::FromStream(s);
    Bitmap* out = nullptr;
    if (src && src->GetLastStatus() == Ok)
        out = src->Clone(0, 0, src->GetWidth(), src->GetHeight(), PixelFormat32bppPARGB);
    delete src;
    s->Release();
    return out;
}

static HICON MakeIcon(Bitmap* src, int sz) {
    if (!src) return LoadIconW(nullptr, IDI_APPLICATION);
    Bitmap b(sz, sz, PixelFormat32bppARGB);
    HICON ic = nullptr;
    {
        Graphics g(&b);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.Clear(Color(0, 0, 0, 0));
        g.DrawImage(src, Rect(0, 0, sz, sz));
    }
    b.GetHICON(&ic);
    return ic;
}

static Font* GetFont(float px, bool semi) {
    static std::map<int, Font*> cache;
    int key = (int)(px * 4) * 2 + (semi ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Font* f = new Font(semi ? L"Segoe UI Semibold" : L"Segoe UI", px, FontStyleRegular, UnitPixel);
    cache[key] = f;
    return f;
}

static void Txt(Graphics& g, const wchar_t* t, float px, bool semi, Color c, RectF r,
                StringAlignment ha = StringAlignmentNear, StringAlignment va = StringAlignmentNear) {
    StringFormat sf(StringFormatFlagsNoWrap);
    sf.SetAlignment(ha);
    sf.SetLineAlignment(va);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(t, -1, GetFont(px, semi), r, &sf, &b);
}

static void RRect(GraphicsPath& p, RectF r, float rad) {
    float d = rad * 2;
    p.Reset();
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
}

struct Lang { std::wstring code, name; };
static std::vector<Lang> g_langs;
static std::map<std::string, std::wstring> g_tr;

static const struct { const char* k; const wchar_t* v; } kDefaults[] = {
    { "subtitle",       L"Choose how you want to switch the power mode." },
    { "tray_title",     L"Tray icon" },
    { "tray_desc",      L"Click the notification area icon to open a small menu." },
    { "shortcut_title", L"Desktop shortcut" },
    { "shortcut_desc",  L"Double-click the shortcut to switch the mode." },
    { "autostart",      L"Start with Windows" },
    { "btn_done",       L"Done" },
    { "btn_cancel",     L"Cancel" },
    { "mode_balanced",  L"Balanced" },
    { "mode_high",      L"High performance" },
    { "popup_active",   L"Active" },
    { "popup_settings", L"Settings" },
    { "popup_quit",     L"Quit" },
    { "tray_other",     L"Other plan" },
    { "info_ok",        L"Power plans detected: Balanced, High performance." },
    { "info_created",   L"\u201cHigh performance\u201d was created and detected." },
    { "info_missing",   L"Warning: \u201cHigh performance\u201d was not found." },
    { "err_shortcut",   L"The shortcut could not be created." },
};

static const wchar_t* T(const char* k) {
    auto it = g_tr.find(k);
    if (it != g_tr.end() && !it->second.empty()) return it->second.c_str();
    for (auto& d : kDefaults) if (!strcmp(d.k, k)) return d.v;
    return L"?";
}
static const wchar_t* ModeName(int i) { return T(i == 1 ? "mode_high" : "mode_balanced"); }

static std::wstring I18nDir() {
    std::wstring e = ExePath();
    return e.substr(0, e.find_last_of(L'\\')) + L"\\i18n";
}

static bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[8192]; DWORD n;
    out.clear();
    while (ReadFile(f, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
    CloseHandle(f);
    if (out.size() >= 3 && (BYTE)out[0] == 0xEF && (BYTE)out[1] == 0xBB && (BYTE)out[2] == 0xBF) out.erase(0, 3);
    return true;
}

static void FlushUtf8(std::string& raw, std::wstring& out) {
    if (raw.empty()) return;
    int n = MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), nullptr, 0);
    size_t o = out.size();
    out.resize(o + n);
    MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), &out[o], n);
    raw.clear();
}

static bool JsonString(const std::string& s, size_t& i, std::wstring& out) {
    out.clear();
    std::string raw;
    i++;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') { FlushUtf8(raw, out); return true; }
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            FlushUtf8(raw, out);
            switch (e) {
            case 'n': out += L'\n'; break;
            case 't': out += L'\t'; break;
            case 'r': out += L'\r'; break;
            case 'b': out += L'\b'; break;
            case 'f': out += L'\f'; break;
            case 'u':
                if (i + 4 <= s.size()) { out += (wchar_t)strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4; }
                break;
            default: out += (wchar_t)(unsigned char)e;
            }
        } else raw += c;
    }
    return false;
}

static void ParseJson(const std::string& s, std::map<std::string, std::wstring>& m) {
    size_t i = 0;
    auto ws = [&]() { while (i < s.size() && isspace((unsigned char)s[i])) i++; };
    ws();
    if (i >= s.size() || s[i] != '{') return;
    i++;
    while (true) {
        ws();
        if (i >= s.size() || s[i] == '}') break;
        if (s[i] == ',') { i++; continue; }
        if (s[i] != '"') break;
        std::wstring k;
        if (!JsonString(s, i, k)) break;
        ws();
        if (i >= s.size() || s[i] != ':') break;
        i++; ws();
        if (i < s.size() && s[i] == '"') {
            std::wstring v;
            if (!JsonString(s, i, v)) break;
            m[std::string(k.begin(), k.end())] = v;
        } else {
            while (i < s.size() && s[i] != ',' && s[i] != '}') i++;
        }
    }
}

static void LoadLanguage(const std::wstring& code) {
    g_tr.clear();
    if (_wcsicmp(code.c_str(), L"en") == 0) return;
    std::string data;
    if (ReadFileBytes(I18nDir() + L"\\" + code + L".json", data)) ParseJson(data, g_tr);
}

static void BuildLangList() {
    g_langs.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((I18nDir() + L"\\*.json").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring fn = fd.cFileName;
            Lang l;
            l.code = fn.substr(0, fn.size() - 5);
            if (_wcsicmp(l.code.c_str(), L"en") == 0) continue;
            std::string data;
            std::map<std::string, std::wstring> m;
            if (ReadFileBytes(I18nDir() + L"\\" + fn, data)) ParseJson(data, m);
            auto it = m.find("language");
            l.name = (it != m.end() && !it->second.empty()) ? it->second : l.code;
            g_langs.push_back(l);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    g_langs.push_back({ L"en", L"English" });
    std::sort(g_langs.begin(), g_langs.end(), [](const Lang& a, const Lang& b) {
        bool ae = _wcsicmp(a.code.c_str(), L"en") == 0, be = _wcsicmp(b.code.c_str(), L"en") == 0;
        if (ae != be) return ae;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

static std::wstring CfgPath() {
    PWSTR ap = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &ap);
    std::wstring dir = std::wstring(ap ? ap : L".") + L"\\Boost11";
    CoTaskMemFree(ap);
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

static void LoadCfg() {
    std::wstring p = CfgPath();
    const wchar_t* S = L"Boost11";
    g_cfg.method    = GetPrivateProfileIntW(S, L"method", 0, p.c_str());
    if (g_cfg.method != M_TRAY && g_cfg.method != M_SHORTCUT) g_cfg.method = M_NONE;
    GetPrivateProfileStringW(S, L"language", L"en", g_cfg.lang, 16, p.c_str());
    g_cfg.autostart = GetPrivateProfileIntW(S, L"autostart", 1, p.c_str()) != 0;
    GetPrivateProfileStringW(S, L"guidBalanced", L"", g_cfg.guidBal, 40, p.c_str());
    GetPrivateProfileStringW(S, L"guidHigh", L"", g_cfg.guidHigh, 40, p.c_str());
}

static void SaveCfg() {
    std::wstring p = CfgPath();
    const wchar_t* S = L"Boost11";
    auto wi = [&](const wchar_t* k, unsigned v) {
        WritePrivateProfileStringW(S, k, std::to_wstring(v).c_str(), p.c_str());
    };
    wi(L"method", g_cfg.method);
    WritePrivateProfileStringW(S, L"language", g_cfg.lang, p.c_str());
    wi(L"autostart", g_cfg.autostart ? 1 : 0);
    WritePrivateProfileStringW(S, L"guidBalanced", g_cfg.guidBal, p.c_str());
    WritePrivateProfileStringW(S, L"guidHigh", g_cfg.guidHigh, p.c_str());
}

static std::string RunHidden(std::wstring cmd) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return "";
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi = {};
    std::string out;
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (ok) {
        char buf[4096]; DWORD n;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(rd);
    return out;
}

static std::wstring PowercfgCmd(const wchar_t* args) {
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    return L"\"" + std::wstring(sys) + L"\\powercfg.exe\" " + args;
}

static bool IsGuidAt(const std::wstring& s, size_t i) {
    if (i + 36 > s.size()) return false;
    for (int k = 0; k < 36; k++) {
        wchar_t c = s[i + k];
        if (k == 8 || k == 13 || k == 18 || k == 23) { if (c != L'-') return false; }
        else if (!iswxdigit(c)) return false;
    }
    return true;
}
static bool FindGuid(const std::wstring& s, std::wstring& g, size_t& end) {
    for (size_t i = 0; i + 36 <= s.size(); i++)
        if (IsGuidAt(s, i)) { g = s.substr(i, 36); end = i + 36; return true; }
    return false;
}

struct Scheme { std::wstring guid, name; bool active = false; };

static std::vector<Scheme> ListSchemes() {
    std::vector<Scheme> v;
    std::wstring w = Widen(RunHidden(PowercfgCmd(L"-l")));
    size_t p = 0;
    while (p < w.size()) {
        size_t e = w.find(L'\n', p);
        if (e == std::wstring::npos) e = w.size();
        std::wstring line = w.substr(p, e - p);
        p = e + 1;
        Scheme sc; size_t end = 0;
        if (FindGuid(line, sc.guid, end)) {
            size_t a = line.find(L'(', end), b = line.rfind(L')');
            if (a != std::wstring::npos && b != std::wstring::npos && b > a)
                sc.name = line.substr(a + 1, b - a - 1);
            sc.active = line.find(L'*', end) != std::wstring::npos;
            v.push_back(sc);
        }
    }
    return v;
}

struct Det { bool bal = false, high = false, created = false; };

static Det DetectGuids(Config& c) {
    Det d;
    std::vector<Scheme> list = ListSchemes();
    auto find = [&](const wchar_t* exact, std::initializer_list<const wchar_t*> names) -> std::wstring {
        for (auto& s : list) if (!_wcsicmp(s.guid.c_str(), exact)) return s.guid;
        for (auto& s : list) {
            std::wstring n = Lower(s.name);
            for (auto k : names) if (n.find(k) != std::wstring::npos) return s.guid;
        }
        return L"";
    };
    std::wstring bal  = find(kGuidBalanced, { L"ausbalanciert", L"balanced" });
    std::wstring high = find(kGuidHigh, { L"h\u00f6chstleistung", L"high performance", L"hochleistung" });
    if (high.empty()) high = find(kGuidUltimate, { L"ultimative", L"ultimate" });
    if (high.empty()) {
        std::wstring o = Widen(RunHidden(PowercfgCmd((std::wstring(L"-duplicatescheme ") + kGuidHigh).c_str())));
        std::wstring g; size_t e;
        if (FindGuid(o, g, e)) { high = g; d.created = true; }
    }
    if (bal.empty()) bal = kGuidBalanced;
    wcsncpy(c.guidBal, bal.c_str(), 39);   c.guidBal[39] = 0;
    wcsncpy(c.guidHigh, high.c_str(), 39); c.guidHigh[39] = 0;
    d.bal = !bal.empty();
    d.high = !high.empty();
    return d;
}

static void EnsureGuids() {
    if (!g_cfg.guidBal[0] || !g_cfg.guidHigh[0]) { DetectGuids(g_cfg); SaveCfg(); }
}

static bool GuidFromStr(const wchar_t* s, GUID& g) {
    if (!s || !s[0]) return false;
    std::wstring b = L"{" + std::wstring(s) + L"}";
    return CLSIDFromString(b.c_str(), &g) == S_OK;
}

static int CurrentMode() {
    GUID* act = nullptr;
    if (PowerGetActiveScheme(nullptr, &act) != ERROR_SUCCESS || !act) return -1;
    int r = -1;
    GUID b, h;
    if (GuidFromStr(g_cfg.guidBal, b) && IsEqualGUID(*act, b)) r = 0;
    else if (GuidFromStr(g_cfg.guidHigh, h) && IsEqualGUID(*act, h)) r = 1;
    LocalFree(act);
    return r;
}

static bool ApplyMode(int m) {
    GUID g;
    if (!GuidFromStr(m ? g_cfg.guidHigh : g_cfg.guidBal, g) ||
        PowerSetActiveScheme(nullptr, &g) != ERROR_SUCCESS) {
        DetectGuids(g_cfg);
        SaveCfg();
        if (!GuidFromStr(m ? g_cfg.guidHigh : g_cfg.guidBal, g) ||
            PowerSetActiveScheme(nullptr, &g) != ERROR_SUCCESS) return false;
    }
    return true;
}

static int ToggleMode() {
    int next = (CurrentMode() == 1) ? 0 : 1;
    return ApplyMode(next) ? next : -1;
}

static struct {
    HWND hwnd = nullptr;
    HDC mem = nullptr;
    HBITMAP bmp = nullptr, old = nullptr;
    DWORD t0 = 0;
    int w = 0, h = 0;
    POINT pos = {};
    bool replacing = false;
} OV;

static void OvBlend(BYTE a) {
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, a, AC_SRC_ALPHA };
    POINT src = { 0, 0 };
    SIZE sz = { OV.w, OV.h };
    UpdateLayeredWindow(OV.hwnd, nullptr, &OV.pos, &sz, OV.mem, &src, 0, &bf, ULW_ALPHA);
}

static LRESULT CALLBACK OvProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_TIMER: {
        float e = (float)(GetTickCount() - OV.t0);
        const float tin = 140, hold = 520, tout = 1100;
        float a;
        if (e < tin) a = Smooth(e / tin);
        else if (e < tin + hold) a = 1;
        else if (e < tin + hold + tout) a = 1 - Smooth((e - tin - hold) / tout);
        else { DestroyWindow(h); return 0; }
        OvBlend((BYTE)(a * 255));
        return 0;
    }
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_DESTROY:
        KillTimer(h, 1);
        if (OV.mem) { SelectObject(OV.mem, OV.old); DeleteDC(OV.mem); OV.mem = nullptr; }
        if (OV.bmp) { DeleteObject(OV.bmp); OV.bmp = nullptr; }
        OV.hwnd = nullptr;
        if (g_quitAfterOverlay && !OV.replacing) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowOverlay(int mode) {
    if (OV.hwnd) { OV.replacing = true; DestroyWindow(OV.hwnd); OV.replacing = false; }
    POINT cur; GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);

    int sz = (int)(176.f * dx / 96.f);
    OV.w = OV.h = sz;
    OV.pos.x = (mi.rcMonitor.left + mi.rcMonitor.right - sz) / 2;
    OV.pos.y = (mi.rcMonitor.top + mi.rcMonitor.bottom - sz) / 2;

    HDC scr = GetDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    OV.bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    OV.mem = CreateCompatibleDC(scr);
    OV.old = (HBITMAP)SelectObject(OV.mem, OV.bmp);
    ReleaseDC(nullptr, scr);

    if (bits && g_img[mode]) {
        Bitmap canvas(sz, sz, sz * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&canvas);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.DrawImage(g_img[mode], Rect(0, 0, sz, sz));
    }
    GdiFlush();

    OV.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                              L"Boost11Overlay", L"", WS_POPUP, OV.pos.x, OV.pos.y, sz, sz,
                              nullptr, nullptr, g_inst, nullptr);
    OvBlend(0);
    ShowWindow(OV.hwnd, SW_SHOWNOACTIVATE);
    OV.t0 = GetTickCount();
    SetTimer(OV.hwnd, 1, 15, nullptr);
}

static NOTIFYICONDATAW g_nid;
static bool g_trayOn;
static int  g_trayMode = -2;

static void TrayFill(int mode) {
    HICON old = g_nid.hIcon;
    g_nid.hIcon = MakeIcon(g_img[mode == 1 ? 3 : 2], GetSystemMetrics(SM_CXSMICON));
    std::wstring tip = L"Boost11 \u2013 " + std::wstring(mode >= 0 ? ModeName(mode) : T("tray_other"));
    wcsncpy(g_nid.szTip, tip.c_str(), 127);
    if (old) DestroyIcon(old);
}

static void TrayAdd() {
    int m = CurrentMode();
    g_trayMode = m;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    TrayFill(m);
    g_trayOn = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

static void TrayRemove() {
    if (g_trayOn) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) { DestroyIcon(g_nid.hIcon); g_nid.hIcon = nullptr; }
    g_trayOn = false;
}

static void TrayRefresh() {
    if (!g_trayOn) return;
    int m = CurrentMode();
    if (m == g_trayMode) return;
    g_trayMode = m;
    TrayFill(m);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static struct {
    HWND h = nullptr;
    int hover = -1;
    int active = -1;
    int fy = 0;
    float s = 1;
    DWORD t0 = 0;
    bool tracking = false;
    Sm hv[4];
} P;
static DWORD g_popClosed;

static const int POP_W = 236, POP_H = 185;

static void PopRects(float s, RectF r[4]) {
    float pad = 6 * s, w = (POP_W - 12) * s, y = pad;
    r[0] = RectF(pad, y, w, 46 * s); y += 46 * s;
    r[1] = RectF(pad, y, w, 46 * s); y += 46 * s + 9 * s;
    r[2] = RectF(pad, y, w, 36 * s); y += 36 * s;
    r[3] = RectF(pad, y, w, 36 * s);
}

static void PopPaint(Graphics& g, float s) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(C_POPBG);
    RectF r[4]; PopRects(s, r);

    for (int i = 0; i < 4; i++) {
        float hv = P.hv[i].v;
        if (hv > 0.01f) {
            GraphicsPath p; RRect(p, r[i], 6 * s);
            SolidBrush b(Color((BYTE)(20 * hv), 255, 255, 255));
            g.FillPath(&b, &p);
        }
    }
    for (int i = 0; i < 2; i++) {
        bool act = (P.active == i);
        float cy = r[i].Y + r[i].Height / 2;
        if (act) {
            GraphicsPath p; RRect(p, RectF(r[i].X + 3 * s, cy - 9 * s, 3 * s, 18 * s), 1.5f * s);
            SolidBrush b(C_ACCENT); g.FillPath(&b, &p);
        }
        if (g_img[i]) g.DrawImage(g_img[i], RectF(r[i].X + 16 * s, cy - 13 * s, 26 * s, 26 * s));
        Txt(g, ModeName(i), 14 * s, act, C_TEXT, RectF(r[i].X + 54 * s, r[i].Y, 130 * s, r[i].Height),
            StringAlignmentNear, StringAlignmentCenter);
        if (act)
            Txt(g, T("popup_active"), 11 * s, false, C_ACCENT, RectF(r[i].GetRight() - 56 * s, r[i].Y, 46 * s, r[i].Height),
                StringAlignmentFar, StringAlignmentCenter);
    }
    float sy = r[1].GetBottom() + 4.5f * s;
    Pen sep(Color(28, 255, 255, 255), 1.f);
    g.DrawLine(&sep, 14 * s, sy, (POP_W - 14) * s, sy);
    Txt(g, T("popup_settings"), 13 * s, false, Color(255, 215, 215, 215),
        RectF(r[2].X + 16 * s, r[2].Y, r[2].Width - 20 * s, r[2].Height), StringAlignmentNear, StringAlignmentCenter);
    Txt(g, T("popup_quit"), 13 * s, false, Color(255, 215, 215, 215),
        RectF(r[3].X + 16 * s, r[3].Y, r[3].Width - 20 * s, r[3].Height), StringAlignmentNear, StringAlignmentCenter);
}

static int PopHit(float x, float y) {
    RectF r[4]; PopRects(P.s, r);
    for (int i = 0; i < 4; i++) if (r[i].Contains(x, y)) return i;
    return -1;
}

static LRESULT CALLBACK PopProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        { Graphics g(mem); PopPaint(g, P.s); }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!P.tracking) {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t); P.tracking = true;
        }
        P.hover = PopHit((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l));
        return 0;
    }
    case WM_MOUSELEAVE: P.hover = -1; P.tracking = false; return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, P.hover >= 0 ? IDC_HAND : IDC_ARROW)); return TRUE; }
        break;
    case WM_LBUTTONUP: {
        int i = PopHit((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l));
        if (i < 0) return 0;
        DestroyWindow(h);
        if (i < 2) { if (ApplyMode(i)) { TrayRefresh(); ShowOverlay(i); } }
        else if (i == 2) PostMessageW(g_main, WM_APP_SETUP, 0, 0);
        else PostMessageW(g_main, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_TIMER: {
        float t = Clamp01((float)(GetTickCount() - P.t0) / 170.f);
        float e = 1 - (1 - t) * (1 - t) * (1 - t);
        SetLayeredWindowAttributes(h, 0, (BYTE)(255 * e), LWA_ALPHA);
        RECT wr; GetWindowRect(h, &wr);
        int y = P.fy + (int)((1 - e) * 10 * P.s);
        if (wr.top != y) SetWindowPos(h, nullptr, wr.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        bool ch = false;
        for (int i = 0; i < 4; i++) { P.hv[i].t = (P.hover == i) ? 1.f : 0.f; ch |= P.hv[i].Tick(); }
        if (ch) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        P.h = nullptr;
        g_popClosed = GetTickCount();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowPopup() {
    if (P.h) { DestroyWindow(P.h); return; }
    if (GetTickCount() - g_popClosed < 250) return;

    POINT pt; GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    float s = dx / 96.f;

    int w = (int)(POP_W * s), hh = (int)(POP_H * s), mg = (int)(10 * s);
    int x = pt.x - w / 2;
    if (x + w > mi.rcWork.right - mg) x = mi.rcWork.right - mg - w;
    if (x < mi.rcWork.left + mg) x = mi.rcWork.left + mg;
    bool bottom = pt.y > (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
    int y = bottom ? mi.rcWork.bottom - hh - mg : mi.rcWork.top + mg;

    P = decltype(P)();
    P.s = s;
    P.active = CurrentMode();
    P.fy = y;
    P.t0 = GetTickCount();

    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, L"Boost11Popup", L"",
                             WS_POPUP, x, y + (int)(10 * s), w, hh, nullptr, nullptr, g_inst, nullptr);
    P.h = h;
    DwmSet(h, ATTR_CORNER, 2);
    DwmSet(h, ATTR_BORDER, RGB(60, 60, 60));
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetTimer(h, 1, 15, nullptr);
}

static std::wstring ShortcutPath() {
    PWSTR d = nullptr;
    SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &d);
    std::wstring p = std::wstring(d ? d : L"") + L"\\Boost11.lnk";
    CoTaskMemFree(d);
    return p;
}

static bool CreateDesktopShortcut() {
    std::wstring exe = ExePath();
    std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
    sl->SetPath(exe.c_str());
    sl->SetArguments(L"--toggle");
    sl->SetWorkingDirectory(dir.c_str());
    sl->SetIconLocation(exe.c_str(), 0);
    sl->SetDescription(L"Boost11 \u2013 Energiemodus wechseln");
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
        ok = SUCCEEDED(pf->Save(ShortcutPath().c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

static void SetAutostart(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, L"Boost11", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
    } else RegDeleteValueW(k, L"Boost11");
    RegCloseKey(k);
}

static const int LANG_VIS = 7;

struct Wiz {
    int sel = 0;
    bool autostart = true;
    bool hand = false, tracking = false, swallowUp = false;
    int hover = -1;
    const char* infoKey = "info_ok";
    std::wstring lang;
    bool langOpen = false;
    int langScroll = 0;
    Sm hov[2], radio[2], sw, swVis, bOk, bCancel, langBtn, langA, langItem[LANG_VIS];
};
static Wiz W;

struct WL { RectF card[2], swRow, sw, ok, cancel, langBtn, list; int nvis; };

static void WizLayout(WL& L, float s) {
    float M = 28 * s, Wd = (WIZ_W - 56) * s, y = 132 * s;
    for (int i = 0; i < 2; i++) { L.card[i] = RectF(M, y, Wd, 76 * s); y += 88 * s; }
    float by = (WIZ_H - 28 - 40) * s;
    L.ok      = RectF((WIZ_W - 28 - 120) * s, by, 120 * s, 40 * s);
    L.cancel  = RectF((WIZ_W - 28 - 250) * s, by, 120 * s, 40 * s);
    L.swRow   = RectF(M, by - 56 * s, Wd, 40 * s);
    L.sw      = RectF(L.swRow.GetRight() - 56 * s, L.swRow.Y + 9 * s, 44 * s, 22 * s);
    L.langBtn = RectF(M, by, 190 * s, 40 * s);
    L.nvis = (int)g_langs.size();
    if (L.nvis > LANG_VIS) L.nvis = LANG_VIS;
    float lh = (L.nvis * 36 + 8) * s;
    L.list = RectF(M, by - 6 * s - lh, 190 * s, lh);
}

static void WizTargets() {
    for (int i = 0; i < 2; i++) {
        W.hov[i].t = (W.hover == i) ? 1.f : 0.f;
        W.radio[i].t = (W.sel == i) ? 1.f : 0.f;
    }
    W.sw.t = W.autostart ? 1.f : 0.f;
    W.swVis.t = (W.sel == 1) ? 0.35f : 1.f;
    W.bOk.t = (W.hover == 3) ? 1.f : 0.f;
    W.bCancel.t = (W.hover == 4) ? 1.f : 0.f;
    W.langBtn.t = (W.hover == 5 || W.langOpen) ? 1.f : 0.f;
    W.langA.t = W.langOpen ? 1.f : 0.f;
    for (int k = 0; k < LANG_VIS; k++) W.langItem[k].t = (W.hover == 10 + W.langScroll + k) ? 1.f : 0.f;
}

static void WizSnap() {
    WizTargets();
    for (int i = 0; i < 2; i++) { W.hov[i].Snap(); W.radio[i].Snap(); }
    W.sw.Snap(); W.swVis.Snap(); W.bOk.Snap(); W.bCancel.Snap(); W.langBtn.Snap(); W.langA.Snap();
    for (auto& a : W.langItem) a.Snap();
}

static const wchar_t* LangName(const std::wstring& code) {
    for (auto& l : g_langs) if (l.code == code) return l.name.c_str();
    return code.c_str();
}

static void WizPaint(Graphics& g, float s) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(C_BG);

    WL L; WizLayout(L, s);

    {
        float isz = 56 * s, gap = 12 * s;
        RectF bb; StringFormat sf(StringFormatFlagsNoWrap);
        g.MeasureString(L"Boost11", -1, GetFont(32 * s, true), PointF(0, 0), &sf, &bb);
        float gw = isz + gap + bb.Width - 6 * s;
        float x0 = (WIZ_W * s - gw) / 2;
        if (g_img[1]) g.DrawImage(g_img[1], RectF(x0, 22 * s, isz, isz));
        Txt(g, L"Boost11", 32 * s, true, C_TEXT, RectF(x0 + isz + gap - 4 * s, 22 * s, bb.Width + 10 * s, isz),
            StringAlignmentNear, StringAlignmentCenter);
    }
    Txt(g, T("subtitle"), 13 * s, false, C_SUB, RectF(28 * s, 90 * s, (WIZ_W - 56) * s, 22 * s),
        StringAlignmentCenter);

    const wchar_t* titles[2] = { T("tray_title"), T("shortcut_title") };
    const wchar_t* descs[2]  = { T("tray_desc"), T("shortcut_desc") };

    for (int i = 0; i < 2; i++) {
        RectF r = L.card[i];
        float hv = W.hov[i].v, rd = W.radio[i].v;
        GraphicsPath p; RRect(p, r, 10 * s);
        SolidBrush fill(Lerp(C_CARD, C_CARDH, hv));
        g.FillPath(&fill, &p);
        Pen bp(Lerp(C_BORDER, C_ACCENT, rd), (1.f + 0.6f * rd) * s);
        g.DrawPath(&bp, &p);

        float cx = r.X + 27 * s, cy = r.Y + 38 * s;
        Pen rp(Lerp(Color(255, 150, 150, 150), C_ACCENT, rd), 1.8f * s);
        g.DrawEllipse(&rp, cx - 9 * s, cy - 9 * s, 18 * s, 18 * s);
        float dr = 5.f * s * rd;
        SolidBrush db(C_ACCENT);
        g.FillEllipse(&db, cx - dr, cy - dr, 2 * dr, 2 * dr);

        Txt(g, titles[i], 15 * s, true, C_TEXT, RectF(r.X + 54 * s, r.Y + 16 * s, r.Width - 70 * s, 22 * s));
        Txt(g, descs[i], 12 * s, false, C_SUB, RectF(r.X + 54 * s, r.Y + 41 * s, r.Width - 70 * s, 20 * s));
    }

    Txt(g, T(W.infoKey), 12 * s, false, C_SUB, RectF(28 * s, L.swRow.Y - 30 * s, (WIZ_W - 56) * s, 20 * s));

    float vis = W.swVis.v;
    Txt(g, T("autostart"), 13 * s, false, WithA(C_TEXT, vis),
        RectF(L.swRow.X, L.swRow.Y, 300 * s, L.swRow.Height), StringAlignmentNear, StringAlignmentCenter);
    {
        float k = W.sw.v;
        GraphicsPath tp; RRect(tp, L.sw, L.sw.Height / 2);
        SolidBrush tf(WithA(Lerp(Color(0, 0, 0, 0), C_ACCENT, k), vis));
        g.FillPath(&tf, &tp);
        Pen tb(WithA(Lerp(Color(255, 160, 160, 160), C_ACCENT, k), vis), 1.4f * s);
        g.DrawPath(&tb, &tp);
        float kd = 14.f * s;
        float kx = L.sw.X + 5 * s + k * (L.sw.Width - 10 * s - kd);
        SolidBrush kb(WithA(Lerp(Color(255, 200, 200, 200), Color(255, 20, 20, 20), k), vis));
        g.FillEllipse(&kb, kx, L.sw.Y + (L.sw.Height - kd) / 2, kd, kd);
    }

    {
        GraphicsPath p; RRect(p, L.langBtn, 8 * s);
        SolidBrush f(Lerp(C_CARD, C_CARDH, W.langBtn.v)); g.FillPath(&f, &p);
        Pen b(Lerp(C_BORDER, C_ACCENT, W.langA.v), 1.f * s); g.DrawPath(&b, &p);
        Txt(g, LangName(W.lang), 14 * s, false, C_TEXT,
            RectF(L.langBtn.X + 16 * s, L.langBtn.Y, L.langBtn.Width - 46 * s, L.langBtn.Height),
            StringAlignmentNear, StringAlignmentCenter);
        float cx = L.langBtn.GetRight() - 22 * s, cy = L.langBtn.Y + 20 * s;
        float d = (W.langOpen ? -2.f : 2.f) * s;
        PointF pts[3] = { PointF(cx - 4.5f * s, cy - d), PointF(cx, cy + d), PointF(cx + 4.5f * s, cy - d) };
        Pen cp(C_SUB, 1.7f * s);
        cp.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        cp.SetLineJoin(LineJoinRound);
        g.DrawLines(&cp, pts, 3);
    }

    {
        GraphicsPath p; RRect(p, L.cancel, 8 * s);
        SolidBrush f(Lerp(C_CARD, C_CARDH, W.bCancel.v)); g.FillPath(&f, &p);
        Pen b(C_BORDER, 1.f * s); g.DrawPath(&b, &p);
        Txt(g, T("btn_cancel"), 14 * s, false, C_TEXT, L.cancel, StringAlignmentCenter, StringAlignmentCenter);
    }
    {
        GraphicsPath p; RRect(p, L.ok, 8 * s);
        SolidBrush f(Lerp(C_ACCENT, C_ACCENTH, W.bOk.v)); g.FillPath(&f, &p);
        Txt(g, T("btn_done"), 14 * s, true, Color(255, 10, 10, 10), L.ok, StringAlignmentCenter, StringAlignmentCenter);
    }

    if (W.langA.v > 0.01f) {
        float a = W.langA.v;
        GraphicsPath p; RRect(p, L.list, 8 * s);
        SolidBrush bg(WithA(Color(255, 45, 45, 45), a)); g.FillPath(&bg, &p);
        Pen bd(WithA(Color(255, 80, 80, 80), a), 1.f * s); g.DrawPath(&bd, &p);
        for (int k = 0; k < L.nvis; k++) {
            int idx = W.langScroll + k;
            if (idx >= (int)g_langs.size()) break;
            RectF ir(L.list.X + 4 * s, L.list.Y + 4 * s + k * 36 * s, L.list.Width - 8 * s, 36 * s);
            float hv = W.langItem[k].v;
            if (hv > 0.01f) {
                GraphicsPath ip; RRect(ip, ir, 6 * s);
                SolidBrush hb(Color((BYTE)(22 * hv * a), 255, 255, 255)); g.FillPath(&hb, &ip);
            }
            bool cur = (g_langs[idx].code == W.lang);
            if (cur) {
                GraphicsPath bp2; RRect(bp2, RectF(ir.X + 2 * s, ir.Y + 10 * s, 3 * s, 16 * s), 1.5f * s);
                SolidBrush ab(WithA(C_ACCENT, a)); g.FillPath(&ab, &bp2);
            }
            Txt(g, g_langs[idx].name.c_str(), 14 * s, cur, WithA(cur ? C_ACCENT : C_TEXT, a),
                RectF(ir.X + 14 * s, ir.Y, ir.Width - 20 * s, ir.Height), StringAlignmentNear, StringAlignmentCenter);
        }
    }
}

static int WizHit(float x, float y, const WL& L, float s) {
    if (W.langOpen) {
        if (L.list.Contains(x, y)) {
            int i = (int)((y - L.list.Y - 4 * s) / (36 * s));
            if (i < 0) i = 0;
            if (i >= L.nvis) i = L.nvis - 1;
            int idx = W.langScroll + i;
            return idx < (int)g_langs.size() ? 10 + idx : 99;
        }
        return L.langBtn.Contains(x, y) ? 5 : 99;
    }
    if (L.ok.Contains(x, y)) return 3;
    if (L.cancel.Contains(x, y)) return 4;
    if (L.langBtn.Contains(x, y)) return 5;
    for (int i = 0; i < 2; i++) if (L.card[i].Contains(x, y)) return i;
    if (L.swRow.Contains(x, y)) return 2;
    return -1;
}

static void WizFinish() {
    Config c = g_cfg;
    c.method = (W.sel == 0) ? M_TRAY : M_SHORTCUT;
    c.autostart = W.autostart;
    wcsncpy(c.lang, W.lang.c_str(), 15); c.lang[15] = 0;
    if (c.method == M_SHORTCUT) {
        if (!CreateDesktopShortcut()) {
            W.infoKey = "err_shortcut";
            InvalidateRect(g_wiz, nullptr, FALSE);
            return;
        }
    } else DeleteFileW(ShortcutPath().c_str());
    SetAutostart(c.method == M_TRAY && c.autostart);
    g_cfg = c;
    SaveCfg();
    DestroyWindow(g_wiz);
}

static LRESULT CALLBACK WizProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        DwmSet(h, ATTR_DARK, 1);
        DwmSet(h, ATTR_CORNER, 2);
        DwmSet(h, ATTR_CAPTION, RGB(28, 28, 28));
        DwmSet(h, ATTR_TEXT, RGB(255, 255, 255));
        SetTimer(h, 1, 16, nullptr);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        { Graphics g(mem); WizPaint(g, WinScale(h)); }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER: {
        WizTargets();
        bool ch = false;
        for (int i = 0; i < 2; i++) { ch |= W.hov[i].Tick(); ch |= W.radio[i].Tick(); }
        ch |= W.sw.Tick(); ch |= W.swVis.Tick(); ch |= W.bOk.Tick(); ch |= W.bCancel.Tick();
        ch |= W.langBtn.Tick(); ch |= W.langA.Tick();
        for (auto& a : W.langItem) ch |= a.Tick();
        if (ch) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!W.tracking) { TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 }; TrackMouseEvent(&t); W.tracking = true; }
        WL L; float s = WinScale(h); WizLayout(L, s);
        int hit = WizHit((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), L, s);
        W.hover = hit;
        W.hand = hit >= 0 && hit != 99 && !(hit == 2 && W.sel == 1);
        return 0;
    }
    case WM_MOUSELEAVE: W.hover = -1; W.hand = false; W.tracking = false; return 0;
    case WM_MOUSEWHEEL:
        if (W.langOpen && (int)g_langs.size() > LANG_VIS) {
            int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1;
            int mx = (int)g_langs.size() - LANG_VIS;
            W.langScroll += d;
            if (W.langScroll < 0) W.langScroll = 0;
            if (W.langScroll > mx) W.langScroll = mx;
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, W.hand ? IDC_HAND : IDC_ARROW)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        WL L; float s = WinScale(h); WizLayout(L, s);
        int hit = WizHit((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), L, s);
        SetFocus(h);
        if (W.langOpen) {
            W.langOpen = false;
            W.swallowUp = true;
            if (hit >= 10 && hit < 99) {
                W.lang = g_langs[hit - 10].code;
                LoadLanguage(W.lang);
            }
            return 0;
        }
        if (hit == 0 || hit == 1) W.sel = hit;
        else if (hit == 2) { if (W.sel != 1) W.autostart = !W.autostart; }
        else if (hit == 5) { W.langOpen = true; W.swallowUp = true; }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (W.swallowUp) { W.swallowUp = false; return 0; }
        WL L; float s = WinScale(h); WizLayout(L, s);
        int hit = WizHit((float)GET_X_LPARAM(l), (float)GET_Y_LPARAM(l), L, s);
        if (hit == 3) WizFinish();
        else if (hit == 4) DestroyWindow(h);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { if (W.langOpen) W.langOpen = false; else DestroyWindow(h); return 0; }
        if (w == VK_RETURN && !W.langOpen) { WizFinish(); return 0; }
        break;
    case WM_KILLFOCUS: W.langOpen = false; return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        g_wiz = nullptr;
        LoadLanguage(g_cfg.lang);
        PostMessageW(g_main, WM_APP_WIZCLOSED, 0, 0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void OpenWizard() {
    if (g_wiz) { SetForegroundWindow(g_wiz); return; }

    W = Wiz();
    W.sel = (g_cfg.method == M_SHORTCUT) ? 1 : 0;
    W.autostart = g_cfg.autostart;
    W.lang = g_cfg.lang;
    BuildLangList();

    Det d = DetectGuids(g_cfg);
    SaveCfg();
    W.infoKey = d.created ? "info_created" : (d.high ? "info_ok" : "info_missing");
    WizSnap();

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    g_wiz = CreateWindowExW(0, L"Boost11Wizard", L"Boost11 Wizard", style, 100, 100, 300, 300,
                            nullptr, nullptr, g_inst, nullptr);
    UINT dpi = GetDpiForWindow(g_wiz);
    RECT rc = { 0, 0, (LONG)(WIZ_W * dpi / 96.f), (LONG)(WIZ_H * dpi / 96.f) };
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    SetWindowPos(g_wiz, nullptr, (mi.rcWork.left + mi.rcWork.right - ww) / 2,
                 (mi.rcWork.top + mi.rcWork.bottom - wh) / 2, ww, wh, SWP_NOZORDER);
    ShowWindow(g_wiz, SW_SHOW);
    SetForegroundWindow(g_wiz);
}

static void ApplyConfig() {
    if (g_cfg.method == M_TRAY) { if (!g_trayOn) TrayAdd(); }
    else TrayRemove();
    KillTimer(g_main, 1);
    if (g_cfg.method == M_TRAY) SetTimer(g_main, 1, 3000, nullptr);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_msgTaskbar && g_msgTaskbar) { if (g_cfg.method == M_TRAY) { g_trayOn = false; TrayAdd(); } return 0; }
    switch (m) {
    case WM_TRAYICON:
        if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_RBUTTONUP) ShowPopup();
        return 0;
    case WM_TIMER: TrayRefresh(); return 0;
    case WM_APP_SETUP: OpenWizard(); return 0;
    case WM_APP_WIZCLOSED:
        if (g_cfg.method == M_TRAY) { ApplyConfig(); g_trayMode = -2; TrayRefresh(); }
        else PostQuitMessage(0);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        TrayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void RegClass(const wchar_t* name, WNDPROC proc, UINT style = 0, HICON icon = nullptr) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    g_inst = hi;
    std::wstring cmd = GetCommandLineW();
    bool toggle = cmd.find(L"--toggle") != std::wstring::npos;
    bool setup  = cmd.find(L"--setup") != std::wstring::npos;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput gin;
    GdiplusStartup(&g_gdip, &gin, nullptr);
    g_img[0] = LoadPng(IDR_BALANCED);
    g_img[1] = LoadPng(IDR_HIGH);
    g_img[2] = LoadPng(IDR_TRAY_BAL);
    g_img[3] = LoadPng(IDR_TRAY_HIGH);
    LoadCfg();
    LoadLanguage(g_cfg.lang);

    HICON appIcon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APP));
    RegClass(L"Boost11Overlay", OvProc);
    RegClass(L"Boost11Popup", PopProc, CS_DROPSHADOW);
    RegClass(L"Boost11Wizard", WizProc, 0, appIcon);
    RegClass(L"Boost11Main", MainProc);

    int exitCode = 0;
    if (toggle) {
        EnsureGuids();
        int r = ToggleMode();
        if (r < 0) exitCode = 1;
        else { g_quitAfterOverlay = true; ShowOverlay(r); }
    } else {
        HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\Boost11_Singleton");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND w = FindWindowW(L"Boost11Main", nullptr);
            if (w) PostMessageW(w, WM_APP_SETUP, 0, 0);
            exitCode = 0;
            goto done;
        }
        g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
        g_main = CreateWindowExW(WS_EX_TOOLWINDOW, L"Boost11Main", L"Boost11", WS_POPUP, 0, 0, 0, 0,
                                 nullptr, nullptr, hi, nullptr);
        if (g_cfg.method != M_NONE) EnsureGuids();
        if (g_cfg.method == M_NONE || g_cfg.method == M_SHORTCUT || setup) OpenWizard();
        else ApplyConfig();
        (void)mtx;
    }

    if (exitCode == 0) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    for (auto* b : g_img) delete b;
    GdiplusShutdown(g_gdip);
    CoUninitialize();
    return exitCode;
}