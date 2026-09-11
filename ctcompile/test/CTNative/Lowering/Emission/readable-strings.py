"""Native literals must be readable C++ and preserve their exact input bytes."""

import argparse
from pathlib import Path
import shutil
import subprocess


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, capture_output=True, **kwargs).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--translate", required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)

    # Input bytes and selected human-readable spellings are independent of the
    # runtime oracle, which uses unsigned numeric byte arrays below.
    cases = [
        (b"price", 'std::string("price", 5)'),
        (b'a"b\\c', 'std::string(R"(a"b\\c)", 5)'),
        (b"a\x007F", 'std::string("a\\0007F", 4)'),
        (b'a)"b', 'std::string("a)\\"b", 4)'),
        (b"end\\", 'std::string(R"(end\\)", 4)'),
        (b"\x01Af", 'std::string("\\001Af", 3)'),
        (b"", 'std::string("", 0)'),
        ("caf\u00e9 \U0001f642".encode(), None),
        (b"\xed\xa0\x80", None),  # A lone surrogate's WTF-8 bytes.
        (b"??= ??/ ??' ??( ??) ??! ??< ??> ??-", None),
        (b"??/\\", None),  # Raw strings also need to compile without warnings.
        (bytes(range(256)), None),
    ]
    functions = []
    for index, (value, _) in enumerate(cases):
        ir_bytes = "".join(f"\\{byte:02X}" for byte in value)
        functions.append(f"""
ctjs.func @literal${index}(%receiver: !ctjs.value, %new_target: !ctjs.value,
                         %callee: !ctjs.value) -> !ctjs.value
    attributes {{upvalue_count = 0 : i32}} {{
  %value = ctjs.constant #ctjs.string<"{ir_bytes}">
  ctjs.return %value
}}
""")
    lowered = run([args.opt, "--ctnative-lower-to-emitc"], input="\n".join(functions))
    if "ctnative.not_native" in lowered or "ctjs.func" in lowered:
        raise AssertionError(f"literal functions must lower completely:\n{lowered}")
    cpp = run([args.translate, "--mlir-to-cpp"], input=lowered)
    for _, spelling in cases:
        if spelling is not None and spelling not in cpp:
            raise AssertionError(f"missing readable spelling: {spelling}\n{cpp}")

    checks = []
    for index, (value, _) in enumerate(cases):
        array = ", ".join(str(byte) for byte in value)
        checks.append(f"  if (!same_bytes(literal_{index}(), {{{array}}})) return {index + 1};")
    cpp += """
#include <initializer_list>
bool same_bytes(const std::string& actual, std::initializer_list<unsigned char> expected) {
  if (actual.size() != expected.size()) return false;
  auto cursor = actual.begin();
  for (unsigned char byte : expected) {
    if (static_cast<unsigned char>(*cursor++) != byte) return false;
  }
  return true;
}
int main() {
""" + "\n".join(checks) + "\n  return 0;\n}\n"
    source = args.work / "literals.cpp"
    source.write_text(cpp)
    for name in ("g++", "clang++"):
        compiler = shutil.which(name)
        if compiler is None:
            raise RuntimeError(f"literal regression requires {name}")
        executable = args.work / name
        run([compiler, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wconversion", "-pedantic", str(source), "-o", str(executable)])
        run([str(executable)])
    print(f"{len(cases)} native literal cases preserve every byte under GCC and Clang")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        print(error.stdout or "", end="")
        print(error.stderr or "", end="")
        raise
