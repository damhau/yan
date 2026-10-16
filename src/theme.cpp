// Colors, fonts, dark mode plumbing and the custom-drawn menu bar.
#include "common.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>

namespace yan {

Theme theme;
Fonts fonts;

bool SystemUsesDarkTheme() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return false;
    return value == 0;
}

void BuildTheme(bool dark) {
    Theme& t = theme;
    t.dark = dark;
    if (dark) {
        t.chrome = RGB(0x20, 0x20, 0x20);
        t.surface = RGB(0x26, 0x26, 0x26);
        t.editorBg = RGB(0x1A, 0x1A, 0x1A);
        t.editorFg = RGB(0xD8, 0xD8, 0xD8);
        t.text = RGB(0xEC, 0xEC, 0xEC);
        t.textDim = RGB(0x9E, 0x9E, 0x9E);
        t.textDisabled = RGB(0x5C, 0x5C, 0x5C);
        t.border = RGB(0x33, 0x33, 0x33);
        t.borderStrong = RGB(0x4A, 0x4A, 0x4A);
        t.hover = RGB(0x2F, 0x2F, 0x2F);
        t.pressed = RGB(0x3A, 0x3A, 0x3A);
        t.accent = RGB(0x4C, 0xC2, 0xFF);
        t.accentText = RGB(0x00, 0x1A, 0x2E);
        t.accentSoft = RGB(0x1E, 0x3A, 0x4E);
        t.inputBg = RGB(0x1C, 0x1C, 0x1C);
        t.tabActive = t.editorBg;
        t.lineNumFg = RGB(0x6E, 0x6E, 0x6E);
        t.lineNumBg = t.editorBg;
        t.caretLine = RGB(0x24, 0x24, 0x24);
        t.selection = RGB(0x26, 0x4F, 0x78);
        t.selectionAdditional = RGB(0x22, 0x44, 0x66);
        t.selectionInactive = RGB(0x3A, 0x3D, 0x41);
        t.caret = RGB(0xAE, 0xDF, 0xFF);
        t.whitespace = RGB(0x4A, 0x4A, 0x4A);
        t.indentGuide = RGB(0x33, 0x33, 0x33);
        t.findHL = RGB(0xC8, 0x8A, 0x1E);
        t.markHL = RGB(0x2E, 0xA0, 0x43);
        t.smartHL = RGB(0x5A, 0x8F, 0xC8);
        t.scopeHL = RGB(0x4C, 0x6A, 0x8A);
        t.braceFg = RGB(0xFF, 0xD7, 0x00);
        t.error = RGB(0xFF, 0x7B, 0x72);
        t.success = RGB(0x6C, 0xCB, 0x5F);
        t.modified = RGB(0xE8, 0xB3, 0x4B);
        t.histModified = RGB(0xC2, 0x8B, 0x2C);
        t.histSaved = RGB(0x3F, 0x9F, 0x4F);
        t.histReverted = RGB(0x4C, 0x8F, 0xD6);
    } else {
        t.chrome = RGB(0xF3, 0xF3, 0xF3);
        t.surface = RGB(0xF7, 0xF7, 0xF7);
        t.editorBg = RGB(0xFF, 0xFF, 0xFF);
        t.editorFg = RGB(0x1F, 0x1F, 0x1F);
        t.text = RGB(0x1A, 0x1A, 0x1A);
        t.textDim = RGB(0x5F, 0x5F, 0x5F);
        t.textDisabled = RGB(0xA8, 0xA8, 0xA8);
        t.border = RGB(0xE0, 0xE0, 0xE0);
        t.borderStrong = RGB(0xC4, 0xC4, 0xC4);
        t.hover = RGB(0xE6, 0xE6, 0xE6);
        t.pressed = RGB(0xDA, 0xDA, 0xDA);
        t.accent = RGB(0x00, 0x5F, 0xB8);
        t.accentText = RGB(0xFF, 0xFF, 0xFF);
        t.accentSoft = RGB(0xD6, 0xE8, 0xF8);
        t.inputBg = RGB(0xFF, 0xFF, 0xFF);
        t.tabActive = t.editorBg;
        t.lineNumFg = RGB(0x9A, 0x9A, 0x9A);
        t.lineNumBg = t.editorBg;
        t.caretLine = RGB(0xF4, 0xF7, 0xFB);
        t.selection = RGB(0xAD, 0xD6, 0xFF);
        t.selectionAdditional = RGB(0xC4, 0xE1, 0xFF);
        t.selectionInactive = RGB(0xE0, 0xE0, 0xE0);
        t.caret = RGB(0x00, 0x00, 0x00);
        t.whitespace = RGB(0xBF, 0xBF, 0xBF);
        t.indentGuide = RGB(0xE4, 0xE4, 0xE4);
        t.findHL = RGB(0xFF, 0xB0, 0x20);
        t.markHL = RGB(0x3C, 0xB3, 0x50);
        t.smartHL = RGB(0x5B, 0x9B, 0xE0);
        t.scopeHL = RGB(0x9E, 0xC2, 0xE6);
        t.braceFg = RGB(0xC0, 0x00, 0x60);
        t.error = RGB(0xC4, 0x2B, 0x1C);
        t.success = RGB(0x10, 0x7C, 0x10);
        t.modified = RGB(0xCA, 0x7A, 0x00);
        t.histModified = RGB(0xE0, 0xA0, 0x30);
        t.histSaved = RGB(0x5C, 0xB8, 0x5C);
        t.histReverted = RGB(0x5B, 0x9B, 0xE0);
    }

    // Syntax colors: VS Code "Dark Modern" / "Light Modern" inspired.
    struct SynColor { Syn role; COLORREF dark, light; };
    const SynColor kSyntax[] = {
        {Syn::Comment,    RGB(0x6A, 0x99, 0x55), RGB(0x00, 0x80, 0x00)},
        {Syn::CommentDoc, RGB(0x60, 0x8B, 0x4E), RGB(0x3F, 0x7F, 0x5F)},
        {Syn::Keyword,    RGB(0x56, 0x9C, 0xD6), RGB(0x00, 0x00, 0xFF)},
        {Syn::Type,       RGB(0x4E, 0xC9, 0xB0), RGB(0x26, 0x7F, 0x99)},
        {Syn::String,     RGB(0xCE, 0x91, 0x78), RGB(0xA3, 0x15, 0x15)},
        {Syn::Number,     RGB(0xB5, 0xCE, 0xA8), RGB(0x09, 0x86, 0x58)},
        {Syn::Operator,   RGB(0xB4, 0xB4, 0xB4), RGB(0x40, 0x40, 0x40)},
        {Syn::Preproc,    RGB(0xC5, 0x86, 0xC0), RGB(0xAF, 0x00, 0xDB)},
        {Syn::Regex,      RGB(0xD7, 0xBA, 0x7D), RGB(0x81, 0x1F, 0x3F)},
        {Syn::Tag,        RGB(0x56, 0x9C, 0xD6), RGB(0x80, 0x00, 0x00)},
        {Syn::Attribute,  RGB(0x9C, 0xDC, 0xFE), RGB(0xE5, 0x00, 0x00)},
        {Syn::Entity,     RGB(0xD7, 0xBA, 0x7D), RGB(0x8A, 0x5A, 0x00)},
        {Syn::Variable,   RGB(0x9C, 0xDC, 0xFE), RGB(0x00, 0x10, 0x80)},
        {Syn::Function,   RGB(0xDC, 0xDC, 0xAA), RGB(0x79, 0x5E, 0x26)},
        {Syn::Key,        RGB(0x9C, 0xDC, 0xFE), RGB(0x04, 0x51, 0xA5)},
        {Syn::Heading,    RGB(0x56, 0x9C, 0xD6), RGB(0x00, 0x00, 0xA0)},
        {Syn::Strong,     t.editorFg,            t.editorFg},
        {Syn::Emphasis,   t.editorFg,            t.editorFg},
        {Syn::Link,       RGB(0x4F, 0xC1, 0xFF), RGB(0x00, 0x66, 0xCC)},
        {Syn::Deleted,    RGB(0xF4, 0x87, 0x71), RGB(0xC4, 0x1A, 0x16)},
        {Syn::Inserted,   RGB(0x81, 0xC7, 0x84), RGB(0x10, 0x7C, 0x10)},
        {Syn::Meta,       RGB(0xC5, 0x86, 0xC0), RGB(0x00, 0x70, 0xC1)},
        {Syn::Error,      t.error,               t.error},
    };
    static_assert(std::size(kSyntax) == (size_t)Syn::Count);
    for (const SynColor& c : kSyntax) t.syntax[(int)c.role] = dark ? c.dark : c.light;
}

