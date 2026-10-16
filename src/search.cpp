// Search engine.
//  - Normal / Extended modes use Scintilla's own (fast, Unicode case folding) search.
//  - Regex mode uses Boost.Regex (Perl syntax, like Notepad++) running directly on
//    Scintilla's UTF-8 buffer through a UTF-8 -> UTF-16 bidirectional iterator,
//    so no copy of the document is made.
#include "common.h"
#include <algorithm>
#include <bitset>
#include <iterator>
#include <boost/regex.hpp>

namespace yan {

namespace {

class U8Iter {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = wchar_t;
    using difference_type = std::ptrdiff_t;
    using pointer = const wchar_t*;
    using reference = const wchar_t&;

    U8Iter() = default;
    U8Iter(const unsigned char* doc, Sci_Position len, Sci_Position pos) : doc_(doc), len_(len), pos_(pos) { Decode(); }

    reference operator*() const { return ch_[half_]; }
    U8Iter& operator++() {
        if (half_ == 0 && units_ == 2) {
            half_ = 1;
        } else {
            pos_ += clen_;
            Decode();
        }
        return *this;
    }
    U8Iter operator++(int) { U8Iter t = *this; ++*this; return t; }
    U8Iter& operator--() {
        if (half_ == 1) { half_ = 0; return *this; }
        const Sci_Position orig = pos_;
        Sci_Position p = pos_ - 1;
        for (int k = 0; p > 0 && k < 3 && (doc_[p] & 0xC0) == 0x80; k++) p--;
        pos_ = p;
        Decode();
        if (pos_ + clen_ != orig) { pos_ = orig - 1; Decode(); } // invalid sequence: single byte
        half_ = units_ - 1;
        return *this;
    }
    U8Iter operator--(int) { U8Iter t = *this; --*this; return t; }
    bool operator==(const U8Iter& o) const { return pos_ == o.pos_ && half_ == o.half_; }
    bool operator!=(const U8Iter& o) const { return !(*this == o); }
    Sci_Position Pos() const { return pos_; }

private:
    void Decode() {
        half_ = 0;
        if (pos_ >= len_) { clen_ = 0; units_ = 1; ch_[0] = 0; return; }
        const unsigned c = doc_[pos_];
        if (c < 0x80) { ch_[0] = (wchar_t)c; clen_ = 1; units_ = 1; return; }
        int n;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
        else { Bad(); return; }
        if (pos_ + n > len_) { Bad(); return; }
        for (int k = 1; k < n; k++) {
            const unsigned b = doc_[pos_ + k];
            if ((b & 0xC0) != 0x80) { Bad(); return; }
            cp = (cp << 6) | (b & 0x3F);
        }
        clen_ = n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            ch_[0] = (wchar_t)(0xD800 + (cp >> 10));
            ch_[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
            units_ = 2;
        } else {
            ch_[0] = (wchar_t)cp;
            units_ = 1;
        }
    }
    void Bad() { ch_[0] = 0xFFFD; clen_ = 1; units_ = 1; }

