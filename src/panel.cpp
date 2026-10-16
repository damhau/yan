// Docked bottom panel: Find / Replace (Notepad++ modes and options), Go to line,
// Column editor. Drawn immediate-mode; only the text boxes are real windows.
#include "app.h"
#include <windowsx.h>
#include <algorithm>

namespace yan {

static const wchar_t* kClass = L"YanPanel";
static WNDPROC g_editProc = nullptr;
constexpr UINT_PTR TIMER_INCREMENTAL = 1;

enum PanelId {
    P_TOGGLE_REPLACE = 1, P_LABEL, P_FIND_EDIT, P_PREV, P_NEXT, P_COUNT, P_SELECTALL, P_MARK, P_CLEAR, P_CLOSE,
    P_REPLACE_EDIT, P_REPLACE_ONE, P_REPLACE_ALL, P_MODE, P_CASE, P_WORD, P_WRAP, P_DOTNL, P_INSEL, P_BOOKMARK,
    P_STATUS, P_GOTO_EDIT, P_GOTO_MODE, P_GOTO_GO, P_COL_MODE, P_COL_TEXT, P_COL_INIT, P_COL_STEP, P_COL_REPEAT,
    P_COL_BASE, P_COL_LEAD, P_COL_INSERT,
};

void Panel::Create(HWND parent) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kClass, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                           parent, nullptr, GetModuleHandleW(nullptr), this);
}

LRESULT CALLBACK Panel::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = (Panel*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    auto* self = (Panel*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

int Panel::Rows() const {
    switch (page_) {
    case PanelPage::Find: return 2;
    case PanelPage::Replace: return 3;
    case PanelPage::GoTo: return 1;
    case PanelPage::Column: return 2;
    default: return 0;
    }
}

int Panel::Height() const {
    if (page_ == PanelPage::None) return 0;
    return Rows() * Px(34, app->dpi) + Px(12, app->dpi) + 1;
}

PanelItem* Panel::ItemById(int id) {
    for (auto& it : items_)
        if (it.id == id) return &it;
    return nullptr;
}

// ---------------------------------------------------------------------------
HWND Panel::MakeEdit(int id) {
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(e, WM_SETFONT, (WPARAM)fonts.ui, FALSE);
    SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)EditProc);
    if (!g_editProc) g_editProc = old;
    ApplyControlTheme(e, true);
    return e;
}

void Panel::EnsureEdits() {
    if (findEdit_) return;
    findEdit_ = MakeEdit(P_FIND_EDIT);
    replaceEdit_ = MakeEdit(P_REPLACE_EDIT);
    gotoEdit_ = MakeEdit(P_GOTO_EDIT);
    colText_ = MakeEdit(P_COL_TEXT);
    colInit_ = MakeEdit(P_COL_INIT);
    colStep_ = MakeEdit(P_COL_STEP);
    colRepeat_ = MakeEdit(P_COL_REPEAT);
    SetEditText(findEdit_, opt.what);
    SetEditText(replaceEdit_, opt.with);
    SetEditText(colInit_, std::to_wstring(col_.initial));
    SetEditText(colStep_, std::to_wstring(col_.step));
    SetEditText(colRepeat_, std::to_wstring(col_.repeat));
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, fonts.ui);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    editHeight_ = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
}

std::wstring Panel::EditText(HWND e) const {
    if (!e) return {};
    int n = GetWindowTextLengthW(e);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(e, s.data(), n + 1);
    return s;
}

void Panel::SetEditText(HWND e, const std::wstring& s) {
    if (!e) return;
    settingText_ = true;
    SetWindowTextW(e, s.c_str());
    settingText_ = false;
}

void Panel::SyncOptionsFromEdits() {
    if (findEdit_) opt.what = EditText(findEdit_);
    if (replaceEdit_) opt.with = EditText(replaceEdit_);
}

std::vector<HWND> Panel::VisibleEdits() const {
    std::vector<HWND> v;
    for (auto& it : items_)
        if (it.kind == PanelItem::Edit && it.edit) v.push_back(it.edit);
    return v;
}

void Panel::FocusPrimary() {
    HWND e = nullptr;
    switch (page_) {
    case PanelPage::Find:
    case PanelPage::Replace: e = findEdit_; break;
    case PanelPage::GoTo: e = gotoEdit_; break;
    case PanelPage::Column: e = col_.numbers ? colInit_ : colText_; break;
    default: break;
    }
    if (e) SetFocus(e);
}

// ---------------------------------------------------------------------------
void Panel::Show(PanelPage page) {
    EnsureEdits();
    const Sci& sci = app->sci;
    if (page == PanelPage::Find || page == PanelPage::Replace) {
        Sci_Position a = (Sci_Position)sci(SCI_GETSELECTIONSTART), b = (Sci_Position)sci(SCI_GETSELECTIONEND);
        if (sci(SCI_GETSELECTIONS) > 1 || (b > a && sci.Line(a) != sci.Line(b))) {
            SetScopeFromSelection();
        } else if (b > a && b - a < 1000) {
            std::wstring sel = Widen(sci.Range(a, b));
            if (!sel.empty()) SetEditText(findEdit_, sel);
        }
        incAnchor_ = a;
        if (page == PanelPage::Find && page_ == PanelPage::Replace) page = PanelPage::Replace;
    }
    if (page == PanelPage::GoTo) {
        SetEditText(gotoEdit_, L"");
        Status(L"Lines 1 – " + std::to_wstring(sci.LineCount()) + L"   (line, line:col)");
    } else if (page == PanelPage::Column) {
        Status(L"Inserts at the caret column on each line down to the end, or into every row of a column "
               L"selection (Alt+drag or Alt+Shift+arrows).");
    } else if (page_ != PanelPage::Find && page_ != PanelPage::Replace) {
        Status(L"");
    }
    page_ = page;
    Layout();
    ShowWindow(hwnd, SW_SHOWNA);
    app->Layout();
    FocusPrimary();
    if (page == PanelPage::Find || page == PanelPage::Replace) {
        SendMessageW(findEdit_, EM_SETSEL, 0, -1);
        UpdateLiveHighlight();
    } else if (page == PanelPage::Column) {
        HWND e = col_.numbers ? colInit_ : colText_;
        SendMessageW(e, EM_SETSEL, 0, -1);
    }
}

