// Yan: a fast Notepad++-inspired text editor for Windows.
#include "app.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <commctrl.h>
#include <algorithm>

namespace yan {

App* app = nullptr;

static const wchar_t* kMainClass = L"YanMainWindow";
constexpr UINT WM_APP_CHECKFILES = WM_APP + 1;
constexpr ULONG_PTR COPYDATA_OPEN = 0x59414E31; // 'YAN1'

// ---------------------------------------------------------------------------
// Drop target: opens dropped files, forwards text drags to Scintilla.
class FileDropTarget : public IDropTarget {
public:
    explicit FileDropTarget(IDropTarget* inner) : inner_(inner) {}
    ~FileDropTarget() { if (inner_) inner_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD keys, POINTL pt, DWORD* effect) override {
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        files_ = d->QueryGetData(&fmt) == S_OK;
        if (files_) { *effect = DROPEFFECT_COPY; return S_OK; }
        if (inner_) return inner_->DragEnter(d, keys, pt, effect);
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        if (files_) { *effect = DROPEFFECT_COPY; return S_OK; }
        if (inner_) return inner_->DragOver(keys, pt, effect);
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        bool f = files_;
        files_ = false;
        return !f && inner_ ? inner_->DragLeave() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD keys, POINTL pt, DWORD* effect) override {
        if (!files_) return inner_ ? inner_->Drop(d, keys, pt, effect) : S_OK;
        files_ = false;
        FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stg{};
        if (d->GetData(&fmt, &stg) == S_OK) {
            HDROP drop = (HDROP)GlobalLock(stg.hGlobal);
            std::vector<std::wstring> paths;
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; i++) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring p(len, L'\0');
                DragQueryFileW(drop, i, p.data(), len + 1);
                paths.push_back(p);
            }
            GlobalUnlock(stg.hGlobal);
            ReleaseStgMedium(&stg);
            SetForegroundWindow(app->hwnd);
            app->OpenFiles(paths, L"", 0);
        }
        *effect = DROPEFFECT_COPY;
        return S_OK;
    }

private:
    LONG ref_ = 1;
    IDropTarget* inner_;
    bool files_ = false;
};

static void InstallDropTarget(HWND h, bool forwardToScintilla) {
    IDropTarget* inner = nullptr;
    if (forwardToScintilla) {
        inner = (IDropTarget*)GetPropW(h, L"OleDropTargetInterface");
        if (inner) inner->AddRef();
        RevokeDragDrop(h);
    }
    auto* t = new FileDropTarget(inner);
    RegisterDragDrop(h, t);
    t->Release();
}

// ---------------------------------------------------------------------------
static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        app->hwnd = h;
        EnableNonClientDpiScaling(h);
    }
    if (!app || !app->hwnd) return DefWindowProcW(h, msg, wp, lp);
    return app->WndProc(msg, wp, lp);
}

void App::LoadViewSettings() {
    themeMode = (ThemeMode)std::clamp(settings.GetInt(L"view.theme", 0), 0, 2);
    view.wrap = settings.GetInt(L"view.wrap", 0) != 0;
    view.whitespace = settings.GetInt(L"view.whitespace", 0) != 0;
    view.eolMarks = settings.GetInt(L"view.eol", 0) != 0;
    view.indentGuides = settings.GetInt(L"view.indentGuides", 0) != 0;
    view.lineNumbers = settings.GetInt(L"view.lineNumbers", 1) != 0;
    // GDI by default: DirectWrite adds ~130 ms to the first paint (Direct2D/GPU setup).
    view.directWrite = settings.GetInt(L"view.directWrite", 0) != 0;
    view.technology = settings.GetInt(L"view.technology", SC_TECHNOLOGY_DIRECTWRITE);
    view.fontSize = std::clamp(settings.GetInt(L"view.fontSize", 11), 5, 72);
    view.font = settings.Get(L"view.font");
    if (view.font.empty()) view.font = FontInstalled(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    singleInstance = settings.GetInt(L"app.singleInstance", 1) != 0;
    restoreSession = settings.GetInt(L"app.restoreSession", 1) != 0;
    exitOnLastTab = settings.GetInt(L"app.exitOnLastTab", 0) != 0;
    checkChanges = settings.GetInt(L"app.checkChanges", 1) != 0;
    autoReload = settings.GetInt(L"app.autoReload", 0) != 0;
    saveAllDir = settings.Get(L"app.saveAllDir");
    view.tabWidth = std::clamp(settings.GetInt(L"edit.tabWidth", 4), 1, 16);
    view.useSpaces = settings.GetInt(L"edit.useSpaces", 0) != 0;
    view.autoIndent = settings.GetInt(L"edit.autoIndent", 1) != 0;
    view.trimOnSave = settings.GetInt(L"edit.trimOnSave", 0) != 0;
    view.caretLine = settings.GetInt(L"view.caretLine", 1) != 0;
    view.smartHighlight = settings.GetInt(L"view.smartHighlight", 1) != 0;
    view.braceMatch = settings.GetInt(L"view.braceMatch", 1) != 0;
    view.scrollPastEnd = settings.GetInt(L"view.scrollPastEnd", 1) != 0;
    view.syntax = settings.GetInt(L"view.syntax", 1) != 0;
    view.folding = settings.GetInt(L"view.folding", 1) != 0;
    recentMax = std::clamp(settings.GetInt(L"app.recentMax", 15), 0, CMD_RECENT_LAST - CMD_RECENT + 1);
    recent = settings.GetList(L"recent.");
    if ((int)recent.size() > recentMax) recent.resize(recentMax);
    int eol = settings.GetInt(L"new.eol", SC_EOL_CRLF);
    newEol = eol == SC_EOL_LF || eol == SC_EOL_CR ? eol : SC_EOL_CRLF;
    newEncoding = settings.GetInt(L"new.utf8Bom", 0) ? Encoding::Utf8Bom : Encoding::Utf8;
}

void App::SaveViewSettings() {
    settings.SetInt(L"view.theme", (int)themeMode);
    settings.SetInt(L"view.wrap", view.wrap);
    settings.SetInt(L"view.whitespace", view.whitespace);
    settings.SetInt(L"view.eol", view.eolMarks);
    settings.SetInt(L"view.indentGuides", view.indentGuides);
    settings.SetInt(L"view.lineNumbers", view.lineNumbers);
    settings.SetInt(L"view.directWrite", view.directWrite);
    settings.SetInt(L"view.fontSize", view.fontSize);
    settings.Set(L"view.font", view.font);
    settings.SetInt(L"view.zoom", (int)sci(SCI_GETZOOM));
    settings.SetInt(L"app.singleInstance", singleInstance);
    settings.SetInt(L"app.restoreSession", restoreSession);
    settings.SetInt(L"app.exitOnLastTab", exitOnLastTab);
    settings.SetInt(L"app.checkChanges", checkChanges);
    settings.SetInt(L"app.autoReload", autoReload);
    settings.Set(L"app.saveAllDir", saveAllDir);
    settings.SetInt(L"edit.tabWidth", view.tabWidth);
    settings.SetInt(L"edit.useSpaces", view.useSpaces);
    settings.SetInt(L"edit.autoIndent", view.autoIndent);
    settings.SetInt(L"edit.trimOnSave", view.trimOnSave);
    settings.SetInt(L"view.caretLine", view.caretLine);
    settings.SetInt(L"view.smartHighlight", view.smartHighlight);
    settings.SetInt(L"view.braceMatch", view.braceMatch);
    settings.SetInt(L"view.scrollPastEnd", view.scrollPastEnd);
    settings.SetInt(L"view.syntax", view.syntax);
    settings.SetInt(L"view.folding", view.folding);
    settings.SetInt(L"app.recentMax", recentMax);
    if ((int)recent.size() > recentMax) recent.resize(recentMax);
    settings.SetList(L"recent.", recent);
    settings.SetInt(L"new.eol", newEol);
    settings.SetInt(L"new.utf8Bom", newEncoding == Encoding::Utf8Bom);
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(hwnd, &wp)) {
        RECT r = wp.rcNormalPosition;
        settings.SetInt(L"window.x", r.left);
        settings.SetInt(L"window.y", r.top);
        settings.SetInt(L"window.w", r.right - r.left);
        settings.SetInt(L"window.h", r.bottom - r.top);
        settings.SetInt(L"window.max", wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED));
    }
}

// ---------------------------------------------------------------------------
static void AddItem(HMENU m, int id, const wchar_t* text) { AppendMenuW(m, MF_STRING, id, text); }
static void AddSep(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }
static void AddSub(HMENU m, HMENU sub, const wchar_t* text) { AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, text); }
static void AddLanguageItems(HMENU m) {
    AddItem(m, CMD_LANG_AUTO, L"&Auto-detect from file name");
    AddSep(m);
    for (int l = 0; l < (int)Lang::Count; l++) AddItem(m, CMD_LANG + l, LangName((Lang)l));
}

