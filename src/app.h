// The application object: one main window, one Scintilla view, many documents.
#pragma once
#include "common.h"

namespace yan {

enum Cmd : int {
    // File
    CMD_NEW = 1000, CMD_OPEN, CMD_RELOAD, CMD_SAVE, CMD_SAVEAS, CMD_SAVEALL, CMD_CLOSE, CMD_CLOSEALL,
    CMD_CLOSEOTHERS, CMD_REOPENCLOSED, CMD_OPENFOLDER, CMD_COPYPATH, CMD_COPYNAME, CMD_EXIT, CMD_SAVEALLTO,
    CMD_SETTINGS,
    // Edit
    CMD_UNDO = 1100, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DELETE, CMD_SELECTALL,
    CMD_COLUMNEDITOR, CMD_DUPLINE, CMD_DELLINE, CMD_MOVEUP, CMD_MOVEDOWN, CMD_UPPER, CMD_LOWER,
    CMD_JOIN, CMD_TRIM, CMD_SORTASC, CMD_SORTDESC, CMD_SORTASCCI, CMD_DEDUPE, CMD_REMOVEEMPTY, CMD_REVERSE,
    CMD_EOL_CRLF, CMD_EOL_LF, CMD_EOL_CR, CMD_SELECTNEXT, CMD_SELECTALLOCC, CMD_INSERTDATE,
    // Search
    CMD_FIND = 1200, CMD_REPLACE, CMD_FINDNEXT, CMD_FINDPREV, CMD_FINDSELNEXT, CMD_FINDSELPREV,
    CMD_CLEARMARKS, CMD_GOTO, CMD_BRACE, CMD_BM_TOGGLE, CMD_BM_NEXT, CMD_BM_PREV, CMD_BM_CLEAR,
    CMD_BM_COPY, CMD_BM_CUT, CMD_BM_REMOVE, CMD_BM_REMOVEUNMARKED, CMD_BM_INVERSE, CMD_ESCAPE,
    // View
    CMD_WRAP = 1300, CMD_WHITESPACE, CMD_EOLMARKS, CMD_INDENTGUIDES, CMD_LINENUMBERS, CMD_ZOOMIN,
    CMD_ZOOMOUT, CMD_ZOOMRESET, CMD_THEME_SYSTEM, CMD_THEME_LIGHT, CMD_THEME_DARK, CMD_FONT,
    CMD_NEXTTAB, CMD_PREVTAB, CMD_DIRECTWRITE, CMD_SINGLEINSTANCE, CMD_RESTORESESSION,
    CMD_FOLDING, CMD_FOLDALL, CMD_UNFOLDALL, CMD_FOLDTOGGLE,
    // Encoding: CMD_ENC_TO + Encoding, CMD_ENC_RELOAD + Encoding
    CMD_ENC_TO = 1400, CMD_ENC_RELOAD = 1420,
    // Help
    // Language: CMD_LANG + Lang
    CMD_LANG_AUTO = 1450, CMD_LANG = 1451,
    CMD_ABOUT = 1500, CMD_SHORTCUTS,
    // Recent files: CMD_RECENT + index
    CMD_RECENT = 1600, CMD_RECENT_LAST = 1649, CMD_RECENT_CLEAR, CMD_RECENT_OPENALL,
    CMD_TAB_FIRST = 2000, // activate tab N via window list
};

class App {
public:
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    Sci sci;
    TabBar tabs;
    StatusBar status;
    Panel panel;
    Settings settings;
    ViewOptions view;
    ThemeMode themeMode = ThemeMode::System;
    UINT dpi = 96;
    HACCEL accel = nullptr;
    bool singleInstance = true;
    bool restoreSession = true;
    bool exitOnLastTab = false;  // closing the last tab closes the window
    bool checkChanges = true;    // watch for files changed by other programs
    bool autoReload = false;     // ...and reload unmodified ones without asking
    int newEol = SC_EOL_CRLF;    // new documents
    Encoding newEncoding = Encoding::Utf8;
    std::wstring saveAllDir;     // last "Save all to folder"
    std::vector<std::wstring> recent; // recently opened/saved files, newest first
    int recentMax = 15;
    bool bench = false;

