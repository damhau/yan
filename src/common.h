// Shared declarations for Yan.
#pragma once

#include <windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdint>

#include "Scintilla.h"

namespace yan {

// ---------------------------------------------------------------------------
// Utilities (util.cpp)
std::wstring Widen(std::string_view utf8);
std::string Narrow(std::wstring_view wide);
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirOf(const std::wstring& path);
std::wstring FullPath(const std::wstring& path, const std::wstring& cwd = {});
inline int Px(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }
void CopyToClipboard(HWND owner, const std::wstring& text);

// Small, fast key=value ini store (we avoid GetPrivateProfileString for speed).
class Settings {
public:
    void Load(const std::wstring& path);
    void Save() const;
    std::wstring Get(const std::wstring& key, const std::wstring& def = {}) const;
    int GetInt(const std::wstring& key, int def) const;
    void Set(const std::wstring& key, const std::wstring& value);
    void SetInt(const std::wstring& key, int value);
    std::vector<std::wstring> GetList(const std::wstring& prefix) const;
    void SetList(const std::wstring& prefix, const std::vector<std::wstring>& values);
    const std::wstring& Path() const { return path_; }
private:
    std::wstring path_;
    std::vector<std::pair<std::wstring, std::wstring>> items_;
};

// ---------------------------------------------------------------------------
// Scintilla direct-call wrapper (much faster than SendMessage).
struct Sci {
    HWND hwnd = nullptr;
    SciFnDirect fn = nullptr;
    sptr_t ptr = 0;
    sptr_t operator()(unsigned msg, uptr_t w = 0, sptr_t l = 0) const { return fn(ptr, msg, w, l); }
    sptr_t Str(unsigned msg, uptr_t w, const char* s) const { return fn(ptr, msg, w, (sptr_t)s); }
    Sci_Position Length() const { return (Sci_Position)(*this)(SCI_GETLENGTH); }
    Sci_Position Pos() const { return (Sci_Position)(*this)(SCI_GETCURRENTPOS); }
    Sci_Position Line(Sci_Position pos) const { return (Sci_Position)(*this)(SCI_LINEFROMPOSITION, pos); }
    Sci_Position LineStart(Sci_Position line) const { return (Sci_Position)(*this)(SCI_POSITIONFROMLINE, line); }
    Sci_Position LineEnd(Sci_Position line) const { return (Sci_Position)(*this)(SCI_GETLINEENDPOSITION, line); }
    Sci_Position LineCount() const { return (Sci_Position)(*this)(SCI_GETLINECOUNT); }
    std::string Range(Sci_Position a, Sci_Position b) const;
    std::string SelText() const;
    void Replace(Sci_Position a, Sci_Position b, std::string_view text) const;
};

// ---------------------------------------------------------------------------
// Theme (theme.cpp)
enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

// Syntax highlighting roles: every lexer style maps to one of these (language.cpp).
enum class Syn : unsigned char {
    Comment, CommentDoc, Keyword, Type, String, Number, Operator, Preproc, Regex, Tag, Attribute, Entity,
    Variable, Function, Key, Heading, Strong, Emphasis, Link, Deleted, Inserted, Meta, Error, Count
};

struct Theme {
    bool dark = false;
    COLORREF chrome;      // menu bar, tab strip, status bar
    COLORREF surface;     // find panel
    COLORREF editorBg, editorFg;
    COLORREF text, textDim, textDisabled;
    COLORREF border, borderStrong;
    COLORREF hover, pressed;
    COLORREF accent, accentText, accentSoft;
    COLORREF inputBg;
    COLORREF tabActive;
    COLORREF lineNumFg, lineNumBg;
    COLORREF caretLine, selection, selectionAdditional, selectionInactive, caret;
    COLORREF whitespace, indentGuide;
    COLORREF findHL, markHL, smartHL, scopeHL, braceFg;
    COLORREF error, success, modified;
    COLORREF histModified, histSaved, histReverted;
    COLORREF syntax[(int)Syn::Count];
};

extern Theme theme;
bool SystemUsesDarkTheme();
void BuildTheme(bool dark);
void ApplyWindowTheme(HWND hwnd);              // title bar + menus
void ApplyControlTheme(HWND hwnd, bool editLike = false); // scrollbars of children
void InitDarkModeSupport();
bool HandleMenuBarMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

// GDI helpers
struct Fonts {
    HFONT ui = nullptr, uiBold = nullptr, uiSmall = nullptr, icons = nullptr, iconsSmall = nullptr;
    UINT dpi = 96;
    void Create(UINT dpi);
    void Destroy();
};
extern Fonts fonts;
void FillRectColor(HDC dc, const RECT& rc, COLORREF c);
void RoundRectColor(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border);
void DrawTextColor(HDC dc, const std::wstring& s, RECT rc, COLORREF c, HFONT f, UINT flags);
int TextWidth(HDC dc, const std::wstring& s, HFONT f);
bool FontInstalled(const wchar_t* face);

// Icon glyphs from Segoe Fluent Icons / Segoe MDL2 Assets.
namespace Icon {
constexpr wchar_t Close = 0xE711, Add = 0xE710, Up = 0xE74A, Down = 0xE74B,
                  ChevronRight = 0xE76C, ChevronDown = 0xE70D;
}

// ---------------------------------------------------------------------------
// Languages and syntax highlighting (language.cpp)
enum class Lang : int {
    Text, Batch, Bash, C, Cpp, CSharp, CMake, Css, Diff, Go, Html, Ini, Java, JavaScript, Json, Lua,
    Makefile, Markdown, PowerShell, Python, Rust, Sql, TypeScript, Xml, Yaml, Count
};
const wchar_t* LangName(Lang l);
bool LangFolds(Lang l); // the lexer provides fold levels
// From the file name / extension; Lang::Text when unknown. known: the name or extension was recognized.
Lang LangFromPath(const std::wstring& path, bool* known = nullptr);
// From the first bytes of the text (shebang, <?xml); Lang::Text when unknown.
Lang LangFromContent(std::string_view head);
// Attaches a new lexer for l to the view's current document (none for Lang::Text).
void SetDocumentLexer(const Sci& sci, Lang l);
// Colors the lexer's styles; call after SCI_STYLECLEARALL.
void ApplySyntaxStyles(const Sci& sci, Lang l);

// ---------------------------------------------------------------------------
// Documents (fileio.cpp)
enum class Encoding { Utf8 = 0, Utf8Bom, Utf16LE, Utf16BE, Ansi };
const wchar_t* EncodingName(Encoding e);

struct Doc {
    void* sciDoc = nullptr;     // Scintilla document (we hold one reference)
    std::wstring path;          // empty for untitled
    std::wstring untitledName;  // "new 1"
    Encoding encoding = Encoding::Utf8;
    int eol = SC_EOL_CRLF;
    bool loaded = true;         // false: lazy session tab not read from disk yet
    bool modified = false;      // cached when not the active document
    bool encodingChanged = false; // unsaved encoding conversion
    FILETIME mtime{};
    bool missing = false;
    Lang lang = Lang::Text;
    bool langChosen = false;    // picked from the Language menu: no auto-detection
    bool langDetected = false;  // auto-detection already ran for the current path
    int lexer = -1;             // Lang whose lexer is attached to sciDoc, -1 none yet
    std::vector<Sci_Position> folded; // contracted fold headers, kept while another tab is shown
    // Saved view state
    Sci_Position anchor = 0, caret = 0, firstLine = 0, xOffset = 0;
    std::wstring Title() const { return path.empty() ? untitledName : FileNameOf(path); }
};

struct LoadResult {
    bool ok = false;
    std::wstring error;
    void* sciDoc = nullptr;
    Encoding encoding = Encoding::Utf8;
    int eol = SC_EOL_CRLF;
    FILETIME mtime{};
};
// forceEncoding < 0: autodetect.
LoadResult LoadFile(const Sci& sci, const std::wstring& path, int forceEncoding = -1);
bool SaveFile(const Sci& sci, Doc& doc, const std::wstring& path, std::wstring& error, HWND owner);
bool GetFileTime(const std::wstring& path, FILETIME& ft);
int DetectEol(const char* text, size_t len);

// ---------------------------------------------------------------------------
// Editor configuration and editing commands (editor.cpp)
constexpr int MARGIN_LINENUM = 0, MARGIN_BOOKMARK = 1, MARGIN_HISTORY = 2, MARGIN_FOLD = 3;
constexpr int MARK_BOOKMARK = 20;
constexpr int IND_FIND = 8, IND_MARK = 9, IND_SMART = 10, IND_SCOPE = 11;

struct ViewOptions {
    bool wrap = false, whitespace = false, eolMarks = false, indentGuides = false, lineNumbers = true;
    bool directWrite = false;
    int technology = SC_TECHNOLOGY_DIRECTWRITE;
    std::wstring font;
    int fontSize = 11;
    int tabWidth = 4;
    bool useSpaces = false;      // Tab key inserts spaces
    bool autoIndent = true;      // Enter keeps the previous line's indentation
    bool caretLine = true;       // highlight the current line
    bool smartHighlight = true;  // highlight other occurrences of the selected word
    bool braceMatch = true;
    bool scrollPastEnd = true;   // allow scrolling the last line to the top
    bool syntax = true;          // syntax highlighting
    bool folding = true;         // fold margin for languages that support it
    bool trimOnSave = false;     // remove trailing whitespace when saving
};

void ConfigureEditor(const Sci& sci, const ViewOptions& opt, UINT dpi);
void ApplyEditorTheme(const Sci& sci, const ViewOptions& opt, Lang lang = Lang::Text);
// Default + lexer styles only (font, colors); used when the language changes.
void ApplyEditorStyles(const Sci& sci, const ViewOptions& opt, Lang lang);
void ApplyDocSettings(const Sci& sci, const Doc& doc, const ViewOptions& opt);
// Removes spaces and tabs at line ends in the whole document (one undo step). Returns true if changed.
bool TrimTrailingWhitespace(const Sci& sci);
// After Enter: indents the new line like the previous one.
void AutoIndent(const Sci& sci);
void UpdateLineNumberWidth(const Sci& sci, const ViewOptions& opt);
// Shows the fold margin when folding is on and the document's lexer folds.
void UpdateFoldMargin(const Sci& sci, const ViewOptions& opt, bool canFold, UINT dpi);
enum class FoldOp { FoldAll, UnfoldAll, ToggleCurrent };
void DoFold(const Sci& sci, FoldOp op);
void UpdateSmartHighlight(const Sci& sci);
void UpdateBraceMatch(const Sci& sci);
void ClearIndicator(const Sci& sci, int ind);
void RevealRange(const Sci& sci, Sci_Position a, Sci_Position b);
void CenterLine(const Sci& sci, Sci_Position line);

enum class LineOp { SortAsc, SortDesc, SortAscCI, Dedupe, RemoveEmpty, TrimTrailing, Join, Reverse };
void DoLineOp(const Sci& sci, LineOp op);
void ToggleBookmark(const Sci& sci, Sci_Position line);
void NextBookmark(const Sci& sci, bool forward);
enum class BookmarkOp { ClearAll, Copy, Cut, Remove, RemoveUnmarked, Inverse };
void DoBookmarkOp(const Sci& sci, BookmarkOp op, HWND owner);
void SelectAllOccurrences(const Sci& sci);
void AddNextOccurrence(const Sci& sci);

struct ColumnInsert {
    bool numbers = false;
    std::wstring text;
    long long initial = 1, step = 1, repeat = 1;
    int base = 10;      // 10, 16, 8, 2
    int leading = 0;    // 0 none, 1 zeros, 2 spaces
    bool upperHex = true;
};
// Returns number of lines touched.
int InsertColumn(const Sci& sci, const ColumnInsert& ci);

// ---------------------------------------------------------------------------
// Search (search.cpp)
enum class SearchMode { Normal = 0, Extended = 1, Regex = 2 };

struct FindOptions {
    std::wstring what;
    std::wstring with;
    SearchMode mode = SearchMode::Normal;
    bool matchCase = false, wholeWord = false, wrap = true, dotNewline = false;
};

struct Match {
    Sci_Position start = -1, end = -1;
    bool Found() const { return start >= 0; }
    bool operator==(const Match&) const = default;
};

class Searcher {
public:
    Searcher();
    ~Searcher();
    // Compiles the pattern. Returns false and fills error on invalid regex / empty pattern.
    bool Prepare(const Sci& sci, const FindOptions& opt, std::wstring& error);
    // First match fully inside [from, to).
    Match Forward(Sci_Position from, Sci_Position to);
    // Last match fully inside [from, to).
    Match Backward(Sci_Position from, Sci_Position to);
    // All matches in [from, to); callback returns false to stop. Returns count.
    template <class F> size_t Each(Sci_Position from, Sci_Position to, F&& f);
    // Replacement text (UTF-8) for the most recent match from Forward/Each.
    std::string Replacement();
    bool IsRegex() const;
    const std::wstring& Error() const { return error_; }