void App::CreateMenus() {
    menu_ = CreateMenu();
    HMENU file = CreatePopupMenu();
    AddItem(file, CMD_NEW, L"&New\tCtrl+N");
    AddItem(file, CMD_OPEN, L"&Open...\tCtrl+O");
    AddItem(file, CMD_RELOAD, L"&Reload from disk");
    AddSep(file);
    AddItem(file, CMD_SAVE, L"&Save\tCtrl+S");
    AddItem(file, CMD_SAVEAS, L"Save &As...\tCtrl+Alt+S");
    AddItem(file, CMD_SAVEALL, L"Save A&ll\tCtrl+Shift+S");
    AddItem(file, CMD_SAVEALLTO, L"Save all to fol&der...");
    AddSep(file);
    AddItem(file, CMD_CLOSE, L"&Close\tCtrl+W");
    AddItem(file, CMD_CLOSEOTHERS, L"Close &others");
    AddItem(file, CMD_CLOSEALL, L"Close all");
    AddItem(file, CMD_REOPENCLOSED, L"Reopen closed &tab\tCtrl+Shift+T");
    recentMenu_ = CreatePopupMenu(); // filled each time it opens (BuildRecentMenu)
    AddSub(file, recentMenu_, L"Recent &files");
    AddSep(file);
    AddItem(file, CMD_OPENFOLDER, L"Open containing &folder");
    AddItem(file, CMD_COPYPATH, L"Copy full &path");
    AddItem(file, CMD_COPYNAME, L"Copy file na&me");
    AddSep(file);
    AddItem(file, CMD_EXIT, L"E&xit\tAlt+F4");
    AddSub(menu_, file, L"&File");

    HMENU edit = CreatePopupMenu();
    AddItem(edit, CMD_UNDO, L"&Undo\tCtrl+Z");
    AddItem(edit, CMD_REDO, L"&Redo\tCtrl+Y");
    AddSep(edit);
    AddItem(edit, CMD_CUT, L"Cu&t\tCtrl+X");
    AddItem(edit, CMD_COPY, L"&Copy\tCtrl+C");
    AddItem(edit, CMD_PASTE, L"&Paste\tCtrl+V");
    AddItem(edit, CMD_DELETE, L"&Delete\tDel");
    AddItem(edit, CMD_SELECTALL, L"Select &all\tCtrl+A");
    AddSep(edit);
    AddItem(edit, CMD_COLUMNEDITOR, L"Colu&mn editor...\tAlt+C");
    AddItem(edit, CMD_SELECTNEXT, L"Add &next occurrence\tCtrl+Shift+D");
    AddItem(edit, CMD_SELECTALLOCC, L"Select all &occurrences\tAlt+F3");
    AddSep(edit);
    HMENU lines = CreatePopupMenu();
    AddItem(lines, CMD_DUPLINE, L"&Duplicate\tCtrl+D");
    AddItem(lines, CMD_DELLINE, L"D&elete\tCtrl+Shift+L");
    AddItem(lines, CMD_MOVEUP, L"Move &up\tCtrl+Shift+Up");
    AddItem(lines, CMD_MOVEDOWN, L"Move do&wn\tCtrl+Shift+Down");
    AddItem(lines, CMD_JOIN, L"&Join\tCtrl+J");
    AddSep(lines);
    AddItem(lines, CMD_SORTASC, L"Sort &ascending");
    AddItem(lines, CMD_SORTASCCI, L"Sort ascending (&ignore case)");
    AddItem(lines, CMD_SORTDESC, L"Sort de&scending");
    AddItem(lines, CMD_REVERSE, L"&Reverse order");
    AddSep(lines);
    AddItem(lines, CMD_DEDUPE, L"Remove d&uplicate lines");
    AddItem(lines, CMD_REMOVEEMPTY, L"Remove empty &lines");
    AddItem(lines, CMD_TRIM, L"&Trim trailing whitespace");
    AddSub(edit, lines, L"&Line operations");
    HMENU caseMenu = CreatePopupMenu();
    AddItem(caseMenu, CMD_UPPER, L"&UPPERCASE\tCtrl+Shift+U");
    AddItem(caseMenu, CMD_LOWER, L"&lowercase\tCtrl+U");
    AddSub(edit, caseMenu, L"Convert &case");
    HMENU eol = CreatePopupMenu();
    AddItem(eol, CMD_EOL_CRLF, L"&Windows (CRLF)");
    AddItem(eol, CMD_EOL_LF, L"&Unix (LF)");
    AddItem(eol, CMD_EOL_CR, L"&Mac (CR)");
    AddSub(edit, eol, L"&EOL conversion");
    AddItem(edit, CMD_INSERTDATE, L"Insert &time/date\tF5");
    AddSub(menu_, edit, L"&Edit");

    HMENU search = CreatePopupMenu();
    AddItem(search, CMD_FIND, L"&Find...\tCtrl+F");
    AddItem(search, CMD_REPLACE, L"&Replace...\tCtrl+H");
    AddItem(search, CMD_FINDNEXT, L"Find &next\tF3");
    AddItem(search, CMD_FINDPREV, L"Find &previous\tShift+F3");
    AddItem(search, CMD_FINDSELNEXT, L"Find &selection next\tCtrl+F3");
    AddItem(search, CMD_FINDSELPREV, L"Find selection pre&vious\tCtrl+Shift+F3");
    AddItem(search, CMD_CLEARMARKS, L"&Clear all marks");
    AddSep(search);
    AddItem(search, CMD_GOTO, L"&Go to...\tCtrl+G");
    AddItem(search, CMD_BRACE, L"Go to matching &brace\tCtrl+B");
    AddSep(search);
    HMENU bm = CreatePopupMenu();
    AddItem(bm, CMD_BM_TOGGLE, L"&Toggle bookmark\tCtrl+F2");
    AddItem(bm, CMD_BM_NEXT, L"&Next bookmark\tF2");
    AddItem(bm, CMD_BM_PREV, L"&Previous bookmark\tShift+F2");
    AddItem(bm, CMD_BM_CLEAR, L"&Clear all bookmarks");
    AddSep(bm);
    AddItem(bm, CMD_BM_COPY, L"C&opy bookmarked lines");
    AddItem(bm, CMD_BM_CUT, L"C&ut bookmarked lines");
    AddItem(bm, CMD_BM_REMOVE, L"&Remove bookmarked lines");
    AddItem(bm, CMD_BM_REMOVEUNMARKED, L"Remove &unbookmarked lines");
    AddItem(bm, CMD_BM_INVERSE, L"&Inverse bookmarks");
    AddSub(search, bm, L"&Bookmarks");
    AddSub(menu_, search, L"&Search");

    HMENU viewMenu = CreatePopupMenu();
    AddItem(viewMenu, CMD_WRAP, L"&Word wrap\tAlt+Z");
    AddItem(viewMenu, CMD_WHITESPACE, L"Show white&space");
    AddItem(viewMenu, CMD_EOLMARKS, L"Show &end of line");
    AddItem(viewMenu, CMD_INDENTGUIDES, L"&Indent guides");
    AddItem(viewMenu, CMD_LINENUMBERS, L"&Line numbers");
    AddItem(viewMenu, CMD_FOLDING, L"Code &folding");
    HMENU fold = CreatePopupMenu();
    AddItem(fold, CMD_FOLDALL, L"&Fold all\tAlt+0");
    AddItem(fold, CMD_UNFOLDALL, L"&Unfold all\tAlt+Shift+0");
    AddItem(fold, CMD_FOLDTOGGLE, L"&Toggle current block\tCtrl+Alt+F");
    AddSub(viewMenu, fold, L"F&old");
    AddSep(viewMenu);
    AddItem(viewMenu, CMD_ZOOMIN, L"Zoom &in\tCtrl++");
    AddItem(viewMenu, CMD_ZOOMOUT, L"Zoom &out\tCtrl+-");
    AddItem(viewMenu, CMD_ZOOMRESET, L"&Reset zoom\tCtrl+0");
    AddSep(viewMenu);
    HMENU themeMenu = CreatePopupMenu();
    AddItem(themeMenu, CMD_THEME_SYSTEM, L"&System");
    AddItem(themeMenu, CMD_THEME_LIGHT, L"&Light");
    AddItem(themeMenu, CMD_THEME_DARK, L"&Dark");
    AddSub(viewMenu, themeMenu, L"&Theme");
    AddItem(viewMenu, CMD_FONT, L"&Font...");
    AddItem(viewMenu, CMD_DIRECTWRITE, L"&DirectWrite rendering (slower startup)");
    AddSep(viewMenu);
    AddItem(viewMenu, CMD_NEXTTAB, L"&Next tab\tCtrl+Tab");
    AddItem(viewMenu, CMD_PREVTAB, L"&Previous tab\tCtrl+Shift+Tab");
    AddSep(viewMenu);
    AddItem(viewMenu, CMD_SINGLEINSTANCE, L"Open files in the &same window");
    AddItem(viewMenu, CMD_RESTORESESSION, L"&Restore open files on startup");
    AddSep(viewMenu);
    AddItem(viewMenu, CMD_SETTINGS, L"S&ettings...\tCtrl+,");
    AddSub(menu_, viewMenu, L"&View");

    HMENU lang = CreatePopupMenu();
    AddLanguageItems(lang);
    AddSub(menu_, lang, L"&Language");

    HMENU enc = CreatePopupMenu();
    for (int e = 0; e <= (int)Encoding::Ansi; e++) {
        std::wstring label = std::wstring(L"Convert to ") + EncodingName((Encoding)e);
        AddItem(enc, CMD_ENC_TO + e, label.c_str());
    }
    AddSep(enc);
    HMENU reopen = CreatePopupMenu();
    for (int e = 0; e <= (int)Encoding::Ansi; e++) AddItem(reopen, CMD_ENC_RELOAD + e, EncodingName((Encoding)e));
    AddSub(enc, reopen, L"&Reopen with encoding");
    AddSub(menu_, enc, L"E&ncoding");

    HMENU help = CreatePopupMenu();
    AddItem(help, CMD_SHORTCUTS, L"&Keyboard shortcuts");
    AddItem(help, CMD_ABOUT, L"&About Yan");
    AddSub(menu_, help, L"&?");
}

