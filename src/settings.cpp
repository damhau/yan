// Settings window: custom-drawn like the find panel so it follows the light/dark theme.
// Every change is applied at once and written to yan.ini.
#include "app.h"
#include <algorithm>
#include <functional>
#include <windowsx.h>

namespace yan {
namespace {

const wchar_t* kClass = L"YanSettings";

struct Row {
    enum Kind { Header, Switch, Choice, Number, Button } kind;
    std::wstring label, desc;
    std::function<int()> get;            // Switch: 0/1, Choice: index, Number: value
    std::function<void(int)> set;
    std::vector<std::wstring> choices;   // Choice
    int min = 0, max = 0;                // Number
    std::function<std::wstring()> value; // Button: current value shown under the label
    std::function<bool()> enabled;
    // Layout
    RECT rc{}, ctrl{};
    std::vector<RECT> parts;             // Choice segments, Number minus/plus
    bool Enabled() const { return !enabled || enabled(); }
    bool Focusable() const { return kind != Header && Enabled(); }
};

struct Page { const wchar_t* name; std::vector<Row> rows; };

class SettingsWindow {
public:
    void Run();

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Build();
    void Layout();
    void Paint(HDC dc);
    void Changed();
    void MakeTitleFont();
    void Activate(int row, int part); // click or key on a control
    void MoveFocus(int dir);
    void Hit(POINT pt, int& nav, int& row, int& part, bool& close) const;
    int P(int v) const { return Px(v, app->dpi); }

