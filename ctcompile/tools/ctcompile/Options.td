include "llvm/Option/OptParser.td"

def help : Flag<["--"], "help">, HelpText<"show this message">;
def h : Flag<["-"], "h">, Alias<help>, HelpText<"alias for --help">;
def version : Flag<["--"], "version">,
  HelpText<"report the compiler and engine versions">;
def v : Flag<["-"], "v">, Alias<version>, HelpText<"alias for --version">;

def application : Separate<["--"], "application">, Flags<[HelpHidden]>;
def application_eq : Joined<["--"], "application=">, Alias<application>;

// `--name VALUE` and its `--name=VALUE` spelling, the latter an alias.
multiclass Eq<string name, string meta, string help> {
  def NAME : Separate<["--"], name>, MetaVarName<meta>, HelpText<help>;
  def NAME # _eq : Joined<["--"], name # "=">, Alias<!cast<Separate>(NAME)>;
}

defm output : Eq<"output", "FILE", "where to write the application executable">;
def o : JoinedOrSeparate<["-"], "o">, Alias<output>, HelpText<"alias for --output">;
defm entry : Eq<"entry", "FILE",
  "the page to package, relative to the application directory (default index.html)">;
def bundle : Flag<["--"], "bundle">,
  HelpText<"write the .ctapp bundle alone instead of an executable">;
defm launcher : Eq<"launcher", "FILE",
  "the launcher to build the executable from (default: ctrun beside this compiler)">;
defm manifest : Eq<"manifest", "FILE", "also write the application manifest here, as JSON">;
defm fonts : Eq<"fonts", "DIR",
  "where the vendored faces are (default $CTBROWSER_FONT_PATH, else `fonts`)">;
def allow_env_overrides : Flag<["--"], "allow-env-overrides">,
  HelpText<"let the packaged application read the CTBROWSER_* test overrides (frames, fps, network, clock)">;
def verbose : Flag<["--"], "verbose">, HelpText<"report each stage as it runs">;