void App::UpdateMenuChecks(HMENU m) {
    auto check = [&](int id, bool on) { CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    auto enable = [&](int id, bool on) { EnableMenuItem(m, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    check(CMD_WRAP, view.wrap);
    check(CMD_WHITESPACE, view.whitespace);
    check(CMD_EOLMARKS, view.eolMarks);
    check(CMD_INDENTGUIDES, view.indentGuides);
    check(CMD_LINENUMBERS, view.lineNumbers);
    check(CMD_DIRECTWRITE, view.directWrite);
    check(CMD_SINGLEINSTANCE, singleInstance);
    check(CMD_RESTORESESSION, restoreSession);
    check(CMD_FOLDING, view.folding);
    bool foldable = sci(SCI_GETMARGINWIDTHN, MARGIN_FOLD) > 0;
    for (int id : {CMD_FOLDALL, CMD_UNFOLDALL, CMD_FOLDTOGGLE}) enable(id, foldable);
    CheckMenuRadioItem(m, CMD_THEME_SYSTEM, CMD_THEME_DARK, CMD_THEME_SYSTEM + (int)themeMode, MF_BYCOMMAND);
    if (active >= 0) {
        const Doc& d = Cur();
        CheckMenuRadioItem(m, CMD_EOL_CRLF, CMD_EOL_CR,
                           d.eol == SC_EOL_CRLF ? CMD_EOL_CRLF : d.eol == SC_EOL_LF ? CMD_EOL_LF : CMD_EOL_CR, MF_BYCOMMAND);
        CheckMenuRadioItem(m, CMD_ENC_TO, CMD_ENC_TO + (int)Encoding::Ansi, CMD_ENC_TO + (int)d.encoding, MF_BYCOMMAND);
        bool hasPath = !d.path.empty();
        enable(CMD_RELOAD, hasPath);
        enable(CMD_OPENFOLDER, hasPath);
        enable(CMD_COPYPATH, hasPath);
        for (int e = 0; e <= (int)Encoding::Ansi; e++) enable(CMD_ENC_RELOAD + e, hasPath);
        // Huge files are loaded without style storage: no highlighting possible.
        bool canStyle = !(sci(SCI_GETDOCUMENTOPTIONS) & SC_DOCUMENTOPTION_STYLES_NONE);
        CheckMenuRadioItem(m, CMD_LANG, CMD_LANG + (int)Lang::Count - 1, CMD_LANG + (int)CurLang(), MF_BYCOMMAND);
        check(CMD_LANG_AUTO, !d.langChosen);
        enable(CMD_LANG_AUTO, canStyle);
        for (int l = 0; l < (int)Lang::Count; l++) enable(CMD_LANG + l, canStyle);
        enable(CMD_UNDO, sci(SCI_CANUNDO) != 0);
        enable(CMD_REDO, sci(SCI_CANREDO) != 0);
        enable(CMD_PASTE, sci(SCI_CANPASTE) != 0);
    }
    enable(CMD_REOPENCLOSED, !closedPaths.empty());
}

// ---------------------------------------------------------------------------
bool App::Init(HINSTANCE instance, const std::vector<std::wstring>& files, int gotoLine) {
    inst = instance;
    LoadViewSettings();
    InitDarkModeSupport();
    BuildTheme(themeMode == ThemeMode::System ? SystemUsesDarkTheme() : themeMode == ThemeMode::Dark);
    Scintilla_RegisterClasses(inst);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    CreateMenus();
    int x = settings.GetInt(L"window.x", CW_USEDEFAULT), y = settings.GetInt(L"window.y", CW_USEDEFAULT);
    int w = settings.GetInt(L"window.w", 0), h = settings.GetInt(L"window.h", 0);
    UINT sysDpi = GetDpiForSystem();
    if (w < 200 || h < 150) { w = Px(1100, sysDpi); h = Px(760, sysDpi); x = y = CW_USEDEFAULT; }
    if (x != CW_USEDEFAULT) {
        // Make sure the saved position is still on a monitor.
        RECT r = {x, y, x + w, y + h};
        if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }
    CreateWindowExW(0, kMainClass, L"Yan", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, menu_, inst, nullptr);
    if (!hwnd) return false;

    // Documents: command line files, else the previous session.
    std::vector<std::wstring> sessionFiles = restoreSession && files.empty() && !bench ? settings.GetList(L"session.file.")
                                                                                         : std::vector<std::wstring>();
    if (!sessionFiles.empty()) {
        auto carets = settings.GetList(L"session.caret.");
        auto tops = settings.GetList(L"session.top.");
        for (size_t i = 0; i < sessionFiles.size(); i++) {
            if (GetFileAttributesW(sessionFiles[i].c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            auto d = std::make_unique<Doc>();
            d->path = sessionFiles[i];
            d->loaded = false;
            if (i < carets.size()) d->anchor = d->caret = _wtoi64(carets[i].c_str());
            if (i < tops.size()) d->firstLine = _wtoi64(tops[i].c_str());
            docs.push_back(std::move(d));
        }
        if (docs.size() > 1) {
            int want = std::clamp(settings.GetInt(L"session.active", 0) + 1, 1, (int)docs.size() - 1);
            ActivateTab(want);
            RemoveDoc(0); // the initial empty document
        }
    }
    bool maximized = settings.GetInt(L"window.max", 0) != 0;
    ShowWindow(hwnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    if (!files.empty()) OpenFiles(files, L"", gotoLine);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);

    if (bench) {
        FILETIME created, exitT, kernelT, userT, now;
        GetProcessTimes(GetCurrentProcess(), &created, &exitT, &kernelT, &userT);
        GetSystemTimePreciseAsFileTime(&now);
        ULARGE_INTEGER a{{created.dwLowDateTime, created.dwHighDateTime}}, b{{now.dwLowDateTime, now.dwHighDateTime}};
        benchMs = (int)((b.QuadPart - a.QuadPart) / 10000);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    ACCEL acc[] = {
        {FCONTROL | FVIRTKEY, 'N', CMD_NEW},
        {FCONTROL | FVIRTKEY, 'O', CMD_OPEN},
        {FCONTROL | FVIRTKEY, 'S', CMD_SAVE},
        {FCONTROL | FALT | FVIRTKEY, 'S', CMD_SAVEAS},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', CMD_SAVEALL},
        {FCONTROL | FVIRTKEY, 'W', CMD_CLOSE},
        {FCONTROL | FVIRTKEY, VK_F4, CMD_CLOSE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'T', CMD_REOPENCLOSED},
        {FCONTROL | FVIRTKEY, 'F', CMD_FIND},
        {FCONTROL | FVIRTKEY, 'H', CMD_REPLACE},
        {FVIRTKEY, VK_F3, CMD_FINDNEXT},
        {FSHIFT | FVIRTKEY, VK_F3, CMD_FINDPREV},
        {FCONTROL | FVIRTKEY, VK_F3, CMD_FINDSELNEXT},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_F3, CMD_FINDSELPREV},
        {FCONTROL | FVIRTKEY, 'G', CMD_GOTO},
        {FCONTROL | FVIRTKEY, 'B', CMD_BRACE},
        {FALT | FVIRTKEY, 'C', CMD_COLUMNEDITOR},
        {FCONTROL | FVIRTKEY, VK_F2, CMD_BM_TOGGLE},
        {FVIRTKEY, VK_F2, CMD_BM_NEXT},
        {FSHIFT | FVIRTKEY, VK_F2, CMD_BM_PREV},
        {FCONTROL | FVIRTKEY, 'J', CMD_JOIN},
        {FCONTROL | FSHIFT | FVIRTKEY, 'D', CMD_SELECTNEXT},
        {FALT | FVIRTKEY, VK_F3, CMD_SELECTALLOCC},
        {FALT | FVIRTKEY, 'Z', CMD_WRAP},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, CMD_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_ADD, CMD_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, CMD_ZOOMOUT},
        {FCONTROL | FVIRTKEY, VK_SUBTRACT, CMD_ZOOMOUT},
        {FCONTROL | FVIRTKEY, '0', CMD_ZOOMRESET},
        {FCONTROL | FVIRTKEY, VK_NUMPAD0, CMD_ZOOMRESET},
        {FCONTROL | FVIRTKEY, VK_TAB, CMD_NEXTTAB},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, CMD_PREVTAB},
        {FCONTROL | FVIRTKEY, VK_NEXT, CMD_NEXTTAB},
        {FCONTROL | FVIRTKEY, VK_PRIOR, CMD_PREVTAB},
        {FVIRTKEY, VK_F5, CMD_INSERTDATE},
        {FALT | FVIRTKEY, '0', CMD_FOLDALL},
        {FALT | FSHIFT | FVIRTKEY, '0', CMD_UNFOLDALL},
        {FCONTROL | FALT | FVIRTKEY, 'F', CMD_FOLDTOGGLE},
        {FCONTROL | FVIRTKEY, VK_OEM_COMMA, CMD_SETTINGS},
    };
    accel = CreateAcceleratorTableW(acc, (int)std::size(acc));
    return true;
}

void App::OnCreate() {
    dpi = GetDpiForWindow(hwnd);
    fonts.Create(dpi);
    ApplyWindowTheme(hwnd);

    HWND s = CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0,
                             hwnd, nullptr, inst, nullptr);
    sci.hwnd = s;
    sci.fn = (SciFnDirect)SendMessageW(s, SCI_GETDIRECTFUNCTION, 0, 0);
    sci.ptr = (sptr_t)SendMessageW(s, SCI_GETDIRECTPOINTER, 0, 0);
    ConfigureEditor(sci, view, dpi);
    ApplyEditorTheme(sci, view);
    sci(SCI_SETZOOM, settings.GetInt(L"view.zoom", 0));
    ApplyControlTheme(s);

    tabs.Create(hwnd);
    status.Create(hwnd);
    panel.Create(hwnd);
    panel.LoadState();

    // The view's initial document becomes the first (untitled) tab.
    auto d = std::make_unique<Doc>();
    d->sciDoc = (void*)sci(SCI_GETDOCPOINTER);
    sci(SCI_ADDREFDOCUMENT, 0, (sptr_t)d->sciDoc);
    d->untitledName = L"new 1";
    docs.push_back(std::move(d));
    active = 0;
    ApplyDocSettings(sci, Cur(), view);

    InstallDropTarget(s, true);
    InstallDropTarget(tabs.hwnd, false);
    Layout();
    UpdateTitle();
    status.Update();
}

void App::Shutdown() {
    for (auto& d : docs)
        if (d->sciDoc) sci(SCI_RELEASEDOCUMENT, 0, (sptr_t)d->sciDoc);
    docs.clear();
}

// ---------------------------------------------------------------------------
void App::Layout() {
    if (!hwnd || !sci.hwnd) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    int th = tabs.Height(), sh = status.Height(), ph = panel.Height();
    int eh = std::max(0, h - th - sh - ph);
    HDWP dwp = BeginDeferWindowPos(4);
    dwp = DeferWindowPos(dwp, tabs.hwnd, nullptr, 0, 0, w, th, SWP_NOZORDER | SWP_NOACTIVATE);
    dwp = DeferWindowPos(dwp, sci.hwnd, nullptr, 0, th, w, eh, SWP_NOZORDER | SWP_NOACTIVATE);
    if (ph) dwp = DeferWindowPos(dwp, panel.hwnd, nullptr, 0, th + eh, w, ph, SWP_NOZORDER | SWP_NOACTIVATE);
    dwp = DeferWindowPos(dwp, status.hwnd, nullptr, 0, h - sh, w, sh, SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
}

void App::UpdateTitle() {
    if (active < 0) return;
    const Doc& d = Cur();
    std::wstring t = IsModified(active) ? L"● " : L"";
    t += d.path.empty() ? d.untitledName : d.path;
    t += L" - Yan";
    SetWindowTextW(hwnd, t.c_str());
}

bool App::IsModified(int i) {
    if (i < 0 || i >= (int)docs.size()) return false;
    const Doc& d = *docs[i];
    if (d.encodingChanged) return true;
    return i == active ? sci(SCI_GETMODIFY) != 0 : d.modified;
}

std::wstring App::NextUntitledName() {
    for (int n = 1;; n++) {
        std::wstring name = L"new " + std::to_wstring(n);
        bool used = std::any_of(docs.begin(), docs.end(), [&](auto& d) { return d->path.empty() && d->untitledName == name; });
        if (!used) return name;
    }
}

void App::DetachDoc() {
    if (active < 0) return;
    Doc& d = Cur();
    d.modified = sci(SCI_GETMODIFY) != 0;
    d.anchor = (Sci_Position)sci(SCI_GETANCHOR);
    d.caret = sci.Pos();
    d.firstLine = (Sci_Position)sci(SCI_GETFIRSTVISIBLELINE);
    d.xOffset = (Sci_Position)sci(SCI_GETXOFFSET);
    d.folded.clear();
    for (Sci_Position l = (Sci_Position)sci(SCI_CONTRACTEDFOLDNEXT, 0); l >= 0;
         l = (Sci_Position)sci(SCI_CONTRACTEDFOLDNEXT, l + 1))
        d.folded.push_back(l);
}

void App::AttachDoc(int i) {
    Doc& d = *docs[i];
    if (!d.loaded) {
        LoadResult r = LoadFile(LoaderSci(), d.path);
        d.loaded = true;
        if (r.ok) {
            d.sciDoc = r.sciDoc;
            d.encoding = r.encoding;
            d.eol = r.eol;
            d.mtime = r.mtime;
        } else {
            d.missing = true;
        }
    }
    if (!d.sciDoc) d.sciDoc = (void*)sci(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    sci(SCI_SETDOCPOINTER, 0, (sptr_t)d.sciDoc);
    ApplyDocSettings(sci, d, view);
    ApplyLanguage();
    // Fold state lives in the view, so it is lost when the document is swapped out: restore it.
    if (!d.folded.empty() && sci(SCI_GETMARGINWIDTHN, MARGIN_FOLD) > 0) {
        sci(SCI_COLOURISE, 0, sci.LineEnd(d.folded.back())); // fold levels are computed by the lexer
        for (Sci_Position l : d.folded)
            if (sci(SCI_GETFOLDLEVEL, l) & SC_FOLDLEVELHEADERFLAG) sci(SCI_FOLDLINE, l, SC_FOLDACTION_CONTRACT);
    }
    Sci_Position len = sci.Length();
    sci(SCI_SETSEL, std::min(d.anchor, len), std::min(d.caret, len));
    sci(SCI_SETFIRSTVISIBLELINE, d.firstLine);
    sci(SCI_SETXOFFSET, d.xOffset);
}

void App::ActivateTab(int i) {
    if (i < 0 || i >= (int)docs.size() || i == active) return;
    if (active >= 0) {
        panel.LeavingDocument();
        DetachDoc();
    }
    active = i;
    AttachDoc(i);
    UpdateLineNumberWidth(sci, view);
    tabs.Refresh();
    UpdateTitle();
    status.Update();
    panel.OnDocumentSwitched();
    if (GetFocus() != panel.hwnd && (!GetFocus() || GetParent(GetFocus()) != panel.hwnd)) SetFocus(sci.hwnd);
}

void App::NewTab() {
    auto d = std::make_unique<Doc>();
    d->sciDoc = (void*)sci(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    d->untitledName = NextUntitledName();
    d->eol = newEol;
    d->encoding = newEncoding;
    docs.push_back(std::move(d));
    ActivateTab((int)docs.size() - 1);
    SetFocus(sci.hwnd);
}

void App::RemoveDoc(int i) {
    if (i == active) return;
    if (docs[i]->sciDoc) sci(SCI_RELEASEDOCUMENT, 0, (sptr_t)docs[i]->sciDoc);
    docs.erase(docs.begin() + i);
    if (active > i) active--;
    tabs.Refresh();
}

App::BatchClose::BatchClose(App& a, int keep) : app(a) {
    app.batch_ = true;
    app.batchKeep_ = keep;
    app.batchAnswer_ = BatchAsk;
}

App::BatchClose::~BatchClose() {
    app.batch_ = false;
    app.batchKeep_ = -1;
    app.batchAnswer_ = BatchAsk;
}

bool App::ConfirmClose(int i) {
    if (!IsModified(i)) return true;
    if (batch_ && batchAnswer_ == BatchDiscardAll) return true;
    if (batch_ && batchAnswer_ == BatchSaveAll) return Save(i);
    ActivateTab(i);
    std::wstring msg = L"Save changes to “" + Cur().Title() + L"”?";
    int others = 0;
    if (batch_)
        for (int j = 0; j < (int)docs.size(); j++)
            if (j != i && j != batchKeep_ && IsModified(j)) others++;
    if (!others) {
        int r = MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return false;
        if (r == IDYES) return Save(i);
        return true;
    }

    // Several unsaved documents are being closed: offer to answer for all of them at once.
    enum { ID_SAVE = 100, ID_DISCARD, ID_SAVE_ALL, ID_DISCARD_ALL };
    TASKDIALOG_BUTTON buttons[] = {
        {ID_SAVE, L"&Save"},
        {ID_DISCARD, L"Do&n't save"},
        {ID_SAVE_ALL, L"Save &all"},
        {ID_DISCARD_ALL, L"Don't save a&ll"},
    };
    std::wstring detail = std::to_wstring(others) + (others == 1 ? L" other document also has" : L" other documents also have") +
                          L" unsaved changes.";
    TASKDIALOGCONFIG tdc{sizeof(tdc)};
    tdc.hwndParent = hwnd;
    tdc.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    tdc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tdc.pszWindowTitle = L"Yan";
    tdc.pszMainIcon = TD_WARNING_ICON;
    tdc.pszMainInstruction = msg.c_str();
    tdc.pszContent = detail.c_str();
    tdc.cButtons = (UINT)std::size(buttons);
    tdc.pButtons = buttons;
    tdc.nDefaultButton = ID_SAVE;
    int r = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&tdc, &r, nullptr, nullptr))) r = IDCANCEL;
    switch (r) {
    case ID_SAVE: return Save(i);
    case ID_DISCARD: return true;
    case ID_SAVE_ALL: batchAnswer_ = BatchSaveAll; return Save(i);
    case ID_DISCARD_ALL: batchAnswer_ = BatchDiscardAll; return true;
    default: return false;
    }
}

bool App::CloseTab(int i) {
    if (i < 0 || i >= (int)docs.size()) return false;
    if (!ConfirmClose(i)) return false;
    if (!docs[i]->path.empty()) {
        std::erase(closedPaths, docs[i]->path);
        closedPaths.push_back(docs[i]->path);
    }
    if (docs.size() == 1) {
        bool blank = docs[0]->path.empty() && !IsModified(0) && sci.Length() == 0;
        if (exitOnLastTab) {
            // Leave an empty tab behind so closing the window does not ask about this document again.
            if (!blank) {
                NewTab();
                RemoveDoc(0);
            }
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return true;
        }
        if (blank) return true;
        NewTab();
    }
    if (i == active) ActivateTab(i + 1 < (int)docs.size() ? i + 1 : i - 1);
    RemoveDoc(i);
    UpdateTitle();
    return true;
}

void App::MoveTab(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= (int)docs.size() || to >= (int)docs.size()) return;
    auto d = std::move(docs[from]);
    docs.erase(docs.begin() + from);
    docs.insert(docs.begin() + to, std::move(d));
    if (active == from) active = to;
    else if (from < active && to >= active) active--;
    else if (from > active && to <= active) active++;
    tabs.Refresh();
}

int App::FindDoc(const std::wstring& path) const {
    for (size_t i = 0; i < docs.size(); i++)
        if (!docs[i]->path.empty() && _wcsicmp(docs[i]->path.c_str(), path.c_str()) == 0) return (int)i;
    return -1;
}

bool App::OpenFile(const std::wstring& path, bool activate, int forceEncoding) {
    int existing = FindDoc(path);
    if (existing >= 0) {
        if (activate) ActivateTab(existing);
        return true;
    }
    LoadResult r = LoadFile(LoaderSci(), path, forceEncoding);
    if (!r.ok) {
        std::wstring msg = L"Cannot open “" + path + L"”\n\n" + r.error;
        MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_OK | MB_ICONERROR);
        return false;
    }
    // Replace the lone empty untitled tab, like Notepad++.
    bool replaceEmpty = docs.size() == 1 && docs[0]->path.empty() && !IsModified(0) && active == 0 && sci.Length() == 0;
    auto d = std::make_unique<Doc>();
    d->sciDoc = r.sciDoc;
    d->path = path;
    d->encoding = r.encoding;
    d->eol = r.eol;
    d->mtime = r.mtime;
    int index = active >= 0 ? active + 1 : (int)docs.size();
    if (index > (int)docs.size()) index = (int)docs.size();
    docs.insert(docs.begin() + index, std::move(d));
    if (active >= index) active++;
    std::erase(closedPaths, path);
    AddRecent(path);
    if (activate || replaceEmpty) ActivateTab(index);
    if (replaceEmpty) RemoveDoc(0);
    tabs.Refresh();
    return true;
}