void Panel::Hide() {
    if (page_ == PanelPage::None) return;
    page_ = PanelPage::None;
    KillTimer(hwnd, TIMER_INCREMENTAL);
    ShowWindow(hwnd, SW_HIDE);
    ClearIndicator(app->sci, IND_FIND);
    ClearScope();
    app->Layout();
    SetFocus(app->sci.hwnd);
}

void Panel::LeavingDocument() {
    ClearIndicator(app->sci, IND_FIND);
    if (inSelection) ClearScope();
}

void Panel::OnDocumentSwitched() {
    lastEmptyMatch_ = -1;
    incAnchor_ = 0;
    if (page_ == PanelPage::Find || page_ == PanelPage::Replace) {
        Status(L"");
        UpdateLiveHighlight();
        Layout();
    }
}

void Panel::ThemeChanged() {
    if (inputBrush_) DeleteObject(inputBrush_);
    inputBrush_ = CreateSolidBrush(theme.inputBg);
    for (HWND e : {findEdit_, replaceEdit_, gotoEdit_, colText_, colInit_, colStep_, colRepeat_})
        if (e) {
            ApplyControlTheme(e, true);
            SendMessageW(e, WM_SETFONT, (WPARAM)fonts.ui, FALSE);
        }
    if (findEdit_) {
        HDC dc = GetDC(hwnd);
        HGDIOBJ old = SelectObject(dc, fonts.ui);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        editHeight_ = tm.tmHeight;
        SelectObject(dc, old);
        ReleaseDC(hwnd, dc);
    }
    Layout();
    InvalidateRect(hwnd, nullptr, TRUE);
}

