# Process singleton lock

Opt-in feature in `Engine_Config.app_name`. Set it to a short binary-style
name (e.g. `"blueprinter"`, `"gunslinger"`); `arc_engine_create()` then
flock-protects `/tmp/<app_name>.lock` so two processes with the same
`app_name` can't run simultaneously.

## Behaviour

- `app_name == NULL` -> no lock (legacy default).
- First process: opens the lock file, acquires `flock(LOCK_EX | LOCK_NB)`,
  rewrites the file with `pid=`, `started=`, `cwd=`, `argv=`, and holds
  the fd until process exit. Kernel auto-releases on termination
  (including SIGKILL / crash).
- Second process with same `app_name`: `flock` fails non-blocking, the
  contents of the lock file are echoed to stderr so the user can identify
  the holder, then `arc_engine_create()` returns NULL. Caller's existing
  `arc_engine_run(NULL)` check exits non-zero.
- Different `app_name` -> different lock file -> not mutually exclusive.
  blueprinter and gunslinger can run side by side.

## Failure modes

- `open()` failure (e.g. `chmod 000`): warn to stderr and fail-open. Local
  dev shouldn't be blocked by a misconfigured `/tmp/`.
- Stale lock from a crashed process: kernel already released flock on
  termination, so the next launch acquires cleanly. Manual `rm` only
  needed in edge cases (e.g. zombie lock from a different filesystem
  mount).

## Platform

POSIX only (macOS / Linux) via `<sys/file.h>` flock. Windows lands in a
later sprint with `CreateMutex`.

## Motivation

An AI automation loop launched 14 blueprinter instances on the user's
machine because nothing prevented it. The lock prevents that recurrence
without requiring the host shell to coordinate.