void App::OpenFiles(const std::vector<std::wstring>& files, const std::wstring& cwd, int gotoLine) {
    for (size_t i = 0; i < files.size(); i++) {
        std::wstring p = FullPath(files[i], cwd);
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (attr == INVALID_FILE_ATTRIBUTES) {
            std::wstring msg = L"“" + p + L"” does not exist.\n\nCreate it?";
            if (MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_YESNO | MB_ICONQUESTION) != IDYES) continue;
            NewTab();
            Cur().path = p;
            Cur().langDetected = false;
            ApplyLanguage();
            tabs.Refresh();
            UpdateTitle();
            continue;
        }
        OpenFile(p, true);
    }
    if (gotoLine > 0) {
        Sci_Position line = std::min<Sci_Position>(gotoLine - 1, sci.LineCount() - 1);
        sci(SCI_GOTOLINE, line);
        CenterLine(sci, line);
    }
}

const Sci& App::LoaderSci() {
    if (!worker_.hwnd) {
        worker_.hwnd = CreateWindowExW(0, L"Scintilla", nullptr, WS_CHILD, 0, 0, 0, 0, hwnd, nullptr, inst, nullptr);
        worker_.fn = (SciFnDirect)SendMessageW(worker_.hwnd, SCI_GETDIRECTFUNCTION, 0, 0);
        worker_.ptr = (sptr_t)SendMessageW(worker_.hwnd, SCI_GETDIRECTPOINTER, 0, 0);
        worker_(SCI_SETCODEPAGE, SC_CP_UTF8);
    }
    return worker_;
}