    const unsigned char* doc_ = nullptr;
    Sci_Position len_ = 0, pos_ = 0;
    wchar_t ch_[2] = {};
    int half_ = 0, units_ = 1, clen_ = 0;
};

// Boost's Win32 traits call GetStringTypeExW / LCMapStringW for every character
// tested; cache the answers for the first 256 code points (the common case).
class FastTraits : public boost::w32_regex_traits<wchar_t> {
    using Base = boost::w32_regex_traits<wchar_t>;

public:
    FastTraits() {
        for (int c = 0; c < 256; c++) lower_[c] = Base::tolower((wchar_t)c);
    }
    wchar_t translate_nocase(wchar_t c) const { return c < 256 ? lower_[c] : Base::translate_nocase(c); }
    wchar_t translate(wchar_t c, bool icase) const { return icase ? translate_nocase(c) : c; }
    wchar_t translate(wchar_t c) const { return c; }
    wchar_t tolower(wchar_t c) const { return c < 256 ? lower_[c] : Base::tolower(c); }
    bool isctype(wchar_t c, char_class_type mask) const {
        if (c >= 256) return Base::isctype(c, mask);
        for (const Entry& e : cache_)
            if (e.mask == mask) return e.bits[c];
        Entry e{mask, {}};
        for (int i = 0; i < 256; i++) e.bits[i] = Base::isctype((wchar_t)i, mask);
        cache_.push_back(e);
        return e.bits[c];
    }

private:
    struct Entry {
        char_class_type mask;
        std::bitset<256> bits;
    };
    mutable std::vector<Entry> cache_;
    wchar_t lower_[256];
};

using Regex = boost::basic_regex<wchar_t, FastTraits>;

int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

void AppendCodepoint(std::wstring& out, uint32_t cp) {
    if (cp >= 0x10000) {
        cp -= 0x10000;
        out += (wchar_t)(0xD800 + (cp >> 10));
        out += (wchar_t)(0xDC00 + (cp & 0x3FF));
    } else {
        out += (wchar_t)cp;
    }
}

} // namespace

// Notepad++ "Extended" escapes: \n \r \t \0 \\ \xHH \uHHHH \dNNN \oNNN \bBBBBBBBB
std::string UnescapeExtended(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    const size_t n = s.size();
    for (size_t i = 0; i < n; i++) {
        if (s[i] != L'\\' || i + 1 >= n) { out += s[i]; continue; }
        const wchar_t c = s[i + 1];
        auto number = [&](int digits, int base) -> bool {
            if (i + 2 + digits > n) return false;
            uint32_t v = 0;
            for (int k = 0; k < digits; k++) {
                int d = HexVal(s[i + 2 + k]);
                if (d < 0 || d >= base) return false;
                v = v * base + d;
            }
            AppendCodepoint(out, v);
            i += 1 + digits;
            return true;
        };
        switch (c) {
        case L'n': out += L'\n'; i++; break;
        case L'r': out += L'\r'; i++; break;
        case L't': out += L'\t'; i++; break;
        case L'0': out += L'\0'; i++; break;
        case L'\\': out += L'\\'; i++; break;
        case L'x': if (!number(2, 16)) { out += L'\\'; } break;
        case L'u': if (!number(4, 16)) { out += L'\\'; } break;
        case L'd': if (!number(3, 10)) { out += L'\\'; } break;
        case L'o': if (!number(3, 8)) { out += L'\\'; } break;
        case L'b': if (!number(8, 2)) { out += L'\\'; } break;
        default: out += L'\\'; break;
        }
    }
    return Narrow(out);
}

struct Searcher::Impl {
    const Sci* sci = nullptr;
    FindOptions opt;
    // Normal / Extended
    std::string literal, replacement;
    int sciFlags = 0;
    // Regex
    Regex re;
    std::wstring format;
    boost::match_flag_type matchFlags = boost::match_default;
    boost::match_flag_type formatFlags = boost::format_perl;
    boost::match_results<U8Iter> last;
    bool haveLast = false;
    const unsigned char* doc = nullptr;
    Sci_Position len = 0;

    void Refresh() {
        doc = (const unsigned char*)(*sci)(SCI_GETCHARACTERPOINTER);
        len = sci->Length();
    }

    boost::match_flag_type FlagsFor(Sci_Position from, Sci_Position to) const {
        boost::match_flag_type f = matchFlags;
        if (from > 0) f |= boost::match_prev_avail;
        if (to < len && doc[to] != '\r' && doc[to] != '\n') f |= boost::match_not_eol;
        return f;
    }

    Match RegexForward(Sci_Position from, Sci_Position to) {
        U8Iter first(doc, len, from), lastIt(doc, len, to), base(doc, len, 0);
        boost::match_results<U8Iter> m;
        if (!boost::regex_search(first, lastIt, m, re, FlagsFor(from, to), base)) return {};
        last = m;
        haveLast = true;
        return {m[0].first.Pos(), m[0].second.Pos()};
    }

