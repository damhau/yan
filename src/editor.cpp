// Scintilla configuration and editing commands.
#include "common.h"
#include <algorithm>
#include <thread>
#include <unordered_set>

namespace yan {

static constexpr sptr_t Opaque(COLORREF c) { return (sptr_t)(0xFF000000u | c); }

void ConfigureEditor(const Sci& sci, const ViewOptions& opt, UINT dpi) {
    sci(SCI_SETTECHNOLOGY, opt.directWrite ? opt.technology : SC_TECHNOLOGY_DEFAULT);
    sci(SCI_SETCODEPAGE, SC_CP_UTF8);
    sci(SCI_SETMODEVENTMASK, 0); // we do not need SCN_MODIFIED: fewer notifications, faster edits
    sci(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    sci(SCI_SETLAYOUTTHREADS, std::clamp<unsigned>(std::thread::hardware_concurrency(), 1, 16));
    sci(SCI_USEPOPUP, SC_POPUP_TEXT);
    sci(SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED);

    // Column / multi-caret editing, Notepad++ style.
    sci(SCI_SETMULTIPLESELECTION, 1);            // Ctrl+click adds carets
    sci(SCI_SETADDITIONALSELECTIONTYPING, 1);    // type into all carets
    sci(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    sci(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);
    sci(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, 1);
    sci(SCI_SETRECTANGULARSELECTIONMODIFIER, SCMOD_ALT); // Alt+drag
    sci(SCI_SETADDITIONALCARETSBLINK, 1);
    sci(SCI_SETADDITIONALCARETSVISIBLE, 1);

    sci(SCI_SETCARETSTYLE, CARETSTYLE_LINE);
    sci(SCI_SETCARETWIDTH, Px(2, dpi) > 2 ? 3 : 2);
    sci(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    sci(SCI_SETCARETLINELAYER, SC_LAYER_BASE);
    sci(SCI_SETSELECTIONLAYER, SC_LAYER_UNDER_TEXT);
    sci(SCI_SETSCROLLWIDTHTRACKING, 1);
    sci(SCI_SETSCROLLWIDTH, 1);
    sci(SCI_SETXCARETPOLICY, CARET_SLOP | CARET_EVEN, Px(40, dpi));
    sci(SCI_SETYCARETPOLICY, CARET_EVEN, 0);
    sci(SCI_SETEXTRAASCENT, 1);
    sci(SCI_SETEXTRADESCENT, 1);
    sci(SCI_SETWHITESPACESIZE, std::max(2, Px(2, dpi)));
    sci(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_END);
    sci(SCI_SETWRAPINDENTMODE, SC_WRAPINDENT_SAME);
    sci(SCI_SETMOUSEDWELLTIME, SC_TIME_FOREVER);

    // Margins: line numbers | bookmarks | change history
    sci(SCI_SETMARGINS, 4);
    sci(SCI_SETMARGINTYPEN, MARGIN_LINENUM, SC_MARGIN_NUMBER);
    sci(SCI_SETMARGINTYPEN, MARGIN_BOOKMARK, SC_MARGIN_COLOUR);
    sci(SCI_SETMARGINMASKN, MARGIN_BOOKMARK, 1 << MARK_BOOKMARK);
    sci(SCI_SETMARGINWIDTHN, MARGIN_BOOKMARK, Px(16, dpi));
    sci(SCI_SETMARGINSENSITIVEN, MARGIN_BOOKMARK, 1);
    sci(SCI_SETMARGINCURSORN, MARGIN_BOOKMARK, SC_CURSORARROW);
    sci(SCI_SETMARGINTYPEN, MARGIN_HISTORY, SC_MARGIN_COLOUR);
    sci(SCI_SETMARGINMASKN, MARGIN_HISTORY, SC_MASK_HISTORY);
    sci(SCI_SETMARGINWIDTHN, MARGIN_HISTORY, std::max(3, Px(3, dpi)));
    sci(SCI_SETMARGINLEFT, 0, 0);
    sci(SCI_MARKERDEFINE, MARK_BOOKMARK, SC_MARK_BOOKMARK);
    for (int m : {SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN, SC_MARKNUM_HISTORY_SAVED, SC_MARKNUM_HISTORY_MODIFIED,
                  SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED})
        sci(SCI_MARKERDEFINE, m, SC_MARK_FULLRECT);

    // Folding: box tree like Notepad++; Scintilla handles clicks and edits itself.
    // The margin stays 0 wide until a folding lexer is attached (UpdateFoldMargin).
    sci(SCI_SETMARGINTYPEN, MARGIN_FOLD, SC_MARGIN_SYMBOL);
    sci(SCI_SETMARGINMASKN, MARGIN_FOLD, SC_MASK_FOLDERS);
    sci(SCI_SETMARGINSENSITIVEN, MARGIN_FOLD, 1);
    sci(SCI_SETMARGINCURSORN, MARGIN_FOLD, SC_CURSORARROW);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    sci(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
    sci(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    sci(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    sci.Str(SCI_SETDEFAULTFOLDDISPLAYTEXT, 0, "\xE2\x80\xA6"); // "…" after a folded line
    sci(SCI_FOLDDISPLAYTEXTSETSTYLE, SC_FOLDDISPLAYTEXT_BOXED);

    // Indicators
    sci(SCI_INDICSETSTYLE, IND_FIND, INDIC_ROUNDBOX);
    sci(SCI_INDICSETSTYLE, IND_MARK, INDIC_ROUNDBOX);
    sci(SCI_INDICSETSTYLE, IND_SMART, INDIC_ROUNDBOX);
    sci(SCI_INDICSETSTYLE, IND_SCOPE, INDIC_FULLBOX);
    for (int i : {IND_FIND, IND_MARK, IND_SMART, IND_SCOPE}) sci(SCI_INDICSETUNDER, i, 1);
    sci(SCI_INDICSETALPHA, IND_FIND, 110);
    sci(SCI_INDICSETOUTLINEALPHA, IND_FIND, 230);
    sci(SCI_INDICSETALPHA, IND_MARK, 90);
    sci(SCI_INDICSETOUTLINEALPHA, IND_MARK, 200);
    sci(SCI_INDICSETALPHA, IND_SMART, 60);
    sci(SCI_INDICSETOUTLINEALPHA, IND_SMART, 150);
    sci(SCI_INDICSETALPHA, IND_SCOPE, 35);
    sci(SCI_INDICSETOUTLINEALPHA, IND_SCOPE, 0);

    // Keys: Ctrl+Shift+Up/Down moves lines (Notepad++).
    sci(SCI_ASSIGNCMDKEY, SCK_UP + ((SCMOD_CTRL | SCMOD_SHIFT) << 16), SCI_MOVESELECTEDLINESUP);
    sci(SCI_ASSIGNCMDKEY, SCK_DOWN + ((SCMOD_CTRL | SCMOD_SHIFT) << 16), SCI_MOVESELECTEDLINESDOWN);
}

void ApplyEditorStyles(const Sci& sci, const ViewOptions& opt, Lang lang) {
    const Theme& t = theme;
    std::string font = Narrow(opt.font);
    sci.Str(SCI_STYLESETFONT, STYLE_DEFAULT, font.c_str());
    sci(SCI_STYLESETSIZEFRACTIONAL, STYLE_DEFAULT, opt.fontSize * SC_FONT_SIZE_MULTIPLIER);
    sci(SCI_STYLESETFORE, STYLE_DEFAULT, t.editorFg);
    sci(SCI_STYLESETBACK, STYLE_DEFAULT, t.editorBg);
    sci(SCI_STYLECLEARALL);
    sci(SCI_STYLESETFORE, STYLE_LINENUMBER, t.lineNumFg);
    sci(SCI_STYLESETBACK, STYLE_LINENUMBER, t.lineNumBg);
    sci(SCI_STYLESETFORE, STYLE_BRACELIGHT, t.braceFg);
    sci(SCI_STYLESETBACK, STYLE_BRACELIGHT, t.editorBg);
    sci(SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
    sci(SCI_STYLESETFORE, STYLE_BRACEBAD, t.error);
    sci(SCI_STYLESETFORE, STYLE_INDENTGUIDE, t.indentGuide);
    sci(SCI_STYLESETBACK, STYLE_INDENTGUIDE, t.editorBg);
    sci(SCI_STYLESETFORE, STYLE_CONTROLCHAR, t.textDim);
    sci(SCI_STYLESETFORE, STYLE_FOLDDISPLAYTEXT, t.textDim);
    sci(SCI_STYLESETBACK, STYLE_FOLDDISPLAYTEXT, t.caretLine);
    ApplySyntaxStyles(sci, lang);
}

void ApplyEditorTheme(const Sci& sci, const ViewOptions& opt, Lang lang) {
    const Theme& t = theme;
    ApplyEditorStyles(sci, opt, lang);

    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, Opaque(t.selection));
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, Opaque(t.selectionAdditional));
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_SECONDARY_BACK, Opaque(t.selectionInactive));
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, Opaque(t.selectionInactive));
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_BACK, Opaque(t.selectionInactive));
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, Opaque(t.caret));
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_ADDITIONAL, Opaque(t.caret));
    if (opt.caretLine) sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, Opaque(t.caretLine));
    else sci(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE, Opaque(t.whitespace));