const Sci& App::SciFor(int i) {
    if (i == active) return sci;
    LoaderSci();
    worker_(SCI_SETDOCPOINTER, 0, (sptr_t)docs[i]->sciDoc);
    return worker_;
}

void App::ReleaseWorker() {
    if (worker_.hwnd) worker_(SCI_SETDOCPOINTER, 0, 0);
}

std::vector<std::wstring> App::FileDialog(bool save, const std::wstring& name, const std::wstring& dir) {
    std::vector<std::wstring> out;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return out;
    COMDLG_FILTERSPEC filters[] = {{L"All files (*.*)", L"*.*"}, {L"Text files (*.txt)", L"*.txt"}};
    dlg->SetFileTypes(2, filters);
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    opts |= save ? FOS_OVERWRITEPROMPT : (FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST);
    dlg->SetOptions(opts);
    if (!name.empty()) dlg->SetFileName(name.c_str());
    if (!dir.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(hwnd))) {
        if (save) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    out.push_back(p);
                    CoTaskMemFree(p);
                }
                item->Release();
            }
        } else {
            // GetResults is only on IFileOpenDialog: ask for that interface, a cast is not enough.
            IFileOpenDialog* open = nullptr;
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&open)))) {
                if (FAILED(open->GetResults(&items))) items = nullptr;
                open->Release();
            }
            if (items) {
                DWORD n = 0;
                items->GetCount(&n);
                for (DWORD k = 0; k < n; k++) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(k, &item))) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                            out.push_back(p);
                            CoTaskMemFree(p);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
        }
    }
    dlg->Release();
    return out;
}

bool App::Save(int i, bool saveAs) {
    Doc& d = *docs[i];
    if (!d.loaded) return true;
    std::wstring path = d.path;
    if (saveAs || path.empty()) {
        std::wstring dir = d.path.empty() ? (active >= 0 ? DirOf(Cur().path) : L"") : DirOf(d.path);
        auto r = FileDialog(true, d.path.empty() ? d.untitledName + L".txt" : FileNameOf(d.path), dir);
        if (r.empty()) return false;
        path = r[0];
        int other = FindDoc(path);
        if (other >= 0 && other != i) {
            MessageBoxW(hwnd, L"That file is already open in another tab.", L"Yan", MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    return SaveTo(i, path);
}

bool App::SaveTo(int i, const std::wstring& path) {
    Doc& d = *docs[i];
    const Sci& s = SciFor(i);
    if (view.trimOnSave) TrimTrailingWhitespace(s);
    std::wstring err;
    bool ok = SaveFile(s, d, path, err, hwnd);
    if (ok) {
        s(SCI_SETSAVEPOINT);
        if (path != d.path) {
            d.langDetected = false; // "Save as" another type
            AddRecent(path);
        }
        d.path = path;
        d.missing = false;
        d.modified = false;
        d.encodingChanged = false;
    }
    if (&s == &worker_) ReleaseWorker();
    else if (ok) ApplyLanguage();
    if (!ok) {
        if (!err.empty()) {
            std::wstring msg = L"Cannot save “" + path + L"”\n\n" + err;
            MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_OK | MB_ICONERROR);
        }
        return false;
    }
    tabs.Refresh();
    UpdateTitle();
    status.Update();
    return true;
}

bool App::SaveAll() {
    for (int i = 0; i < (int)docs.size(); i++)
        if (IsModified(i) && !Save(i)) return false;
    return true;
}

void App::SaveAllToFolder() {
    std::wstring dir = FolderDialog(saveAllDir.empty() && active >= 0 ? DirOf(Cur().path) : saveAllDir);
    if (dir.empty()) return;
    saveAllDir = dir;
    // No confirmation: files with a path are saved in place, untitled ones go to the folder as
    // "<tab name>.txt" ("new 1.txt"), with " (2)" added when that name is already taken.
    std::vector<std::wstring> failed;
    for (int i = 0; i < (int)docs.size(); i++) {
        Doc& d = *docs[i];
        if (!d.loaded) continue;
        if (!d.path.empty()) {
            if (IsModified(i) && !SaveTo(i, d.path)) failed.push_back(d.path);
            continue;
        }
        const Sci& s = SciFor(i);
        bool empty = s.Length() == 0;
        if (&s == &worker_) ReleaseWorker();
        if (empty && !IsModified(i)) continue;
        std::wstring base = dir + (dir.back() == L'\\' ? L"" : L"\\") + d.untitledName, path = base + L".txt";
        for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES || FindDoc(path) >= 0; n++)
            path = base + L" (" + std::to_wstring(n) + L").txt";
        if (!SaveTo(i, path)) failed.push_back(path);
    }
    if (!failed.empty()) {
        std::wstring msg = L"These files could not be saved:\n";
        for (auto& f : failed) msg += L"\n" + f;
        MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_OK | MB_ICONERROR);
    }
}

