#include "SourceNames.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include <string_view>

namespace ctcompile::cpp {
namespace {

constexpr bool letter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
constexpr bool digit(char ch) {
    return ch >= '0' && ch <= '9';
}

constexpr std::string_view keywords =
    " alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto "
    "bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept "
    "const consteval constexpr constinit const_cast continue co_await co_return co_yield "
    "decltype default delete do double dynamic_cast else enum explicit export extern false "
    "float for friend goto if inline int long mutable namespace new noexcept not not_eq "
    "nullptr operator or or_eq private protected public reflexpr register reinterpret_cast "
    "requires return short signed sizeof static static_assert static_cast struct switch "
    "synchronized template this thread_local throw true try typedef typeid typename union "
    "unsigned using virtual void volatile wchar_t while xor xor_eq import module ";

bool keyword(llvm::StringRef name) {
    return keywords.find(" " + name.str() + " ") != std::string_view::npos;
}

// This is deliberately token reservation, not a C++ parser. The native helper
// text can declare macros, types and functions. Reserving its identifier tokens
// also protects opaque calls and expressions that the emitter cannot rewrite.
void reserveTokens(llvm::StringRef code, llvm::StringSet<> & names) {
    for (std::size_t at = 0; at < code.size();) {
        if (at + 1 < code.size() && code[at] == '/' && code[at + 1] == '/') {
            at = code.find('\n', at + 2);
            if (at == llvm::StringRef::npos) { return; }
        } else if (at + 1 < code.size() && code[at] == '/' && code[at + 1] == '*') {
            at = code.find("*/", at + 2);
            if (at == llvm::StringRef::npos) { return; }
            at += 2;
        } else if (code[at] == '"' || code[at] == '\'') {
            const char quote = code[at++];
            while (at < code.size()) {
                if (code[at++] == quote) { break; }
                if (code[at - 1] == '\\' && at < code.size()) { ++at; }
            }
        } else if (letter(code[at]) || code[at] == '_') {
            const std::size_t begin = at++;
            while (at < code.size() && (letter(code[at]) || digit(code[at]) || code[at] == '_')) {
                ++at;
            }
            names.insert(code.slice(begin, at));
        } else {
            ++at;
        }
    }
}

} // namespace

std::string localIdentifier(llvm::StringRef source) {
    std::string name;
    constexpr char hex[] = "0123456789abcdef";
    for (char ch : source) {
        if (letter(ch) || digit(ch) || (ch == '_' && (name.empty() || name.back() != '_'))) {
            name += ch;
        } else {
            // Encode unsupported bytes and repeated underscores. Merely adding
            // a prefix would leave embedded double underscores reserved in C++.
            const auto byte = static_cast<unsigned char>(ch);
            name += "u";
            name += hex[byte >> 4];
            name += hex[byte & 15];
        }
    }
    if (name.empty()) { return "js_value"; }
    if (!letter(name.front()) || keyword(name)) { name = "js_" + name; }
    // Prefixing a leading underscore above would itself create a double one.
    if (llvm::StringRef(name).starts_with("js__")) { name.erase(3, 1); }
    return name;
}

void reserveCppIdentifiers(mlir::Operation * function, llvm::StringSet<> & names) {
    reserveTokens(llvm::StringRef(keywords.data(), keywords.size()), names);
    // Builtin MLIR type spelling differs from the emitted C++ typedefs.
    // Their names must remain visible after a parameter/local declaration.
    reserveTokens("size_t ptrdiff_t intptr_t uintptr_t intmax_t uintmax_t "
                  "float_t double_t FILE fpos_t",
                  names);
    for (const char * prefix :
         {"int", "uint", "int_least", "uint_least", "int_fast", "uint_fast"}) {
        for (unsigned width : {8u, 16u, 32u, 64u}) {
            names.insert(std::string(prefix) + std::to_string(width) + "_t");
        }
    }
    // Native translation units include these standard C compatibility headers.
    // Object-like macros expand even when a token is used as a local name.
    for (const char * prefix :
         {"INT", "UINT", "INT_LEAST", "UINT_LEAST", "INT_FAST", "UINT_FAST"}) {
        for (unsigned width : {8u, 16u, 32u, 64u}) {
            for (const char * suffix : {"_MIN", "_MAX", "_C"}) {
                names.insert(std::string(prefix) + std::to_string(width) + suffix);
            }
        }
    }
    reserveTokens("INTPTR_MIN INTPTR_MAX UINTPTR_MAX INTMAX_MIN INTMAX_MAX UINTMAX_MAX "
                  "INTMAX_C UINTMAX_C PTRDIFF_MIN PTRDIFF_MAX SIZE_MAX SIG_ATOMIC_MIN "
                  "SIG_ATOMIC_MAX WCHAR_MIN WCHAR_MAX WINT_MIN WINT_MAX "
                  "EXIT_SUCCESS EXIT_FAILURE RAND_MAX MB_CUR_MAX CHAR_BIT MB_LEN_MAX "
                  "CHAR_MIN CHAR_MAX SCHAR_MIN SCHAR_MAX UCHAR_MAX SHRT_MIN SHRT_MAX "
                  "USHRT_MAX INT_MIN INT_MAX UINT_MAX LONG_MIN LONG_MAX ULONG_MAX "
                  "LLONG_MIN LLONG_MAX ULLONG_MAX HUGE_VAL HUGE_VALF HUGE_VALL "
                  "FP_INFINITE FP_NAN FP_NORMAL FP_SUBNORMAL FP_ZERO FP_ILOGB0 FP_ILOGBNAN "
                  "MATH_ERRNO MATH_ERREXCEPT math_errhandling BUFSIZ FOPEN_MAX FILENAME_MAX "
                  "L_tmpnam TMP_MAX SEEK_SET SEEK_CUR SEEK_END EDOM EILSEQ ERANGE",
                  names);
    // The optional type-pin macro must not change allocation between the
    // explicit-type and deduced-type versions of the same native module.
    reserveTokens("NAN INFINITY EOF NULL stdin stdout stderr errno "
                  "CTCOMPILE_PIN CTCOMPILE_NO_TYPE_PINS ifndef define endif "
                  "name site std is_same_v __VA_ARGS__",
                  names);
    mlir::Operation * root = function;
    if (auto module = function->getParentOfType<mlir::ModuleOp>()) { root = module; }
    const auto reserveType = [&](mlir::Type type) {
        type.walk([&](mlir::Type nested) {
            if (auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueType>(nested)) {
                reserveTokens(opaque.getValue(), names);
            }
        });
    };
    root->walk([&](mlir::Operation * op) {
        if (auto symbol =
                op->getAttrOfType<mlir::StringAttr>(mlir::SymbolTable::getSymbolAttrName())) {
            reserveTokens(symbol.getValue(), names);
        }
        if (auto call = llvm::dyn_cast<mlir::emitc::CallOpaqueOp>(op)) {
            reserveTokens(call.getCallee(), names);
        }
        if (auto literal = llvm::dyn_cast<mlir::emitc::LiteralOp>(op)) {
            reserveTokens(literal.getValue(), names);
        }
        if (auto verbatim = llvm::dyn_cast<mlir::emitc::VerbatimOp>(op)) {
            reserveTokens(verbatim.getValue(), names);
        }
        for (mlir::NamedAttribute entry : op->getAttrs()) {
            entry.getValue().walk([&](mlir::Attribute attribute) {
                if (auto opaque = llvm::dyn_cast<mlir::emitc::OpaqueAttr>(attribute)) {
                    reserveTokens(opaque.getValue(), names);
                } else if (auto type = llvm::dyn_cast<mlir::TypeAttr>(attribute)) {
                    reserveType(type.getValue());
                }
            });
        }
        for (mlir::Type type : op->getResultTypes()) { reserveType(type); }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    reserveType(argument.getType());
                }
            }
        }
    });
}

} // namespace ctcompile::cpp