    sci(SCI_SETMARGINBACKN, MARGIN_BOOKMARK, t.lineNumBg);
    sci(SCI_SETMARGINBACKN, MARGIN_HISTORY, t.lineNumBg);
    sci(SCI_MARKERSETFORE, MARK_BOOKMARK, t.accent);
    sci(SCI_MARKERSETBACK, MARK_BOOKMARK, t.accent);
    sci(SCI_MARKERSETBACK, SC_MARKNUM_HISTORY_MODIFIED, t.histModified);
    sci(SCI_MARKERSETFORE, SC_MARKNUM_HISTORY_MODIFIED, t.histModified);
    sci(SCI_MARKERSETBACK, SC_MARKNUM_HISTORY_SAVED, t.histSaved);
    sci(SCI_MARKERSETFORE, SC_MARKNUM_HISTORY_SAVED, t.histSaved);
    sci(SCI_MARKERSETBACK, SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN, t.histReverted);
    sci(SCI_MARKERSETFORE, SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN, t.histReverted);
    sci(SCI_MARKERSETBACK, SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED, t.histReverted);
    sci(SCI_MARKERSETFORE, SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED, t.histReverted);
    for (int m = SC_MARKNUM_FOLDEREND; m <= SC_MARKNUM_FOLDEROPEN; m++) {
        sci(SCI_MARKERSETFORE, m, t.lineNumBg); // box fill
        sci(SCI_MARKERSETBACK, m, t.lineNumFg); // box border, sign and tree lines
    }
    sci(SCI_SETFOLDMARGINCOLOUR, 1, t.lineNumBg);
    sci(SCI_SETFOLDMARGINHICOLOUR, 1, t.lineNumBg);
    sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_FOLD_LINE, Opaque(t.lineNumFg));

    sci(SCI_INDICSETFORE, IND_FIND, t.findHL);
    sci(SCI_INDICSETFORE, IND_MARK, t.markHL);
    sci(SCI_INDICSETFORE, IND_SMART, t.smartHL);
    sci(SCI_INDICSETFORE, IND_SCOPE, t.scopeHL);

    sci(SCI_SETWRAPMODE, opt.wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
    sci(SCI_SETVIEWWS, opt.whitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    sci(SCI_SETVIEWEOL, opt.eolMarks);
    sci(SCI_SETINDENTATIONGUIDES, opt.indentGuides ? SC_IV_LOOKBOTH : SC_IV_NONE);
    sci(SCI_SETENDATLASTLINE, !opt.scrollPastEnd);
    UpdateLineNumberWidth(sci, opt);
}

void ApplyDocSettings(const Sci& sci, const Doc& doc, const ViewOptions& opt) {
    // Documents built with SCI_CREATELOADER start with undo collection off.
    if (!sci(SCI_GETUNDOCOLLECTION)) {
        sci(SCI_SETUNDOCOLLECTION, 1);
        sci(SCI_EMPTYUNDOBUFFER);
        sci(SCI_SETSAVEPOINT);
    }
    sci(SCI_SETEOLMODE, doc.eol);
    sci(SCI_SETTABWIDTH, opt.tabWidth);
    sci(SCI_SETUSETABS, !opt.useSpaces);
    sci(SCI_SETINDENT, 0);
    sci(SCI_SETTABINDENTS, 1);
    sci(SCI_SETBACKSPACEUNINDENTS, 1);
    sci(SCI_SETCHANGEHISTORY, SC_CHANGE_HISTORY_ENABLED | SC_CHANGE_HISTORY_MARKERS);
}

void UpdateLineNumberWidth(const Sci& sci, const ViewOptions& opt) {
    if (!opt.lineNumbers) {
        sci(SCI_SETMARGINWIDTHN, MARGIN_LINENUM, 0);
        return;
    }
    int digits = 1;
    for (Sci_Position n = sci.LineCount(); n >= 10; n /= 10) digits++;
    digits = std::max(digits, 3);
    std::string sample = "_" + std::string(digits, '9');
    int w = (int)sci.Str(SCI_TEXTWIDTH, STYLE_LINENUMBER, sample.c_str());
    sci(SCI_SETMARGINWIDTHN, MARGIN_LINENUM, w + 4);
}

void UpdateFoldMargin(const Sci& sci, const ViewOptions& opt, bool canFold, UINT dpi) {
    bool show = opt.folding && canFold;
    int width = show ? Px(14, dpi) : 0;
    if ((int)sci(SCI_GETMARGINWIDTHN, MARGIN_FOLD) == width) return;
    if (!show) sci(SCI_FOLDALL, SC_FOLDACTION_EXPAND); // nothing may stay hidden without a margin
    sci(SCI_SETMARGINWIDTHN, MARGIN_FOLD, width);
}

void DoFold(const Sci& sci, FoldOp op) {
    if (sci(SCI_GETMARGINWIDTHN, MARGIN_FOLD) == 0) return;
    switch (op) {
    case FoldOp::FoldAll: sci(SCI_FOLDALL, SC_FOLDACTION_CONTRACT_EVERY_LEVEL); break;
    case FoldOp::UnfoldAll: sci(SCI_FOLDALL, SC_FOLDACTION_EXPAND); break;
    case FoldOp::ToggleCurrent: {
        // The block containing the caret: its own header line, or the enclosing one.
        Sci_Position line = sci.Line(sci.Pos());
        if (!(sci(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG)) line = (Sci_Position)sci(SCI_GETFOLDPARENT, line);
        if (line >= 0) sci(SCI_FOLDLINE, line, SC_FOLDACTION_TOGGLE);
        break;
    }
    }
}

void ClearIndicator(const Sci& sci, int ind) {
    sci(SCI_SETINDICATORCURRENT, ind);
    sci(SCI_INDICATORCLEARRANGE, 0, sci.Length());
}

static void VisibleRange(const Sci& sci, Sci_Position& a, Sci_Position& b) {
    Sci_Position firstVis = (Sci_Position)sci(SCI_GETFIRSTVISIBLELINE);
    Sci_Position lines = (Sci_Position)sci(SCI_LINESONSCREEN);
    Sci_Position first = (Sci_Position)sci(SCI_DOCLINEFROMVISIBLE, firstVis);
    Sci_Position last = (Sci_Position)sci(SCI_DOCLINEFROMVISIBLE, firstVis + lines + 1);
    a = sci.LineStart(first);
    b = sci.LineEnd(last);
}

static bool IsWordByte(unsigned char c) { return c >= 0x80 || isalnum(c) || c == '_'; }

void UpdateSmartHighlight(const Sci& sci) {
    static std::string lastWord;
    static Sci_Position lastA = -1, lastB = -1;
    static sptr_t lastDoc = 0;
    Sci_Position a = 0, b = 0;
    sptr_t doc = sci(SCI_GETDOCPOINTER);
    std::string word;
    if (sci(SCI_GETSELECTIONS) == 1) {
        Sci_Position s = (Sci_Position)sci(SCI_GETSELECTIONSTART), e = (Sci_Position)sci(SCI_GETSELECTIONEND);
        if (e > s && e - s <= 200 && sci.Line(s) == sci.Line(e)) {
            word = sci.Range(s, e);
            bool ok = std::all_of(word.begin(), word.end(), [](char c) { return IsWordByte((unsigned char)c); });
            if (ok && s > 0 && IsWordByte((unsigned char)sci(SCI_GETCHARAT, s - 1))) ok = false;
            if (ok && e < sci.Length() && IsWordByte((unsigned char)sci(SCI_GETCHARAT, e))) ok = false;
            if (!ok) word.clear();
        }
    }
    if (!word.empty()) VisibleRange(sci, a, b);
    if (word == lastWord && a == lastA && b == lastB && doc == lastDoc) return;
    ClearIndicator(sci, IND_SMART);
    lastDoc = doc;
    lastWord = word;
    lastA = a;
    lastB = b;
    if (word.empty()) return;
    Sci_Position selStart = (Sci_Position)sci(SCI_GETSELECTIONSTART);
    sci(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
    Sci_Position pos = a;
    int count = 0;
    while (pos < b && count < 2000) {
        sci(SCI_SETTARGETRANGE, pos, b);
        Sci_Position p = (Sci_Position)sci(SCI_SEARCHINTARGET, word.size(), (sptr_t)word.data());
        if (p < 0) break;
        Sci_Position e = (Sci_Position)sci(SCI_GETTARGETEND);
        if (p != selStart) {
            sci(SCI_INDICATORFILLRANGE, p, e - p);
            count++;
        }
        pos = e;
    }
}

void UpdateBraceMatch(const Sci& sci) {
    auto isBrace = [](int c) { return c && strchr("()[]{}", c) != nullptr; };
    Sci_Position pos = sci.Pos();
    Sci_Position at = -1;
    if (pos > 0 && isBrace((int)sci(SCI_GETCHARAT, pos - 1))) at = pos - 1;
    else if (isBrace((int)sci(SCI_GETCHARAT, pos))) at = pos;
    if (at < 0) {
        sci(SCI_BRACEHIGHLIGHT, -1, -1);
        return;
    }
    Sci_Position m = (Sci_Position)sci(SCI_BRACEMATCH, at, 0);
    if (m >= 0) sci(SCI_BRACEHIGHLIGHT, at, m);
    else sci(SCI_BRACEBADLIGHT, at);
}

void CenterLine(const Sci& sci, Sci_Position line) {
    Sci_Position vis = (Sci_Position)sci(SCI_VISIBLEFROMDOCLINE, line);
    Sci_Position lines = (Sci_Position)sci(SCI_LINESONSCREEN);
    sci(SCI_SETFIRSTVISIBLELINE, std::max<Sci_Position>(0, vis - lines / 2));
}

void RevealRange(const Sci& sci, Sci_Position a, Sci_Position b) {
    Sci_Position line = sci.Line(a);
    Sci_Position vis = (Sci_Position)sci(SCI_VISIBLEFROMDOCLINE, line);
    Sci_Position first = (Sci_Position)sci(SCI_GETFIRSTVISIBLELINE);
    Sci_Position lines = (Sci_Position)sci(SCI_LINESONSCREEN);
    if (vis < first || vis >= first + lines) CenterLine(sci, line);
    sci(SCI_SCROLLRANGE, b, a);
}

// ---------------------------------------------------------------------------
static const char* EolString(const Sci& sci) {
    switch ((int)sci(SCI_GETEOLMODE)) {
    case SC_EOL_LF: return "\n";
    case SC_EOL_CR: return "\r";
    default: return "\r\n";
    }
}

static void SplitLines(const std::string& text, std::vector<std::string>& lines) {
    size_t start = 0;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\r' || c == '\n') {
            lines.emplace_back(text, start, i - start);
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') i++;
            start = i + 1;
        }
    }
    lines.emplace_back(text, start, text.size() - start);
}

static void LineRange(const Sci& sci, Sci_Position& firstLine, Sci_Position& lastLine) {
    Sci_Position s = (Sci_Position)sci(SCI_GETSELECTIONSTART), e = (Sci_Position)sci(SCI_GETSELECTIONEND);
    firstLine = 0;
    lastLine = sci.LineCount() - 1;
    if (e > s && sci.Line(s) != sci.Line(e)) {
        firstLine = sci.Line(s);
        lastLine = sci.Line(e);
        if (e == sci.LineStart(lastLine)) lastLine--;
    }
}

void DoLineOp(const Sci& sci, LineOp op) {
    if (op == LineOp::Join) {
        Sci_Position s = (Sci_Position)sci(SCI_GETSELECTIONSTART), e = (Sci_Position)sci(SCI_GETSELECTIONEND);
        Sci_Position l1 = sci.Line(s), l2 = sci.Line(e);
        if (l1 == l2) {
            if (l1 + 1 >= sci.LineCount()) return;
            l2 = l1 + 1;
        }
        sci(SCI_SETTARGETRANGE, sci.LineStart(l1), sci.LineEnd(l2));
        sci(SCI_LINESJOIN);
        return;
    }
    Sci_Position l1, l2;
    LineRange(sci, l1, l2);
    Sci_Position a = sci.LineStart(l1), b = sci.LineEnd(l2);
    std::string text = sci.Range(a, b);
    std::vector<std::string> lines;
    SplitLines(text, lines);
    switch (op) {
    case LineOp::SortAsc: std::stable_sort(lines.begin(), lines.end()); break;
    case LineOp::SortDesc: std::stable_sort(lines.begin(), lines.end(), std::greater<>()); break;
    case LineOp::SortAscCI: {
        std::vector<std::pair<std::wstring, std::string>> keyed;
        keyed.reserve(lines.size());
        for (auto& l : lines) {
            std::wstring k = Widen(l);
            if (!k.empty()) CharLowerBuffW(k.data(), (DWORD)k.size());
            keyed.emplace_back(std::move(k), std::move(l));
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](auto& x, auto& y) { return x.first < y.first; });
        for (size_t i = 0; i < lines.size(); i++) lines[i] = std::move(keyed[i].second);
        break;
    }
    case LineOp::Dedupe: {
        std::unordered_set<std::string> seen;
        std::vector<std::string> out;
        for (auto& l : lines)
            if (seen.insert(l).second) out.push_back(l);
        lines.swap(out);
        break;
    }
    case LineOp::RemoveEmpty:
        std::erase_if(lines, [](const std::string& l) {
            return l.find_first_not_of(" \t") == std::string::npos;
        });
        break;
    case LineOp::TrimTrailing:
        for (auto& l : lines) {
            size_t n = l.find_last_not_of(" \t");
            l.resize(n == std::string::npos ? 0 : n + 1);
        }
        break;
    case LineOp::Reverse: std::reverse(lines.begin(), lines.end()); break;
    default: break;
    }
    std::string eol = EolString(sci), out;
    out.reserve(text.size());
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) out += eol;
        out += lines[i];
    }
    if (out == text) return;
    sci(SCI_BEGINUNDOACTION);
    sci.Replace(a, b, out);
    sci(SCI_ENDUNDOACTION);
    sci(SCI_SETSEL, a, a + (Sci_Position)out.size());
}