    template <class F> size_t RegexEach(Sci_Position from, Sci_Position to, F&& f) {
        U8Iter first(doc, len, from), lastIt(doc, len, to);
        boost::regex_iterator<U8Iter, wchar_t, FastTraits> it(first, lastIt, re, FlagsFor(from, to)), end;
        size_t count = 0;
        for (; it != end; ++it) {
            last = *it;
            haveLast = true;
            count++;
            if (!f(Match{(*it)[0].first.Pos(), (*it)[0].second.Pos()})) break;
        }
        return count;
    }
};

Searcher::Searcher() : impl_(std::make_unique<Impl>()) {}
Searcher::~Searcher() = default;

bool Searcher::IsRegex() const { return impl_->opt.mode == SearchMode::Regex; }

bool Searcher::Prepare(const Sci& sci, const FindOptions& opt, std::wstring& error) {
    Impl& d = *impl_;
    d.sci = &sci;
    d.opt = opt;
    d.haveLast = false;
    error_.clear();
    error.clear();
    if (opt.what.empty()) { error = L""; return false; }
    switch (opt.mode) {
    case SearchMode::Normal:
        d.literal = Narrow(opt.what);
        d.replacement = Narrow(opt.with);
        break;
    case SearchMode::Extended:
        d.literal = UnescapeExtended(opt.what);
        d.replacement = UnescapeExtended(opt.with);
        if (d.literal.empty()) { error = L"Empty search"; return false; }
        break;
    case SearchMode::Regex: {
        boost::regex_constants::syntax_option_type flags = boost::regex_constants::perl;
        if (!opt.matchCase) flags |= boost::regex_constants::icase;
        try {
            d.re.assign(opt.what, flags);
        } catch (const std::exception& e) {
            error = L"Invalid regular expression: " + Widen(e.what());
            error_ = error;
            return false;
        }
        d.matchFlags = boost::match_default;
        if (!opt.dotNewline) d.matchFlags |= boost::match_not_dot_newline;
        d.format = opt.with;
        // Boost's extended format (conditionals like (?1yes:no)) treats parentheses as special,
        // so only enable it when the replacement actually uses a conditional.
        d.formatFlags = opt.with.find(L"(?") != std::wstring::npos ? boost::format_all : boost::format_perl;
        break;
    }
    }
    d.sciFlags = (opt.matchCase ? SCFIND_MATCHCASE : 0) | (opt.wholeWord ? SCFIND_WHOLEWORD : 0);
    return true;
}

Match Searcher::Forward(Sci_Position from, Sci_Position to) {
    Impl& d = *impl_;
    d.Refresh();
    from = std::clamp<Sci_Position>(from, 0, d.len);
    to = std::clamp<Sci_Position>(to, from, d.len);
    if (d.opt.mode != SearchMode::Regex) {
        const Sci& sci = *d.sci;
        sci(SCI_SETSEARCHFLAGS, d.sciFlags);
        sci(SCI_SETTARGETRANGE, from, to);
        Sci_Position p = (Sci_Position)sci(SCI_SEARCHINTARGET, d.literal.size(), (sptr_t)d.literal.data());
        if (p < 0) return {};
        return {p, (Sci_Position)sci(SCI_GETTARGETEND)};
    }
    try {
        return d.RegexForward(from, to);
    } catch (const std::exception& e) {
        error_ = Widen(e.what());
        return {};
    }
}

Match Searcher::Backward(Sci_Position from, Sci_Position to) {
    Impl& d = *impl_;
    d.Refresh();
    from = std::clamp<Sci_Position>(from, 0, d.len);
    to = std::clamp<Sci_Position>(to, from, d.len);
    if (d.opt.mode != SearchMode::Regex) {
        const Sci& sci = *d.sci;
        sci(SCI_SETSEARCHFLAGS, d.sciFlags);
        sci(SCI_SETTARGETRANGE, to, from); // start > end searches backwards
        Sci_Position p = (Sci_Position)sci(SCI_SEARCHINTARGET, d.literal.size(), (sptr_t)d.literal.data());
        if (p < 0) return {};
        return {p, (Sci_Position)sci(SCI_GETTARGETEND)};
    }
    // Boost cannot search backwards: scan growing windows that end at `to`,
    // starting at line boundaries, and keep the last match.
    try {
        Sci_Position window = 64 * 1024;
        for (;;) {
            Sci_Position start = std::max(from, to - window);
            if (start > from) start = std::max(from, d.sci->LineStart(d.sci->Line(start)));
            Match best;
            boost::match_results<U8Iter> bestM;
            d.RegexEach(start, to, [&](const Match& m) {
                if (m.start < to) {
                    best = m;
                    bestM = d.last;
                }
                return true;
            });
            if (best.Found()) {
                d.last = bestM;
                d.haveLast = true;
                return best;
            }
            if (start <= from) return {};
            window *= 4;
        }
    } catch (const std::exception& e) {
        error_ = Widen(e.what());
        return {};
    }
}

size_t Searcher::EachImpl(Sci_Position from, Sci_Position to, bool (*cb)(void*, const Match&), void* ctx) {
    Impl& d = *impl_;
    d.Refresh();
    from = std::clamp<Sci_Position>(from, 0, d.len);
    to = std::clamp<Sci_Position>(to, from, d.len);
    if (d.opt.mode != SearchMode::Regex) {
        const Sci& sci = *d.sci;
        sci(SCI_SETSEARCHFLAGS, d.sciFlags);
        size_t count = 0;
        Sci_Position pos = from;
        while (pos <= to) {
            sci(SCI_SETTARGETRANGE, pos, to);
            Sci_Position p = (Sci_Position)sci(SCI_SEARCHINTARGET, d.literal.size(), (sptr_t)d.literal.data());
            if (p < 0) break;
            Sci_Position e = (Sci_Position)sci(SCI_GETTARGETEND);
            count++;
            if (!cb(ctx, Match{p, e})) break;
            pos = e > p ? e : p + 1;
        }
        return count;
    }
    try {
        return d.RegexEach(from, to, [&](const Match& m) { return cb(ctx, m); });
    } catch (const std::exception& e) {
        error_ = Widen(e.what());
        return 0;
    }
}

std::string Searcher::Replacement() {
    Impl& d = *impl_;
    if (d.opt.mode != SearchMode::Regex) return d.replacement;
    if (!d.haveLast) return {};
    std::wstring out;
    try {
        d.last.format(std::back_inserter(out), d.format, d.formatFlags, d.re);
    } catch (const std::exception& e) {
        error_ = Widen(e.what());
        return {};
    }
    return Narrow(out);
}

} // namespace yan