    HWND hwnd_ = nullptr;
    HFONT title_ = nullptr;
    std::vector<Page> pages_;
    int page_ = 0;
    std::vector<RECT> nav_;
    RECT close_{}, footer_{};
    int hoverNav_ = -1, hoverRow_ = -1, hoverPart_ = -1, focus_ = -1;
    bool hoverClose_ = false, tracking_ = false, done_ = false;
};

void SettingsWindow::Build() {
    App& a = *app;
    ViewOptions& v = a.view;
    auto sw = [](const wchar_t* label, const wchar_t* desc, bool& value) {
        Row r{Row::Switch, label, desc ? desc : L""};
        r.get = [&value] { return value ? 1 : 0; };
        r.set = [&value](int x) { value = x != 0; };
        return r;
    };
    auto header = [](const wchar_t* label) { return Row{Row::Header, label}; };

    Page general{L"General"};
    general.rows.push_back(header(L"Startup and closing"));
    general.rows.push_back(sw(L"Reopen the files that were open", L"When Yan starts, restore the tabs from last time",
                              a.restoreSession));
    general.rows.push_back(sw(L"Close Yan when the last tab is closed", L"Otherwise an empty tab is left open",
                              a.exitOnLastTab));
    general.rows.push_back(sw(L"Open files in the same window",
                              L"Files opened from Explorer or the command line go to the running Yan",
                              a.singleInstance));
    Row recentCount{Row::Number, L"Recent files to remember", L"Listed in File > Recent files; 0 turns the list off"};
    recentCount.min = 0;
    recentCount.max = CMD_RECENT_LAST - CMD_RECENT + 1;
    recentCount.get = [&a] { return a.recentMax; };
    recentCount.set = [&a](int x) { a.recentMax = x; };
    general.rows.push_back(recentCount);
    general.rows.push_back(header(L"Files changed by other programs"));
    general.rows.push_back(sw(L"Detect changes", L"Checked when Yan gets the focus back", a.checkChanges));
    Row reload = sw(L"Reload without asking", L"Only for files with no unsaved changes in Yan", a.autoReload);
    reload.enabled = [&a] { return a.checkChanges; };
    general.rows.push_back(reload);
    general.rows.push_back(header(L"Save all to folder"));
    Row dir{Row::Button, L"Default folder"};
    dir.value = [&a] { return a.saveAllDir.empty() ? std::wstring(L"Not set: starts in the current file's folder")
                                                   : a.saveAllDir; };
    dir.set = [this, &a](int) {
        std::wstring d = a.FolderDialog(a.saveAllDir, hwnd_);
        if (!d.empty()) a.saveAllDir = d;
    };
    general.rows.push_back(dir);

    Page editor{L"Editor"};
    editor.rows.push_back(header(L"Typing"));
    Row tab{Row::Number, L"Tab width", L"Columns per tab character"};
    tab.min = 1;
    tab.max = 16;
    tab.get = [&v] { return v.tabWidth; };
    tab.set = [&v](int x) { v.tabWidth = x; };
    editor.rows.push_back(tab);
    editor.rows.push_back(sw(L"Insert spaces instead of tabs", nullptr, v.useSpaces));
    editor.rows.push_back(sw(L"Auto-indent", L"A new line starts with the indentation of the previous one",
                             v.autoIndent));
    editor.rows.push_back(sw(L"Trim trailing whitespace when saving", nullptr, v.trimOnSave));
    editor.rows.push_back(header(L"Highlighting"));
    editor.rows.push_back(sw(L"Syntax highlighting", L"Colors for C++, Python, HTML, JSON and 20 more languages",
                             v.syntax));
    editor.rows.push_back(sw(L"Current line", nullptr, v.caretLine));
    editor.rows.push_back(sw(L"Other occurrences of the selected word", nullptr, v.smartHighlight));
    editor.rows.push_back(sw(L"Matching braces", nullptr, v.braceMatch));

    Page display{L"Display"};
    display.rows.push_back(header(L"Appearance"));
    Row th{Row::Choice, L"Theme"};
    th.choices = {L"System", L"Light", L"Dark"};
    th.get = [&a] { return (int)a.themeMode; };
    th.set = [&a](int x) { a.SetThemeMode((ThemeMode)x); };
    display.rows.push_back(th);
    Row font{Row::Button, L"Font"};
    font.value = [&v] { return v.font + L", " + std::to_wstring(v.fontSize) + L" pt"; };
    font.set = [this, &a](int) { a.ChooseEditorFont(hwnd_); };
    display.rows.push_back(font);
    display.rows.push_back(sw(L"DirectWrite rendering",
                              L"Smoother text and color emoji, but about 130 ms slower to start", v.directWrite));
    display.rows.push_back(header(L"Show"));
    display.rows.push_back(sw(L"Line numbers", nullptr, v.lineNumbers));
    display.rows.push_back(sw(L"Code folding", L"Fold margin for languages with blocks (Alt+0 folds all)", v.folding));
    display.rows.push_back(sw(L"Word wrap", nullptr, v.wrap));
    display.rows.push_back(sw(L"Whitespace", nullptr, v.whitespace));
    display.rows.push_back(sw(L"End of line markers", nullptr, v.eolMarks));
    display.rows.push_back(sw(L"Indent guides", nullptr, v.indentGuides));
    display.rows.push_back(sw(L"Scroll beyond the last line", nullptr, v.scrollPastEnd));

    Page newDocs{L"New documents"};
    newDocs.rows.push_back(header(L"New documents"));
    Row eol{Row::Choice, L"Line endings"};
    eol.choices = {L"Windows (CRLF)", L"Unix (LF)", L"Mac (CR)"};
    eol.get = [&a] { return a.newEol == SC_EOL_CRLF ? 0 : a.newEol == SC_EOL_LF ? 1 : 2; };
    eol.set = [&a](int x) { a.newEol = x == 0 ? SC_EOL_CRLF : x == 1 ? SC_EOL_LF : SC_EOL_CR; };
    newDocs.rows.push_back(eol);
    Row enc{Row::Choice, L"Encoding"};
    enc.choices = {L"UTF-8", L"UTF-8 with BOM"};
    enc.get = [&a] { return a.newEncoding == Encoding::Utf8Bom ? 1 : 0; };
    enc.set = [&a](int x) { a.newEncoding = x ? Encoding::Utf8Bom : Encoding::Utf8; };
    newDocs.rows.push_back(enc);

    pages_ = {std::move(general), std::move(editor), std::move(display), std::move(newDocs)};
}

void SettingsWindow::Layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    HDC dc = GetDC(hwnd_);
    nav_.clear();
    int y = P(52);
    for (size_t i = 0; i < pages_.size(); i++) {
        nav_.push_back({P(12), y, P(176), y + P(34)});
        y += P(38);
    }
    const int left = P(208), right = rc.right - P(24);
    y = P(18); // first header lines up with the "Settings" title
    for (Row& r : pages_[page_].rows) {
        r.parts.clear();
        if (r.kind == Row::Header) {
            if (&r != &pages_[page_].rows.front()) y += P(14);
            r.rc = {left, y, right, y + P(28)};
            y += P(30);
            continue;
        }
        bool twoLines = !r.desc.empty() || r.kind == Row::Button;
        int h = P(twoLines ? 52 : 40);
        r.rc = {left, y, right, y + h};
        int cy = (r.rc.top + r.rc.bottom) / 2;
        switch (r.kind) {
        case Row::Switch: r.ctrl = {right - P(44), cy - P(10), right, cy + P(10)}; break;
        case Row::Number: {
            r.ctrl = {right - P(112), cy - P(15), right, cy + P(15)};
            r.parts.push_back({r.ctrl.left, r.ctrl.top, r.ctrl.left + P(32), r.ctrl.bottom});
            r.parts.push_back({r.ctrl.right - P(32), r.ctrl.top, r.ctrl.right, r.ctrl.bottom});
            break;
        }
        case Row::Choice: {
            int x = right;
            std::vector<RECT> segs;
            for (size_t i = r.choices.size(); i-- > 0;) {
                int w = TextWidth(dc, r.choices[i], fonts.uiBold) + P(24);
                segs.insert(segs.begin(), {x - w, cy - P(15), x, cy + P(15)});
                x -= w;
            }
            r.parts = segs;
            r.ctrl = {x - P(3), cy - P(15), right, cy + P(15)};
            for (RECT& s : r.parts) OffsetRect(&s, -P(2), 0);
            r.ctrl.left -= P(2);
            break;
        }
        case Row::Button: {
            int w = TextWidth(dc, L"Change...", fonts.ui) + P(28);
            r.ctrl = {right - w, cy - P(15), right, cy + P(15)};
            break;
        }
        default: break;
        }
        y += h;
    }
    close_ = {rc.right - P(24) - P(96), rc.bottom - P(52), rc.right - P(24), rc.bottom - P(20)};
    footer_ = {P(208), close_.top, close_.left - P(16), close_.bottom};
    ReleaseDC(hwnd_, dc);
}

void SettingsWindow::Paint(HDC dc) {
    const Theme& t = theme;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    FillRectColor(dc, rc, t.surface);
    RECT side = {0, 0, P(188), rc.bottom};
    FillRectColor(dc, side, t.chrome);
    RECT sep = {side.right, 0, side.right + 1, rc.bottom};
    FillRectColor(dc, sep, t.border);
    const int radius = P(4);

    RECT ttl = {P(24), P(14), side.right, P(44)};
    DrawTextColor(dc, L"Settings", ttl, t.text, title_, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    for (int i = 0; i < (int)nav_.size(); i++) {
        RECT r = nav_[i];
        bool sel = i == page_;
        if (sel || i == hoverNav_) RoundRectColor(dc, r, radius, sel ? t.accentSoft : t.hover, sel ? t.accentSoft : t.hover);
        if (sel) {
            RECT bar = {r.left, r.top + P(9), r.left + P(3), r.bottom - P(9)};
            FillRectColor(dc, bar, t.accent);
        }
        RECT tr = r;
        tr.left += P(14);
        DrawTextColor(dc, pages_[i].name, tr, sel ? t.text : t.textDim, sel ? fonts.uiBold : fonts.ui,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    const auto& rows = pages_[page_].rows;
    for (int i = 0; i < (int)rows.size(); i++) {
        const Row& r = rows[i];
        if (r.kind == Row::Header) {
            DrawTextColor(dc, r.label, r.rc, t.text, fonts.uiBold, DT_SINGLELINE | DT_BOTTOM | DT_LEFT);
            continue;
        }
        const bool en = r.Enabled();
        const bool hov = i == hoverRow_ && en;
        if (i == focus_ && GetFocus() == hwnd_) {
            RECT f = r.rc;
            InflateRect(&f, P(8), 0);
            RoundRectColor(dc, f, radius, t.surface, t.accent);
        }
        if (i != (int)rows.size() - 1 && rows[i + 1].kind != Row::Header) {
            RECT line = {r.rc.left, r.rc.bottom - 1, r.rc.right, r.rc.bottom};
            FillRectColor(dc, line, t.border);
        }
        RECT lr = {r.rc.left, r.rc.top, r.ctrl.left - P(16), r.rc.bottom};
        std::wstring sub = r.kind == Row::Button ? r.value() : r.desc;
        COLORREF fg = en ? t.text : t.textDisabled, dim = en ? t.textDim : t.textDisabled;
        if (sub.empty()) {
            DrawTextColor(dc, r.label, lr, fg, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        } else {
            int mid = (lr.top + lr.bottom) / 2;
            RECT a = {lr.left, lr.top, lr.right, mid + P(1)}, b = {lr.left, mid + P(1), lr.right, lr.bottom};
            DrawTextColor(dc, r.label, a, fg, fonts.ui, DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_END_ELLIPSIS);
            DrawTextColor(dc, sub, b, dim, fonts.uiSmall, DT_SINGLELINE | DT_TOP | DT_LEFT | DT_PATH_ELLIPSIS);
        }
        switch (r.kind) {
        case Row::Switch: {
            bool on = r.get() != 0;
            RECT s = r.ctrl;
            int h = s.bottom - s.top;
            COLORREF fill = !en ? t.surface : on ? t.accent : (hov ? t.hover : t.surface);
            COLORREF border = !en ? t.border : on ? t.accent : t.textDim;
            RoundRectColor(dc, s, h / 2, fill, border);
            int k = h - P(8);
            int kx = on ? s.right - P(4) - k : s.left + P(4);
            RECT knob = {kx, s.top + P(4), kx + k, s.top + P(4) + k};
            COLORREF kc = !en ? t.textDisabled : on ? t.accentText : t.textDim;
            RoundRectColor(dc, knob, k / 2, kc, kc);
            break;
        }
        case Row::Choice: {
            RoundRectColor(dc, r.ctrl, radius, t.surface, en ? t.borderStrong : t.border);
            int sel = r.get();
            for (int s = 0; s < (int)r.parts.size(); s++) {
                RECT seg = r.parts[s];
                bool isSel = s == sel, h = hov && s == hoverPart_;
                if (isSel || h) {
                    RECT inner = seg;
                    InflateRect(&inner, 0, -P(3));
                    RoundRectColor(dc, inner, P(3), isSel ? t.accentSoft : t.hover, isSel ? t.accent : t.hover);
                }
                DrawTextColor(dc, r.choices[s], seg, isSel ? t.text : dim, isSel ? fonts.uiBold : fonts.ui,
                              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
            break;
        }
        case Row::Number: {
            RoundRectColor(dc, r.ctrl, radius, t.inputBg, t.borderStrong);
            int value = r.get();
            for (int s = 0; s < 2; s++) {
                bool can = s == 0 ? value > r.min : value < r.max;
                if (hov && s == hoverPart_ && can) {
                    RECT b = r.parts[s];
                    InflateRect(&b, -P(3), -P(3));
                    RoundRectColor(dc, b, P(3), t.hover, t.hover);
                }
                DrawTextColor(dc, s == 0 ? L"−" : L"+", r.parts[s], can ? t.text : t.textDisabled, fonts.ui,
                              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
            DrawTextColor(dc, std::to_wstring(value), r.ctrl, t.text, fonts.uiBold, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            break;
        }
        case Row::Button: {
            bool h = hov && hoverPart_ == 0;
            RoundRectColor(dc, r.ctrl, radius, h ? t.hover : t.surface, t.borderStrong);
            DrawTextColor(dc, L"Change...", r.ctrl, t.text, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            break;
        }
        default: break;
        }
    }

    std::wstring where = L"Saved automatically to " + app->settings.Path();
    DrawTextColor(dc, where, footer_, t.textDim, fonts.uiSmall, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS);
    COLORREF cf = hoverClose_ ? (t.dark ? RGB(0x6C, 0xCE, 0xFF) : RGB(0x19, 0x6E, 0xBF)) : t.accent;
    RoundRectColor(dc, close_, radius, cf, cf);
    DrawTextColor(dc, L"Close", close_, t.accentText, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

void SettingsWindow::MakeTitleFont() {
    if (title_) DeleteObject(title_);
    LOGFONTW lf{};
    GetObjectW(fonts.ui, sizeof(lf), &lf); // same face as the rest of the UI
    lf.lfHeight = -MulDiv(14, (int)app->dpi, 72);
    lf.lfWeight = FW_SEMIBOLD;
    title_ = CreateFontIndirectW(&lf);
}

void SettingsWindow::Changed() {
    app->ApplyViewOptions();
    ApplyWindowTheme(hwnd_); // the theme may have changed
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::Activate(int i, int part) {
    Row& r = pages_[page_].rows[i];
    if (!r.Focusable()) return;
    switch (r.kind) {
    case Row::Switch: r.set(!r.get()); break;
    case Row::Choice:
        if (part < 0 || part == r.get()) return;
        r.set(part);
        break;
    case Row::Number: {
        int v = std::clamp(r.get() + (part == 0 ? -1 : 1), r.min, r.max);
        if (part < 0 || v == r.get()) return;
        r.set(v);
        break;
    }
    case Row::Button: r.set(0); break;
    default: return;
    }
    focus_ = i;
    Changed();
}

void SettingsWindow::MoveFocus(int dir) {
    const auto& rows = pages_[page_].rows;
    int n = (int)rows.size();
    for (int k = 1; k <= n; k++) {
        int i = ((focus_ < 0 ? (dir > 0 ? -1 : 0) : focus_) + dir * k + n * 2) % n;
        if (rows[i].Focusable()) {
            focus_ = i;
            break;
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::Hit(POINT pt, int& nav, int& row, int& part, bool& close) const {
    nav = row = part = -1;
    close = PtInRect(&close_, pt) != 0;
    for (int i = 0; i < (int)nav_.size(); i++)
        if (PtInRect(&nav_[i], pt)) nav = i;
    const auto& rows = pages_[page_].rows;
    for (int i = 0; i < (int)rows.size(); i++) {
        const Row& r = rows[i];
        if (r.kind == Row::Header || !PtInRect(&r.rc, pt)) continue;
        row = i;
        if (r.kind == Row::Choice || r.kind == Row::Number) {
            for (int s = 0; s < (int)r.parts.size(); s++)
                if (PtInRect(&r.parts[s], pt)) part = s;
            if (part < 0) row = -1; // only the control itself reacts
        } else if (r.kind == Row::Button) {
            if (PtInRect(&r.ctrl, pt)) part = 0;
            else row = -1;
        }
    }
}

LRESULT CALLBACK SettingsWindow::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = (SettingsWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        EnableNonClientDpiScaling(h);
    }
    auto* self = (SettingsWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT SettingsWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: Layout(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            tracking_ = true;
        }
        int nav, row, part;
        bool close;
        Hit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, nav, row, part, close);
        if (nav != hoverNav_ || row != hoverRow_ || part != hoverPart_ || close != hoverClose_) {
            hoverNav_ = nav;
            hoverRow_ = row;
            hoverPart_ = part;
            hoverClose_ = close;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        bool hand = close || nav >= 0 || (row >= 0 && pages_[page_].rows[row].Enabled());
        SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hoverNav_ = hoverRow_ = hoverPart_ = -1;
        hoverClose_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) return TRUE; // set in WM_MOUSEMOVE
        break;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        int nav, row, part;
        bool close;
        Hit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, nav, row, part, close);
        if (close) done_ = true;
        else if (nav >= 0 && nav != page_) {
            page_ = nav;
            focus_ = hoverRow_ = -1;
            Layout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (row >= 0) {
            focus_ = row;
            Activate(row, part);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        const auto& rows = pages_[page_].rows;
        Row* r = focus_ >= 0 && focus_ < (int)rows.size() ? &pages_[page_].rows[focus_] : nullptr;
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (wp) {
        case VK_ESCAPE:
        case VK_RETURN: done_ = true; break;
        case VK_TAB:
            if (ctrl) {
                page_ = (page_ + (GetKeyState(VK_SHIFT) < 0 ? (int)pages_.size() - 1 : 1)) % (int)pages_.size();
                focus_ = -1;
                Layout();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
            }
            break;
        case VK_DOWN: MoveFocus(1); break;
        case VK_UP: MoveFocus(-1); break;
        case VK_SPACE:
            if (r && (r->kind == Row::Switch || r->kind == Row::Button)) Activate(focus_, 0);
            break;
        case VK_LEFT:
        case VK_RIGHT:
            if (r && r->kind == Row::Choice) {
                int n = (int)r->choices.size();
                Activate(focus_, (r->get() + (wp == VK_LEFT ? n - 1 : 1)) % n);
            } else if (r && r->kind == Row::Number) {
                Activate(focus_, wp == VK_LEFT ? 0 : 1);
            } else if (r && r->kind == Row::Switch && (r->get() != 0) != (wp == VK_RIGHT)) {
                Activate(focus_, 0);
            }
            break;
        }
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS: InvalidateRect(hwnd_, nullptr, FALSE); return 0;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_DPICHANGED: {
        MakeTitleFont();
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE: done_ = true; return 0;
    }
    if (msg == WM_SETTINGCHANGE) InvalidateRect(hwnd_, nullptr, FALSE);
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void SettingsWindow::Run() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = app->inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    Build();
    MakeTitleFont();

    // Centered on the main window, sized for the longest page.
    RECT owner;
    GetWindowRect(app->hwnd, &owner);
    RECT r = {0, 0, P(720), P(600)};
    AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, 0, app->dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (owner.left + owner.right - w) / 2, y = (owner.top + owner.bottom - h) / 2;
    HMONITOR mon = MonitorFromWindow(app->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    x = std::clamp(x, (int)mi.rcWork.left, std::max((int)mi.rcWork.left, (int)mi.rcWork.right - w));
    y = std::clamp(y, (int)mi.rcWork.top, std::max((int)mi.rcWork.top, (int)mi.rcWork.bottom - h));
    CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Settings", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, app->hwnd,
                    nullptr, app->inst, this);
    if (!hwnd_) return;
    ApplyWindowTheme(hwnd_);
    Layout();
    EnableWindow(app->hwnd, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(hwnd_);

    MSG msg;
    while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (msg.message == WM_QUIT) PostQuitMessage((int)msg.wParam);
    EnableWindow(app->hwnd, TRUE);
    DestroyWindow(hwnd_);
    if (title_) DeleteObject(title_);
    SetForegroundWindow(app->hwnd);
}

} // namespace

void App::ShowSettings() {
    SettingsWindow w;
    w.Run();
}

} // namespace yan