bool TrimTrailingWhitespace(const Sci& sci) {
    bool changed = false;
    for (Sci_Position l = sci.LineCount() - 1; l >= 0; l--) {
        Sci_Position start = sci.LineStart(l), end = sci.LineEnd(l), p = end;
        while (p > start) {
            int c = (int)sci(SCI_GETCHARAT, p - 1);
            if (c != ' ' && c != '\t') break;
            p--;
        }
        if (p == end) continue;
        if (!changed) sci(SCI_BEGINUNDOACTION);
        changed = true;
        sci(SCI_DELETERANGE, p, end - p);
    }
    if (changed) sci(SCI_ENDUNDOACTION);
    return changed;
}

void AutoIndent(const Sci& sci) {
    if (sci(SCI_GETSELECTIONS) > 1) return;
    Sci_Position line = sci.Line(sci.Pos());
    if (line == 0) return;
    int indent = (int)sci(SCI_GETLINEINDENTATION, line - 1);
    if (indent <= 0 || sci(SCI_GETLINEINDENTATION, line) != 0) return;
    sci(SCI_SETLINEINDENTATION, line, indent);
    sci(SCI_GOTOPOS, sci(SCI_GETLINEINDENTPOSITION, line));
}

// ---------------------------------------------------------------------------
void ToggleBookmark(const Sci& sci, Sci_Position line) {
    if (sci(SCI_MARKERGET, line) & (1 << MARK_BOOKMARK)) sci(SCI_MARKERDELETE, line, MARK_BOOKMARK);
    else sci(SCI_MARKERADD, line, MARK_BOOKMARK);
}

