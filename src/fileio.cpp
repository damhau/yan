// Loading and saving files, encoding and line-ending detection.
#include "common.h"
#include "ILoader.h"

namespace yan {

const wchar_t* EncodingName(Encoding e) {
    switch (e) {
    case Encoding::Utf8: return L"UTF-8";
    case Encoding::Utf8Bom: return L"UTF-8 BOM";
    case Encoding::Utf16LE: return L"UTF-16 LE";
    case Encoding::Utf16BE: return L"UTF-16 BE";
    case Encoding::Ansi: return L"ANSI";
    }
    return L"";
}

static std::wstring LastErrorText(DWORD err) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring s = buf ? buf : L"Unknown error";
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
    return s;
}

static bool IsValidUtf8(const unsigned char* s, size_t n) {
    size_t i = 0;
    while (i < n) {
        // ASCII fast path, 8 bytes at a time.
        while (i + 8 <= n) {
            uint64_t v;
            memcpy(&v, s + i, 8);
            if (v & 0x8080808080808080ULL) break;
            i += 8;
        }
        if (i >= n) break;
        unsigned char c = s[i];
        if (c < 0x80) { i++; continue; }
        int len;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (int k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)))
            return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += len;
    }
    return true;
}

int DetectEol(const char* s, size_t n) {
    size_t crlf = 0, lf = 0, cr = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') lf++;
        else if (c == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') { crlf++; i++; }
            else cr++;
        } else continue;
        if (crlf + lf + cr > 10000) break; // plenty to decide
    }
    if (crlf == 0 && lf == 0 && cr == 0) return SC_EOL_CRLF;
    if (lf > crlf && lf >= cr) return SC_EOL_LF;
    if (cr > crlf && cr > lf) return SC_EOL_CR;
    return SC_EOL_CRLF;
}

static Encoding DetectEncoding(const unsigned char* d, size_t n, size_t& bomLen) {
    bomLen = 0;
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) { bomLen = 3; return Encoding::Utf8Bom; }
    if (n >= 2 && d[0] == 0xFF && d[1] == 0xFE) { bomLen = 2; return Encoding::Utf16LE; }
    if (n >= 2 && d[0] == 0xFE && d[1] == 0xFF) { bomLen = 2; return Encoding::Utf16BE; }
    // UTF-16 without BOM: look for a strong pattern of zero bytes on one side.
    if (n >= 4 && n % 2 == 0) {
        size_t sample = std::min<size_t>(n, 4096), zeroEven = 0, zeroOdd = 0;
        for (size_t i = 0; i < sample; i += 2) {
            if (d[i] == 0) zeroEven++;
            if (d[i + 1] == 0) zeroOdd++;
        }
        size_t pairs = sample / 2;
        if (zeroOdd * 10 > pairs * 4 && zeroEven * 20 < pairs) return Encoding::Utf16LE;
        if (zeroEven * 10 > pairs * 4 && zeroOdd * 20 < pairs) return Encoding::Utf16BE;
    }
    return IsValidUtf8(d, n) ? Encoding::Utf8 : Encoding::Ansi;
}

static std::string WideToUtf8Raw(const wchar_t* w, size_t n) {
    if (!n) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, (int)n, s.data(), len, nullptr, nullptr);
    return s;
}

bool GetFileTime(const std::wstring& path, FILETIME& ft) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    ft = fad.ftLastWriteTime;
    return true;
}

