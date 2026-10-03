# Development notes

Build configuration and debugging notes. Framework-agnostic: nothing here is
specific to a particular editor, assistant, or agent.

## Never merge upstream; port by hand

This fork is a descendant of `areofyl/fetch`, kept as a merge base so the
lineage stays visible. Upstream is **read-only**: its push URL is set to
`DISABLED`, and every commit goes to `origin` (`ViolaPeracia/fetch-win`).

```bash
git remote set-url --push upstream DISABLED
```

Do not run `git merge upstream/main`. The fork split the monolithic `fetch.c`
into `src/{core,config,logo,platform,renderer}`, which upstream does not have.
Merging would reintroduce thousands of lines of single-file code and undo the
refactor. Pick upstream changes over manually, one at a time, and verify each
against the Windows, Linux, and macOS builds.

A `git log main..upstream/main` listing commits here does not mean they are
missing. It shows commits that were ported by hand rather than merged, so
their SHAs never entered this history. Check the actual content before assuming
work is outstanding.

## Strict C11 hides POSIX declarations

`CMakeLists.txt` sets `CMAKE_C_EXTENSIONS OFF`, which compiles as `-std=c11`.
That defines `__STRICT_ANSI__`, so glibc hides the POSIX-only declarations of
`popen`, `pclose`, `usleep`, `strdup`, `O_CLOEXEC`, and others.

The failure mode is not a compile error. An implicitly declared function is
assumed to return `int`, so a pointer return value gets truncated to 32 bits on
a 64-bit target and the caller operates on a garbage pointer. The code compiles,
links, and then crashes at runtime.

This bit the POSIX build three ways:

| Symptom | Cause |
|---|---|
| `O_CLOEXEC` undeclared | `O_CLOEXEC` is POSIX-only |
| Segfault at startup | `popen`'s `FILE*` truncated to `int`, then passed to `fgets` |
| `usleep` misbehaving | implicit declaration, same class |

**Rule:** any translation unit using a POSIX-only function must request the
feature macros before its first include. The guard must be per-file; these are
not inherited across translation units.

```c
/* first thing in the .c file, before any include */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
```

`CMakeLists.txt` also adds `_GNU_SOURCE` for non-Windows targets so new POSIX
code is covered without remembering the per-file guard.

Note that MinGW defines `_GNU_SOURCE` itself, so a Windows-only build **cannot**
catch this class of bug. Verified on 2026-10-03: the Windows build was green
while the Linux build did not compile and then segfaulted.

## Treat `-Wimplicit-function-declaration` as critical

This warning is not noise and not cosmetic. On a 64-bit target it means a
pointer return value is being truncated. Every occurrence above was present in
the build output for months before anything broke.

The same applies to `-Wformat-truncation` and `-Wstringop-truncation`: they
indicate a real overflow path, not a theoretical one.

## Verify the platforms you cannot build locally

A single-platform build is blind to that platform's failures. Practical steps:

1. **Put the other platform in CI.** A Linux runner costs about 20 seconds and
   catches what a Windows build structurally cannot.
2. **Balance preprocessor directives explicitly.** Guards for platform-specific
   code are invisible to a build of the other platform, so a stray `#endif`
   ships silently. Preprocess every translation unit under each target's macros
   and fail on `unterminated #if`, `#endif without`, `#else after #else`.
   `.github/workflows/ci.yml` does this for Windows, Linux, and macOS.
3. **When a failure is not obvious, get a trace rather than guessing.** Add a
   temporary job gated on `failure()` that rebuilds with
   `-fsanitize=address,undefined -fno-omit-frame-pointer -g` and runs the
   crashing command. The symbolized trace located the `popen` bug in one run.
   Remove the job once the cause is fixed.

## Tests must pin every environment variable in the lookup chain

Lookups follow a precedence chain, and a CI runner's environment is not a
developer's. `tests/test_baseline.c` pinned `HOME` to load
`.config/fetch/config`, but POSIX resolution consults `XDG_CONFIG_HOME` first,
which GitHub runners export. The test silently read a real user file instead of
its fixture and 24 assertions failed.

Before relying on an environment variable to redirect a path, enumerate every
variable the resolver consults and pin all of them. Restore each afterwards.

| Platform | Precedence for `%APPDATA%`-style paths |
|---|---|
| Windows | `APPDATA`, then `HOME`, then `USERPROFILE` |
| POSIX | `XDG_CONFIG_HOME`, then `HOME` |

## Reading CI as evidence, not decoration

A red CI run is information. Three real defects surfaced on the first run of
this workflow, all pre-existing and none caused by the change under test, and
none reachable from a Windows-only build.

When a job fails, read its full log rather than the summary line. The
`compiler warnings` job in this repo is deliberately report-only, so its green
status carries no information about warnings; the count it prints is the signal.