void NextBookmark(const Sci& sci, bool forward) {
    Sci_Position line = sci.Line(sci.Pos());
    const int mask = 1 << MARK_BOOKMARK;
    Sci_Position n;
    if (forward) {
        n = (Sci_Position)sci(SCI_MARKERNEXT, line + 1, mask);
        if (n < 0) n = (Sci_Position)sci(SCI_MARKERNEXT, 0, mask);
    } else {
        n = (Sci_Position)sci(SCI_MARKERPREVIOUS, line - 1, mask);
        if (n < 0) n = (Sci_Position)sci(SCI_MARKERPREVIOUS, sci.LineCount() - 1, mask);
    }
    if (n < 0) return;
    sci(SCI_GOTOLINE, n);
    RevealRange(sci, sci.LineStart(n), sci.LineStart(n));
}

void DoBookmarkOp(const Sci& sci, BookmarkOp op, HWND) {
    const int mask = 1 << MARK_BOOKMARK;
    const Sci_Position count = sci.LineCount();
    auto marked = [&](Sci_Position l) { return (sci(SCI_MARKERGET, l) & mask) != 0; };
    switch (op) {
    case BookmarkOp::ClearAll: sci(SCI_MARKERDELETEALL, MARK_BOOKMARK); return;
    case BookmarkOp::Inverse:
        for (Sci_Position l = 0; l < count; l++) ToggleBookmark(sci, l);
        return;
    case BookmarkOp::Copy:
    case BookmarkOp::Cut: {
        std::string text, eol = EolString(sci);
        for (Sci_Position l = (Sci_Position)sci(SCI_MARKERNEXT, 0, mask); l >= 0;
             l = (Sci_Position)sci(SCI_MARKERNEXT, l + 1, mask)) {
            text += sci.Range(sci.LineStart(l), sci.LineEnd(l));
            text += eol;
        }
        if (!text.empty()) sci(SCI_COPYTEXT, text.size(), (sptr_t)text.data());
        if (op == BookmarkOp::Copy) return;
        [[fallthrough]];
    }
    case BookmarkOp::Remove:
    case BookmarkOp::RemoveUnmarked: {
        bool removeMarked = op != BookmarkOp::RemoveUnmarked;
        sci(SCI_BEGINUNDOACTION);
        for (Sci_Position l = count - 1; l >= 0; l--) {
            if (marked(l) != removeMarked) continue;
            Sci_Position a = sci.LineStart(l), b = sci.LineStart(l + 1);
            if (l == sci.LineCount() - 1) {
                if (l == 0) { b = sci.Length(); }
                else { a = sci.LineEnd(l - 1); b = sci.Length(); }
            }
            sci(SCI_DELETERANGE, a, b - a);
        }
        sci(SCI_ENDUNDOACTION);
        return;
    }
    }
}