std::wstring App::FolderDialog(const std::wstring& dir, HWND owner) {
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Save all documents to folder");
    dlg->SetOkButtonLabel(L"Save all here");
    if (!dir.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(owner ? owner : hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    if (out.size() > 3 && out.back() == L'\\') out.pop_back();
    return out;
}

void App::Reload(int i, int forceEncoding) {
    Doc& d = *docs[i];
    if (d.path.empty()) return;
    if (IsModified(i)) {
        ActivateTab(i);
        std::wstring msg = L"Reload “" + d.Title() + L"” and lose your changes?";
        if (MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    LoadResult r = LoadFile(LoaderSci(), d.path, forceEncoding);
    if (!r.ok) {
        std::wstring msg = L"Cannot reload “" + d.path + L"”\n\n" + r.error;
        MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_OK | MB_ICONERROR);
        return;
    }
    void* old = d.sciDoc;
    d.sciDoc = r.sciDoc;
    d.encoding = r.encoding;
    d.eol = r.eol;
    d.mtime = r.mtime;
    d.missing = false;
    d.modified = false;
    d.encodingChanged = false;
    d.lexer = -1; // a new Scintilla document
    d.folded.clear();
    d.langDetected = false;
    if (i == active) {
        Sci_Position caret = sci.Pos(), top = (Sci_Position)sci(SCI_GETFIRSTVISIBLELINE);
        panel.LeavingDocument();
        sci(SCI_SETDOCPOINTER, 0, (sptr_t)d.sciDoc);
        ApplyDocSettings(sci, d, view);
        ApplyLanguage();
        sci(SCI_GOTOPOS, std::min(caret, sci.Length()));
        sci(SCI_SETFIRSTVISIBLELINE, top);
        UpdateLineNumberWidth(sci, view);
        panel.OnDocumentSwitched();
    }
    if (old) sci(SCI_RELEASEDOCUMENT, 0, (sptr_t)old);
    tabs.Refresh();
    UpdateTitle();
    status.Update();
}

void App::CheckExternalChanges() {
    if (checkingFiles_ || !checkChanges) return;
    checkingFiles_ = true;
    for (int i = 0; i < (int)docs.size(); i++) {
        Doc& d = *docs[i];
        if (d.path.empty() || !d.loaded) continue;
        FILETIME ft;
        if (!GetFileTime(d.path, ft)) {
            if (!d.missing) {
                d.missing = true;
                tabs.Refresh();
            }
            continue;
        }
        if (d.missing) {
            d.missing = false;
            tabs.Refresh();
        }
        if (CompareFileTime(&ft, &d.mtime) == 0) continue;
        d.mtime = ft;
        if (autoReload && !IsModified(i)) {
            Reload(i);
            continue;
        }
        ActivateTab(i);
        std::wstring msg = L"“" + d.path + L"” was changed by another program.\n\nReload it?";
        if (IsModified(i)) msg += L"\n\nYour unsaved changes in Yan will be lost.";
        if (MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            d.modified = false;
            d.encodingChanged = false;
            sci(SCI_SETSAVEPOINT);
            Reload(i);
        }
    }
    checkingFiles_ = false;
}

void App::SaveSession() {
    std::vector<std::wstring> files, carets, tops;
    int activeIndex = 0;
    for (int i = 0; i < (int)docs.size(); i++) {
        Doc& d = *docs[i];
        if (d.path.empty()) continue;
        if (i == active) {
            activeIndex = (int)files.size();
            DetachDoc();
        }
        files.push_back(d.path);
        carets.push_back(std::to_wstring(d.caret));
        tops.push_back(std::to_wstring(d.firstLine));
    }
    settings.SetList(L"session.file.", files);
    settings.SetList(L"session.caret.", carets);
    settings.SetList(L"session.top.", tops);
    settings.SetInt(L"session.active", activeIndex);
}

// ---------------------------------------------------------------------------
void App::SetEol(int mode) {
    sci(SCI_CONVERTEOLS, mode);
    sci(SCI_SETEOLMODE, mode);
    Cur().eol = mode;
    status.Update();
}

void App::SetThemeMode(ThemeMode m) {
    themeMode = m;
    RefreshTheme();
}

void App::RefreshTheme() {
    BuildTheme(themeMode == ThemeMode::System ? SystemUsesDarkTheme() : themeMode == ThemeMode::Dark);
    ApplyWindowTheme(hwnd);
    ApplyControlTheme(sci.hwnd);
    ApplyEditorTheme(sci, view, styledLang_);
    panel.ThemeChanged();
    DrawMenuBar(hwnd);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void App::OnDpiChanged(UINT newDpi) {
    dpi = newDpi;
    fonts.Create(dpi);
    ConfigureEditor(sci, view, dpi);
    ApplyEditorTheme(sci, view, styledLang_);
    UpdateFoldMargin(sci, view, LangFolds(styledLang_), dpi);
    panel.ThemeChanged();
    Layout();
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void App::ApplyViewOptions() {
    sci(SCI_SETTECHNOLOGY, view.directWrite ? view.technology : SC_TECHNOLOGY_DEFAULT);
    ApplyEditorTheme(sci, view, styledLang_);
    if (active >= 0) {
        ApplyDocSettings(sci, Cur(), view); // other documents get theirs when shown
        ApplyLanguage();
    }
    if (!view.braceMatch) sci(SCI_BRACEHIGHLIGHT, -1, -1);
    if (!view.smartHighlight) ClearIndicator(sci, IND_SMART);
    status.Update();
    SaveViewSettings();
    settings.Save();
}

bool App::ChooseEditorFont(HWND owner) {
    LOGFONTW lf{};
    HDC dc = GetDC(hwnd);
    lf.lfHeight = -MulDiv(view.fontSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(hwnd, dc);
    wcsncpy_s(lf.lfFaceName, view.font.c_str(), _TRUNCATE);
    CHOOSEFONTW cf{sizeof(cf)};
    cf.hwndOwner = owner ? owner : hwnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_NOSCRIPTSEL;
    if (!ChooseFontW(&cf)) return false;
    view.font = lf.lfFaceName;
    view.fontSize = std::max(5, cf.iPointSize / 10);
    ApplyEditorTheme(sci, view, styledLang_);
    status.Update();
    return true;
}

void App::ShowTabMenu(int i, POINT pt) {
    HMENU m = CreatePopupMenu();
    AddItem(m, CMD_CLOSE, L"&Close\tCtrl+W");
    AddItem(m, CMD_CLOSEOTHERS, L"Close &others");
    AddItem(m, CMD_CLOSEALL, L"Close &all");
    AddSep(m);
    AddItem(m, CMD_SAVE, L"&Save\tCtrl+S");
    AddItem(m, CMD_SAVEAS, L"Save &as...");
    AddItem(m, CMD_RELOAD, L"&Reload from disk");
    AddSep(m);
    AddItem(m, CMD_COPYPATH, L"Copy full &path");
    AddItem(m, CMD_COPYNAME, L"Copy file &name");
    AddItem(m, CMD_OPENFOLDER, L"Open containing &folder");
    bool hasPath = !docs[i]->path.empty();
    for (int id : {CMD_RELOAD, CMD_COPYPATH, CMD_OPENFOLDER})
        EnableMenuItem(m, id, MF_BYCOMMAND | (hasPath ? MF_ENABLED : MF_GRAYED));
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(m);
    if (!cmd) return;
    ActivateTab(i);
    OnCommand(cmd);
}

void App::ShowEncodingMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    for (int e = 0; e <= (int)Encoding::Ansi; e++) {
        std::wstring label = std::wstring(L"Convert to ") + EncodingName((Encoding)e);
        AddItem(m, CMD_ENC_TO + e, label.c_str());
    }
    AddSep(m);
    HMENU reopen = CreatePopupMenu();
    for (int e = 0; e <= (int)Encoding::Ansi; e++) AddItem(reopen, CMD_ENC_RELOAD + e, EncodingName((Encoding)e));
    AddSub(m, reopen, L"Reopen with encoding");
    UpdateMenuChecks(m);
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) OnCommand(cmd);
}

void App::ShowLanguageMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    AddLanguageItems(m);
    UpdateMenuChecks(m);
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) OnCommand(cmd);
}

Lang App::CurLang() {
    if (active < 0 || !view.syntax || (sci(SCI_GETDOCUMENTOPTIONS) & SC_DOCUMENTOPTION_STYLES_NONE)) return Lang::Text;
    return Cur().lang;
}

void App::ApplyLanguage() {
    Doc& d = Cur();
    if (!d.langChosen && !d.langDetected) {
        d.langDetected = true;
        bool known = false;
        d.lang = LangFromPath(d.path, &known);
        if (!known && !d.path.empty()) {
            Sci_Position n = std::min<Sci_Position>(sci.Length(), 256);
            if (n > 0) d.lang = LangFromContent(sci.Range(0, n));
        }
    }
    // The lexer belongs to the Scintilla document; it is created the first time the document is shown.
    Lang l = CurLang();
    if (d.lexer != (int)l) {
        if (d.lexer >= 0 || l != Lang::Text) SetDocumentLexer(sci, l);
        d.lexer = (int)l;
    }
    // Style colors belong to the view.
    if (styledLang_ != l) {
        styledLang_ = l;
        ApplyEditorStyles(sci, view, l);
    }
    UpdateFoldMargin(sci, view, LangFolds(l), dpi);
}

void App::AddRecent(const std::wstring& path) {
    std::erase_if(recent, [&](const std::wstring& p) { return _wcsicmp(p.c_str(), path.c_str()) == 0; });
    recent.insert(recent.begin(), path);
    if ((int)recent.size() > recentMax) recent.resize(recentMax);
}

void App::BuildRecentMenu() {
    while (GetMenuItemCount(recentMenu_) > 0) DeleteMenu(recentMenu_, 0, MF_BYPOSITION);
    for (int i = 0; i < (int)recent.size() && i <= CMD_RECENT_LAST - CMD_RECENT; i++) {
        // "&1 C:\path" ... "1&0", then no mnemonic; double the & a path may contain.
        std::wstring label = i < 9 ? L"&" + std::to_wstring(i + 1) : i == 9 ? L"1&0" : std::to_wstring(i + 1);
        label += L"  ";
        for (wchar_t c : recent[i]) label += c == L'&' ? std::wstring(L"&&") : std::wstring(1, c);
        AddItem(recentMenu_, CMD_RECENT + i, label.c_str());
    }
    if (recent.empty()) {
        AppendMenuW(recentMenu_, MF_STRING | MF_GRAYED, 0, L"(no recent files)");
        return;
    }
    AddSep(recentMenu_);
    AddItem(recentMenu_, CMD_RECENT_OPENALL, L"&Open all");
    AddItem(recentMenu_, CMD_RECENT_CLEAR, L"&Clear the list");
}

void App::OpenRecent(int index) {
    if (index < 0 || index >= (int)recent.size()) return;
    std::wstring path = recent[index];
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring msg = L"“" + path + L"” no longer exists.\n\nRemove it from the recent files?";
        if (MessageBoxW(hwnd, msg.c_str(), L"Yan", MB_YESNO | MB_ICONQUESTION) == IDYES) recent.erase(recent.begin() + index);
        return;
    }
    OpenFile(path);
}

void App::SetLanguage(int cmd) {
    Doc& d = Cur();
    d.langChosen = cmd != CMD_LANG_AUTO;
    d.langDetected = false;
    if (d.langChosen) d.lang = (Lang)(cmd - CMD_LANG);
    ApplyLanguage();
}

void App::ShowEolMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    AddItem(m, CMD_EOL_CRLF, L"Windows (CRLF)");
    AddItem(m, CMD_EOL_LF, L"Unix (LF)");
    AddItem(m, CMD_EOL_CR, L"Mac (CR)");
    UpdateMenuChecks(m);
    int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(m);
    if (cmd) OnCommand(cmd);
}

