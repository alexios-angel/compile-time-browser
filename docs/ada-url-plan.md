# Ada, and the fact that this engine parses URLs by the wrong standard

**Status: investigated, measured, NOT started.** The finding is a correctness
one and it is worse than the performance question that prompted it.

## The problem, in one line

**Browsers implement the WHATWG URL Standard. `shell/url.cpp` implements RFC
3986, via Boost.URL.** They are different specifications, and the difference is
not academic.

`include/ctbrowser/shell/url.hpp` already records the symptom without naming the
cause:

> LENIENT, LIKE THE REST OF THIS TREE. Boost.URL is a strict parser and refuses
> a raw space or a UTF-8 byte in a path. A browser accepts both […] so the
> implementation percent-encodes what RFC 3986 disallows before parsing

Pre-encoding the input to get it past a strict RFC 3986 parser is what using the
wrong standard looks like from the inside. Boost.URL is an excellent RFC 3986
parser; a browser does not want one.

## Measured, against ada (WHATWG), 15 cases

| case | ctbrowser (Boost.URL) | ada (WHATWG) | |
|---|---|---|---|
| default port dropped | `http://example.com:80/a` | `http://example.com/a` | **differs** |
| https default port | `https://example.com:443/a` | `https://example.com/a` | **differs** |
| backslashes in a special scheme | `http://example.com/%5C%5Cexample.com%5Ca` | `http://example.com/a` | **differs** |
| tab removed from anywhere | `http://example.com/a%09b` | `http://example.com/ab` | **differs** |
| IDNA to punycode | `http://%E6%97%A5%E6%9C%AC.jp/` | `http://xn--wgv71a.jp/` | **differs** |
| empty path becomes `/` | `http://example.com` | `http://example.com/` | **differs** |
| encoded dot segments `/%2e%2e/x` | `http://example.com/../x` | `http://example.com/x` | **differs** |
| leading/trailing space stripped | (differs in trailing bytes) | `http://example.com/a` | **differs** |
| host lowercased, path not | `http://example.com/A` | same | ok |
| dot segments | `http://example.com/a/c` | same | ok |
| protocol-relative | `http://other.com/x` | same | ok |
| relative up | `http://example.com/up` | same | ok |
| raw space in path | `http://example.com/a%20b` | same | ok |
| credentials | `http://user:pw@example.com/` | same | ok |
| query only | `http://example.com/?q=1` | same | ok |

**8 of 15.** The ones that matter most:

* **Backslashes lose the host entirely.** `http:\\example.com\a` resolves to
  `http://example.com/%5C%5Cexample.com%5Ca` - the authority became part of the
  path. WHATWG says `\` is `/` in a special scheme, because Windows-style URLs
  are everywhere. A page using one does not fetch a wrong resource; it fetches
  nonsense.
* **IDNA is not done.** `http://日本.jp/` becomes a percent-encoded UTF-8 host
  rather than `xn--wgv71a.jp`. That host does not resolve. Every
  internationalized domain is unreachable.
* **`/%2e%2e/x` is not normalized.** WHATWG decodes it and resolves it away;
  this engine leaves `..` in the path. This is the shape of a path-traversal
  difference and deserves care rather than a shrug.
* **Default ports are not dropped**, so `http://example.com:80/` and
  `http://example.com/` are different strings. Anything comparing origins,
  caching by URL, or reporting `location.href` sees two URLs where a browser
  sees one.

## Why ada specifically

* **It implements WHATWG**, which is the whole point. It passes the
  web-platform-tests URL suite, which is the same corpus browsers are held to.
* **It is what Node.js uses**, so it is exercised far beyond this tree.
* Same authors as simdutf, already adopted here, and the same shape of
  dependency - a compiled library that needs the mingw sysroot treatment
  `tools/mingw/build-mimalloc-mingw.sh` and `tools/mingw/build-simdutf-mingw.sh` already
  establish. That path is now well worn.
* Speed is real but is NOT the argument: URL parsing does not appear in any
  profile in `docs/performance.md`. **Adopt it for the eight rows above.**

## What it would touch, and what it must not break

`shell/url.cpp` is the only implementation - `url.hpp` deliberately keeps
Boost out of every consumer, so the blast radius is one file behind three
functions: `parse_absolute`, `location_parts`, `resolve`.

The pre-encoding workaround described in `url.hpp` **goes away**: ada is lenient
by specification, so there is nothing to work around.

**This changes observable behaviour, on purpose.** That is the reason to do it
and the reason to be careful:

* `tests/url_basics` (and `data_url`) pin today's answers. Cases that change are
  changing TOWARD the browser, and each one should be re-recorded deliberately,
  with the WHATWG rule named in the diff - not bulk-updated.
* `tools/check/compare.py` drives ctbrowser and Chrome through the same page. It is
  the right instrument here: for URL behaviour there is an oracle, and it is
  sitting in the repository already.
* The thirteen goldens should not move at all. If one does, that is a finding.
* `fetch_url::authority` and the `Host:` header rules are RFC-shaped and
  correct as they are; WHATWG normalization applies to the URL, not to what goes
  on the wire.

## Staging

0. **A differential harness first**, `tests/url_whatwg.cpp`, running the table
   above and whatever else the web-platform-tests URL corpus can be reduced to.
   It fails today. That is the point: the same shape as the p5, Phaser and
   WebGL 2 ratchets, where the measurement exists before the fix.
1. Ada behind `resolve` only - the most-used entry point and the one with the
   clearest oracle.
2. `parse_absolute` and `location_parts`.
3. Delete the pre-encoding workaround and the note in `url.hpp` that explains
   it.

**Boost.URL does not necessarily leave.** `fetch_url` wants an RFC-shaped
authority for the wire, and Boost.URL is good at that. Two parsers for two jobs
is only a problem when they are two parsers for ONE job, which is what
`url.hpp`'s own history warns about - so if both stay, the split has to be
stated: WHATWG for what a page sees, RFC 3986 for what goes on the socket.
