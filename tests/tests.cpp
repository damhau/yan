// Console tests for the search engine, column editor, line ops, file I/O and language detection.
// Runs a hidden Scintilla window, so it exercises the exact code the editor uses.
#include "common.h"
#include <cstdio>
#include "SciLexer.h"

using namespace yan;

namespace yan {
App* app = nullptr; // referenced by UI code that is not linked here
}

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        checks++;                                                                   \
        if (!(cond)) {                                                              \
            failures++;                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
        }                                                                           \
    } while (0)
#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        checks++;                                                                   \
        auto va = (a);                                                              \
        auto vb = (b);                                                              \
        if (!(va == vb)) {                                                          \
            failures++;                                                             \
            printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);           \
        }                                                                           \
    } while (0)

static Sci sci;

static void SetText(const char* utf8) {
    sci(SCI_CLEARALL);
    sci(SCI_EMPTYUNDOBUFFER);
    sci.Str(SCI_SETTEXT, 0, utf8);
}
static std::string Text() { return sci.Range(0, sci.Length()); }

static FindOptions Opt(const wchar_t* what, SearchMode mode = SearchMode::Normal, const wchar_t* with = L"") {
    FindOptions o;
    o.what = what;
    o.with = with;
    o.mode = mode;
    return o;
}

static size_t CountAll(const FindOptions& o) {
    Searcher s;
    std::wstring err;
    if (!s.Prepare(sci, o, err)) return (size_t)-1;
    return s.Each(0, sci.Length(), [](const Match&) { return true; });
}

// Same algorithm as Panel::ReplaceAll (small path).
static size_t ReplaceAll(const FindOptions& o) {
    Searcher s;
    std::wstring err;
    if (!s.Prepare(sci, o, err)) return (size_t)-1;
    struct Rep { Match m; std::string t; };
    std::vector<Rep> reps;
    s.Each(0, sci.Length(), [&](const Match& m) {
        reps.push_back({m, s.Replacement()});
        return true;
    });
    for (size_t i = reps.size(); i-- > 0;) sci.Replace(reps[i].m.start, reps[i].m.end, reps[i].t);
    return reps.size();
}

static void TestNormal() {
    SetText("Hello hello HELLO helloworld");
    auto o = Opt(L"hello");
    CHECK_EQ(CountAll(o), (size_t)4);
    o.matchCase = true;
    CHECK_EQ(CountAll(o), (size_t)2);
    o.matchCase = false;
    o.wholeWord = true;
    CHECK_EQ(CountAll(o), (size_t)3);

    Searcher s;
    std::wstring err;
    CHECK(s.Prepare(sci, Opt(L"hello"), err));
    Match m = s.Forward(1, sci.Length());
    CHECK_EQ(m.start, (Sci_Position)6);
    m = s.Backward(0, 18);
    CHECK_EQ(m.start, (Sci_Position)12);

    // Unicode case folding in normal mode
    SetText("\xC3\x89T\xC3\x89 \xC3\xA9t\xC3\xA9"); // ÉTÉ été
    CHECK_EQ(CountAll(Opt(L"été")), (size_t)2);
}

static void TestExtended() {
    CHECK_EQ(UnescapeExtended(L"a\\tb\\nc\\r\\\\"), std::string("a\tb\nc\r\\"));
    CHECK_EQ(UnescapeExtended(L"\\x41\\u00E9\\d065\\o101\\b01000001"), std::string("A\xC3\xA9" "AAA"));
    CHECK_EQ(UnescapeExtended(L"\\q\\x4"), std::string("\\q\\x4"));
    SetText("a\tb\r\nc\td");
    CHECK_EQ(CountAll(Opt(L"\\t", SearchMode::Extended)), (size_t)2);
    CHECK_EQ(CountAll(Opt(L"\\r\\n", SearchMode::Extended)), (size_t)1);
    ReplaceAll(Opt(L"\\t", SearchMode::Extended, L"\\n"));
    CHECK_EQ(Text(), std::string("a\nb\r\nc\nd"));
}