    std::vector<std::unique_ptr<Doc>> docs;
    int active = -1;
    std::vector<std::wstring> closedPaths;

    bool Init(HINSTANCE inst, const std::vector<std::wstring>& files, int gotoLine);
    void Shutdown();

    Doc& Cur() { return *docs[active]; }
    bool IsModified(int i);
    void ActivateTab(int i);
    bool CloseTab(int i);
    void NewTab();
    void MoveTab(int from, int to);
    int FindDoc(const std::wstring& path) const;
    bool OpenFile(const std::wstring& path, bool activate = true, int forceEncoding = -1);
    void OpenFiles(const std::vector<std::wstring>& files, const std::wstring& cwd, int gotoLine);
    bool Save(int i, bool saveAs = false);
    bool SaveAll();
    void SaveAllToFolder();      // all documents, untitled ones as "new 1.txt"...
    void ShowSettings();         // settings.cpp
    void ApplyViewOptions();     // after a settings change: re-apply to the editor and save yan.ini
    void SaveViewSettings();
    void AddRecent(const std::wstring& path);
    bool ChooseEditorFont(HWND owner = nullptr);  // true if the font changed
    std::wstring FolderDialog(const std::wstring& dir, HWND owner = nullptr);
    bool ConfirmClose(int i);
    void Reload(int i, int forceEncoding = -1);
    void CheckExternalChanges();

    void UpdateTitle();
    void Layout();
    void OnCommand(int id);
    void ShowTabMenu(int i, POINT screen);
    void ShowEncodingMenu(POINT screen);
    void ShowEolMenu(POINT screen);
    void ShowLanguageMenu(POINT screen);
    void SetLanguage(int cmd);  // CMD_LANG_AUTO or CMD_LANG + Lang
    Lang CurLang();             // language actually highlighted (Text for huge files)
    void SetThemeMode(ThemeMode m);
    void RefreshTheme();
    void OnDocumentChanged();
    void SetEol(int mode);

    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    bool PreTranslate(MSG* msg);
    void NoteFocus(HWND h) { lastFocus_ = h; }
    const Sci& LoaderSci(); // hidden view used to build documents off-screen
    int benchMs = 0;

private:
    void OnCreate();
    const Sci& SciFor(int i);   // a view holding document i (hidden worker view if not active)
    void ReleaseWorker();
    void RemoveDoc(int i);      // i must not be active
    std::vector<std::wstring> FileDialog(bool save, const std::wstring& name, const std::wstring& dir);
    bool SaveTo(int i, const std::wstring& path); // no dialog
    void CreateMenus();
    void UpdateMenuChecks(HMENU menu);
    void SaveSession();
    void OnNotify(SCNotification* n);
    void OnDpiChanged(UINT newDpi);
    void LoadViewSettings();
    void AttachDoc(int i);
    void DetachDoc();
    void ApplyLanguage();      // lexer + styles for the active document
    std::wstring NextUntitledName();

    // While several tabs close at once, ConfirmClose can remember "save all" / "don't save all".
    enum BatchAnswer { BatchAsk, BatchSaveAll, BatchDiscardAll };
    struct BatchClose {
        explicit BatchClose(App& a, int keep = -1);
        ~BatchClose();
        App& app;
    };
    bool batch_ = false;
    int batchKeep_ = -1; // tab that stays open (Close others)
    BatchAnswer batchAnswer_ = BatchAsk;

    HMENU menu_ = nullptr;
    Sci worker_;
    HWND lastFocus_ = nullptr;
    int lastLineDigits_ = 0;
    bool checkingFiles_ = false;
    HMENU recentMenu_ = nullptr;
    void BuildRecentMenu();
    void OpenRecent(int index);
    Lang styledLang_ = Lang::Text; // language whose colors the view currently has
};

} // namespace yan