// ---------------------------------------------------------------------------
// Undocumented but stable uxtheme exports (used by Explorer, Notepad++, Terminal...)
enum PreferredAppMode { AppModeDefault, AppModeAllowDark, AppModeForceDark, AppModeForceLight };
using FnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FnFlushMenuThemes = void(WINAPI*)();
using FnAllowDarkModeForWindow = bool(WINAPI*)(HWND, bool);
static FnSetPreferredAppMode pSetPreferredAppMode;
static FnFlushMenuThemes pFlushMenuThemes;
static FnAllowDarkModeForWindow pAllowDarkModeForWindow;

void InitDarkModeSupport() {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    pSetPreferredAppMode = (FnSetPreferredAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    pFlushMenuThemes = (FnFlushMenuThemes)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    pAllowDarkModeForWindow = (FnAllowDarkModeForWindow)GetProcAddress(ux, MAKEINTRESOURCEA(133));
}

void ApplyWindowTheme(HWND hwnd) {
    if (pSetPreferredAppMode) pSetPreferredAppMode(theme.dark ? AppModeForceDark : AppModeForceLight);
    if (pFlushMenuThemes) pFlushMenuThemes();
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(hwnd, theme.dark);
    BOOL dark = theme.dark;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    // Windows 11: make the caption blend into our chrome color.
    COLORREF caption = theme.chrome;
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
    COLORREF border = theme.dark ? RGB(0x38, 0x38, 0x38) : RGB(0xD0, 0xD0, 0xD0);
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
}

void ApplyControlTheme(HWND hwnd, bool editLike) {
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(hwnd, theme.dark);
    if (editLike)
        SetWindowTheme(hwnd, theme.dark ? L"DarkMode_CFD" : L"CFD", nullptr);
    else
        SetWindowTheme(hwnd, theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

// ---------------------------------------------------------------------------
// Menu bar painting through the undocumented UAH messages, so the menu bar
// matches our chrome color in both light and dark themes.
constexpr UINT WM_UAHDRAWMENU = 0x0091;
constexpr UINT WM_UAHDRAWMENUITEM = 0x0092;

struct UAHMENU { HMENU hmenu; HDC hdc; DWORD dwFlags; };
union UAHMENUITEMMETRICS {
    struct { DWORD cx, cy; } rgsizeBar[2];
    struct { DWORD cx, cy; } rgsizePopup[4];
};
struct UAHMENUPOPUPMETRICS { DWORD rgcx[4]; DWORD fUpdateMaxWidths : 2; };
struct UAHMENUITEM { int iPosition; UAHMENUITEMMETRICS umim; UAHMENUPOPUPMETRICS umpm; };
struct UAHDRAWMENUITEM { DRAWITEMSTRUCT dis; UAHMENU um; UAHMENUITEM umi; };

static void PaintMenuBarBottomLine(HWND hwnd) {
    MENUBARINFO mbi{sizeof(mbi)};
    if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) return;
    RECT client;
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, nullptr, (POINT*)&client, 2);
    RECT win;
    GetWindowRect(hwnd, &win);
    OffsetRect(&client, -win.left, -win.top);
    RECT line = client;
    line.bottom = line.top;
    line.top--;
    HDC dc = GetWindowDC(hwnd);
    FillRectColor(dc, line, theme.chrome);
    ReleaseDC(hwnd, dc);
}

bool HandleMenuBarMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    switch (msg) {
    case WM_UAHDRAWMENU: {
        auto* m = (UAHMENU*)lp;
        MENUBARINFO mbi{sizeof(mbi)};
        GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi);
        RECT win;
        GetWindowRect(hwnd, &win);
        RECT rc = mbi.rcBar;
        OffsetRect(&rc, -win.left, -win.top);
        rc.top -= 1;
        FillRectColor(m->hdc, rc, theme.chrome);
        *result = TRUE;
        return true;
    }
    case WM_UAHDRAWMENUITEM: {
        auto* d = (UAHDRAWMENUITEM*)lp;
        wchar_t text[128] = {};
        MENUITEMINFOW mii{sizeof(mii), MIIM_STRING};
        mii.dwTypeData = text;
        mii.cch = 127;
        GetMenuItemInfoW(d->um.hmenu, d->umi.iPosition, TRUE, &mii);
        UINT state = d->dis.itemState;
        COLORREF bg = theme.chrome, fg = theme.text;
        if (state & (ODS_HOTLIGHT)) bg = theme.hover;
        if (state & ODS_SELECTED) bg = theme.pressed;
        if (state & (ODS_INACTIVE | ODS_DISABLED | ODS_GRAYED)) fg = theme.textDim;
        RECT rc = d->dis.rcItem;
        FillRectColor(d->um.hdc, rc, theme.chrome);
        if (bg != theme.chrome) {
            RECT r = rc;
            InflateRect(&r, 0, -2);
            RoundRectColor(d->um.hdc, r, 4, bg, bg);
        }
        UINT flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        if (state & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
        SetBkMode(d->um.hdc, TRANSPARENT);
        SetTextColor(d->um.hdc, fg);
        DrawTextW(d->um.hdc, text, -1, &rc, flags);
        *result = TRUE;
        return true;
    }
    case WM_NCPAINT:
    case WM_NCACTIVATE:
        *result = DefWindowProcW(hwnd, msg, wp, lp);
        PaintMenuBarBottomLine(hwnd);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
static HFONT MakeFont(const wchar_t* face, int pt, int weight, UINT dpi) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pt, (int)dpi, 72);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, face);
    return CreateFontIndirectW(&lf);
}