static void TestRegex() {
    SetText("foo123 bar45 baz6\r\nqux");
    CHECK_EQ(CountAll(Opt(L"[a-z]+(\\d+)", SearchMode::Regex)), (size_t)3);
    CHECK_EQ(CountAll(Opt(L"^\\w+", SearchMode::Regex)), (size_t)2);    // multiline ^
    CHECK_EQ(CountAll(Opt(L"\\d+$", SearchMode::Regex)), (size_t)1);    // $ before \r\n
    CHECK_EQ(CountAll(Opt(L"ba(?=z)", SearchMode::Regex)), (size_t)1);  // lookahead
    CHECK_EQ(CountAll(Opt(L"(?<=ba)r", SearchMode::Regex)), (size_t)1); // lookbehind
    CHECK_EQ(CountAll(Opt(L"6.q", SearchMode::Regex)), (size_t)0);      // . excludes newline
    auto dot = Opt(L"6..?.q", SearchMode::Regex);
    dot.dotNewline = true;
    CHECK_EQ(CountAll(dot), (size_t)1);
    CHECK_EQ(CountAll(Opt(L"FOO", SearchMode::Regex)), (size_t)1);      // icase default
    auto cs = Opt(L"FOO", SearchMode::Regex);
    cs.matchCase = true;
    CHECK_EQ(CountAll(cs), (size_t)0);

    // Invalid regex reports an error
    Searcher bad;
    std::wstring err;
    CHECK(!bad.Prepare(sci, Opt(L"(abc", SearchMode::Regex), err));
    CHECK(!err.empty());

    // Replacement formats
    SetText("john smith, jane doe");
    ReplaceAll(Opt(L"(\\w+) (\\w+)", SearchMode::Regex, L"$2 $1"));
    CHECK_EQ(Text(), std::string("smith john, doe jane"));
    SetText("john smith");
    ReplaceAll(Opt(L"(\\w)(\\w*)", SearchMode::Regex, L"\\u$1$2"));
    CHECK_EQ(Text(), std::string("John Smith"));
    SetText("abc");
    ReplaceAll(Opt(L"(b)", SearchMode::Regex, L"\\U$1\\E-(x)"));
    CHECK_EQ(Text(), std::string("aB-(x)c")); // literal parentheses kept
    SetText("cat dog");
    ReplaceAll(Opt(L"(cat)|(dog)", SearchMode::Regex, L"(?1feline:canine)"));
    CHECK_EQ(Text(), std::string("feline canine"));
    SetText("a1b2");
    ReplaceAll(Opt(L"(?<n>\\d)", SearchMode::Regex, L"[$+{n}]"));
    CHECK_EQ(Text(), std::string("a[1]b[2]"));
    SetText("x\ny");
    ReplaceAll(Opt(L"\\n", SearchMode::Regex, L"\\t"));
    CHECK_EQ(Text(), std::string("x\ty"));

    // Empty matches: prefix every line
    SetText("one\ntwo\nthree");
    CHECK_EQ(ReplaceAll(Opt(L"^", SearchMode::Regex, L"> ")), (size_t)3);
    CHECK_EQ(Text(), std::string("> one\n> two\n> three"));

    // Backreference in pattern
    SetText("hello wworld aa");
    CHECK_EQ(CountAll(Opt(L"(\\w)\\1", SearchMode::Regex)), (size_t)3);
}

static void TestUnicodePositions() {
    // "aé😀b" : a(1) é(2) 😀(4) b(1) bytes
    SetText("a\xC3\xA9\xF0\x9F\x98\x80" "b \xC3\x89");
    Searcher s;
    std::wstring err;
    CHECK(s.Prepare(sci, Opt(L"b", SearchMode::Regex), err));
    Match m = s.Forward(0, sci.Length());
    CHECK_EQ(m.start, (Sci_Position)7);
    CHECK(s.Prepare(sci, Opt(L"\U0001F600", SearchMode::Regex), err));
    m = s.Forward(0, sci.Length());
    CHECK_EQ(m.start, (Sci_Position)3);
    CHECK_EQ(m.end, (Sci_Position)7);
    CHECK(s.Prepare(sci, Opt(L"é.b", SearchMode::Regex), err)); // . matches a surrogate pair? (no: 2 units)
    CHECK(!s.Forward(0, sci.Length()).Found());
    CHECK(s.Prepare(sci, Opt(L"é..b", SearchMode::Regex), err));
    CHECK(s.Forward(0, sci.Length()).Found());
    // Case-insensitive non-ASCII in regex mode
    CHECK_EQ(CountAll(Opt(L"é", SearchMode::Regex)), (size_t)2);
    // Backward search over multibyte text
    CHECK(s.Prepare(sci, Opt(L"é", SearchMode::Regex), err));
    m = s.Backward(0, sci.Length());
    CHECK_EQ(m.start, (Sci_Position)9);
    // Regex starting mid-document sees the previous char (\b and lookbehind)
    SetText("xab ab");
    CHECK(s.Prepare(sci, Opt(L"\\bab", SearchMode::Regex), err));
    m = s.Forward(1, sci.Length());
    CHECK_EQ(m.start, (Sci_Position)4);
}

