//===- ctcss-translate.cpp - Compile a stylesheet to C++ ------------------===//
//
// ctcss-translate --css-to-cpp <file.css> --name <identifier> -o <out.hpp>
//
// Parses the stylesheet with ctbrowser's own CSS parser, flattens it with
// css::compile, and writes it as C++ constants (ctcompile/CSS/CppEmitter.hpp):
// a header a program includes to materialize the sheet without parsing CSS at
// run time. Needs neither MLIR nor the script engine.
//
//===----------------------------------------------------------------------===//

#include <ctcompile/CSS/CppEmitter.hpp>

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/style/css/compiled.hpp>
#include <ctbrowser/style/css/parser.hpp>

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

/// Whether `name` can be a C++ identifier: the constant and its tables are
/// named after it.
[[nodiscard]] bool is_identifier(llvm::StringRef name) {
    if (name.empty() || llvm::isDigit(name.front())) { return false; }
    return llvm::all_of(name, [](char c) { return llvm::isAlnum(c) || c == '_'; });
}

} // namespace

int main(int argc, char ** argv) {
    llvm::cl::OptionCategory category("ctcss-translate options");
    llvm::cl::opt<std::string> input("css-to-cpp", llvm::cl::Required,
                                     llvm::cl::desc("The stylesheet to compile"),
                                     llvm::cl::value_desc("file.css"), llvm::cl::cat(category));
    llvm::cl::opt<std::string> name("name", llvm::cl::Required,
                                    llvm::cl::desc("The C++ name of the compiled sheet"),
                                    llvm::cl::cat(category));
    llvm::cl::opt<std::string> ns("namespace", llvm::cl::init("ctcss"),
                                  llvm::cl::desc("The namespace the constants are declared in"),
                                  llvm::cl::cat(category));
    llvm::cl::opt<std::string> source_name(
        "source-name",
        llvm::cl::desc("How the generated file names its source (default: the input path)"),
        llvm::cl::cat(category));
    llvm::cl::opt<std::string> output("o", llvm::cl::Required, llvm::cl::desc("The C++ to write"),
                                      llvm::cl::value_desc("file"), llvm::cl::cat(category));
    llvm::cl::HideUnrelatedOptions(category);
    if (!llvm::cl::ParseCommandLineOptions(argc, argv, "Compile a stylesheet to C++ constants\n",
                                           &llvm::errs())) {
        return 2;
    }
    if (!is_identifier(name) || !is_identifier(ns)) {
        llvm::errs() << "ctcss-translate: --name and --namespace must be C++ identifiers\n";
        return 2;
    }

    const auto buffer = llvm::MemoryBuffer::getFile(input, /*IsText=*/false);
    if (!buffer) {
        llvm::errs() << "ctcss-translate: cannot read " << input << ": "
                     << buffer.getError().message() << "\n";
        return 1;
    }
    const llvm::StringRef bytes = (*buffer)->getBuffer();
    const std::string sha256 = llvm::toHex(llvm::SHA256::hash(llvm::arrayRefFromStringRef(bytes)),
                                           /*LowerCase=*/true);

    ctbrowser::atom_table atoms;
    const auto sheet = ctbrowser::style::css::parse_stylesheet(bytes, atoms);
    auto compiled = ctbrowser::style::css::compile(sheet, atoms);

    // Written to a string first, so a failure leaves no half-written file
    // for the build to mistake for a current one.
    std::ostringstream text;
    ctcompile::css::open_file(text, source_name.empty() ? input : source_name, sha256, ns);
    ctcompile::css::emit_sheet(text, compiled.view(), name);
    ctcompile::css::close_file(text, ns);

    std::ofstream out{output, std::ios::binary};
    out << text.str();
    if (!out.flush()) {
        llvm::errs() << "ctcss-translate: cannot write " << output << "\n";
        return 1;
    }
    return 0;
}