static bool FontExists(HDC dc, HFONT f, const wchar_t* face) {
    HGDIOBJ old = SelectObject(dc, f);
    wchar_t actual[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, actual);
    SelectObject(dc, old);
    return _wcsicmp(actual, face) == 0;
}

bool FontInstalled(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    HFONT f = MakeFont(face, 10, FW_NORMAL, 96);
    bool ok = FontExists(dc, f, face);
    DeleteObject(f);
    ReleaseDC(nullptr, dc);
    return ok;
}

void Fonts::Create(UINT newDpi) {
    Destroy();
    dpi = newDpi;
    HDC dc = GetDC(nullptr);
    const wchar_t* face = L"Segoe UI Variable Text";
    ui = MakeFont(face, 9, FW_NORMAL, dpi);
    if (!FontExists(dc, ui, face)) {
        DeleteObject(ui);
        face = L"Segoe UI";
        ui = MakeFont(face, 9, FW_NORMAL, dpi);
    }
    uiBold = MakeFont(face, 9, FW_SEMIBOLD, dpi);
    uiSmall = MakeFont(face, 8, FW_NORMAL, dpi);
    const wchar_t* iconFace = L"Segoe Fluent Icons";
    icons = MakeFont(iconFace, 8, FW_NORMAL, dpi);
    if (!FontExists(dc, icons, iconFace)) {
        DeleteObject(icons);
        iconFace = L"Segoe MDL2 Assets";
        icons = MakeFont(iconFace, 8, FW_NORMAL, dpi);
    }
    iconsSmall = MakeFont(iconFace, 6, FW_NORMAL, dpi);
    ReleaseDC(nullptr, dc);
}