    struct Impl;
private:
    size_t EachImpl(Sci_Position from, Sci_Position to, bool (*cb)(void*, const Match&), void* ctx);
    std::unique_ptr<Impl> impl_;
    std::wstring error_;
};

template <class F> size_t Searcher::Each(Sci_Position from, Sci_Position to, F&& f) {
    auto thunk = [](void* ctx, const Match& m) -> bool { return (*static_cast<F*>(ctx))(m); };
    return EachImpl(from, to, thunk, &f);
}

std::string UnescapeExtended(const std::wstring& s);

// ---------------------------------------------------------------------------
// Child windows
class App;
extern App* app;

class TabBar {
public:
    HWND hwnd = nullptr;
    void Create(HWND parent);
    int Height() const;
    void Refresh();               // re-measure & repaint
    void EnsureVisible(int index);
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
private:
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Layout(HDC dc);
    int HitTest(POINT pt, bool* onClose) const;
    void Paint(HDC dc);
    std::vector<RECT> rects_;
    RECT plus_{};
    int scroll_ = 0, contentWidth_ = 0;
    int hover_ = -1, pressed_ = -1;
    bool hoverClose_ = false, hoverPlus_ = false, tracking_ = false, dragging_ = false, pressClose_ = false;
    POINT pressPt_{};
};

class StatusBar {
public:
    HWND hwnd = nullptr;
    void Create(HWND parent);
    int Height() const;
    void Update();
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
private:
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Paint(HDC dc);
    struct Part { std::wstring text; RECT rc; int id; };
    std::vector<Part> parts_;
    std::wstring left_;
    int hover_ = -1;
    bool tracking_ = false;
};

enum class PanelPage { None, Find, Replace, GoTo, Column };

// One clickable / drawable element of the panel (immediate-mode layout).
struct PanelItem {
    enum Kind { Label, Edit, Button, IconButton, Toggle, Segment, StatusText } kind;
    int id = 0;
    std::wstring text;
    wchar_t icon = 0;
    RECT rc{};
    bool on = false;
    bool enabled = true;
    bool primary = false;
    HWND edit = nullptr;
    std::vector<std::wstring> segs;
    std::vector<RECT> segRects;
    int segSel = 0;
    std::wstring tip;
};

class Panel {
public:
    HWND hwnd = nullptr;
    void Create(HWND parent);
    int Height() const;
    PanelPage Page() const { return page_; }
    void Show(PanelPage page);
    void Hide();
    bool PreTranslate(MSG* msg);
    void LoadState();
    void SaveState();
    void ThemeChanged();
    // Search commands usable from menus/accelerators.
    void FindNext(bool forward, bool fromAccelerator = false);
    void FindSelection(bool forward);
    void UpdateLiveHighlight();
    void LeavingDocument();      // called before the active document changes
    void OnDocumentSwitched();   // called after
    void SelectAllMatches();
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);