void SelectAllOccurrences(const Sci& sci) {
    bool empty = sci(SCI_GETSELECTIONEMPTY) != 0;
    sci(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | (empty ? SCFIND_WHOLEWORD : 0));
    sci(SCI_TARGETWHOLEDOCUMENT);
    sci(SCI_MULTIPLESELECTADDEACH);
}

void AddNextOccurrence(const Sci& sci) {
    bool empty = sci(SCI_GETSELECTIONEMPTY) != 0;
    sci(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | (empty ? SCFIND_WHOLEWORD : 0));
    sci(SCI_TARGETWHOLEDOCUMENT);
    sci(SCI_MULTIPLESELECTADDNEXT);
    int main = (int)sci(SCI_GETMAINSELECTION);
    RevealRange(sci, (Sci_Position)sci(SCI_GETSELECTIONNSTART, main), (Sci_Position)sci(SCI_GETSELECTIONNEND, main));
}

// ---------------------------------------------------------------------------
static std::string FormatNumber(long long v, int base, bool upper) {
    if (base == 10) return std::to_string(v);
    bool neg = v < 0;
    unsigned long long u = neg ? 0ULL - (unsigned long long)v : (unsigned long long)v;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.insert(s.begin(), digits[u % base]); u /= base; } while (u);
    if (neg) s.insert(s.begin(), '-');
    return s;
}

