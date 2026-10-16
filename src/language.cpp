// Languages: detection from the file name, Lexilla lexers and their colors.
// Lexers are linked statically and only created when a document is shown.
#include "common.h"
#include <span>
#include "ILexer.h"
#include "SciLexer.h"
#include "LexerModule.h"

extern const Lexilla::LexerModule lmBatch, lmBash, lmCPP, lmCmake, lmCss, lmDiff, lmHTML, lmXML, lmProps, lmJSON,
    lmLua, lmMake, lmMarkdown, lmPowerShell, lmPython, lmRust, lmSQL, lmYAML;

namespace yan {
namespace {

struct StyleRole { int style; Syn role; };
struct Prop { const char* key; const char* value; };

struct LangDef {
    Lang lang;
    const wchar_t* name;
    const Lexilla::LexerModule* module; // nullptr: plain text
    std::span<const StyleRole> styles;
    std::span<const char* const> words; // keyword lists by index, nullptr entries skipped
    std::span<const Prop> props;
};

// ---- Keywords ---------------------------------------------------------------
constexpr const char* kCWords[] = {
    "auto break case const continue default do else enum extern for goto if inline register restrict return sizeof "
    "static struct switch typedef union volatile while _Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert "
    "_Thread_local alignas alignof constexpr false nullptr static_assert thread_local true typeof typeof_unqual",
    "bool char double float int long short signed unsigned void _Bool _Complex size_t ssize_t ptrdiff_t intptr_t "
    "uintptr_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t wchar_t FILE NULL",
};
constexpr const char* kCppWords[] = {
    "alignas alignof and and_eq asm auto bitand bitor break case catch class compl concept const consteval "
    "constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast "
    "else enum explicit export extern false final for friend goto if import inline module mutable namespace new "
    "noexcept not not_eq nullptr operator or or_eq override private protected public register reinterpret_cast "
    "requires return sizeof static static_assert static_cast struct switch template this thread_local throw true "
    "try typedef typeid typename union using virtual volatile while xor xor_eq",
    "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t "
    "ssize_t ptrdiff_t intptr_t uintptr_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
    "std string string_view vector map unordered_map set array unique_ptr shared_ptr optional NULL",
};
constexpr const char* kCSharpWords[] = {
    "abstract as base break case catch checked class const continue default delegate do else enum event explicit "
    "extern false finally fixed for foreach goto if implicit in interface internal is lock namespace new null "
    "operator out override params private protected public readonly ref return sealed sizeof stackalloc static "
    "struct switch this throw true try typeof unchecked unsafe using virtual volatile while add alias and "
    "ascending async await by descending equals from get global group init into join let managed nameof not "
    "notnull on or orderby partial record remove required scoped select set unmanaged value var when where with yield",
    "bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint ulong ushort void "
    "String Object Task List Dictionary IEnumerable Exception Console Math",
};
constexpr const char* kJavaWords[] = {
    "abstract assert break case catch class const continue default do else enum extends final finally for goto if "
    "implements import instanceof interface native new non-sealed package permits private protected public record "
    "return sealed static strictfp super switch synchronized this throw throws transient try var volatile while "
    "yield true false null",
    "boolean byte char double float int long short void String Object Integer Long Double Boolean Character List "
    "Map Set ArrayList HashMap Exception System Math",
};
#define JS_KEYWORDS                                                                                              \
    "async await break case catch class const continue debugger default delete do else export extends false "    \
    "finally for from function get if import in instanceof let new null of return set static super switch this " \
    "throw true try typeof undefined var void while with yield"
#define JS_GLOBALS                                                                                              \
    "Array ArrayBuffer BigInt Boolean Date Error Function Intl JSON Map Math Number Object Promise Proxy Reflect " \
    "RegExp Set String Symbol WeakMap WeakSet console document globalThis window process require module exports"
constexpr const char* kJsWords[] = {JS_KEYWORDS, JS_GLOBALS};
constexpr const char* kTsWords[] = {
    JS_KEYWORDS " abstract accessor as asserts declare enum implements infer interface is keyof namespace "
                "override private protected public readonly satisfies type unique",
    JS_GLOBALS " any bigint boolean never number object string symbol unknown Record Partial Readonly Pick Omit",
};
constexpr const char* kGoWords[] = {
    "break case chan const continue default defer else fallthrough for func go goto if import interface map "
    "package range return select struct switch type var true false nil iota",
    "any bool byte comparable complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string "
    "uint uint8 uint16 uint32 uint64 uintptr append cap clear close complex copy delete imag len make max min new "
    "panic print println real recover",
};
constexpr const char* kJsonWords[] = {"false true null"};
constexpr const char* kPythonWords[] = {
    "False None True and as assert async await break case class continue def del elif else except finally for "
    "from global if import in is lambda match nonlocal not or pass raise return try type while with yield",
    "abs aiter all anext any ascii bin bool breakpoint bytearray bytes callable chr classmethod compile complex "
    "delattr dict dir divmod enumerate eval exec filter float format frozenset getattr globals hasattr hash help "
    "hex id input int isinstance issubclass iter len list locals map max memoryview min next object oct open ord "
    "pow print property range repr reversed round set setattr slice sorted staticmethod str sum super tuple vars "
    "zip self cls __name__ __init__ Exception ValueError TypeError KeyError IndexError RuntimeError OSError",
};
// HTML / XML: list 0 would hold known tags/attributes; unknown ones get the same color here.
// List 5 is needed or <!DOCTYPE> shows as an error.
#define SGML_KEYWORDS \
    "ELEMENT DOCTYPE doctype ATTLIST ENTITY PCDATA CDATA EMPTY SHORTREF USEMAP NOTATION IMPLIED NDATA SYSTEM PUBLIC"
constexpr const char* kHtmlWords[] = {nullptr, JS_KEYWORDS, nullptr, nullptr, nullptr, SGML_KEYWORDS};
constexpr const char* kXmlWords[] = {nullptr, nullptr, nullptr, nullptr, nullptr, SGML_KEYWORDS};
constexpr const char* kSqlWords[] = {
    "add all alter and any as asc begin between by case cascade check column commit constraint create cross "
    "database declare default delete desc distinct drop else end escape except exec execute exists fetch foreign "
    "from full function go grant group having if in index inner insert intersect into is join key left like limit "
    "merge natural not null of offset on or order outer over partition primary procedure references replace "
    "returning returns revoke right rollback row rows schema select set table then to top transaction trigger "
    "truncate union unique update use using values view when where while with",
    "bigint binary bit blob bool boolean char date datetime datetime2 decimal double float int integer interval "
    "json jsonb money nchar numeric nvarchar real serial smallint text time timestamp tinyint uuid "
    "uniqueidentifier varbinary varchar xml avg cast coalesce concat convert count isnull length lower max min "
    "now nullif round substring sum trim upper",
};
constexpr const char* kYamlWords[] = {"true false yes no null on off True False Yes No Null TRUE FALSE NULL"};
constexpr const char* kBatchWords[] = {
    "assoc break call cd chdir cls color copy date defined del dir do echo else endlocal equ erase errorlevel exist "
    "exit for ftype geq goto gtr if in leq lss md mkdir mklink move neq not nul path pause popd prompt pushd rd rem "
    "ren rename rmdir set setlocal shift start time title type ver verify vol enableextensions "
    "enabledelayedexpansion",
    "attrib choice cmd findstr find ipconfig msbuild net ping powershell reg robocopy sc taskkill tasklist timeout "
    "where xcopy",
};
constexpr const char* kPowerShellWords[] = {
    "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for "
    "foreach from function hidden if in param process return static switch throw trap try until using var while",
    "add-content add-member add-type clear-host compare-object convertfrom-json convertto-json copy-item "
    "export-csv foreach-object format-list format-table get-childitem get-command get-content get-date get-help "
    "get-item get-location get-member get-process get-service import-csv import-module invoke-command "
    "invoke-expression invoke-restmethod invoke-webrequest join-path measure-object move-item new-item new-object "
    "out-file out-null out-string read-host remove-item rename-item resolve-path select-object select-string "
    "set-content set-item set-location set-strictmode sort-object split-path start-process start-sleep test-path "
    "where-object write-debug write-error write-host write-output write-progress write-verbose write-warning",
    "cat cd chdir clear cls copy cp del dir echo erase gc gci gcm gi gm gps gsv iex irm iwr kill ls man md mv "
    "popd ps pushd pwd rd ren rm rmdir select sleep sort tee type where",
};
constexpr const char* kBashWords[] = {
    "if then else elif fi case esac for select while until do done in function time coproc return exit break "
    "continue local declare typeset readonly export unset shift source alias unalias echo printf read cd pwd "
    "test eval exec set trap wait true false let shopt type command builtin getopts kill ulimit umask "
    "mkdir rm cp mv ls cat grep sed awk find xargs sort uniq head tail chmod chown curl wget tar git sudo",
};
constexpr const char* kLuaWords[] = {
    "and break do else elseif end false for function goto if in local nil not or repeat return then true until while",
    "assert collectgarbage dofile error getmetatable ipairs load loadfile next pairs pcall print rawequal rawget "
    "rawlen rawset require select setmetatable tonumber tostring type xpcall _G _VERSION",
    "string.byte string.char string.find string.format string.gmatch string.gsub string.len string.lower "
    "string.match string.rep string.reverse string.sub string.upper table.concat table.insert table.remove "
    "table.sort table.unpack math.abs math.ceil math.floor math.huge math.max math.min math.pi math.random math.sqrt",
    "io.open io.read io.write io.lines os.clock os.date os.exit os.getenv os.time coroutine.create "
    "coroutine.resume coroutine.status coroutine.wrap coroutine.yield",
};
constexpr const char* kRustWords[] = {
    "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod "
    "move mut pub ref return self Self static struct super trait true type union unsafe use where while",
    "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec Option Result Box Rc "
    "Arc HashMap Some None Ok Err",
};
constexpr const char* kCMakeWords[] = {
    "add_compile_definitions add_compile_options add_custom_command add_custom_target add_definitions "
    "add_dependencies add_executable add_library add_link_options add_subdirectory add_test block "
    "cmake_minimum_required cmake_parse_arguments cmake_policy configure_file else elseif enable_language "
    "enable_testing endblock endforeach endfunction endif endmacro endwhile execute_process fetchcontent_declare "
    "fetchcontent_makeavailable file find_library find_package find_path find_program foreach function "
    "get_filename_component get_property get_target_property if include include_directories install "
    "link_directories link_libraries list macro math message option project return set set_property "
    "set_target_properties string target_compile_definitions target_compile_features target_compile_options "
    "target_include_directories target_link_directories target_link_libraries target_link_options "
    "target_precompile_headers target_sources unset while",
    "AFTER ALIAS ALL AND APPEND BEFORE BOOL CACHE COMMAND COMPONENTS CONFIG DEFINED DEPENDS DESTINATION DIRECTORY "
    "EQUAL EXCLUDE EXCLUDE_FROM_ALL EXISTS FALSE FATAL_ERROR FILES FILTER FORCE GLOB GLOB_RECURSE GREATER IMPORTED "
    "INCLUDE INTERFACE LANGUAGES LESS MATCHES MODULE NAME NOT OBJECT OFF ON OPTIONAL OR PARENT_SCOPE PATH PATHS "
    "PRIVATE PROPERTIES PUBLIC REGEX REMOVE_ITEM REQUIRED SHARED SOURCES STATIC STATUS STREQUAL STRING TARGET "
    "TARGETS TRUE VERSION VERSION_GREATER VERSION_LESS WARNING WIN32 WORKING_DIRECTORY",
};

// ---- Lexer properties ---------------------------------------------------------
constexpr Prop kCProps[] = {{"lexer.cpp.track.preprocessor", "0"}, {"lexer.cpp.escape.sequence", "1"}};
constexpr Prop kJavaProps[] = {{"lexer.cpp.enable.preprocessor", "0"}, {"lexer.cpp.escape.sequence", "1"}};
constexpr Prop kJsProps[] = {{"lexer.cpp.enable.preprocessor", "0"}, {"lexer.cpp.escape.sequence", "1"},
                             {"lexer.cpp.backquoted.strings", "2"}};
constexpr Prop kGoProps[] = {{"lexer.cpp.enable.preprocessor", "0"}, {"lexer.cpp.escape.sequence", "1"},
                             {"lexer.cpp.backquoted.strings", "1"}};
constexpr Prop kJsonProps[] = {{"lexer.json.allow.comments", "1"}, {"lexer.json.escape.sequence", "1"}};
constexpr Prop kPythonProps[] = {{"lexer.python.keywords2.no.sub.identifiers", "1"}};
constexpr Prop kSqlProps[] = {{"lexer.sql.backticks.identifier", "1"}, {"lexer.sql.numbersign.comment", "1"}};
constexpr Prop kMarkdownProps[] = {{"lexer.markdown.header.eolfill", "1"}};

// ---- Styles ---------------------------------------------------------------------
using S = Syn;
constexpr StyleRole kCStyles[] = {
    {SCE_C_COMMENT, S::Comment}, {SCE_C_COMMENTLINE, S::Comment}, {SCE_C_PREPROCESSORCOMMENT, S::Comment},
    {SCE_C_PREPROCESSORCOMMENTDOC, S::CommentDoc}, {SCE_C_COMMENTDOC, S::CommentDoc},
    {SCE_C_COMMENTLINEDOC, S::CommentDoc}, {SCE_C_COMMENTDOCKEYWORD, S::Preproc},
    {SCE_C_COMMENTDOCKEYWORDERROR, S::CommentDoc}, {SCE_C_TASKMARKER, S::Preproc},
    {SCE_C_NUMBER, S::Number}, {SCE_C_UUID, S::Number}, {SCE_C_USERLITERAL, S::Number},
    {SCE_C_WORD, S::Keyword}, {SCE_C_WORD2, S::Type}, {SCE_C_GLOBALCLASS, S::Type},
    {SCE_C_STRING, S::String}, {SCE_C_CHARACTER, S::String}, {SCE_C_VERBATIM, S::String},
    {SCE_C_STRINGRAW, S::String}, {SCE_C_TRIPLEVERBATIM, S::String}, {SCE_C_HASHQUOTEDSTRING, S::String},
    {SCE_C_STRINGEOL, S::String}, {SCE_C_ESCAPESEQUENCE, S::Regex}, {SCE_C_REGEX, S::Regex},
    {SCE_C_PREPROCESSOR, S::Preproc}, {SCE_C_OPERATOR, S::Operator},
};
constexpr StyleRole kJsonStyles[] = {
    {SCE_JSON_NUMBER, S::Number}, {SCE_JSON_STRING, S::String}, {SCE_JSON_STRINGEOL, S::Error},
    {SCE_JSON_PROPERTYNAME, S::Key}, {SCE_JSON_ESCAPESEQUENCE, S::Regex}, {SCE_JSON_LINECOMMENT, S::Comment},
    {SCE_JSON_BLOCKCOMMENT, S::Comment}, {SCE_JSON_OPERATOR, S::Operator}, {SCE_JSON_URI, S::Link},
    {SCE_JSON_COMPACTIRI, S::Link}, {SCE_JSON_KEYWORD, S::Keyword}, {SCE_JSON_LDKEYWORD, S::Preproc},
    {SCE_JSON_ERROR, S::Error},
};
constexpr StyleRole kPythonStyles[] = {
    {SCE_P_COMMENTLINE, S::Comment}, {SCE_P_COMMENTBLOCK, S::Comment}, {SCE_P_NUMBER, S::Number},
    {SCE_P_STRING, S::String}, {SCE_P_CHARACTER, S::String}, {SCE_P_TRIPLE, S::String},
    {SCE_P_TRIPLEDOUBLE, S::String}, {SCE_P_FSTRING, S::String}, {SCE_P_FCHARACTER, S::String},
    {SCE_P_FTRIPLE, S::String}, {SCE_P_FTRIPLEDOUBLE, S::String}, {SCE_P_STRINGEOL, S::String},
    {SCE_P_WORD, S::Keyword}, {SCE_P_WORD2, S::Type}, {SCE_P_CLASSNAME, S::Type}, {SCE_P_DEFNAME, S::Function},
    {SCE_P_OPERATOR, S::Operator}, {SCE_P_DECORATOR, S::Preproc},
};
constexpr StyleRole kHtmlStyles[] = {
    {SCE_H_TAG, S::Tag}, {SCE_H_TAGUNKNOWN, S::Tag}, {SCE_H_TAGEND, S::Tag}, {SCE_H_SCRIPT, S::Tag},
    {SCE_H_OTHER, S::Tag}, {SCE_H_ATTRIBUTE, S::Attribute}, {SCE_H_ATTRIBUTEUNKNOWN, S::Attribute},
    {SCE_H_NUMBER, S::Number}, {SCE_H_DOUBLESTRING, S::String}, {SCE_H_SINGLESTRING, S::String},
    {SCE_H_VALUE, S::String}, {SCE_H_CDATA, S::String}, {SCE_H_COMMENT, S::Comment}, {SCE_H_XCCOMMENT, S::Comment},
    {SCE_H_ENTITY, S::Entity}, {SCE_H_XMLSTART, S::Preproc}, {SCE_H_XMLEND, S::Preproc},
    {SCE_H_QUESTION, S::Preproc}, {SCE_H_ASP, S::Preproc}, {SCE_H_ASPAT, S::Preproc},
    {SCE_H_SGML_DEFAULT, S::Preproc}, {SCE_H_SGML_COMMAND, S::Preproc}, {SCE_H_SGML_1ST_PARAM, S::Preproc},
    {SCE_H_SGML_DOUBLESTRING, S::String}, {SCE_H_SGML_SIMPLESTRING, S::String}, {SCE_H_SGML_ERROR, S::Error},
    {SCE_H_SGML_SPECIAL, S::Preproc}, {SCE_H_SGML_ENTITY, S::Entity}, {SCE_H_SGML_COMMENT, S::Comment},
    {SCE_H_SGML_1ST_PARAM_COMMENT, S::Comment}, {SCE_H_SGML_BLOCK_DEFAULT, S::Preproc},
    // Embedded JavaScript
    {SCE_HJ_COMMENT, S::Comment}, {SCE_HJ_COMMENTLINE, S::Comment}, {SCE_HJ_COMMENTDOC, S::CommentDoc},
    {SCE_HJ_NUMBER, S::Number}, {SCE_HJ_KEYWORD, S::Keyword}, {SCE_HJ_DOUBLESTRING, S::String},
    {SCE_HJ_SINGLESTRING, S::String}, {SCE_HJ_TEMPLATELITERAL, S::String}, {SCE_HJ_STRINGEOL, S::String},
    {SCE_HJ_SYMBOLS, S::Operator}, {SCE_HJ_REGEX, S::Regex},
    {SCE_HJA_COMMENT, S::Comment}, {SCE_HJA_COMMENTLINE, S::Comment}, {SCE_HJA_COMMENTDOC, S::CommentDoc},
    {SCE_HJA_NUMBER, S::Number}, {SCE_HJA_KEYWORD, S::Keyword}, {SCE_HJA_DOUBLESTRING, S::String},
    {SCE_HJA_SINGLESTRING, S::String}, {SCE_HJA_TEMPLATELITERAL, S::String}, {SCE_HJA_STRINGEOL, S::String},
    {SCE_HJA_SYMBOLS, S::Operator}, {SCE_HJA_REGEX, S::Regex},
};
constexpr StyleRole kCssStyles[] = {
    {SCE_CSS_TAG, S::Tag}, {SCE_CSS_CLASS, S::Function}, {SCE_CSS_ID, S::Function},
    {SCE_CSS_PSEUDOCLASS, S::Type}, {SCE_CSS_UNKNOWN_PSEUDOCLASS, S::Type}, {SCE_CSS_PSEUDOELEMENT, S::Type},
    {SCE_CSS_EXTENDED_PSEUDOCLASS, S::Type}, {SCE_CSS_EXTENDED_PSEUDOELEMENT, S::Type},
    {SCE_CSS_OPERATOR, S::Operator}, {SCE_CSS_IDENTIFIER, S::Key}, {SCE_CSS_IDENTIFIER2, S::Key},
    {SCE_CSS_IDENTIFIER3, S::Key}, {SCE_CSS_EXTENDED_IDENTIFIER, S::Key}, {SCE_CSS_UNKNOWN_IDENTIFIER, S::Key},
    {SCE_CSS_VALUE, S::String}, {SCE_CSS_COMMENT, S::Comment}, {SCE_CSS_IMPORTANT, S::Keyword},
    {SCE_CSS_DIRECTIVE, S::Preproc}, {SCE_CSS_GROUP_RULE, S::Preproc}, {SCE_CSS_DOUBLESTRING, S::String},
    {SCE_CSS_SINGLESTRING, S::String}, {SCE_CSS_ATTRIBUTE, S::Attribute}, {SCE_CSS_VARIABLE, S::Variable},
};
constexpr StyleRole kSqlStyles[] = {
    {SCE_SQL_COMMENT, S::Comment}, {SCE_SQL_COMMENTLINE, S::Comment}, {SCE_SQL_SQLPLUS_COMMENT, S::Comment},
    {SCE_SQL_COMMENTDOC, S::CommentDoc}, {SCE_SQL_COMMENTLINEDOC, S::CommentDoc},
    {SCE_SQL_COMMENTDOCKEYWORD, S::Preproc}, {SCE_SQL_COMMENTDOCKEYWORDERROR, S::CommentDoc},
    {SCE_SQL_NUMBER, S::Number}, {SCE_SQL_WORD, S::Keyword}, {SCE_SQL_WORD2, S::Type}, {SCE_SQL_USER1, S::Function},
    {SCE_SQL_STRING, S::String}, {SCE_SQL_CHARACTER, S::String}, {SCE_SQL_OPERATOR, S::Operator},
    {SCE_SQL_QOPERATOR, S::Operator}, {SCE_SQL_QUOTEDIDENTIFIER, S::Variable}, {SCE_SQL_SQLPLUS, S::Preproc},
    {SCE_SQL_SQLPLUS_PROMPT, S::Preproc},
};
constexpr StyleRole kMarkdownStyles[] = {
    {SCE_MARKDOWN_HEADER1, S::Heading}, {SCE_MARKDOWN_HEADER2, S::Heading}, {SCE_MARKDOWN_HEADER3, S::Heading},
    {SCE_MARKDOWN_HEADER4, S::Heading}, {SCE_MARKDOWN_HEADER5, S::Heading}, {SCE_MARKDOWN_HEADER6, S::Heading},
    {SCE_MARKDOWN_STRONG1, S::Strong}, {SCE_MARKDOWN_STRONG2, S::Strong}, {SCE_MARKDOWN_EM1, S::Emphasis},
    {SCE_MARKDOWN_EM2, S::Emphasis}, {SCE_MARKDOWN_ULIST_ITEM, S::Keyword}, {SCE_MARKDOWN_OLIST_ITEM, S::Keyword},
    {SCE_MARKDOWN_BLOCKQUOTE, S::Comment}, {SCE_MARKDOWN_STRIKEOUT, S::Comment}, {SCE_MARKDOWN_HRULE, S::Preproc},
    {SCE_MARKDOWN_LINK, S::Link}, {SCE_MARKDOWN_CODE, S::String}, {SCE_MARKDOWN_CODE2, S::String},
    {SCE_MARKDOWN_CODEBK, S::String},
};
constexpr StyleRole kYamlStyles[] = {
    {SCE_YAML_COMMENT, S::Comment}, {SCE_YAML_IDENTIFIER, S::Key}, {SCE_YAML_KEYWORD, S::Keyword},
    {SCE_YAML_NUMBER, S::Number}, {SCE_YAML_REFERENCE, S::Variable}, {SCE_YAML_DOCUMENT, S::Preproc},
    {SCE_YAML_TEXT, S::String}, {SCE_YAML_ERROR, S::Error}, {SCE_YAML_OPERATOR, S::Operator},
};
constexpr StyleRole kPropsStyles[] = {
    {SCE_PROPS_COMMENT, S::Comment}, {SCE_PROPS_SECTION, S::Heading}, {SCE_PROPS_ASSIGNMENT, S::Operator},
    {SCE_PROPS_DEFVAL, S::Preproc}, {SCE_PROPS_KEY, S::Key},
};
constexpr StyleRole kBatchStyles[] = {
    {SCE_BAT_COMMENT, S::Comment}, {SCE_BAT_WORD, S::Keyword}, {SCE_BAT_LABEL, S::Function},
    {SCE_BAT_HIDE, S::Preproc}, {SCE_BAT_COMMAND, S::Type}, {SCE_BAT_IDENTIFIER, S::Variable},
    {SCE_BAT_OPERATOR, S::Operator}, {SCE_BAT_AFTER_LABEL, S::Comment},
};
constexpr StyleRole kPowerShellStyles[] = {
    {SCE_POWERSHELL_COMMENT, S::Comment}, {SCE_POWERSHELL_COMMENTSTREAM, S::Comment},
    {SCE_POWERSHELL_COMMENTDOCKEYWORD, S::Preproc}, {SCE_POWERSHELL_STRING, S::String},
    {SCE_POWERSHELL_CHARACTER, S::String}, {SCE_POWERSHELL_HERE_STRING, S::String},
    {SCE_POWERSHELL_HERE_CHARACTER, S::String}, {SCE_POWERSHELL_NUMBER, S::Number},
    {SCE_POWERSHELL_VARIABLE, S::Variable}, {SCE_POWERSHELL_OPERATOR, S::Operator},
    {SCE_POWERSHELL_KEYWORD, S::Keyword}, {SCE_POWERSHELL_CMDLET, S::Function}, {SCE_POWERSHELL_ALIAS, S::Type},
    {SCE_POWERSHELL_FUNCTION, S::Function}, {SCE_POWERSHELL_USER1, S::Type},
};
constexpr StyleRole kBashStyles[] = {
    {SCE_SH_ERROR, S::Error}, {SCE_SH_COMMENTLINE, S::Comment}, {SCE_SH_NUMBER, S::Number},
    {SCE_SH_WORD, S::Keyword}, {SCE_SH_STRING, S::String}, {SCE_SH_CHARACTER, S::String},
    {SCE_SH_OPERATOR, S::Operator}, {SCE_SH_SCALAR, S::Variable}, {SCE_SH_PARAM, S::Variable},
    {SCE_SH_BACKTICKS, S::Function}, {SCE_SH_HERE_DELIM, S::Preproc}, {SCE_SH_HERE_Q, S::String},
};
constexpr StyleRole kMakeStyles[] = {
    {SCE_MAKE_COMMENT, S::Comment}, {SCE_MAKE_PREPROCESSOR, S::Preproc}, {SCE_MAKE_IDENTIFIER, S::Variable},
    {SCE_MAKE_OPERATOR, S::Operator}, {SCE_MAKE_TARGET, S::Function}, {SCE_MAKE_IDEOL, S::Error},
};
constexpr StyleRole kCMakeStyles[] = {
    {SCE_CMAKE_COMMENT, S::Comment}, {SCE_CMAKE_STRINGDQ, S::String}, {SCE_CMAKE_STRINGLQ, S::String},
    {SCE_CMAKE_STRINGRQ, S::String}, {SCE_CMAKE_COMMANDS, S::Keyword}, {SCE_CMAKE_PARAMETERS, S::Type},
    {SCE_CMAKE_VARIABLE, S::Variable}, {SCE_CMAKE_STRINGVAR, S::Variable}, {SCE_CMAKE_USERDEFINED, S::Function},
    {SCE_CMAKE_WHILEDEF, S::Keyword}, {SCE_CMAKE_FOREACHDEF, S::Keyword}, {SCE_CMAKE_IFDEFINEDEF, S::Keyword},
    {SCE_CMAKE_MACRODEF, S::Keyword}, {SCE_CMAKE_NUMBER, S::Number},
};
constexpr StyleRole kDiffStyles[] = {
    {SCE_DIFF_COMMENT, S::Comment}, {SCE_DIFF_COMMAND, S::Keyword}, {SCE_DIFF_HEADER, S::Heading},
    {SCE_DIFF_POSITION, S::Meta}, {SCE_DIFF_DELETED, S::Deleted}, {SCE_DIFF_ADDED, S::Inserted},
    {SCE_DIFF_CHANGED, S::Meta}, {SCE_DIFF_PATCH_ADD, S::Inserted}, {SCE_DIFF_PATCH_DELETE, S::Deleted},
    {SCE_DIFF_REMOVED_PATCH_ADD, S::Inserted}, {SCE_DIFF_REMOVED_PATCH_DELETE, S::Deleted},
};
constexpr StyleRole kRustStyles[] = {
    {SCE_RUST_COMMENTBLOCK, S::Comment}, {SCE_RUST_COMMENTLINE, S::Comment},
    {SCE_RUST_COMMENTBLOCKDOC, S::CommentDoc}, {SCE_RUST_COMMENTLINEDOC, S::CommentDoc},
    {SCE_RUST_NUMBER, S::Number}, {SCE_RUST_WORD, S::Keyword}, {SCE_RUST_WORD2, S::Type},
    {SCE_RUST_WORD3, S::Keyword}, {SCE_RUST_STRING, S::String}, {SCE_RUST_STRINGR, S::String},
    {SCE_RUST_CHARACTER, S::String}, {SCE_RUST_BYTESTRING, S::String}, {SCE_RUST_BYTESTRINGR, S::String},
    {SCE_RUST_BYTECHARACTER, S::String}, {SCE_RUST_CSTRING, S::String}, {SCE_RUST_CSTRINGR, S::String},
    {SCE_RUST_OPERATOR, S::Operator}, {SCE_RUST_LIFETIME, S::Variable}, {SCE_RUST_MACRO, S::Preproc},
    {SCE_RUST_LEXERROR, S::Error},
};
constexpr StyleRole kLuaStyles[] = {
    {SCE_LUA_COMMENT, S::Comment}, {SCE_LUA_COMMENTLINE, S::Comment}, {SCE_LUA_COMMENTDOC, S::CommentDoc},
    {SCE_LUA_NUMBER, S::Number}, {SCE_LUA_WORD, S::Keyword}, {SCE_LUA_STRING, S::String},
    {SCE_LUA_CHARACTER, S::String}, {SCE_LUA_LITERALSTRING, S::String}, {SCE_LUA_STRINGEOL, S::String},
    {SCE_LUA_PREPROCESSOR, S::Preproc}, {SCE_LUA_OPERATOR, S::Operator}, {SCE_LUA_WORD2, S::Function},
    {SCE_LUA_WORD3, S::Function}, {SCE_LUA_WORD4, S::Function}, {SCE_LUA_LABEL, S::Preproc},
};

// In Lang order.
const LangDef kLangs[] = {
    {Lang::Text, L"Normal text", nullptr, {}, {}, {}},
    {Lang::Batch, L"Batch", &lmBatch, kBatchStyles, kBatchWords, {}},
    {Lang::Bash, L"Shell", &lmBash, kBashStyles, kBashWords, {}},
    {Lang::C, L"C", &lmCPP, kCStyles, kCWords, kCProps},
    {Lang::Cpp, L"C++", &lmCPP, kCStyles, kCppWords, kCProps},
    {Lang::CSharp, L"C#", &lmCPP, kCStyles, kCSharpWords, kCProps},
    {Lang::CMake, L"CMake", &lmCmake, kCMakeStyles, kCMakeWords, {}},
    {Lang::Css, L"CSS", &lmCss, kCssStyles, {}, {}},
    {Lang::Diff, L"Diff", &lmDiff, kDiffStyles, {}, {}},
    {Lang::Go, L"Go", &lmCPP, kCStyles, kGoWords, kGoProps},
    {Lang::Html, L"HTML", &lmHTML, kHtmlStyles, kHtmlWords, {}},
    {Lang::Ini, L"INI / Properties", &lmProps, kPropsStyles, {}, {}},
    {Lang::Java, L"Java", &lmCPP, kCStyles, kJavaWords, kJavaProps},
    {Lang::JavaScript, L"JavaScript", &lmCPP, kCStyles, kJsWords, kJsProps},
    {Lang::Json, L"JSON", &lmJSON, kJsonStyles, kJsonWords, kJsonProps},
    {Lang::Lua, L"Lua", &lmLua, kLuaStyles, kLuaWords, {}},
    {Lang::Makefile, L"Makefile", &lmMake, kMakeStyles, {}, {}},
    {Lang::Markdown, L"Markdown", &lmMarkdown, kMarkdownStyles, {}, kMarkdownProps},
    {Lang::PowerShell, L"PowerShell", &lmPowerShell, kPowerShellStyles, kPowerShellWords, {}},
    {Lang::Python, L"Python", &lmPython, kPythonStyles, kPythonWords, kPythonProps},
    {Lang::Rust, L"Rust", &lmRust, kRustStyles, kRustWords, {}},
    {Lang::Sql, L"SQL", &lmSQL, kSqlStyles, kSqlWords, kSqlProps},
    {Lang::TypeScript, L"TypeScript", &lmCPP, kCStyles, kTsWords, kJsProps},
    {Lang::Xml, L"XML", &lmXML, kHtmlStyles, kXmlWords, {}},
    {Lang::Yaml, L"YAML", &lmYAML, kYamlStyles, kYamlWords, {}},
};
static_assert(std::size(kLangs) == (size_t)Lang::Count);

const LangDef& Def(Lang l) {
    int i = (int)l;
    return kLangs[i >= 0 && i < (int)Lang::Count ? i : 0];
}

struct NameMap { const wchar_t* name; Lang lang; };
// Extensions, lower case without the dot.
constexpr NameMap kExtensions[] = {
    {L"txt", Lang::Text}, {L"log", Lang::Text},
    {L"bat", Lang::Batch}, {L"cmd", Lang::Batch}, {L"nt", Lang::Batch},
    {L"sh", Lang::Bash}, {L"bash", Lang::Bash}, {L"zsh", Lang::Bash}, {L"ksh", Lang::Bash},
    {L"c", Lang::C},
    {L"cpp", Lang::Cpp}, {L"cxx", Lang::Cpp}, {L"cc", Lang::Cpp}, {L"c++", Lang::Cpp}, {L"h", Lang::Cpp},
    {L"hh", Lang::Cpp}, {L"hpp", Lang::Cpp}, {L"hxx", Lang::Cpp}, {L"h++", Lang::Cpp}, {L"inl", Lang::Cpp},
    {L"ipp", Lang::Cpp}, {L"tpp", Lang::Cpp}, {L"ino", Lang::Cpp}, {L"cu", Lang::Cpp}, {L"ixx", Lang::Cpp},
    {L"cppm", Lang::Cpp}, {L"mm", Lang::Cpp}, {L"rc", Lang::Cpp},
    {L"cs", Lang::CSharp}, {L"csx", Lang::CSharp},
    {L"cmake", Lang::CMake},
    {L"css", Lang::Css}, {L"scss", Lang::Css}, {L"less", Lang::Css},
    {L"diff", Lang::Diff}, {L"patch", Lang::Diff},
    {L"go", Lang::Go},
    {L"html", Lang::Html}, {L"htm", Lang::Html}, {L"xhtml", Lang::Html}, {L"shtml", Lang::Html},
    {L"hta", Lang::Html}, {L"vue", Lang::Html}, {L"svelte", Lang::Html},
    {L"ini", Lang::Ini}, {L"inf", Lang::Ini}, {L"cfg", Lang::Ini}, {L"conf", Lang::Ini},
    {L"properties", Lang::Ini}, {L"toml", Lang::Ini}, {L"reg", Lang::Ini}, {L"editorconfig", Lang::Ini},
    {L"gitconfig", Lang::Ini}, {L"gitattributes", Lang::Ini}, {L"desktop", Lang::Ini},
    {L"java", Lang::Java},
    {L"js", Lang::JavaScript}, {L"mjs", Lang::JavaScript}, {L"cjs", Lang::JavaScript}, {L"jsx", Lang::JavaScript},
    {L"json", Lang::Json}, {L"jsonc", Lang::Json}, {L"json5", Lang::Json}, {L"geojson", Lang::Json},
    {L"webmanifest", Lang::Json}, {L"code-workspace", Lang::Json}, {L"ipynb", Lang::Json},
    {L"lua", Lang::Lua},
    {L"mak", Lang::Makefile}, {L"mk", Lang::Makefile},
    {L"md", Lang::Markdown}, {L"markdown", Lang::Markdown}, {L"mdown", Lang::Markdown}, {L"mkd", Lang::Markdown},
    {L"mkdn", Lang::Markdown},
    {L"ps1", Lang::PowerShell}, {L"psm1", Lang::PowerShell}, {L"psd1", Lang::PowerShell},
    {L"py", Lang::Python}, {L"pyw", Lang::Python}, {L"pyi", Lang::Python}, {L"pyx", Lang::Python},
    {L"rs", Lang::Rust},
    {L"sql", Lang::Sql},
    {L"ts", Lang::TypeScript}, {L"tsx", Lang::TypeScript}, {L"mts", Lang::TypeScript}, {L"cts", Lang::TypeScript},
    {L"xml", Lang::Xml}, {L"xsd", Lang::Xml}, {L"xsl", Lang::Xml}, {L"xslt", Lang::Xml}, {L"svg", Lang::Xml},
    {L"plist", Lang::Xml}, {L"xaml", Lang::Xml}, {L"csproj", Lang::Xml}, {L"vcxproj", Lang::Xml},
    {L"filters", Lang::Xml}, {L"props", Lang::Xml}, {L"targets", Lang::Xml}, {L"resx", Lang::Xml},
    {L"config", Lang::Xml}, {L"manifest", Lang::Xml}, {L"nuspec", Lang::Xml}, {L"rss", Lang::Xml},
    {L"atom", Lang::Xml}, {L"wsdl", Lang::Xml}, {L"kml", Lang::Xml}, {L"gpx", Lang::Xml},
    {L"yml", Lang::Yaml}, {L"yaml", Lang::Yaml},
};
// Whole file names (lower case), checked before extensions.
constexpr NameMap kFileNames[] = {
    {L"makefile", Lang::Makefile}, {L"gnumakefile", Lang::Makefile}, {L"cmakelists.txt", Lang::CMake},
    {L".bashrc", Lang::Bash}, {L".bash_profile", Lang::Bash}, {L".bash_aliases", Lang::Bash},
    {L".profile", Lang::Bash}, {L".zshrc", Lang::Bash}, {L".zprofile", Lang::Bash},
    {L".clang-format", Lang::Yaml}, {L".clang-tidy", Lang::Yaml}, {L".clangd", Lang::Yaml},
    {L".gitconfig", Lang::Ini}, {L".editorconfig", Lang::Ini}, {L".npmrc", Lang::Ini},
    {L".babelrc", Lang::Json}, {L".eslintrc", Lang::Json}, {L".prettierrc", Lang::Json},
};

} // namespace

const wchar_t* LangName(Lang l) { return Def(l).name; }

bool LangFolds(Lang l) {
    // These lexers compute no fold levels.
    return Def(l).module && l != Lang::Batch && l != Lang::Makefile && l != Lang::Markdown;
}

Lang LangFromPath(const std::wstring& path, bool* known) {
    bool dummy;
    if (!known) known = &dummy;
    *known = true;
    std::wstring name = FileNameOf(path);
    if (name.empty()) return Lang::Text;
    CharLowerBuffW(name.data(), (DWORD)name.size());
    for (const NameMap& m : kFileNames)
        if (name == m.name) return m.lang;
    size_t dot = name.rfind(L'.');
    *known = false;
    if (dot == std::wstring::npos) return Lang::Text;
    std::wstring_view ext = std::wstring_view(name).substr(dot + 1);
    for (const NameMap& m : kExtensions)
        if (ext == m.name) {
            *known = true;
            return m.lang;
        }
    return Lang::Text;
}

Lang LangFromContent(std::string_view head) {
    if (head.size() >= 3 && (unsigned char)head[0] == 0xEF && (unsigned char)head[1] == 0xBB) head.remove_prefix(3);
    if (head.starts_with("<?xml")) return Lang::Xml;
    if (head.starts_with("<!DOCTYPE html") || head.starts_with("<!doctype html") || head.starts_with("<html"))
        return Lang::Html;
    if (head.starts_with("diff ")) return Lang::Diff;
    if (!head.starts_with("#!")) return Lang::Text;
    std::string_view line = head.substr(0, head.find_first_of("\r\n"));
    // "#!/usr/bin/env python3" or "#!/bin/bash": take the interpreter's name.
    size_t slash = line.rfind('/');
    std::string_view prog = line.substr(slash == std::string_view::npos ? 2 : slash + 1);
    if (prog.starts_with("env ")) prog.remove_prefix(4);
    while (prog.starts_with(' ') || prog.starts_with('-')) {
        size_t sp = prog.find(' ');
        if (prog.starts_with(' ')) prog.remove_prefix(1);
        else prog.remove_prefix(sp == std::string_view::npos ? prog.size() : sp); // "env -S"
    }
    prog = prog.substr(0, prog.find(' '));
    if (prog.starts_with("python")) return Lang::Python;
    if (prog == "sh" || prog == "bash" || prog == "zsh" || prog == "ksh" || prog == "dash") return Lang::Bash;
    if (prog.starts_with("node") || prog == "deno" || prog == "bun") return Lang::JavaScript;
    if (prog.starts_with("lua")) return Lang::Lua;
    if (prog.starts_with("pwsh")) return Lang::PowerShell;
    if (prog == "make") return Lang::Makefile;
    return Lang::Text;
}

void SetDocumentLexer(const Sci& sci, Lang l) {
    const LangDef& d = Def(l);
    Scintilla::ILexer5* lexer = d.module ? d.module->Create() : nullptr;
    if (lexer) {
        // Fold levels for the fold margin (lexers ignore the properties they do not know).
        static constexpr Prop kFoldProps[] = {{"fold", "1"}, {"fold.comment", "1"}, {"fold.compact", "0"},
                                              {"fold.html", "1"}, {"fold.preprocessor", "1"}};
        for (const Prop& p : kFoldProps) lexer->PropertySet(p.key, p.value);
        for (const Prop& p : d.props) lexer->PropertySet(p.key, p.value);
        for (size_t i = 0; i < d.words.size(); i++)
            if (d.words[i]) lexer->WordListSet((int)i, d.words[i]);
    }
    sci(SCI_SETILEXER, 0, (sptr_t)lexer);
    if (!lexer) sci(SCI_CLEARDOCUMENTSTYLE);
}

void ApplySyntaxStyles(const Sci& sci, Lang l) {
    for (const StyleRole& s : Def(l).styles) {
        sci(SCI_STYLESETFORE, s.style, theme.syntax[(int)s.role]);
        switch (s.role) {
        case Syn::Heading:
        case Syn::Strong: sci(SCI_STYLESETBOLD, s.style, 1); break;
        case Syn::Emphasis: sci(SCI_STYLESETITALIC, s.style, 1); break;
        case Syn::Link: sci(SCI_STYLESETUNDERLINE, s.style, 1); break;
        default: break;
        }
    }
}

} // namespace yan
