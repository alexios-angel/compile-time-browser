include "llvm/Option/OptParser.td"

def help : Flag<["--"], "help">, HelpText<"show this message">;
def h : Flag<["-"], "h">, Alias<help>, HelpText<"alias for --help">;
def version : Flag<["--"], "version">,
  HelpText<"report the compiler and engine versions">;
def v : Flag<["-"], "v">, Alias<version>, HelpText<"alias for --version">;

def application : Separate<["--"], "application">, Flags<[HelpHidden]>;
def application_eq : Joined<["--"], "application=">, Alias<application>;
def output : Separate<["--"], "output">, MetaVarName<"FILE">,
  HelpText<"where to write the application executable">;
def output_eq : Joined<["--"], "output=">, Alias<output>;
def o : JoinedOrSeparate<["-"], "o">, Alias<output>, HelpText<"alias for --output">;
def entry : Separate<["--"], "entry">, MetaVarName<"FILE">,
  HelpText<"the page to package, relative to the application directory (default index.html)">;
def entry_eq : Joined<["--"], "entry=">, Alias<entry>;
def bundle : Flag<["--"], "bundle">,
  HelpText<"write the .ctapp bundle alone instead of an executable">;
def launcher : Separate<["--"], "launcher">, MetaVarName<"FILE">,
  HelpText<"the launcher to build the executable from (default: ctrun beside this compiler)">;
def launcher_eq : Joined<["--"], "launcher=">, Alias<launcher>;
def manifest : Separate<["--"], "manifest">, MetaVarName<"FILE">,
  HelpText<"also write the application manifest here, as JSON">;
def manifest_eq : Joined<["--"], "manifest=">, Alias<manifest>;
def fonts : Separate<["--"], "fonts">, MetaVarName<"DIR">,
  HelpText<"where the vendored faces are (default $CTBROWSER_FONT_PATH, else `fonts`)">;
def fonts_eq : Joined<["--"], "fonts=">, Alias<fonts>;
def verbose : Flag<["--"], "verbose">, HelpText<"report each stage as it runs">;