static const wchar_t* kShortcuts =
    L"Column selection\n"
    L"   Alt + drag\t\tSelect a rectangle\n"
    L"   Alt + Shift + arrows\tExtend a column selection\n"
    L"   Ctrl + click\t\tAdd a caret\n"
    L"   Ctrl + Shift + D\t\tAdd next occurrence\n"
    L"   Alt + F3\t\tSelect all occurrences\n"
    L"   Alt + C\t\tColumn editor (insert text / numbers)\n\n"
    L"Find and replace\n"
    L"   Ctrl + F / Ctrl + H\tFind / Replace\n"
    L"   F3 / Shift + F3\t\tNext / previous match\n"
    L"   Ctrl + F3\t\tFind the selected text\n"
    L"   Enter / Shift + Enter\tNext / previous (in the find box)\n"
    L"   Alt + Enter\t\tSelect all matches as carets\n"
    L"   Ctrl + Alt + Enter\tReplace all (in the replace box)\n"
    L"   Alt + C / W / R / E / L\tCase, whole word, regex, extended, in selection\n"
    L"   Up / Down\t\tSearch history\n\n"
    L"Lines and navigation\n"
    L"   Ctrl + D\t\tDuplicate line\n"
    L"   Ctrl + Shift + L\t\tDelete line\n"
    L"   Ctrl + Shift + Up / Down\tMove lines\n"
    L"   Ctrl + J\t\tJoin lines\n"
    L"   Ctrl + G\t\tGo to line\n"
    L"   Ctrl + F2 / F2\t\tToggle / next bookmark\n"
    L"   Ctrl + Tab\t\tNext tab\n"
    L"   Alt + Z\t\tWord wrap\n";

void App::OnCommand(int id) {
    if (active < 0) return;
    switch (id) {
    case CMD_NEW: NewTab(); break;
    case CMD_OPEN: {
        auto files = FileDialog(false, L"", DirOf(Cur().path));
        OpenFiles(files, L"", 0);
        break;
    }
    case CMD_RELOAD: Reload(active); break;
    case CMD_SAVE: Save(active); break;
    case CMD_SAVEAS: Save(active, true); break;
    case CMD_SAVEALL: SaveAll(); break;
    case CMD_SAVEALLTO: SaveAllToFolder(); break;
    case CMD_SETTINGS: ShowSettings(); break;
    case CMD_CLOSE: CloseTab(active); break;
    case CMD_CLOSEOTHERS: {
        BatchClose batch(*this, active);
        for (int i = (int)docs.size() - 1; i >= 0; i--) {
            if (i == active) continue;
            if (!CloseTab(i)) break;
        }
        break;
    }
    case CMD_CLOSEALL: {
        BatchClose batch(*this);
        for (int i = (int)docs.size() - 1; i >= 0; i--)
            if (!CloseTab(i)) break;
        break;
    }
    case CMD_REOPENCLOSED:
        while (!closedPaths.empty()) {
            std::wstring p = closedPaths.back();
            closedPaths.pop_back();
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                OpenFile(p);
                break;
            }
        }
        break;
    case CMD_OPENFOLDER:
        if (!Cur().path.empty()) {
            std::wstring args = L"/select,\"" + Cur().path + L"\"";
            ShellExecuteW(hwnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case CMD_COPYPATH: CopyToClipboard(hwnd, Cur().path); break;
    case CMD_COPYNAME: CopyToClipboard(hwnd, Cur().Title()); break;
    case CMD_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;

    case CMD_UNDO: sci(SCI_UNDO); break;
    case CMD_REDO: sci(SCI_REDO); break;
    case CMD_CUT: sci(SCI_CUT); break;
    case CMD_COPY: sci(SCI_COPY); break;
    case CMD_PASTE: sci(SCI_PASTE); break;
    case CMD_DELETE: sci(SCI_CLEAR); break;
    case CMD_SELECTALL: sci(SCI_SELECTALL); break;
    case CMD_COLUMNEDITOR: panel.Show(PanelPage::Column); break;
    case CMD_SELECTNEXT: AddNextOccurrence(sci); break;
    case CMD_SELECTALLOCC: SelectAllOccurrences(sci); break;
    case CMD_DUPLINE: sci(SCI_SELECTIONDUPLICATE); break;
    case CMD_DELLINE: sci(SCI_LINEDELETE); break;
    case CMD_MOVEUP: sci(SCI_MOVESELECTEDLINESUP); break;
    case CMD_MOVEDOWN: sci(SCI_MOVESELECTEDLINESDOWN); break;
    case CMD_UPPER: sci(SCI_UPPERCASE); break;
    case CMD_LOWER: sci(SCI_LOWERCASE); break;
    case CMD_JOIN: DoLineOp(sci, LineOp::Join); break;
    case CMD_TRIM: DoLineOp(sci, LineOp::TrimTrailing); break;
    case CMD_SORTASC: DoLineOp(sci, LineOp::SortAsc); break;
    case CMD_SORTDESC: DoLineOp(sci, LineOp::SortDesc); break;
    case CMD_SORTASCCI: DoLineOp(sci, LineOp::SortAscCI); break;
    case CMD_DEDUPE: DoLineOp(sci, LineOp::Dedupe); break;
    case CMD_REMOVEEMPTY: DoLineOp(sci, LineOp::RemoveEmpty); break;
    case CMD_REVERSE: DoLineOp(sci, LineOp::Reverse); break;
    case CMD_EOL_CRLF: SetEol(SC_EOL_CRLF); break;
    case CMD_EOL_LF: SetEol(SC_EOL_LF); break;
    case CMD_EOL_CR: SetEol(SC_EOL_CR); break;
    case CMD_INSERTDATE: {
        wchar_t t[64] = {}, d[64] = {};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, t, 64);
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, d, 64, nullptr);
        std::string s = Narrow(std::wstring(t) + L" " + d);
        sci.Str(SCI_REPLACESEL, 0, s.c_str());
        break;
    }

    case CMD_FIND: panel.Show(PanelPage::Find); break;
    case CMD_REPLACE: panel.Show(PanelPage::Replace); break;
    case CMD_FINDNEXT: panel.FindNext(true, true); break;
    case CMD_FINDPREV: panel.FindNext(false, true); break;
    case CMD_FINDSELNEXT: panel.FindSelection(true); break;
    case CMD_FINDSELPREV: panel.FindSelection(false); break;
    case CMD_CLEARMARKS: ClearIndicator(sci, IND_MARK); break;
    case CMD_GOTO: panel.Show(PanelPage::GoTo); break;
    case CMD_BRACE: {
        Sci_Position p = sci.Pos();
        Sci_Position m = (Sci_Position)sci(SCI_BRACEMATCH, p, 0);
        if (m < 0 && p > 0) m = (Sci_Position)sci(SCI_BRACEMATCH, p - 1, 0);
        if (m >= 0) sci(SCI_GOTOPOS, m);
        break;
    }
    case CMD_BM_TOGGLE: ToggleBookmark(sci, sci.Line(sci.Pos())); break;
    case CMD_BM_NEXT: NextBookmark(sci, true); break;
    case CMD_BM_PREV: NextBookmark(sci, false); break;
    case CMD_BM_CLEAR: DoBookmarkOp(sci, BookmarkOp::ClearAll, hwnd); break;
    case CMD_BM_COPY: DoBookmarkOp(sci, BookmarkOp::Copy, hwnd); break;
    case CMD_BM_CUT: DoBookmarkOp(sci, BookmarkOp::Cut, hwnd); break;
    case CMD_BM_REMOVE: DoBookmarkOp(sci, BookmarkOp::Remove, hwnd); break;
    case CMD_BM_REMOVEUNMARKED: DoBookmarkOp(sci, BookmarkOp::RemoveUnmarked, hwnd); break;
    case CMD_BM_INVERSE: DoBookmarkOp(sci, BookmarkOp::Inverse, hwnd); break;

    case CMD_WRAP: view.wrap = !view.wrap; sci(SCI_SETWRAPMODE, view.wrap ? SC_WRAP_WORD : SC_WRAP_NONE); break;
    case CMD_WHITESPACE: view.whitespace = !view.whitespace; sci(SCI_SETVIEWWS, view.whitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE); break;
    case CMD_EOLMARKS: view.eolMarks = !view.eolMarks; sci(SCI_SETVIEWEOL, view.eolMarks); break;
    case CMD_INDENTGUIDES: view.indentGuides = !view.indentGuides; sci(SCI_SETINDENTATIONGUIDES, view.indentGuides ? SC_IV_LOOKBOTH : SC_IV_NONE); break;
    case CMD_LINENUMBERS: view.lineNumbers = !view.lineNumbers; UpdateLineNumberWidth(sci, view); break;
    case CMD_ZOOMIN: sci(SCI_ZOOMIN); break;
    case CMD_ZOOMOUT: sci(SCI_ZOOMOUT); break;
    case CMD_ZOOMRESET: sci(SCI_SETZOOM, 0); break;
    case CMD_THEME_SYSTEM: SetThemeMode(ThemeMode::System); break;
    case CMD_THEME_LIGHT: SetThemeMode(ThemeMode::Light); break;
    case CMD_THEME_DARK: SetThemeMode(ThemeMode::Dark); break;
    case CMD_FONT: ChooseEditorFont(); break;
    case CMD_DIRECTWRITE:
        view.directWrite = !view.directWrite;
        sci(SCI_SETTECHNOLOGY, view.directWrite ? view.technology : SC_TECHNOLOGY_DEFAULT);
        break;
    case CMD_NEXTTAB: if (docs.size() > 1) ActivateTab((active + 1) % (int)docs.size()); break;
    case CMD_PREVTAB: if (docs.size() > 1) ActivateTab((active + (int)docs.size() - 1) % (int)docs.size()); break;
    case CMD_SINGLEINSTANCE: singleInstance = !singleInstance; break;
    case CMD_RESTORESESSION: restoreSession = !restoreSession; break;
    case CMD_FOLDING:
        view.folding = !view.folding;
        ApplyViewOptions();
        break;
    case CMD_FOLDALL: DoFold(sci, FoldOp::FoldAll); break;
    case CMD_UNFOLDALL: DoFold(sci, FoldOp::UnfoldAll); break;
    case CMD_FOLDTOGGLE: DoFold(sci, FoldOp::ToggleCurrent); break;
    case CMD_RECENT_CLEAR: recent.clear(); break;
    case CMD_RECENT_OPENALL: {
        std::vector<std::wstring> all(recent.rbegin(), recent.rend());
        for (auto& p : all)
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) OpenFile(p);
        break;
    }

    case CMD_ABOUT:
        MessageBoxW(hwnd,
                    L"Yan 1.0\n\nA fast text editor inspired by Notepad++.\n\n"
                    L"Built on Scintilla and Boost.Regex.\n"
                    L"Settings are stored in yan.ini (next to yan.exe for portable use, otherwise in %APPDATA%\\Yan).",
                    L"About Yan", MB_OK | MB_ICONINFORMATION);
        break;
    case CMD_SHORTCUTS: MessageBoxW(hwnd, kShortcuts, L"Keyboard shortcuts", MB_OK); break;

    default:
        if (id >= CMD_ENC_TO && id <= CMD_ENC_TO + (int)Encoding::Ansi) {
            Encoding e = (Encoding)(id - CMD_ENC_TO);
            if (Cur().encoding != e) {
                Cur().encoding = e;
                Cur().encodingChanged = true;
            }
        } else if (id >= CMD_ENC_RELOAD && id <= CMD_ENC_RELOAD + (int)Encoding::Ansi) {
            Reload(active, id - CMD_ENC_RELOAD);
        } else if (id >= CMD_RECENT && id <= CMD_RECENT_LAST) {
            OpenRecent(id - CMD_RECENT);
        } else if (id >= CMD_LANG_AUTO && id < CMD_LANG + (int)Lang::Count) {
            SetLanguage(id);
        }
        break;
    }
    tabs.Refresh();
    UpdateTitle();
    status.Update();
}