int InsertColumn(const Sci& sci, const ColumnInsert& ci) {
    struct Slot { Sci_Position start, end, vs; };
    std::vector<Slot> slots;
    const int nsel = (int)sci(SCI_GETSELECTIONS);
    const bool multi = nsel > 1 || sci(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE;
    Sci_Position column = 0, firstLine = 0;
    if (multi) {
        for (int i = 0; i < nsel; i++)
            slots.push_back({(Sci_Position)sci(SCI_GETSELECTIONNSTART, i), (Sci_Position)sci(SCI_GETSELECTIONNEND, i),
                             (Sci_Position)sci(SCI_GETSELECTIONNSTARTVIRTUALSPACE, i)});
        std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return a.start < b.start; });
    } else {
        int main = (int)sci(SCI_GETMAINSELECTION);
        Sci_Position caret = sci.Pos();
        column = (Sci_Position)sci(SCI_GETCOLUMN, caret) + (Sci_Position)sci(SCI_GETSELECTIONNCARETVIRTUALSPACE, main);
        firstLine = sci.Line(caret);
        Sci_Position lastLine = sci.LineCount() - 1;
        if (lastLine > firstLine && sci.LineStart(lastLine) == sci.LineEnd(lastLine)) lastLine--; // trailing empty line
        for (Sci_Position l = firstLine; l <= lastLine; l++) {
            Sci_Position p = (Sci_Position)sci(SCI_FINDCOLUMN, l, column);
            Sci_Position actual = (Sci_Position)sci(SCI_GETCOLUMN, p);
            slots.push_back({p, p, std::max<Sci_Position>(0, column - actual)});
        }
    }
    if (slots.empty()) return 0;

    std::vector<std::string> values(slots.size());
    if (ci.numbers) {
        size_t width = 0;
        for (size_t i = 0; i < slots.size(); i++) {
            long long rep = std::max(1LL, ci.repeat);
            long long v = ci.initial + ci.step * (long long)(i / (size_t)rep);
            values[i] = FormatNumber(v, ci.base, ci.upperHex);
            width = std::max(width, values[i].size());
        }
        if (ci.leading)
            for (auto& v : values)
                if (v.size() < width) v.insert(0, width - v.size(), ci.leading == 1 ? '0' : ' ');
    } else {
        std::string t = Narrow(ci.text);
        for (auto& v : values) v = t;
    }

    sci(SCI_BEGINUNDOACTION);
    for (size_t i = slots.size(); i-- > 0;) {
        std::string text(size_t(slots[i].vs), ' ');
        text += values[i];
        sci.Replace(slots[i].start, slots[i].end, text);
    }
    sci(SCI_ENDUNDOACTION);

    // Put a caret after each inserted piece so typing can continue in column mode.
    Sci_Position delta = 0;
    std::vector<Sci_Position> carets;
    for (size_t i = 0; i < slots.size(); i++) {
        Sci_Position ins = slots[i].vs + (Sci_Position)values[i].size();
        Sci_Position start = slots[i].start + delta;
        carets.push_back(start + ins);
        delta += ins - (slots[i].end - slots[i].start);
    }
    if (multi) {
        sci(SCI_SETSELECTION, carets[0], carets[0]);
        for (size_t i = 1; i < carets.size(); i++) sci(SCI_ADDSELECTION, carets[i], carets[i]);
    } else {
        sci(SCI_SETSEL, carets[0], carets[0]);
    }
    return (int)slots.size();
}

} // namespace yan