void Fonts::Destroy() {
    for (HFONT* f : {&ui, &uiBold, &uiSmall, &icons, &iconsSmall})
        if (*f) { DeleteObject(*f); *f = nullptr; }
}

void FillRectColor(HDC dc, const RECT& rc, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

// Anti-aliased rounded rectangle with a 1 px border. GDI's RoundRect has jagged corners, so we
// compute each pixel's coverage from the shape's signed distance and AlphaBlend the result.
void RoundRectColor(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border) {
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    // One reusable 32-bit DIB, grown as needed.
    static HDC mem;
    static HBITMAP bmp;
    static uint32_t* bits;
    static int capW, capH;
    if (w > capW || h > capH) {
        capW = std::max(w, capW);
        capH = std::max(h, capH);
        if (!mem) mem = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), capW, -capH, 1, 32, BI_RGB};
        HBITMAP nb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        SelectObject(mem, nb);
        if (bmp) DeleteObject(bmp);
        bmp = nb;
    }
    const float r = std::clamp((float)radius, 0.0f, std::min(w, h) / 2.0f);
    const float hx = w / 2.0f, hy = h / 2.0f;
    const float fr = GetRValue(fill), fg = GetGValue(fill), fb = GetBValue(fill);
    const float br = GetRValue(border), bg = GetGValue(border), bb = GetBValue(border);
    for (int y = 0; y < h; y++) {
        uint32_t* row = bits + (size_t)y * capW;
        float py = std::fabs(y + 0.5f - hy) - (hy - r);
        for (int x = 0; x < w; x++) {
            float px = std::fabs(x + 0.5f - hx) - (hx - r);
            float qx = std::max(px, 0.0f), qy = std::max(py, 0.0f);
            float d = std::sqrt(qx * qx + qy * qy) + std::min(std::max(px, py), 0.0f) - r; // <0 inside
            float outer = std::clamp(0.5f - d, 0.0f, 1.0f);       // whole shape
            float inner = std::clamp(-0.5f - d, 0.0f, 1.0f);      // inside the 1 px border
            float ring = outer - inner;
            // Premultiplied BGRA.
            auto ch = [&](float f, float b) { return (uint32_t)(f * inner + b * ring + 0.5f); };
            row[x] = ch(fb, bb) | ch(fg, bg) << 8 | ch(fr, br) << 16 | (uint32_t)(outer * 255 + 0.5f) << 24;
        }
    }
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, rc.left, rc.top, w, h, mem, 0, 0, w, h, bf);
}

void DrawTextColor(HDC dc, const std::wstring& s, RECT rc, COLORREF c, HFONT f, UINT flags) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), (int)s.size(), &rc, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

int TextWidth(HDC dc, const std::wstring& s, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

} // namespace yan
