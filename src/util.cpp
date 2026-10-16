#include "common.h"
#include <algorithm>

namespace yan {

std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring DirOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring FullPath(const std::wstring& path, const std::wstring& cwd) {
    std::wstring p = path;
    bool absolute = p.size() >= 2 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'));
    if (!absolute && !cwd.empty()) p = cwd + L"\\" + p;
    wchar_t buf[MAX_PATH];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH, buf, nullptr);
    if (n == 0) return p;
    if (n < MAX_PATH) return buf;
    std::wstring big(n, L'\0');
    n = GetFullPathNameW(p.c_str(), n, big.data(), nullptr);
    big.resize(n);
    return big;
}

void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(h), text.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

// ---------------------------------------------------------------------------
std::string Sci::Range(Sci_Position a, Sci_Position b) const {
    if (b <= a) return {};
    // Buffer must hold the terminating NUL as well.
    std::string s(size_t(b - a) + 1, '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = a;
    tr.chrg.cpMax = b;
    tr.lpstrText = s.data();
    (*this)(SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
    s.resize(size_t(b - a));
    return s;
}

std::string Sci::SelText() const {
    sptr_t n = (*this)(SCI_GETSELTEXT, 0, 0);
    std::string s(size_t(n) + 1, '\0');
    (*this)(SCI_GETSELTEXT, 0, (sptr_t)s.data());
    s.resize(strlen(s.c_str()));
    return s;
}

void Sci::Replace(Sci_Position a, Sci_Position b, std::string_view text) const {
    (*this)(SCI_SETTARGETRANGE, a, b);
    (*this)(SCI_REPLACETARGET, text.size(), (sptr_t)text.data());
}

// ---------------------------------------------------------------------------
void Settings::Load(const std::wstring& path) {
    path_ = path;
    items_.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    std::string raw(size_t(size.QuadPart), '\0');
    DWORD read = 0;
    if (!raw.empty()) ReadFile(f, raw.data(), (DWORD)raw.size(), &read, nullptr);
    CloseHandle(f);
    raw.resize(read);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw.erase(0, 3);
    std::wstring text = Widen(raw);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L';' || line[0] == L'#' || line[0] == L'[') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        items_.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
}

void Settings::Save() const {
    if (path_.empty()) return;
    std::wstring dir = DirOf(path_);
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring text = L"; Yan settings\r\n";
    for (auto& [k, v] : items_) text += k + L"=" + v + L"\r\n";
    std::string utf8 = Narrow(text);
    HANDLE f = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(f);
}

std::wstring Settings::Get(const std::wstring& key, const std::wstring& def) const {
    for (auto& [k, v] : items_)
        if (k == key) return v;
    return def;
}

int Settings::GetInt(const std::wstring& key, int def) const {
    std::wstring v = Get(key);
    if (v.empty()) return def;
    return _wtoi(v.c_str());
}

void Settings::Set(const std::wstring& key, const std::wstring& value) {
    for (auto& [k, v] : items_)
        if (k == key) { v = value; return; }
    items_.emplace_back(key, value);
}

void Settings::SetInt(const std::wstring& key, int value) { Set(key, std::to_wstring(value)); }

std::vector<std::wstring> Settings::GetList(const std::wstring& prefix) const {
    std::vector<std::wstring> out;
    for (int i = 0;; i++) {
        std::wstring key = prefix + std::to_wstring(i);
        bool found = false;
        for (auto& [k, v] : items_)
            if (k == key) { out.push_back(v); found = true; break; }
        if (!found) break;
    }
    return out;
}

void Settings::SetList(const std::wstring& prefix, const std::vector<std::wstring>& values) {
    std::erase_if(items_, [&](auto& kv) {
        return kv.first.size() > prefix.size() && kv.first.compare(0, prefix.size(), prefix) == 0 &&
               iswdigit(kv.first[prefix.size()]);
    });
    for (size_t i = 0; i < values.size(); i++) items_.emplace_back(prefix + std::to_wstring(i), values[i]);
}

} // namespace yan