LoadResult LoadFile(const Sci& sci, const std::wstring& path, int forceEncoding) {
    LoadResult r;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        r.error = LastErrorText(GetLastError());
        return r;
    }
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    ::GetFileTime(f, nullptr, nullptr, &r.mtime);
    if (size.QuadPart > (1LL << 31) - 1024) {
        CloseHandle(f);
        r.error = L"The file is too large (over 2 GB).";
        return r;
    }
    size_t n = (size_t)size.QuadPart;
    HANDLE map = nullptr;
    const unsigned char* data = nullptr;
    if (n > 0) {
        map = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (map) data = (const unsigned char*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
        if (!data) {
            r.error = LastErrorText(GetLastError());
            if (map) CloseHandle(map);
            CloseHandle(f);
            return r;
        }
    }

    size_t bom = 0;
    Encoding enc = n ? DetectEncoding(data, n, bom) : Encoding::Utf8;
    if (forceEncoding >= 0) {
        enc = (Encoding)forceEncoding;
        bom = 0;
        if (enc == Encoding::Utf8Bom && n >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) bom = 3;
        if ((enc == Encoding::Utf16LE && n >= 2 && data[0] == 0xFF && data[1] == 0xFE) ||
            (enc == Encoding::Utf16BE && n >= 2 && data[0] == 0xFE && data[1] == 0xFF))
            bom = 2;
    }
    r.encoding = enc;

    std::string converted;
    const char* text = (const char*)data + bom;
    size_t len = n - bom;
    try {
        if (enc == Encoding::Utf16LE || enc == Encoding::Utf16BE) {
            size_t wn = len / 2;
            std::wstring w(wn, L'\0');
            memcpy(w.data(), text, wn * 2);
            if (enc == Encoding::Utf16BE)
                for (auto& c : w) c = (wchar_t)((c >> 8) | (c << 8));
            converted = WideToUtf8Raw(w.data(), w.size());
            text = converted.data();
            len = converted.size();
        } else if (enc == Encoding::Ansi && len) {
            int wn = MultiByteToWideChar(CP_ACP, 0, text, (int)len, nullptr, 0);
            std::wstring w(wn, L'\0');
            MultiByteToWideChar(CP_ACP, 0, text, (int)len, w.data(), wn);
            converted = WideToUtf8Raw(w.data(), w.size());
            text = converted.data();
            len = converted.size();
        }
    } catch (const std::bad_alloc&) {
        if (data) UnmapViewOfFile(data);
        if (map) CloseHandle(map);
        CloseHandle(f);
        r.error = L"Not enough memory to open the file.";
        return r;
    }

    r.eol = DetectEol(text, len);
    int options = SC_DOCUMENTOPTION_DEFAULT;
    if (len > 64u * 1024 * 1024) options |= SC_DOCUMENTOPTION_STYLES_NONE;
    auto* loader = (Scintilla::ILoader*)sci(SCI_CREATELOADER, len + 1, options);
    bool ok = loader != nullptr;
    if (loader) {
        constexpr size_t chunk = 16u * 1024 * 1024;
        for (size_t off = 0; off < len && ok; off += chunk)
            ok = loader->AddData(text + off, (Sci_Position)std::min(chunk, len - off)) == SC_STATUS_OK;
        if (ok) r.sciDoc = loader->ConvertToDocument();
        else loader->Release();
    }
    if (data) UnmapViewOfFile(data);
    if (map) CloseHandle(map);
    CloseHandle(f);
    if (!ok || !r.sciDoc) {
        r.error = L"Not enough memory to open the file.";
        return r;
    }
    r.ok = true;
    return r;
}

bool SaveFile(const Sci& sci, Doc& doc, const std::wstring& path, std::wstring& error, HWND owner) {
    const char* text = (const char*)sci(SCI_GETCHARACTERPOINTER);
    size_t len = (size_t)sci.Length();
    std::string out;
    const char* ptr = text;
    size_t outLen = len;
    const char* bom = "";
    size_t bomLen = 0;

    auto toWide = [&]() {
        std::wstring w;
        if (!len) return w;
        int wn = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, nullptr, 0);
        w.resize(wn);
        MultiByteToWideChar(CP_UTF8, 0, text, (int)len, w.data(), wn);
        return w;
    };

    switch (doc.encoding) {
    case Encoding::Utf8: break;
    case Encoding::Utf8Bom: bom = "\xEF\xBB\xBF"; bomLen = 3; break;
    case Encoding::Utf16LE:
    case Encoding::Utf16BE: {
        std::wstring w = toWide();
        if (doc.encoding == Encoding::Utf16BE) {
            for (auto& c : w) c = (wchar_t)((c >> 8) | (c << 8));
            bom = "\xFE\xFF";
        } else {
            bom = "\xFF\xFE";
        }
        bomLen = 2;
        out.assign((const char*)w.data(), w.size() * 2);
        ptr = out.data();
        outLen = out.size();
        break;
    }
    case Encoding::Ansi: {
        std::wstring w = toWide();
        BOOL lossy = FALSE;
        int an = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.data(), (int)w.size(), nullptr, 0, nullptr, &lossy);
        if (lossy) {
            int answer = MessageBoxW(owner,
                L"This document contains characters that cannot be saved as ANSI.\n\n"
                L"Save it as UTF-8 instead?\n\nYes: save as UTF-8\nNo: save as ANSI (characters will be lost)",
                L"Yan", MB_YESNOCANCEL | MB_ICONWARNING);
            if (answer == IDCANCEL) { error.clear(); return false; }
            if (answer == IDYES) {
                doc.encoding = Encoding::Utf8;
                return SaveFile(sci, doc, path, error, owner);
            }
        }
        out.resize(an);
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.data(), (int)w.size(), out.data(), an, nullptr, nullptr);
        ptr = out.data();
        outLen = out.size();
        break;
    }
    }

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        error = LastErrorText(err);
        if (err == ERROR_ACCESS_DENIED) error += L"\n\nYou may need to run Yan as administrator to save this file.";
        return false;
    }
    bool ok = true;
    DWORD written = 0;
    if (bomLen) ok = WriteFile(f, bom, (DWORD)bomLen, &written, nullptr);
    size_t off = 0;
    while (ok && off < outLen) {
        DWORD chunk = (DWORD)std::min<size_t>(outLen - off, 64u * 1024 * 1024);
        ok = WriteFile(f, ptr + off, chunk, &written, nullptr) && written == chunk;
        off += chunk;
    }
    if (ok) ok = SetEndOfFile(f);
    if (!ok) error = LastErrorText(GetLastError());
    ::GetFileTime(f, nullptr, nullptr, &doc.mtime);
    CloseHandle(f);
    return ok;
}

} // namespace yan