void App::OnNotify(SCNotification* n) {
    switch (n->nmhdr.code) {
    case SCN_UPDATEUI:
        status.Update();
        if (view.braceMatch) UpdateBraceMatch(sci);
        if (view.smartHighlight && (n->updated & (SC_UPDATE_SELECTION | SC_UPDATE_V_SCROLL | SC_UPDATE_TEXT)))
            UpdateSmartHighlight(sci);
        if (panel.Page() != PanelPage::None && (n->updated & (SC_UPDATE_V_SCROLL | SC_UPDATE_TEXT)))
            panel.UpdateLiveHighlight();
        if (n->updated & SC_UPDATE_LINE_COUNT) {
            int digits = 1;
            for (Sci_Position c = sci.LineCount(); c >= 10; c /= 10) digits++;
            if (digits != lastLineDigits_) {
                lastLineDigits_ = digits;
                UpdateLineNumberWidth(sci, view);
            }
        }
        break;
    case SCN_CHARADDED:
        if (view.autoIndent && (n->ch == '\n' || (n->ch == '\r' && Cur().eol == SC_EOL_CR))) AutoIndent(sci);
        break;
    case SCN_SAVEPOINTREACHED:
    case SCN_SAVEPOINTLEFT:
        tabs.Refresh();
        UpdateTitle();
        break;
    case SCN_MARGINCLICK:
        if (n->margin == MARGIN_BOOKMARK) ToggleBookmark(sci, sci.Line(n->position));
        break;
    case SCN_ZOOM:
        UpdateLineNumberWidth(sci, view);
        status.Update();
        break;
    case SCN_FOCUSIN:
        NoteFocus(sci.hwnd);
        break;
    }
}

bool App::PreTranslate(MSG* msg) {
    if (panel.PreTranslate(msg)) return true;
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE && msg->hwnd == sci.hwnd) {
        if (sci(SCI_GETSELECTIONS) > 1) {
            int main = (int)sci(SCI_GETMAINSELECTION);
            Sci_Position caret = (Sci_Position)sci(SCI_GETSELECTIONNCARET, main);
            sci(SCI_SETSELECTION, caret, caret);
            return true;
        }
        if (panel.Page() != PanelPage::None) {
            panel.Hide();
            return true;
        }
    }
    return false;
}

LRESULT App::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT menuResult = 0;
    if (HandleMenuBarMessage(hwnd, msg, wp, lp, &menuResult)) return menuResult;
    switch (msg) {
    case WM_CREATE: OnCreate(); return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_COMMAND:
        if (lp) return 0; // control notifications (e.g. Scintilla's SCEN_*), not commands
        OnCommand(LOWORD(wp));
        return 0;
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr->hwndFrom == sci.hwnd) OnNotify((SCNotification*)lp);
        return 0;
    }
    case WM_INITMENUPOPUP:
        if ((HMENU)wp == recentMenu_) BuildRecentMenu();
        UpdateMenuChecks((HMENU)wp);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) break;
        [[fallthrough]];
    case WM_SETFOCUS:
        // Give focus back to whichever control had it (editor or a panel text box).
        if (lastFocus_ && IsWindow(lastFocus_) && IsWindowVisible(lastFocus_) && IsChild(hwnd, lastFocus_)) SetFocus(lastFocus_);
        else SetFocus(sci.hwnd);
        return 0;
    case WM_ACTIVATEAPP:
        if (wp) PostMessageW(hwnd, WM_APP_CHECKFILES, 0, 0);
        return 0;
    case WM_APP_CHECKFILES: CheckExternalChanges(); return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize = {Px(480, dpi), Px(300, dpi)};
        return 0;
    }
    case WM_DPICHANGED: {
        OnDpiChanged(HIWORD(wp));
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0 && themeMode == ThemeMode::System) RefreshTheme();
        break;
    case WM_COPYDATA: {
        auto* cds = (COPYDATASTRUCT*)lp;
        if (cds->dwData != COPYDATA_OPEN) return FALSE;
        const wchar_t* p = (const wchar_t*)cds->lpData;
        const wchar_t* end = p + cds->cbData / sizeof(wchar_t);
        std::vector<std::wstring> parts;
        while (p < end) {
            std::wstring s(p);
            p += s.size() + 1;
            parts.push_back(s);
        }
        if (parts.size() >= 2) {
            std::wstring cwd = parts[0];
            int line = _wtoi(parts[1].c_str());
            std::vector<std::wstring> files(parts.begin() + 2, parts.end());
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            OpenFiles(files, cwd, line);
        }
        return TRUE;
    }
    case WM_CLOSE:
        if (!bench) {
            BatchClose batch(*this);
            for (int i = 0; i < (int)docs.size(); i++)
                if (!ConfirmClose(i)) return 0;
            SaveSession();
            SaveViewSettings();
            panel.SaveState();
            settings.Save();
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        // Children (Scintilla) still exist here; release our document references now.
        Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace yan

// ---------------------------------------------------------------------------
static std::vector<std::wstring> SplitCommandLine(const wchar_t* cmd) {
    std::vector<std::wstring> out;
    const wchar_t* p = cmd;
    while (*p) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;
        std::wstring arg;
        bool quoted = false;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            if (*p == L'"') quoted = !quoted;
            else arg += *p;
            p++;
        }
        out.push_back(arg);
    }
    return out;
}

static std::wstring SettingsPath() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring portable = yan::DirOf(exe) + L"\\yan.ini";
    if (GetFileAttributesW(portable.c_str()) != INVALID_FILE_ATTRIBUTES) return portable;
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    return std::wstring(appdata) + L"\\Yan\\yan.ini";
}

// Hand the files to an already running instance. Returns true if it took them.
static bool ForwardToRunningInstance(const std::vector<std::wstring>& files, int line) {
    HWND other = nullptr;
    for (int tries = 0; tries < 40 && !other; tries++) {
        other = FindWindowW(L"YanMainWindow", nullptr);
        if (!other) Sleep(50);
    }
    if (!other) return false;
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring payload = cwd;
    payload.push_back(L'\0');
    payload += std::to_wstring(line);
    payload.push_back(L'\0');
    for (auto& f : files) {
        payload += f;
        payload.push_back(L'\0');
    }
    COPYDATASTRUCT cds{yan::COPYDATA_OPEN, (DWORD)(payload.size() * sizeof(wchar_t)), payload.data()};
    DWORD pid = 0;
    GetWindowThreadProcessId(other, &pid);
    AllowSetForegroundWindow(pid);
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 5000, &result) && result;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    auto args = SplitCommandLine(GetCommandLineW());
    std::vector<std::wstring> files;
    int line = 0;
    bool multi = false, bench = false;
    for (size_t i = 1; i < args.size(); i++) {
        const std::wstring& a = args[i];
        if (a == L"--bench") bench = true;
        else if (a == L"-multiInst" || a == L"--new-window") multi = true;
        else if (a.size() > 2 && a[0] == L'-' && a[1] == L'n' && iswdigit(a[2])) line = _wtoi(a.c_str() + 2);
        else if (!a.empty() && a[0] != L'-') files.push_back(a);
    }

    static yan::App application;
    yan::app = &application;
    yan::App& a = application;
    a.bench = bench;
    a.settings.Load(SettingsPath());

    HANDLE mutex = nullptr;
    if (a.settings.GetInt(L"app.singleInstance", 1) && !multi && !bench) {
        wchar_t user[256] = L"user";
        DWORD n = 256;
        GetUserNameW(user, &n);
        std::wstring name = std::wstring(L"Local\\Yan.SingleInstance.") + user;
        mutex = CreateMutexW(nullptr, TRUE, name.c_str());
        if (GetLastError() == ERROR_ALREADY_EXISTS && ForwardToRunningInstance(files, line)) return 0;
    }

    if (!a.Init(inst, files, line)) return 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (a.PreTranslate(&msg)) continue;
        if (TranslateAcceleratorW(a.hwnd, a.accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mutex) CloseHandle(mutex);
    return bench ? a.benchMs : 0;
}