static void TestColumnInsert() {
    SetText("aaa\nbb\ncccc\n");
    sci(SCI_GOTOPOS, 1); // column 1 on line 0
    ColumnInsert ci;
    ci.numbers = true;
    ci.initial = 8;
    ci.step = 1;
    ci.leading = 1;
    CHECK_EQ(InsertColumn(sci, ci), 3);
    CHECK_EQ(Text(), std::string("a08aa\nb09b\nc10ccc\n"));

    // Rectangular selection with virtual space
    SetText("abcdef\nab\nabcdef");
    sci(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
    sci(SCI_SETRECTANGULARSELECTIONANCHOR, 4);
    sci(SCI_SETRECTANGULARSELECTIONCARET, sci.LineStart(2) + 4);
    ColumnInsert t;
    t.text = L"|";
    CHECK_EQ(InsertColumn(sci, t), 3);
    CHECK_EQ(Text(), std::string("abcd|ef\nab  |\nabcd|ef"));

    // Hex with repeat
    SetText("\n\n\n\n");
    sci(SCI_GOTOPOS, 0);
    ColumnInsert h;
    h.numbers = true;
    h.initial = 254;
    h.step = 1;
    h.repeat = 2;
    h.base = 16;
    InsertColumn(sci, h);
    CHECK_EQ(Text(), std::string("FE\nFE\nFF\nFF\n"));
}

static void TestLineOps() {
    SetText("b\na\nc\na\n\n  \nB");
    sci(SCI_SETEOLMODE, SC_EOL_LF);
    DoLineOp(sci, LineOp::Dedupe);
    CHECK_EQ(Text(), std::string("b\na\nc\n\n  \nB"));
    DoLineOp(sci, LineOp::RemoveEmpty);
    CHECK_EQ(Text(), std::string("b\na\nc\nB"));
    sci(SCI_SETEMPTYSELECTION, 0);
    DoLineOp(sci, LineOp::SortAsc);
    CHECK_EQ(Text(), std::string("B\na\nb\nc"));
    sci(SCI_SETEMPTYSELECTION, 0);
    DoLineOp(sci, LineOp::SortAscCI);
    CHECK_EQ(Text(), std::string("a\nB\nb\nc"));
    SetText("x  \ny\t");
    DoLineOp(sci, LineOp::TrimTrailing);
    CHECK_EQ(Text(), std::string("x\ny"));
}

static std::wstring TempFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

static void WriteBytes(const std::wstring& path, const std::string& bytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w;
    WriteFile(f, bytes.data(), (DWORD)bytes.size(), &w, nullptr);
    CloseHandle(f);
}

static std::string ReadBytes(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    char buf[4096];
    DWORD r = 0;
    ReadFile(f, buf, sizeof(buf), &r, nullptr);
    CloseHandle(f);
    return std::string(buf, r);
}

static void TestFiles() {
    struct Case { const char* bytes; size_t n; Encoding enc; int eol; };
    const Case cases[] = {
        {"plain\r\ntext\r\n", 13, Encoding::Utf8, SC_EOL_CRLF},
        {"\xEF\xBB\xBFh\xC3\xA9\n", 7, Encoding::Utf8Bom, SC_EOL_LF},
        {"\xFF\xFEh\0\xE9\0\n\0", 8, Encoding::Utf16LE, SC_EOL_LF},
        {"\xFE\xFF\0h\0\xE9\0\r", 8, Encoding::Utf16BE, SC_EOL_CR},
        {"caf\xE9\r\n", 6, Encoding::Ansi, SC_EOL_CRLF},
    };
    std::wstring path = TempFile(L"yan-test.txt");
    for (auto& c : cases) {
        std::string original(c.bytes, c.n);
        WriteBytes(path, original);
        LoadResult r = LoadFile(sci, path);
        CHECK(r.ok);
        CHECK_EQ((int)r.encoding, (int)c.enc);
        CHECK_EQ(r.eol, c.eol);
        sptr_t old = sci(SCI_GETDOCPOINTER);
        sci(SCI_ADDREFDOCUMENT, 0, old);
        sci(SCI_SETDOCPOINTER, 0, (sptr_t)r.sciDoc);
        sci(SCI_RELEASEDOCUMENT, 0, (sptr_t)r.sciDoc);
        if (c.enc != Encoding::Utf8) CHECK(Text().find("\xC3\xA9") != std::string::npos);
        Doc d;
        d.encoding = r.encoding;
        std::wstring err;
        CHECK(SaveFile(sci, d, path, err, nullptr));
        CHECK(ReadBytes(path) == original); // byte-exact round trip
        sci(SCI_SETDOCPOINTER, 0, old);
        sci(SCI_RELEASEDOCUMENT, 0, old);
    }
    DeleteFileW(path.c_str());
}

static void Perf() {
    std::string big;
    const char* line = "The quick brown fox jumps over the lazy dog 1234567890 lorem ipsum dolor sit amet\r\n";
    while (big.size() < 50u * 1024 * 1024) big += line;
    sci(SCI_SETTEXT, 0, (sptr_t)big.c_str());
    auto time = [](const char* what, auto&& f) {
        LARGE_INTEGER a, b, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&a);
        size_t n = f();
        QueryPerformanceCounter(&b);
        printf("  %-38s %8zu matches  %7.1f ms\n", what, n, (b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart);
    };
    printf("Search speed on %zu MB:\n", big.size() >> 20);
    time("normal \"lazy\"", [] { return CountAll(Opt(L"lazy")); });
    time("normal \"LAZY\" (ignore case)", [] { return CountAll(Opt(L"LAZY")); });
    time("regex \\d{10}", [] { return CountAll(Opt(L"\\d{10}", SearchMode::Regex)); });
    time("regex ^The.*amet$", [] { return CountAll(Opt(L"^The.*amet$", SearchMode::Regex)); });
    time("regex (fox|dog) (jumps|1234)", [] { return CountAll(Opt(L"(fox|dog) (jumps|1234)", SearchMode::Regex)); });
    time("regex no match \"zebra\"", [] { return CountAll(Opt(L"zebra", SearchMode::Regex)); });
}

static void TestLanguages() {
    CHECK(LangFromPath(L"C:\\src\\main.cpp") == Lang::Cpp);
    CHECK(LangFromPath(L"x\\App.Xaml") == Lang::Xml);
    CHECK(LangFromPath(L"x\\CMakeLists.txt") == Lang::CMake);
    CHECK(LangFromPath(L"x\\Makefile") == Lang::Makefile);
    CHECK(LangFromPath(L"x\\.bashrc") == Lang::Bash);
    CHECK(LangFromPath(L"x\\notes.txt") == Lang::Text);
    bool known = true;
    CHECK(LangFromPath(L"x\\script", &known) == Lang::Text && !known);
    CHECK(LangFromContent("#!/usr/bin/env python3\nprint(1)") == Lang::Python);
    CHECK(LangFromContent("#!/bin/bash\n") == Lang::Bash);
    CHECK(LangFromContent("<?xml version=\"1.0\"?>") == Lang::Xml);
    CHECK(LangFromContent("hello") == Lang::Text);

    // The lexer really styles the document.
    SetText("int x = 1; // hi");
    SetDocumentLexer(sci, Lang::Cpp);
    sci(SCI_COLOURISE, 0, -1);
    CHECK_EQ((int)sci(SCI_GETSTYLEAT, 0), SCE_C_WORD2);
    CHECK_EQ((int)sci(SCI_GETSTYLEAT, 8), SCE_C_NUMBER);
    CHECK_EQ((int)sci(SCI_GETSTYLEAT, 12), SCE_C_COMMENTLINE);
    SetDocumentLexer(sci, Lang::Text);
    CHECK_EQ((int)sci(SCI_GETSTYLEAT, 12), 0);
}

static void TestEditorOptions() {
    SetText("a  \nb\t\nc\n  \nd");
    CHECK(TrimTrailingWhitespace(sci));
    CHECK_EQ(Text(), std::string("a\nb\nc\n\nd"));
    CHECK(!TrimTrailingWhitespace(sci));

    SetText("    x\n");
    sci(SCI_SETUSETABS, 0);
    sci(SCI_GOTOPOS, sci.Length());
    AutoIndent(sci);
    CHECK_EQ(Text(), std::string("    x\n    "));
    CHECK_EQ(sci.Pos(), sci.Length());
    sci(SCI_SETUSETABS, 1);
}

int main(int argc, char** argv) {
    Scintilla_RegisterClasses(GetModuleHandleW(nullptr));
    HWND h = CreateWindowExW(0, L"Scintilla", nullptr, WS_POPUP, 0, 0, 800, 600, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    sci.hwnd = h;
    sci.fn = (SciFnDirect)SendMessageW(h, SCI_GETDIRECTFUNCTION, 0, 0);
    sci.ptr = (sptr_t)SendMessageW(h, SCI_GETDIRECTPOINTER, 0, 0);
    sci(SCI_SETCODEPAGE, SC_CP_UTF8);
    sci.Str(SCI_STYLESETFONT, STYLE_DEFAULT, "Consolas"); // monospace, like the editor
    sci(SCI_STYLECLEARALL);
    sci(SCI_SETMULTIPLESELECTION, 1);
    sci(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);

    TestNormal();
    TestExtended();
    TestRegex();
    TestUnicodePositions();
    TestColumnInsert();
    TestLineOps();
    TestFiles();
    TestLanguages();
    TestEditorOptions();

    printf("%d checks, %d failures\n", checks, failures);
    if (argc > 1 && strcmp(argv[1], "--perf") == 0) Perf();
    DestroyWindow(h);
    return failures ? 1 : 0;
}
