# Paiza Mongolian IME — orientation for a new session

Read this first; it is loaded automatically. It says what the project is,
**what has actually been verified and where**, and what is left on each
platform. The detail lives in `docs/`; this file exists so that a session
starting on a machine that has never run this code knows what it is walking
into.

## What this is

A cross-platform Traditional Mongolian IME: one C++20 engine in `src/core`
plus three native front-ends — Windows (TSF), macOS (InputMethodKit), Linux
(IBus). All input behaviour belongs in `core`; the front-ends are adapters.
AGPL-3.0, also offered commercially, which is why the external engine below
runs out of process.

## Two repositories

| | |
|---|---|
| `paiza` (this one) | https://github.com/hugohu789-droid/paiza |
| `zcode-cpp` | https://github.com/hugohu789-droid/zcode-cpp |

`zcode-cpp` is a separate GPL-2.0-or-later port of the ZCode form-code
shaper and dictionary. It is **never linked in** — it runs as its own
process speaking a line protocol (`docs/engine-contract.md`). Clone it
beside this repo. Its `zcode_stdio` executable must sit **next to the
profile files**, because the host resolves an engine name inside the
install directory only and never searches `PATH`.

## Where things stand

Increments 1–7 of `docs/input-profiles.md` are done and merged to `main`:
input profiles, the external-engine contract, the ZCode profile, dictionary
modes, the user's own words with a time-decayed habit model, and the
per-profile behaviour fields. `README.md` has the overview;
`docs/input-profiles.md` §8 has the rollout record.

## What is verified, and where — read this before claiming anything works

CI (`build-and-test` on windows-latest, ubuntu-latest, macos-latest) builds
and runs the portable tests on all three. Beyond that the three platforms
are **not** equally exercised, and the gap is not visible from the code:

| | Portable tests | Front-end compiled | Ever actually run |
|---|---|---|---|
| Windows | 328, locally + CI | yes | **yes** — candidate window, ZCode profile, habits end to end |
| Linux | 315, locally (WSL) + CI | CI only¹ | **no** — never run under ibus |
| macOS | CI only | CI only | **no** — never run at all |

¹ The WSL used so far has no `libibus-1.0-dev` or `libgtk-3-dev`, so the
ibus engine and the GTK settings dialog were only ever compiled by CI. The
offscreen panel (`paiza_candidate_preview`, needs only pangocairo) *was*
run there, with the real engine, which is where the Linux screenshots in
`README.md` come from.

So: on macOS and Linux, treat "it compiles and the portable tests pass" as
the starting point, not as evidence the IME works. The things most likely
to be wrong are the parts no test covers — installation, the ibus/IMK
plumbing, and the settings dialogs.

## What to do next, per platform

**macOS** (nothing has ever been run):
1. Build and install per `docs/macos-port.md` §"Bring-up steps on the Mac".
2. `tools/macos/panel_preview.mm` takes `OUT_DIR`, `PAIZA_INPUT`,
   `PAIZA_DICT` but **not** `PAIZA_PROFILES` / `PAIZA_PROFILE` /
   `PAIZA_USER`. Its Windows and Linux twins do, which is how the ZCode
   screenshots were produced; add the same and it can show the ZCode
   profile and the habit ranking. (Linux twin:
   `tools/linux/candidate_preview.cc`.)
3. Verify on the real IME what CI only compiled: the ZCode profile's
   candidates, the engine-error row under the profile selector, and the
   per-profile candidate font.
4. Add a macOS ZCode screenshot to `README.md` beside the other two.

**Ubuntu** (compiled by CI, never run):
1. Install `libibus-1.0-dev libgtk-3-dev` and build per
   `docs/linux-port.md` §"Bring-up steps on Ubuntu" — that is the first
   local compile the ibus engine and the GTK dialog will have had.
2. Install and run it under ibus; the component-XML trap is written up in
   that same section and cost real time once.
3. Verify the same three things as macOS above.

Either way, `docs/platform-parity.md` says "all three platforms at parity".
That was true of the engine features it lists, and is **not** yet true of
the ZCode profile work — fix the claim once the platform is actually
exercised.

## House rules worth knowing

- `../ime-java` (if present) is kept **byte-for-byte unmodified**: it is the
  source of zcode-cpp's golden data.
- `main` is protected: CI on all three platforms must pass, the branch must
  be up to date, no direct pushes (admins included). Work on a branch and
  open a PR; no approval is required, so you can merge your own. Merged
  branches are deleted automatically on the remote — delete the local one
  yourself.
- Prefer a merge commit over a squash: the history is deliberately a series
  of increments that each kept the suite green.
- When measuring the engine over its pipe, do **not** use
  `subprocess(bufsize=0)` — `readline()` then reads a byte at a time and a
  22 KB answer looks like 110 ms of engine time. That mistake produced a
  whole round of wrong conclusions once.
