# Yan

A fast, portable text editor for Windows inspired by Notepad++, focused on
find/replace and column editing. One self-contained `yan.exe` (~1.7 MB, no DLLs,
no installer, no runtime), fully painted 50–70 ms after launch (a 100 MB file: ~150 ms).

Built with C++20 on raw Win32, [Scintilla](https://www.scintilla.org/) and
[Lexilla](https://www.scintilla.org/Lexilla.html) (the editing component and syntax
lexers Notepad++ uses, linked statically) and
[Boost.Regex](https://github.com/boostorg/regex) (header-only, Perl syntax like Notepad++).

## Features

**Find / Replace** (docked panel: `Ctrl+F`, `Ctrl+H`)
- Modes: Normal, Extended (`\n \r \t \0 \\ \xHH \uHHHH \dNNN \oNNN \bBBBBBBBB`), Regex
- Regex: Perl syntax, lookahead/lookbehind, backreferences, named groups, `. matches newline`
- Replacement: `$1`, `${name}`, `$&`, `\n`, `\t`, case conversion `\U \L \E \u \l`,
  conditionals `(?1yes:no)`
- Match case, whole word, wrap around, in selection (scope stays highlighted)
- Live highlight of matches while typing, "3 of 12" counter, search history (Up/Down)
- Count, Mark all (+ bookmark lines), Select all matches as multi-carets (`Alt+Enter`),
  Replace, Replace all (one undo step; fast on huge files)

**Column / multi-caret editing**
- `Alt+drag` or `Alt+Shift+arrows` for column selection, with virtual space
- `Ctrl+click` adds carets; typing and pasting go to every caret
- `Ctrl+Shift+D` add next occurrence, `Alt+F3` select all occurrences
- Column editor (`Alt+C`): insert text or number sequences (dec/hex/oct/bin,
  step, repeat, zero/space padding) into a column selection or from the caret down

**Syntax highlighting** (Language menu, or click the language in the status bar)
- Batch, C, C++, C#, CMake, CSS, Diff, Go, HTML (with embedded JavaScript), INI /
  properties / TOML, Java, JavaScript, JSON, Lua, Makefile, Markdown, PowerShell,
  Python, Rust, Shell (bash/zsh), SQL, TypeScript, XML, YAML
- Picked from the file name or extension (`Makefile`, `CMakeLists.txt`, `.bashrc`...),
  else from a shebang (`#!/usr/bin/env python3`) or `<?xml` for unknown extensions
- Light and dark colors for every language; the Language menu overrides the choice
- Lexers are created lazily, only when a document is shown: no cost at startup
- Code folding (box tree margin, like Notepad++): `Alt+0` fold all, `Alt+Shift+0` unfold all,
  `Ctrl+Alt+F` toggle the current block; folds are kept when switching tabs
- Files over 64 MB are loaded without style storage and stay plain text

**Editor**
- Tabs (drag to reorder, middle-click to close, `Ctrl+Tab`, reopen closed `Ctrl+Shift+T`)
- Session restore (tabs load lazily), single instance (files open in the running window)
- Recent files (File > Recent files): the last 15 files opened or saved, open all, clear
- Save all to folder (File menu): saves every open document without asking. Files that
  already have a path are saved in place; untitled tabs go to the chosen folder as
  `new 1.txt`, `new 2.txt`... (`new 1 (2).txt` if the name is taken; empty tabs are skipped)
- Encodings: UTF-8, UTF-8 BOM, UTF-16 LE/BE, ANSI — detection, conversion, reopen-as
- Line endings CRLF/LF/CR detection and conversion
- Bookmarks (`Ctrl+F2`, `F2`) with copy/cut/remove (un)bookmarked lines
- Line operations: sort, sort ignoring case, reverse, remove duplicates / empty lines,
  trim trailing whitespace, join, move, duplicate
- Smart highlight of the selected word, brace matching, change-history margin
- Light / dark theme following Windows, dark title bar and menus, per-monitor DPI
- Detects files changed on disk, drag & drop files

## Build

Requires Visual Studio 2022 Build Tools (C++ workload, includes CMake and Ninja):

```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Then:

```
build.cmd            # Release -> build\Release\yan.exe
build.cmd Debug      # Debug   -> build\Debug\yan.exe
build\Release\yan_tests.exe          # engine tests
build\Release\yan_tests.exe --perf   # search benchmarks on 50 MB
```

## Usage

```
yan.exe [files...] [-n<line>] [--new-window]
```

- `-n42` jumps to line 42, `--new-window` ignores the running instance.
- `yan.exe --bench` prints nothing but exits with the startup time in ms (process
  creation → window fully painted) as its exit code.

**Settings** (View > Settings, `Ctrl+,`) apply at once and are saved to `yan.ini` immediately:

- General: reopen the files that were open, close Yan with the last tab, number of recent
  files to remember, open files in the same window, detect files changed by other programs
  (and reload them silently when unmodified), default folder for Save all to folder
- Editor: tab width, spaces instead of tabs, auto-indent, trim trailing whitespace on save,
  syntax highlighting, current line / selected word / matching brace highlighting
- Display: theme, font, DirectWrite, line numbers, code folding, word wrap, whitespace,
  EOL markers, indent guides, scrolling beyond the last line
- New documents: line endings (CRLF / LF / CR) and encoding (UTF-8 with or without BOM)

Settings live in `%APPDATA%\Yan\yan.ini`. For a portable setup, create an empty
`yan.ini` next to `yan.exe` and it will be used instead.

Text is drawn with GDI by default: DirectWrite (View menu) looks slightly different
and supports color emoji, but costs ~130 ms at startup for Direct2D/GPU setup.

## Layout

```
src/main.cpp       app, window, menus, commands, documents, single instance
src/panel.cpp      find/replace, go to line, column editor panel
src/search.cpp     search engine (Scintilla search + Boost.Regex over UTF-8)
src/editor.cpp     Scintilla setup, highlighting, line/bookmark ops, column insert
src/language.cpp   languages: detection, Lexilla lexers, keywords, style colors
src/fileio.cpp     loading/saving, encoding and EOL detection
src/tabbar.cpp     tab strip          src/statusbar.cpp  status bar
src/theme.cpp      colors, fonts, dark mode, menu bar painting
src/settings.cpp   settings window (custom-drawn, follows the theme)
tests/tests.cpp    tests and benchmarks (hidden Scintilla window)
third_party/       Scintilla 5.6.7, Lexilla 5.5.4, Boost.Regex 1.91 (unmodified;
                   only the lexers in CMakeLists.txt LEX_LEXERS are compiled)
```