void Panel::Status(const std::wstring& s, int kind) {
    status_ = s;
    statusKind_ = kind;
    if (PanelItem* it = ItemById(P_STATUS)) InvalidateRect(hwnd, &it->rc, FALSE);
    else InvalidateRect(hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
void Panel::Layout() {
    items_.clear();
    if (page_ == PanelPage::None || !hwnd) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = GetDC(hwnd);
    const UINT dpi = app->dpi;
    const int pad = Px(10, dpi), rowH = Px(34, dpi), ctlH = Px(28, dpi), gap = Px(6, dpi);
    const int iconW = Px(30, dpi);
    int rowY = Px(6, dpi) + 1;
    auto W = [&](const std::wstring& s) { return TextWidth(dc, s, fonts.ui) + Px(22, dpi); };
    auto add = [&](PanelItem::Kind k, int id, const std::wstring& text, int x, int w, int y) -> PanelItem& {
        PanelItem it{k};
        it.id = id;
        it.text = text;
        int top = y + (rowH - ctlH) / 2;
        it.rc = {x, top, x + w, top + ctlH};
        items_.push_back(std::move(it));
        return items_.back();
    };
    auto addSegment = [&](int id, std::vector<std::wstring> segs, int sel, int x, int y) -> int {
        int w = 0;
        std::vector<int> ws;
        for (auto& s : segs) { ws.push_back(W(s) - Px(4, dpi)); w += ws.back(); }
        PanelItem& it = add(PanelItem::Segment, id, L"", x, w, y);
        int sx = x;
        for (size_t i = 0; i < segs.size(); i++) {
            it.segRects.push_back({sx, it.rc.top, sx + ws[i], it.rc.bottom});
            sx += ws[i];
        }
        it.segs = std::move(segs);
        it.segSel = sel;
        return w;
    };
    auto addToggle = [&](int id, const std::wstring& text, bool on, bool enabled, int x, int y, const wchar_t* tip) {
        int w = W(text);
        PanelItem& it = add(PanelItem::Toggle, id, text, x, w, y);
        it.on = on;
        it.enabled = enabled;
        it.tip = tip;
        return w;
    };
    auto addButton = [&](int id, const std::wstring& text, wchar_t icon, int x, int y, const wchar_t* tip) {
        int w = icon ? iconW : W(text);
        PanelItem& it = add(icon ? PanelItem::IconButton : PanelItem::Button, id, text, x, w, y);
        it.icon = icon;
        it.tip = tip ? tip : L"";
        return w;
    };
    auto addEdit = [&](int id, HWND e, int x, int w, int y) {
        PanelItem& it = add(PanelItem::Edit, id, L"", x, w, y);
        it.edit = e;
    };
    const int closeX = rc.right - pad - iconW;

    if (page_ == PanelPage::Find || page_ == PanelPage::Replace) {
        const bool rep = page_ == PanelPage::Replace;
        struct B { int id; const wchar_t* text; wchar_t icon; const wchar_t* tip; };
        const B r1[] = {
            {P_PREV, L"", Icon::Up, L"Find previous  (Shift+Enter, Shift+F3)"},
            {P_NEXT, L"", Icon::Down, L"Find next  (Enter, F3)"},
            {P_COUNT, L"Count", 0, L"Count matches"},
            {P_SELECTALL, L"Select all", 0, L"Select every match with multiple carets  (Alt+Enter)"},
            {P_MARK, L"Mark all", 0, L"Highlight every match (optionally bookmark the lines)"},
            {P_CLEAR, L"Clear marks", 0, L"Remove all mark highlights"},
        };
        const B r2[] = {
            {P_REPLACE_ONE, L"Replace", 0, L"Replace this match and find the next  (Enter in the replace box)"},
            {P_REPLACE_ALL, L"Replace all", 0, L"Replace every match  (Ctrl+Alt+Enter)"},
        };
        auto groupWidth = [&](const B* b, size_t n) {
            int w = 0;
            for (size_t i = 0; i < n; i++) w += (b[i].icon ? iconW : W(b[i].text)) + (i ? Px(4, dpi) : 0);
            return w;
        };
        int groupW = std::max(groupWidth(r1, std::size(r1)), rep ? groupWidth(r2, std::size(r2)) : 0);
        int labelW = std::max(TextWidth(dc, L"Replace", fonts.ui), TextWidth(dc, L"Find", fonts.ui)) + Px(12, dpi);
        int editL = pad + iconW + Px(4, dpi) + labelW;
        int editR = closeX - gap - groupW - gap;
        if (editR - editL < Px(140, dpi)) editR = editL + Px(140, dpi);

        PanelItem& tg = add(PanelItem::IconButton, P_TOGGLE_REPLACE, L"", pad, iconW, rowY);
        tg.icon = rep ? Icon::ChevronDown : Icon::ChevronRight;
        tg.tip = rep ? L"Hide replace  (Ctrl+F)" : L"Show replace  (Ctrl+H)";
        add(PanelItem::Label, P_LABEL, L"Find", pad + iconW + Px(4, dpi), labelW, rowY);
        addEdit(P_FIND_EDIT, findEdit_, editL, editR - editL, rowY);
        int x = editR + gap;
        for (auto& b : r1) x += addButton(b.id, b.text, b.icon, x, rowY, b.tip) + Px(4, dpi);
        addButton(P_CLOSE, L"", Icon::Close, closeX, rowY, L"Close  (Esc)");
        rowY += rowH;
        if (rep) {
            add(PanelItem::Label, P_LABEL, L"Replace", pad + iconW + Px(4, dpi), labelW, rowY);
            addEdit(P_REPLACE_EDIT, replaceEdit_, editL, editR - editL, rowY);
            x = editR + gap;
            for (auto& b : r2) x += addButton(b.id, b.text, b.icon, x, rowY, b.tip) + Px(4, dpi);
            rowY += rowH;
        }
        x = pad + iconW + Px(4, dpi);
        x += addSegment(P_MODE, {L"Normal", L"Extended", L"Regex"}, (int)opt.mode, x, rowY) + gap * 2;
        const bool regex = opt.mode == SearchMode::Regex;
        x += addToggle(P_CASE, L"Match case", opt.matchCase, true, x, rowY, L"Match case  (Alt+C)") + Px(4, dpi);
        x += addToggle(P_WORD, L"Whole word", opt.wholeWord && !regex, !regex, x, rowY, L"Match whole word only  (Alt+W)") + Px(4, dpi);
        x += addToggle(P_WRAP, L"Wrap around", opt.wrap, true, x, rowY, L"Continue from the start when the end is reached") + Px(4, dpi);
        x += addToggle(P_DOTNL, L". matches newline", opt.dotNewline && regex, regex, x, rowY, L"In regex mode, let . match \\r and \\n") + Px(4, dpi);
        x += addToggle(P_INSEL, L"In selection", inSelection, true, x, rowY, L"Limit find, count, mark and replace to the selection  (Alt+L)") + Px(4, dpi);
        x += addToggle(P_BOOKMARK, L"Bookmark lines", bookmarkLines, true, x, rowY, L"Mark all also bookmarks the matching lines") + gap;
        add(PanelItem::StatusText, P_STATUS, L"", x, std::max(0, (int)rc.right - pad - x), rowY);
    } else if (page_ == PanelPage::GoTo) {
        int x = pad;
        int lw = TextWidth(dc, L"Go to offset", fonts.ui) + Px(12, dpi);
        add(PanelItem::Label, P_LABEL, gotoOffset_ ? L"Go to offset" : L"Go to line", x, lw, rowY);
        x += lw;
        addEdit(P_GOTO_EDIT, gotoEdit_, x, Px(160, dpi), rowY);
        x += Px(160, dpi) + gap;
        x += addSegment(P_GOTO_MODE, {L"Line", L"Offset"}, gotoOffset_ ? 1 : 0, x, rowY) + gap;
        x += addButton(P_GOTO_GO, L"Go", 0, x, rowY, L"Go  (Enter)") + gap;
        add(PanelItem::StatusText, P_STATUS, L"", x, std::max(0, closeX - gap - x), rowY);
        addButton(P_CLOSE, L"", Icon::Close, closeX, rowY, L"Close  (Esc)");
    } else if (page_ == PanelPage::Column) {
        int x = pad;
        x += addSegment(P_COL_MODE, {L"Text", L"Numbers"}, col_.numbers ? 1 : 0, x, rowY) + gap * 2;
        int insertW = W(L"Insert");
        int insertX = closeX - gap - insertW;
        if (!col_.numbers) {
            addEdit(P_COL_TEXT, colText_, x, std::max(Px(120, dpi), insertX - gap - x), rowY);
        } else {
            auto label = [&](const wchar_t* t) {
                int w = TextWidth(dc, t, fonts.ui) + Px(8, dpi);
                add(PanelItem::Label, P_LABEL, t, x, w, rowY);
                x += w;
            };
            label(L"Initial");
            addEdit(P_COL_INIT, colInit_, x, Px(80, dpi), rowY);
            x += Px(80, dpi) + gap;
            label(L"Step");
            addEdit(P_COL_STEP, colStep_, x, Px(56, dpi), rowY);
            x += Px(56, dpi) + gap;
            label(L"Repeat");
            addEdit(P_COL_REPEAT, colRepeat_, x, Px(48, dpi), rowY);
            x += Px(48, dpi) + gap * 2;
            int baseSel = col_.base == 16 ? 1 : col_.base == 8 ? 2 : col_.base == 2 ? 3 : 0;
            x += addSegment(P_COL_BASE, {L"Dec", L"Hex", L"Oct", L"Bin"}, baseSel, x, rowY) + gap * 2;
            addSegment(P_COL_LEAD, {L"No padding", L"Zeros", L"Spaces"}, col_.leading, x, rowY);
        }
        PanelItem& ins = add(PanelItem::Button, P_COL_INSERT, L"Insert", insertX, insertW, rowY);
        ins.primary = true;
        ins.tip = L"Insert  (Enter)";
        addButton(P_CLOSE, L"", Icon::Close, closeX, rowY, L"Close  (Esc)");
        rowY += rowH;
        add(PanelItem::StatusText, P_STATUS, L"", pad, rc.right - pad * 2, rowY);
    }
    ReleaseDC(hwnd, dc);

    // Position the real edit controls inside their drawn boxes; hide the others.
    std::vector<HWND> shown;
    for (auto& it : items_) {
        if (it.kind != PanelItem::Edit || !it.edit) continue;
        int h = editHeight_;
        int x = it.rc.left + Px(8, dpi);
        int y = it.rc.top + (it.rc.bottom - it.rc.top - h) / 2;
        SetWindowPos(it.edit, nullptr, x, y, std::max(0, (int)(it.rc.right - it.rc.left) - Px(16, dpi)), h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        shown.push_back(it.edit);
    }
    for (HWND e : {findEdit_, replaceEdit_, gotoEdit_, colText_, colInit_, colStep_, colRepeat_})
        if (e && std::find(shown.begin(), shown.end(), e) == shown.end()) ShowWindow(e, SW_HIDE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Panel::Paint(HDC dc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const UINT dpi = app->dpi;
    FillRectColor(dc, rc, theme.surface);
    RECT top = {0, 0, rc.right, 1};
    FillRectColor(dc, top, theme.border);
    const int radius = Px(4, dpi);
    HWND focus = GetFocus();
    for (int i = 0; i < (int)items_.size(); i++) {
        const PanelItem& it = items_[i];
        const bool hover = i == hover_ && it.enabled;
        const bool pressed = i == pressed_ && hover;
        switch (it.kind) {
        case PanelItem::Label:
            DrawTextColor(dc, it.text, it.rc, theme.textDim, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            break;
        case PanelItem::Edit: {
            bool focused = focus == it.edit;
            RoundRectColor(dc, it.rc, radius, theme.inputBg, focused ? theme.accent : theme.borderStrong);
            if (focused) {
                RECT u = {it.rc.left + radius, it.rc.bottom - 2, it.rc.right - radius, it.rc.bottom};
                FillRectColor(dc, u, theme.accent);
            }
            break;
        }
        case PanelItem::Button: {
            COLORREF fill = it.primary ? theme.accent : (pressed ? theme.pressed : hover ? theme.hover : theme.surface);
            if (it.primary && hover) fill = theme.dark ? RGB(0x6C, 0xCE, 0xFF) : RGB(0x19, 0x6E, 0xBF);
            RoundRectColor(dc, it.rc, radius, fill, it.primary ? fill : theme.borderStrong);
            DrawTextColor(dc, it.text, it.rc, it.primary ? theme.accentText : theme.text, fonts.ui,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            break;
        }
        case PanelItem::IconButton: {
            if (hover) RoundRectColor(dc, it.rc, radius, pressed ? theme.pressed : theme.hover, pressed ? theme.pressed : theme.hover);
            DrawTextColor(dc, std::wstring(1, it.icon), it.rc, theme.text, fonts.icons, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            break;
        }
        case PanelItem::Toggle: {
            COLORREF fill = it.on ? theme.accentSoft : (hover ? theme.hover : theme.surface);
            COLORREF border = it.on ? theme.accent : theme.border;
            COLORREF fg = !it.enabled ? theme.textDisabled : it.on ? theme.text : theme.textDim;
            if (!it.enabled) { fill = theme.surface; border = theme.border; }
            RECT r = it.rc;
            RoundRectColor(dc, r, (r.bottom - r.top) / 2, fill, border);
            DrawTextColor(dc, it.text, r, fg, fonts.ui, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            break;
        }
        case PanelItem::Segment: {
            RoundRectColor(dc, it.rc, radius, theme.surface, theme.borderStrong);
            for (int s = 0; s < (int)it.segRects.size(); s++) {
                RECT r = it.segRects[s];
                bool sel = s == it.segSel;
                bool h = i == hover_ && s == hoverSeg_;
                if (sel || h) {
                    RECT inner = r;
                    InflateRect(&inner, -Px(2, dpi), -Px(2, dpi));
                    RoundRectColor(dc, inner, Px(3, dpi), sel ? theme.accentSoft : theme.hover, sel ? theme.accent : theme.hover);
                }
                DrawTextColor(dc, it.segs[s], r, sel ? theme.text : theme.textDim, sel ? fonts.uiBold : fonts.ui,
                              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
            break;
        }
        case PanelItem::StatusText: {
            const std::wstring& s = hoverTip_.empty() ? status_ : hoverTip_;
            COLORREF c = !hoverTip_.empty() ? theme.textDim
                         : statusKind_ == 1 ? theme.error : statusKind_ == 2 ? theme.success : theme.textDim;
            UINT align = page_ == PanelPage::Column ? DT_LEFT : DT_RIGHT;
            DrawTextColor(dc, s, it.rc, c, fonts.ui, DT_SINGLELINE | DT_VCENTER | align | DT_END_ELLIPSIS);
            break;
        }
        }
    }
}

// ---------------------------------------------------------------------------
int Panel::HitItem(LPARAM lp, int* seg) const {
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    *seg = -1;
    for (int i = 0; i < (int)items_.size(); i++) {
        const PanelItem& it = items_[i];
        if (it.kind == PanelItem::Label || it.kind == PanelItem::StatusText || it.kind == PanelItem::Edit) continue;
        if (!PtInRect(&it.rc, pt)) continue;
        for (int s = 0; s < (int)it.segRects.size(); s++)
            if (PtInRect(&it.segRects[s], pt)) *seg = s;
        return i;
    }
    return -1;
}

LRESULT Panel::Handle(UINT msg, WPARAM wp, LPARAM lp) {
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
    case WM_SIZE:
        Layout();
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, theme.text);
        SetBkColor(dc, theme.inputBg);
        if (!inputBrush_) inputBrush_ = CreateSolidBrush(theme.inputBg);
        return (LRESULT)inputBrush_;
    }
    case WM_COMMAND: {
        int code = HIWORD(wp), id = LOWORD(wp);
        if (code == EN_SETFOCUS) app->NoteFocus((HWND)lp);
        if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateRect(hwnd, nullptr, FALSE);
        if (code == EN_CHANGE && !settingText_) {
            if (id == P_FIND_EDIT || id == P_REPLACE_EDIT) historyPos_ = -1;
            if (id == P_FIND_EDIT) SetTimer(hwnd, TIMER_INCREMENTAL, 150, nullptr);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_INCREMENTAL) {
            KillTimer(hwnd, TIMER_INCREMENTAL);
            IncrementalFind();
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            tracking_ = true;
        }
        int hs = -1;
        int h = HitItem(lp, &hs);
        if (h != hover_ || hs != hoverSeg_) {
            hover_ = h;
            hoverSeg_ = hs;
            hoverTip_ = h >= 0 ? items_[h].tip : L"";
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = hoverSeg_ = -1;
        hoverTip_.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        pressed_ = HitItem(lp, &pressedSeg_);
        hover_ = pressed_;
        if (pressed_ >= 0) SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int p = pressed_, ps = pressedSeg_;
        pressed_ = -1;
        int seg = -1;
        int h = HitItem(lp, &seg);
        if (GetCapture() == hwnd) ReleaseCapture();
        if (p >= 0 && p == h && seg == ps && p < (int)items_.size() && items_[p].enabled) {
            PanelItem it = items_[p];
            OnClick(it.id, seg);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Panel::EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Panel& p = app->panel;
    switch (msg) {
    case WM_KEYDOWN:
        if (p.OnEditKey(h, wp)) return 0;
        break;
    case WM_CHAR:
        // Swallow keys handled in WM_KEYDOWN so the edit does not beep.
        if (wp == L'\r' || wp == 0x1B || wp == L'\t' || wp == 0x7F) return 0;
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | CallWindowProcW(g_editProc, h, msg, wp, lp);
    }
    return CallWindowProcW(g_editProc, h, msg, wp, lp);
}

bool Panel::OnEditKey(HWND e, WPARAM key) {
    const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    switch (key) {
    case VK_RETURN:
        if (e == findEdit_) {
            if (alt) SelectAllMatches();
            else FindNext(!shift);
        } else if (e == replaceEdit_) {
            if (ctrl && alt) ReplaceAll();
            else Replace();
        } else if (e == gotoEdit_) {
            GoTo();
        } else {
            ColumnInsertNow();
        }
        return true;
    case VK_ESCAPE:
        Hide();
        return true;
    case VK_TAB: {
        auto edits = VisibleEdits();
        auto it = std::find(edits.begin(), edits.end(), e);
        if (edits.empty() || it == edits.end()) return true;
        size_t i = it - edits.begin();
        i = shift ? (i + edits.size() - 1) % edits.size() : (i + 1) % edits.size();
        SetFocus(edits[i]);
        SendMessageW(edits[i], EM_SETSEL, 0, -1);
        return true;
    }
    case VK_UP:
    case VK_DOWN:
        if (e == findEdit_ || e == replaceEdit_) {
            CycleHistory(e, key == VK_UP ? 1 : -1);
            return true;
        }
        return false;
    case VK_BACK:
        if (ctrl) {
            // Delete the previous word, like most modern text boxes.
            DWORD selStart = 0, selEnd = 0;
            SendMessageW(e, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
            std::wstring t = EditText(e);
            size_t i = selStart;
            while (i > 0 && iswspace(t[i - 1])) i--;
            bool word = i > 0 && (iswalnum(t[i - 1]) || t[i - 1] == L'_');
            while (i > 0 && !iswspace(t[i - 1]) && ((iswalnum(t[i - 1]) || t[i - 1] == L'_') == word)) i--;
            SendMessageW(e, EM_SETSEL, i, selEnd);
            SendMessageW(e, EM_REPLACESEL, TRUE, (LPARAM)L"");
            return true;
        }
        return false;
    }
    return false;
}

bool Panel::PreTranslate(MSG* msg) {
    if (page_ == PanelPage::None) return false;
    HWND f = GetFocus();
    if (!f || GetParent(f) != hwnd) return false;
    if (msg->message != WM_SYSKEYDOWN || (page_ != PanelPage::Find && page_ != PanelPage::Replace)) return false;
    switch (msg->wParam) {
    case 'C': OnClick(P_CASE, -1); return true;
    case 'W': OnClick(P_WORD, -1); return true;
    case 'R': OnClick(P_MODE, opt.mode == SearchMode::Regex ? 0 : 2); return true;
    case 'E': OnClick(P_MODE, opt.mode == SearchMode::Extended ? 0 : 1); return true;
    case 'L': OnClick(P_INSEL, -1); return true;
    case VK_RETURN:
        if (f == findEdit_) { SelectAllMatches(); return true; }
        if (f == replaceEdit_) { ReplaceAll(); return true; }
        return false;
    }
    return false;
}

void Panel::OnClick(int id, int seg) {
    bool relayout = true, research = false;
    switch (id) {
    case P_TOGGLE_REPLACE:
        page_ = page_ == PanelPage::Replace ? PanelPage::Find : PanelPage::Replace;
        Layout();
        app->Layout();
        break;
    case P_PREV: FindNext(false); relayout = false; break;
    case P_NEXT: FindNext(true); relayout = false; break;
    case P_COUNT: Count(); relayout = false; break;
    case P_SELECTALL: SelectAllMatches(); relayout = false; break;
    case P_MARK: MarkAll(); relayout = false; break;
    case P_CLEAR:
        ClearIndicator(app->sci, IND_MARK);
        Status(L"Marks cleared");
        relayout = false;
        break;
    case P_CLOSE: Hide(); return;
    case P_REPLACE_ONE: Replace(); relayout = false; break;
    case P_REPLACE_ALL: ReplaceAll(); relayout = false; break;
    case P_MODE:
        if (seg >= 0) opt.mode = (SearchMode)seg;
        research = true;
        break;
    case P_CASE: opt.matchCase = !opt.matchCase; research = true; break;
    case P_WORD:
        if (opt.mode != SearchMode::Regex) opt.wholeWord = !opt.wholeWord;
        research = true;
        break;
    case P_WRAP: opt.wrap = !opt.wrap; break;
    case P_DOTNL: opt.dotNewline = !opt.dotNewline; research = true; break;
    case P_INSEL:
        if (inSelection) ClearScope();
        else if (!SetScopeFromSelection()) Status(L"Select some text first", 1);
        research = true;
        break;
    case P_BOOKMARK: bookmarkLines = !bookmarkLines; break;
    case P_GOTO_MODE:
        if (seg >= 0) gotoOffset_ = seg == 1;
        break;
    case P_GOTO_GO: GoTo(); return;
    case P_COL_MODE:
        if (seg >= 0) col_.numbers = seg == 1;
        break;
    case P_COL_BASE:
        if (seg >= 0) col_.base = seg == 1 ? 16 : seg == 2 ? 8 : seg == 3 ? 2 : 10;
        break;
    case P_COL_LEAD:
        if (seg >= 0) col_.leading = seg;
        break;
    case P_COL_INSERT: ColumnInsertNow(); return;
    }
    if (page_ == PanelPage::None) return;
    if (relayout) Layout();
    if (research) {
        UpdateLiveHighlight();
        SetTimer(hwnd, TIMER_INCREMENTAL, 1, nullptr);
    }
    FocusPrimary();
}

// ---------------------------------------------------------------------------
void Panel::PushHistory(std::vector<std::wstring>& h, const std::wstring& s) {
    if (s.empty()) return;
    std::erase(h, s);
    h.insert(h.begin(), s);
    if (h.size() > 30) h.resize(30);
}

void Panel::CycleHistory(HWND e, int dir) {
    auto& h = e == findEdit_ ? findHistory_ : replaceHistory_;
    if (h.empty()) return;
    int pos = std::clamp(historyPos_ + dir, -1, (int)h.size() - 1);
    if (pos == historyPos_) return;
    historyPos_ = pos;
    SetEditText(e, pos < 0 ? L"" : h[pos]);
    SendMessageW(e, EM_SETSEL, 0, -1);
    if (e == findEdit_) SetTimer(hwnd, TIMER_INCREMENTAL, 150, nullptr);
}

void Panel::LoadState() {
    Settings& s = app->settings;
    opt.matchCase = s.GetInt(L"find.matchCase", 0) != 0;
    opt.wholeWord = s.GetInt(L"find.wholeWord", 0) != 0;
    opt.wrap = s.GetInt(L"find.wrap", 1) != 0;
    opt.dotNewline = s.GetInt(L"find.dotNewline", 0) != 0;
    opt.mode = (SearchMode)std::clamp(s.GetInt(L"find.mode", 0), 0, 2);
    bookmarkLines = s.GetInt(L"find.bookmarkLines", 0) != 0;
    findHistory_ = s.GetList(L"find.history.");
    replaceHistory_ = s.GetList(L"replace.history.");
    if (!findHistory_.empty()) opt.what = findHistory_[0];
    if (!replaceHistory_.empty()) opt.with = replaceHistory_[0];
}

void Panel::SaveState() {
    Settings& s = app->settings;
    SyncOptionsFromEdits();
    s.SetInt(L"find.matchCase", opt.matchCase);
    s.SetInt(L"find.wholeWord", opt.wholeWord);
    s.SetInt(L"find.wrap", opt.wrap);
    s.SetInt(L"find.dotNewline", opt.dotNewline);
    s.SetInt(L"find.mode", (int)opt.mode);
    s.SetInt(L"find.bookmarkLines", bookmarkLines);
    if (!opt.what.empty()) PushHistory(findHistory_, opt.what);
    s.SetList(L"find.history.", findHistory_);
    s.SetList(L"replace.history.", replaceHistory_);
}

// ---------------------------------------------------------------------------
bool Panel::SetScopeFromSelection() {
    const Sci& sci = app->sci;
    Sci_Position a = sci.Length(), b = 0;
    int n = (int)sci(SCI_GETSELECTIONS);
    for (int i = 0; i < n; i++) {
        a = std::min(a, (Sci_Position)sci(SCI_GETSELECTIONNSTART, i));
        b = std::max(b, (Sci_Position)sci(SCI_GETSELECTIONNEND, i));
    }
    if (b <= a) return false;
    ClearIndicator(sci, IND_SCOPE);
    sci(SCI_SETINDICATORCURRENT, IND_SCOPE);
    sci(SCI_INDICATORFILLRANGE, a, b - a);
    inSelection = true;
    incAnchor_ = a;
    return true;
}

void Panel::ClearScope() {
    ClearIndicator(app->sci, IND_SCOPE);
    inSelection = false;
}

bool Panel::GetScope(Sci_Position& a, Sci_Position& b) {
    if (!inSelection) return false;
    const Sci& sci = app->sci;
    Sci_Position len = sci.Length();
    a = sci(SCI_INDICATORVALUEAT, IND_SCOPE, 0) ? 0 : (Sci_Position)sci(SCI_INDICATOREND, IND_SCOPE, 0);
    if (a >= len || (a == 0 && !sci(SCI_INDICATORVALUEAT, IND_SCOPE, 0))) {
        inSelection = false;
        return false;
    }
    b = (Sci_Position)sci(SCI_INDICATOREND, IND_SCOPE, a);
    return b > a;
}

void Panel::Range(Sci_Position& lo, Sci_Position& hi) {
    if (!GetScope(lo, hi)) {
        lo = 0;
        hi = app->sci.Length();
    }
}

bool Panel::PrepareSearch(Searcher& s) {
    SyncOptionsFromEdits();
    std::wstring err;
    if (!s.Prepare(app->sci, opt, err)) {
        if (!err.empty()) Status(err, 1);
        return false;
    }
    return true;
}

std::wstring Panel::MatchCountText(Searcher& s, const Match& m, Sci_Position lo, Sci_Position hi) {
    if (hi - lo > 16 * 1024 * 1024) return L"";
    size_t index = 0, seen = 0;
    size_t total = s.Each(lo, hi, [&](const Match& x) {
        seen++;
        if (x.start == m.start) index = seen;
        return seen < 1000000;
    });
    if (!index) return L"";
    return std::to_wstring(index) + L" of " + std::to_wstring(total);
}

void Panel::FindNext(bool forward, bool) {
    SyncOptionsFromEdits();
    if (opt.what.empty()) {
        Show(PanelPage::Find);
        return;
    }
    Searcher s;
    if (!PrepareSearch(s)) {
        if (page_ == PanelPage::None) Show(PanelPage::Find);
        return;
    }
    const Sci& sci = app->sci;
    Sci_Position lo, hi;
    Range(lo, hi);
    Sci_Position selStart = (Sci_Position)sci(SCI_GETSELECTIONSTART), selEnd = (Sci_Position)sci(SCI_GETSELECTIONEND);
    Match m;
    bool wrapped = false;
    if (forward) {
        Sci_Position from = std::clamp(selEnd, lo, hi);
        if (selStart == selEnd && lastEmptyMatch_ == selEnd) from = (Sci_Position)sci(SCI_POSITIONAFTER, from);
        m = s.Forward(from, hi);
        if (!m.Found() && opt.wrap && s.Error().empty()) {
            m = s.Forward(lo, hi);
            wrapped = m.Found();
        }
    } else {
        Sci_Position to = std::clamp(selStart, lo, hi);
        m = s.Backward(lo, to);
        if (!m.Found() && opt.wrap && s.Error().empty()) {
            m = s.Backward(lo, hi);
            wrapped = m.Found();
        }
    }
    PushHistory(findHistory_, opt.what);
    if (!s.Error().empty()) {
        Status(s.Error(), 1);
        return;
    }
    if (!m.Found()) {
        Status(L"No match for “" + opt.what + L"”", 1);
        MessageBeep(MB_OK);
        return;
    }
    sci(SCI_SETSEL, m.start, m.end);
    lastEmptyMatch_ = m.start == m.end ? m.start : -1;
    RevealRange(sci, m.start, m.end);
    incAnchor_ = m.start;
    std::wstring info = MatchCountText(s, m, lo, hi);
    if (wrapped) info = (info.empty() ? L"" : info + L"  ·  ") + (forward ? L"Wrapped to the top" : L"Wrapped to the bottom");
    Status(info);
    if (page_ != PanelPage::None) UpdateLiveHighlight();
}

void Panel::FindSelection(bool forward) {
    const Sci& sci = app->sci;
    std::string sel = sci.SelText();
    if (sel.empty()) {
        Sci_Position p = sci.Pos();
        Sci_Position a = (Sci_Position)sci(SCI_WORDSTARTPOSITION, p, 1), b = (Sci_Position)sci(SCI_WORDENDPOSITION, p, 1);
        if (b <= a) return;
        sel = sci.Range(a, b);
        sci(SCI_SETSEL, a, b);
    }
    if (sel.find_first_of("\r\n") != std::string::npos) return;
    opt.what = Widen(sel);
    opt.mode = SearchMode::Normal;
    if (findEdit_) SetEditText(findEdit_, opt.what);
    if (page_ != PanelPage::None) Layout();
    FindNext(forward);
}

void Panel::IncrementalFind() {
    if (page_ != PanelPage::Find && page_ != PanelPage::Replace) return;
    SyncOptionsFromEdits();
    const Sci& sci = app->sci;
    if (opt.what.empty()) {
        ClearIndicator(sci, IND_FIND);
        Status(L"");
        return;
    }
    Searcher s;
    std::wstring err;
    if (!s.Prepare(sci, opt, err)) {
        ClearIndicator(sci, IND_FIND);
        Status(err, 1);
        return;
    }
    Sci_Position lo, hi;
    Range(lo, hi);
    if (hi - lo > 64 * 1024 * 1024) { // too big to search on every keystroke
        Status(L"Press Enter to search");
        UpdateLiveHighlight();
        return;
    }
    Sci_Position from = std::clamp(incAnchor_, lo, hi);
    Match m = s.Forward(from, hi);
    if (!m.Found() && opt.wrap && s.Error().empty()) m = s.Forward(lo, hi);
    if (!s.Error().empty()) {
        Status(s.Error(), 1);
        return;
    }
    if (m.Found()) {
        sci(SCI_SETSEL, m.start, m.end);
        RevealRange(sci, m.start, m.end);
        Status(MatchCountText(s, m, lo, hi));
    } else {
        Status(L"No results", 1);
    }
    UpdateLiveHighlight();
}

void Panel::UpdateLiveHighlight() {
    const Sci& sci = app->sci;
    ClearIndicator(sci, IND_FIND);
    if (page_ != PanelPage::Find && page_ != PanelPage::Replace) return;
    SyncOptionsFromEdits();
    if (opt.what.empty()) return;
    Searcher s;
    std::wstring err;
    if (!s.Prepare(sci, opt, err)) return;
    Sci_Position firstVis = (Sci_Position)sci(SCI_GETFIRSTVISIBLELINE);
    Sci_Position lines = (Sci_Position)sci(SCI_LINESONSCREEN);
    Sci_Position a = sci.LineStart((Sci_Position)sci(SCI_DOCLINEFROMVISIBLE, firstVis));
    Sci_Position b = sci.LineEnd((Sci_Position)sci(SCI_DOCLINEFROMVISIBLE, firstVis + lines + 1));
    Sci_Position lo, hi;
    Range(lo, hi);
    a = std::max(a, lo);
    b = std::min(b, hi);
    if (b <= a) return;
    sci(SCI_SETINDICATORCURRENT, IND_FIND);
    int count = 0;
    s.Each(a, b, [&](const Match& m) {
        if (m.end > m.start) sci(SCI_INDICATORFILLRANGE, m.start, m.end - m.start);
        return ++count < 5000;
    });
}

void Panel::Count() {
    Searcher s;
    if (!PrepareSearch(s)) return;
    Sci_Position lo, hi;
    Range(lo, hi);
    size_t n = s.Each(lo, hi, [](const Match&) { return true; });
    PushHistory(findHistory_, opt.what);
    if (!s.Error().empty()) { Status(s.Error(), 1); return; }
    Status(std::to_wstring(n) + (n == 1 ? L" match" : L" matches") + (inSelection ? L" in selection" : L""), n ? 2 : 1);
}

void Panel::MarkAll() {
    Searcher s;
    if (!PrepareSearch(s)) return;
    const Sci& sci = app->sci;
    Sci_Position lo, hi;
    Range(lo, hi);
    ClearIndicator(sci, IND_MARK);
    sci(SCI_SETINDICATORCURRENT, IND_MARK);
    size_t n = s.Each(lo, hi, [&](const Match& m) {
        if (m.end > m.start) sci(SCI_INDICATORFILLRANGE, m.start, m.end - m.start);
        if (bookmarkLines) sci(SCI_MARKERADD, sci.Line(m.start), MARK_BOOKMARK);
        return true;
    });
    PushHistory(findHistory_, opt.what);
    if (!s.Error().empty()) { Status(s.Error(), 1); return; }
    Status(L"Marked " + std::to_wstring(n) + (n == 1 ? L" match" : L" matches") +
               (bookmarkLines && n ? L" and bookmarked their lines" : L""), n ? 2 : 1);
}

void Panel::SelectAllMatches() {
    Searcher s;
    if (!PrepareSearch(s)) return;
    const Sci& sci = app->sci;
    Sci_Position lo, hi;
    Range(lo, hi);
    std::vector<Match> ms;
    s.Each(lo, hi, [&](const Match& m) {
        ms.push_back(m);
        return ms.size() < 200000;
    });
    if (!s.Error().empty()) { Status(s.Error(), 1); return; }
    if (ms.empty()) { Status(L"No results", 1); return; }
    ClearScope();
    sci(SCI_SETSELECTION, ms[0].end, ms[0].start);
    for (size_t i = 1; i < ms.size(); i++) sci(SCI_ADDSELECTION, ms[i].end, ms[i].start);
    sci(SCI_SETMAINSELECTION, 0);
    RevealRange(sci, ms[0].start, ms[0].end);
    Status(std::to_wstring(ms.size()) + L" carets — type to edit all matches");
    SetFocus(sci.hwnd);
}

void Panel::Replace() {
    Searcher s;
    if (!PrepareSearch(s)) return;
    const Sci& sci = app->sci;
    Sci_Position a = (Sci_Position)sci(SCI_GETSELECTIONSTART), b = (Sci_Position)sci(SCI_GETSELECTIONEND);
    Sci_Position lo, hi;
    Range(lo, hi);
    PushHistory(replaceHistory_, opt.with);
    Match m = s.Forward(a, hi);
    if (m.Found() && m.start == a && m.end == b) {
        std::string repl = s.Replacement();
        if (!s.Error().empty()) { Status(s.Error(), 1); return; }
        sci.Replace(a, b, repl);
        Sci_Position after = a + (Sci_Position)repl.size();
        sci(SCI_SETSEL, after, after);
        lastEmptyMatch_ = m.start == m.end ? after : -1;
        FindNext(true);
        if (statusKind_ != 1) Status(L"Replaced 1 · " + status_, 2);
        else Status(L"Replaced 1 · no more matches", 2);
        return;
    }
    FindNext(true);
}

void Panel::ReplaceAll() {
    Searcher s;
    if (!PrepareSearch(s)) return;
    const Sci& sci = app->sci;
    Sci_Position lo, hi;
    Range(lo, hi);
    PushHistory(findHistory_, opt.what);
    PushHistory(replaceHistory_, opt.with);
    struct Rep { Match m; std::string text; };
    std::vector<Rep> reps;
    const bool constant = !s.IsRegex();
    std::string constantText = constant ? s.Replacement() : std::string();
    s.Each(lo, hi, [&](const Match& m) {
        reps.push_back({m, constant ? std::string() : s.Replacement()});
        return true;
    });
    if (!s.Error().empty()) { Status(s.Error(), 1); return; }
    if (reps.empty()) { Status(L"No matches to replace", 1); return; }
    auto textOf = [&](const Rep& r) -> const std::string& { return constant ? constantText : r.text; };

    Sci_Position delta = 0;
    sci(SCI_BEGINUNDOACTION);
    if (reps.size() > 1000) {
        // One big replacement is far faster than thousands of small ones.
        Sci_Position spanA = reps.front().m.start, spanB = reps.back().m.end;
        const char* doc = (const char*)sci(SCI_GETCHARACTERPOINTER);
        std::string out;
        out.reserve(size_t(spanB - spanA) + reps.size() * textOf(reps[0]).size());
        Sci_Position cur = spanA;
        for (auto& r : reps) {
            out.append(doc + cur, size_t(r.m.start - cur));
            out += textOf(r);
            cur = r.m.end;
        }
        Sci_Position caret = sci.Pos();
        Sci_Position firstLine = (Sci_Position)sci(SCI_GETFIRSTVISIBLELINE);
        delta = (Sci_Position)out.size() - (spanB - spanA);
        sci.Replace(spanA, spanB, out);
        sci(SCI_GOTOPOS, std::min(caret, sci.Length()));
        sci(SCI_SETFIRSTVISIBLELINE, firstLine);
    } else {
        for (size_t i = reps.size(); i-- > 0;) {
            const std::string& t = textOf(reps[i]);
            sci.Replace(reps[i].m.start, reps[i].m.end, t);
            delta += (Sci_Position)t.size() - (reps[i].m.end - reps[i].m.start);
        }
    }
    sci(SCI_ENDUNDOACTION);
    if (inSelection) {
        ClearIndicator(sci, IND_SCOPE);
        sci(SCI_SETINDICATORCURRENT, IND_SCOPE);
        sci(SCI_INDICATORFILLRANGE, lo, std::max<Sci_Position>(0, hi + delta - lo));
    }
    Status(L"Replaced " + std::to_wstring(reps.size()) + (reps.size() == 1 ? L" occurrence" : L" occurrences") +
               (inSelection ? L" in selection" : L""), 2);
    UpdateLiveHighlight();
}

void Panel::GoTo() {
    const Sci& sci = app->sci;
    std::wstring t = EditText(gotoEdit_);
    long long a = 0, b = 0;
    int n = swscanf_s(t.c_str(), L"%lld%*[:, ]%lld", &a, &b);
    if (n < 1) {
        Status(L"Enter a number", 1);
        return;
    }
    if (gotoOffset_) {
        Sci_Position pos = std::clamp<Sci_Position>((Sci_Position)a, 0, sci.Length());
        sci(SCI_GOTOPOS, pos);
        CenterLine(sci, sci.Line(pos));
    } else {
        Sci_Position line = std::clamp<Sci_Position>((Sci_Position)a - 1, 0, sci.LineCount() - 1);
        Sci_Position pos = sci.LineStart(line);
        if (n >= 2 && b > 1) pos = (Sci_Position)sci(SCI_FINDCOLUMN, line, (Sci_Position)b - 1);
        sci(SCI_GOTOPOS, pos);
        CenterLine(sci, line);
    }
    Hide();
}

void Panel::ColumnInsertNow() {
    col_.text = EditText(colText_);
    col_.initial = _wtoi64(EditText(colInit_).c_str());
    col_.step = _wtoi64(EditText(colStep_).c_str());
    col_.repeat = std::max<long long>(1, _wtoi64(EditText(colRepeat_).c_str()));
    if (!col_.numbers && col_.text.empty()) {
        Status(L"Enter the text to insert", 1);
        return;
    }
    int n = InsertColumn(app->sci, col_);
    Hide();
    (void)n;
}

} // namespace yan
