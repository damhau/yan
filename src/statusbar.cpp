// Custom-drawn status bar: position, selection, zoom, line endings, encoding.
#include "app.h"
#include <windowsx.h>

namespace yan {

static const wchar_t* kClass = L"YanStatusBar";
enum { PART_ZOOM = 1, PART_EOL, PART_ENCODING, PART_LANG, PART_MODE };

void StatusBar::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }
    hwnd = CreateWindowExW(0, kClass, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr,
                           GetModuleHandleW(nullptr), this);
}

int StatusBar::Height() const { return Px(26, app->dpi); }

LRESULT CALLBACK StatusBar::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = (StatusBar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    auto* self = (StatusBar*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static std::wstring Thousands(long long v) {
    std::wstring s = std::to_wstring(v);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L",");
    return s;
}

void StatusBar::Update() {
    if (!hwnd || app->active < 0) return;
    const Sci& sci = app->sci;
    Sci_Position pos = sci.Pos();
    Sci_Position line = sci.Line(pos);
    int main = (int)sci(SCI_GETMAINSELECTION);
    Sci_Position col = (Sci_Position)sci(SCI_GETCOLUMN, pos) + (Sci_Position)sci(SCI_GETSELECTIONNCARETVIRTUALSPACE, main);
    std::wstring s = L"Ln " + Thousands(line + 1) + L", Col " + Thousands(col + 1);
    int nsel = (int)sci(SCI_GETSELECTIONS);
    if (nsel > 1) {
        bool rect = sci(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE || sci(SCI_SELECTIONISRECTANGLE);
        Sci_Position chars = 0;
        for (int i = 0; i < nsel && i < 10000; i++)
            chars += (Sci_Position)sci(SCI_COUNTCHARACTERS, sci(SCI_GETSELECTIONNSTART, i), sci(SCI_GETSELECTIONNEND, i));
        s += L"    " + Thousands(nsel) + (rect ? L" rows (column)" : L" selections");
        if (chars) s += L", " + Thousands(chars) + L" chars";
    } else {
        Sci_Position a = (Sci_Position)sci(SCI_GETSELECTIONSTART), b = (Sci_Position)sci(SCI_GETSELECTIONEND);
        if (b > a) {
            // Counting characters is O(n); for huge selections show bytes instead.
            Sci_Position chars = b - a > 4 * 1024 * 1024 ? b - a : (Sci_Position)sci(SCI_COUNTCHARACTERS, a, b);
            Sci_Position lines = sci.Line(b) - sci.Line(a) + 1;
            s += L"    Sel " + Thousands(chars);
            if (lines > 1) s += L" | " + Thousands(lines) + L" lines";
        }
    }
    s += L"    " + Thousands(sci.Length()) + L" bytes, " + Thousands(sci.LineCount()) + L" lines";
    left_ = s;

    const Doc& doc = app->Cur();
    int zoom = (int)sci(SCI_GETZOOM);
    int pct = (app->view.fontSize + zoom) * 100 / std::max(1, app->view.fontSize);
    parts_.clear();
    parts_.push_back({std::to_wstring(pct) + L"%", {}, PART_ZOOM});
    parts_.push_back({doc.eol == SC_EOL_CRLF ? L"Windows (CRLF)" : doc.eol == SC_EOL_LF ? L"Unix (LF)" : L"Mac (CR)", {}, PART_EOL});
    parts_.push_back({EncodingName(doc.encoding), {}, PART_ENCODING});
    parts_.push_back({LangName(app->CurLang()), {}, PART_LANG});
    if (sci(SCI_GETOVERTYPE)) parts_.push_back({L"OVR", {}, PART_MODE});
    InvalidateRect(hwnd, nullptr, FALSE);
}

void StatusBar::Paint(HDC dc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRectColor(dc, rc, theme.chrome);
    RECT top = {0, 0, rc.right, 1};
    FillRectColor(dc, top, theme.border);
    const UINT dpi = app->dpi;
    int x = rc.right - Px(8, dpi);
    for (auto& p : parts_) {
        int w = TextWidth(dc, p.text, fonts.ui) + Px(20, dpi);
        p.rc = {x - w, Px(3, dpi), x, rc.bottom - Px(3, dpi)};
        x -= w + Px(2, dpi);
    }
    for (size_t i = 0; i < parts_.size(); i++) {
        auto& p = parts_[i];
        if ((int)i == hover_) RoundRectColor(dc, p.rc, Px(4, dpi), theme.hover, theme.hover);
        DrawTextColor(dc, p.text, p.rc, theme.textDim, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    RECT lr = {Px(12, dpi), 0, x - Px(8, dpi), rc.bottom};
    DrawTextColor(dc, left_, lr, theme.textDim, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

LRESULT StatusBar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            tracking_ = true;
        }
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int h = -1;
        for (size_t i = 0; i < parts_.size(); i++)
            if (PtInRect(&parts_[i].rc, pt)) h = (int)i;
        if (h != hover_) {
            hover_ = h;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        for (auto& p : parts_) {
            if (!PtInRect(&p.rc, pt)) continue;
            POINT sp = {p.rc.left, p.rc.top};
            ClientToScreen(hwnd, &sp);
            if (p.id == PART_ZOOM) app->OnCommand(CMD_ZOOMRESET);
            else if (p.id == PART_EOL) app->ShowEolMenu(sp);
            else if (p.id == PART_ENCODING) app->ShowEncodingMenu(sp);
            else if (p.id == PART_LANG) app->ShowLanguageMenu(sp);
            else if (p.id == PART_MODE) app->sci(SCI_EDITTOGGLEOVERTYPE);
            Update();
            break;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace yan
