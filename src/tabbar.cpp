// Custom-drawn document tab strip.
#include "app.h"
#include <windowsx.h>
#include <algorithm>

namespace yan {

static const wchar_t* kClass = L"YanTabBar";

void TabBar::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.style = CS_DBLCLKS;
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

int TabBar::Height() const { return Px(36, app->dpi); }

LRESULT CALLBACK TabBar::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = (TabBar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    auto* self = (TabBar*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

void TabBar::Layout(HDC dc) {
    const UINT dpi = app->dpi;
    RECT client;
    GetClientRect(hwnd, &client);
    const int top = Px(6, dpi), bottom = client.bottom;
    const int minW = Px(96, dpi), maxW = Px(220, dpi), pad = Px(12, dpi), closeW = Px(22, dpi);
    rects_.resize(app->docs.size());
    int x = Px(6, dpi);
    for (size_t i = 0; i < app->docs.size(); i++) {
        int w = TextWidth(dc, app->docs[i]->Title(), fonts.ui) + pad * 2 + closeW;
        w = std::clamp(w, minW, maxW);
        rects_[i] = {x, top, x + w, bottom};
        x += w + Px(2, dpi);
    }
    const int plusW = Px(30, dpi);
    contentWidth_ = x + plusW + Px(6, dpi);
    int avail = client.right;
    int maxScroll = std::max(0, contentWidth_ - avail);
    scroll_ = std::clamp(scroll_, 0, maxScroll);
    for (auto& r : rects_) OffsetRect(&r, -scroll_, 0);
    plus_ = {x - scroll_, top + Px(2, dpi), x - scroll_ + plusW, bottom - Px(4, dpi)};
    if (contentWidth_ > avail) {
        // Keep the "+" button pinned on the right when tabs overflow.
        plus_.left = avail - plusW - Px(2, dpi);
        plus_.right = avail - Px(2, dpi);
    }
}

void TabBar::EnsureVisible(int index) {
    if (!hwnd || index < 0) return;
    HDC dc = GetDC(hwnd);
    Layout(dc);
    ReleaseDC(hwnd, dc);
    if (index >= (int)rects_.size()) return;
    RECT client;
    GetClientRect(hwnd, &client);
    int right = client.right - Px(36, app->dpi);
    RECT r = rects_[index];
    if (r.left < 0) scroll_ += r.left - Px(6, app->dpi);
    else if (r.right > right) scroll_ += r.right - right;
}

void TabBar::Refresh() {
    EnsureVisible(app->active);
    InvalidateRect(hwnd, nullptr, FALSE);
}

int TabBar::HitTest(POINT pt, bool* onClose) const {
    if (onClose) *onClose = false;
    if (PtInRect(&plus_, pt)) return -2;
    for (size_t i = 0; i < rects_.size(); i++) {
        if (!PtInRect(&rects_[i], pt)) continue;
        int closeW = Px(22, app->dpi);
        if (onClose) *onClose = pt.x >= rects_[i].right - closeW - Px(4, app->dpi);
        return (int)i;
    }
    return -1;
}

void TabBar::Paint(HDC dc) {
    const UINT dpi = app->dpi;
    RECT client;
    GetClientRect(hwnd, &client);
    FillRectColor(dc, client, theme.chrome);
    Layout(dc);
    const int radius = Px(6, dpi);
    RECT plusArea = plus_;
    HRGN clip = nullptr;
    if (contentWidth_ > client.right) {
        clip = CreateRectRgn(0, 0, plus_.left - Px(2, dpi), client.bottom);
        SelectClipRgn(dc, clip);
    }
    for (size_t i = 0; i < rects_.size(); i++) {
        RECT r = rects_[i];
        if (r.right < 0 || r.left > client.right) continue;
        const bool active = (int)i == app->active;
        const bool hover = (int)i == hover_;
        if (active || hover) {
            RECT body = r;
            body.bottom += radius; // only the top corners are rounded
            COLORREF fill = active ? theme.tabActive : theme.hover;
            RoundRectColor(dc, body, radius, fill, active ? theme.border : fill);
            if (active) {
                RECT accent = {r.left + radius, r.top, r.right - radius, r.top + std::max(2, Px(2, dpi))};
                FillRectColor(dc, accent, theme.accent);
            }
        } else if (i + 1 < rects_.size() && (int)i + 1 != app->active && (int)i + 1 != hover_) {
            RECT sep = {r.right, r.top + Px(9, dpi), r.right + 1, r.bottom - Px(9, dpi)};
            FillRectColor(dc, sep, theme.border);
        }
        const Doc& doc = *app->docs[i];
        const int closeW = Px(22, dpi);
        RECT tr = {r.left + Px(12, dpi), r.top, r.right - closeW - Px(4, dpi), r.bottom};
        COLORREF fg = active ? theme.text : (hover ? theme.text : theme.textDim);
        if (doc.missing) fg = theme.error;
        DrawTextColor(dc, doc.Title(), tr, fg, active ? fonts.uiBold : fonts.ui,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_LEFT);
        RECT cr = {r.right - closeW - Px(4, dpi), r.top + Px(6, dpi), r.right - Px(4, dpi), r.bottom - Px(6, dpi)};
        const bool modified = app->IsModified((int)i);
        const bool showClose = hover || (active && !modified);
        if (showClose) {
            if (hover && hoverClose_) RoundRectColor(dc, cr, Px(4, dpi), theme.pressed, theme.pressed);
            DrawTextColor(dc, std::wstring(1, Icon::Close), cr, hoverClose_ && hover ? theme.text : theme.textDim,
                          fonts.iconsSmall, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        } else if (modified) {
            DrawTextColor(dc, L"●", cr, theme.modified, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
    }
    if (clip) {
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);
    }
    // Line under the strip, interrupted by the active tab.
    RECT line = {0, client.bottom - 1, client.right, client.bottom};
    FillRectColor(dc, line, theme.border);
    if (app->active >= 0 && app->active < (int)rects_.size()) {
        RECT a = rects_[app->active];
        RECT gap = {a.left + 1, client.bottom - 1, a.right - 1, client.bottom};
        if (!clip || a.right < plus_.left) FillRectColor(dc, gap, theme.tabActive);
    }
    if (hoverPlus_) RoundRectColor(dc, plusArea, Px(4, dpi), theme.hover, theme.hover);
    DrawTextColor(dc, std::wstring(1, Icon::Add), plusArea, theme.textDim, fonts.icons,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

LRESULT TabBar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
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
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            tracking_ = true;
        }
        if (pressed_ >= 0 && !pressClose_ && (wp & MK_LBUTTON)) {
            if (!dragging_ && abs(pt.x - pressPt_.x) > GetSystemMetrics(SM_CXDRAG)) dragging_ = true;
            if (dragging_) {
                for (size_t i = 0; i < rects_.size(); i++) {
                    if (pt.x >= rects_[i].left && pt.x < rects_[i].right && (int)i != pressed_) {
                        app->MoveTab(pressed_, (int)i);
                        pressed_ = (int)i;
                        break;
                    }
                }
            }
        }
        bool onClose = false;
        int h = HitTest(pt, &onClose);
        bool plus = h == -2;
        if (h < 0) h = -1;
        if (h != hover_ || onClose != hoverClose_ || plus != hoverPlus_) {
            hover_ = h;
            hoverClose_ = onClose;
            hoverPlus_ = plus;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = -1;
        hoverClose_ = hoverPlus_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        bool onClose = false;
        int h = HitTest(pt, &onClose);
        if (h == -2) { app->NewTab(); return 0; }
        if (h < 0) return 0;
        pressed_ = h;
        pressClose_ = onClose;
        pressPt_ = pt;
        dragging_ = false;
        SetCapture(hwnd);
        if (!onClose) app->ActivateTab(h);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (GetCapture() == hwnd) ReleaseCapture();
        if (pressed_ >= 0 && pressClose_) {
            bool onClose = false;
            int h = HitTest(pt, &onClose);
            if (h == pressed_ && onClose) app->CloseTab(h);
        }
        pressed_ = -1;
        pressClose_ = dragging_ = false;
        SetFocus(app->sci.hwnd);
        return 0;
    }
    case WM_MBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int h = HitTest(pt, nullptr);
        if (h >= 0) app->CloseTab(h);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (HitTest(pt, nullptr) == -1) app->NewTab();
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int h = HitTest(pt, nullptr);
        if (h >= 0) {
            ClientToScreen(hwnd, &pt);
            app->ShowTabMenu(h, pt);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (msg == WM_MOUSEHWHEEL) delta = -delta;
        scroll_ -= delta * Px(60, app->dpi) / WHEEL_DELTA;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SIZE:
        EnsureVisible(app->active);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace yan