    FindOptions opt;
    bool inSelection = false, bookmarkLines = false;
private:
    LRESULT Handle(UINT, WPARAM, LPARAM);
    void Layout();
    void Paint(HDC dc);
    void OnClick(int id, int segment);
    void Status(const std::wstring& s, int kind = 0); // 0 info, 1 error, 2 success
    bool PrepareSearch(Searcher& s);
    bool GetScope(Sci_Position& a, Sci_Position& b);
    bool SetScopeFromSelection();
    void ClearScope();
    void Count();
    void MarkAll();
    void Replace();
    void ReplaceAll();
    void GoTo();
    void ColumnInsertNow();
    void IncrementalFind();
    void PushHistory(std::vector<std::wstring>& h, const std::wstring& s);
    void CycleHistory(HWND edit, int dir);
    std::wstring EditText(HWND e) const;
    void SetEditText(HWND e, const std::wstring& s);
    HWND MakeEdit(int id);
    void EnsureEdits();
    std::vector<HWND> VisibleEdits() const;
    void SyncOptionsFromEdits();
    void FocusPrimary();
    bool OnEditKey(HWND edit, WPARAM key);
    void Range(Sci_Position& lo, Sci_Position& hi);
    std::wstring MatchCountText(Searcher& s, const Match& m, Sci_Position lo, Sci_Position hi);

    PanelItem* ItemById(int id);
    int HitItem(LPARAM lp, int* seg) const;
    int Rows() const;
    int pressedSeg_ = -1;

    PanelPage page_ = PanelPage::None;
    std::vector<PanelItem> items_;
    HWND findEdit_ = nullptr, replaceEdit_ = nullptr, gotoEdit_ = nullptr;
    HWND colText_ = nullptr, colInit_ = nullptr, colStep_ = nullptr, colRepeat_ = nullptr;
    HBRUSH inputBrush_ = nullptr;
    std::wstring status_, hoverTip_;
    int statusKind_ = 0;
    bool settingText_ = false;
    int editHeight_ = 16;
    int hover_ = -1, hoverSeg_ = -1, pressed_ = -1;
    bool tracking_ = false;
    std::vector<std::wstring> findHistory_, replaceHistory_;
    int historyPos_ = -1;
    Sci_Position incAnchor_ = 0;
    Sci_Position lastEmptyMatch_ = -1;
    bool gotoOffset_ = false;
    ColumnInsert col_;
    bool replaceExpanded_ = false;
};

} // namespace yan
