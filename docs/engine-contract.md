# External engine contract

How a candidate engine that Paiza did not write plugs in.

Paiza defines this contract; an engine conforms to it. Nothing here is specific
to any one engine. The first implementation is a Mongolian form-code shaper,
but a conforming engine could produce anything: another script, a prediction
model, a handwriting recogniser.

Status: contract defined; the host side is being implemented (see
[input-profiles.md](input-profiles.md), increment 6).

---

## 1. Why a separate process

An external engine runs in **its own process** and speaks a line protocol over
a pipe. Three reasons, in order of importance:

1. **Licence isolation.** Paiza is AGPL-3.0 and is also offered commercially.
   An engine under an incompatible licence (the first one is GPL-2.0-or-later)
   can be driven across a process boundary without being linked in, so the
   core stays dual-licensable.
2. **Fault isolation.** A crashing or wedged engine must not take the IME down.
   On Windows the text service is loaded into arbitrary host applications,
   including sandboxed browser renderers, so an engine fault must never reach a
   host's UI thread.
3. **Language independence.** An engine can be written in anything that can
   read and write lines.

## 2. The protocol

Named **`paiza-engine`**, currently version **1**.

One request per line, one response per line, UTF-8, LF-terminated. A trailing
CR is tolerated. Fields are TAB-separated.

### Requests

| Request | Meaning |
|---|---|
| `HELLO` | Engine name and protocol version. |
| `OPEN` | Allocate a session; the response carries its id. |
| `CLOSE <sid>` | Destroy a session. Session 0 is rejected. |
| `TYPE <sid> <key> <mode> [max] [dedup]` | Append a key **and** return candidates. |
| `UNTYPE <sid> <mode> [max] [dedup]` | Remove the last key **and** return candidates. |
| `PUSH <sid> <key>` / `POP <sid>` / `CLEAR <sid>` / `KEYS <sid>` | Session editing without a query. |
| `CAND <sid> <mode> [max] [dedup]` | Candidates for a session's keys. |
| `SHAPE <mode> <latin>` | Stateless: convert a whole Latin string. |
| `QUIT` | End the conversation. |

`<mode>` is `severe` (the input is finished) or `fuzzy` (the user is still
typing, so treat it as a prefix). `[max]` caps the candidate count, `0` meaning
no cap; `[dedup]` is `0` or `1`.

### Responses

| Response | Meaning |
|---|---|
| `OK` | Success, no payload. |
| `OK<TAB>item<TAB>item...` | Success with a payload. |
| `ERR<TAB><message>` | Rejected. |

### Rules an engine must obey

1. **Session 0 always exists and cannot be closed**, so a single-context host
   need not manage sessions.
2. **A rejected request changes nothing.** Validate every field before
   mutating, so a `TYPE` with a bad mode does not leave the key behind.
3. **Never emit a control character in a field.** They would break the
   framing. Reject them on input rather than stripping them: stripping turns
   an injection attempt into a valid-looking request.
4. **Answer every request with exactly one line**, including `QUIT`, before
   exiting.
5. **Be prompt.** The host applies a hard timeout (§4) and will drop an engine
   that exceeds it.

An engine that implements only `HELLO`, `TYPE`, `UNTYPE` and `QUIT` is usable:
the host needs no more for ordinary typing.

## 3. Declaring an engine in a profile

An engine is named by the input profile, so adding one is a data change. See
[input-profiles.md](input-profiles.md).

```
# data/profiles/<id>.txt
engine             zcode_stdio      # executable, resolved in the install dir
engine_args        --quiet          # optional, space-separated
engine_protocol    paiza-engine     # what HELLO must report
engine_transport   stdio            # optional: stdio | pipe
engine_priority    90               # optional: CandidateSource priority
engine_codepoints  1865-18AA        # optional: reject candidates outside this
```

| Field | Required | Meaning |
|---|---|---|
| `engine` | for an engine profile | Executable name. **Resolved inside the install directory only.** |
| `engine_args` | no | Arguments, space-separated. |
| `engine_protocol` | yes, with `engine` | The name `HELLO` must report. A mismatch disables the profile. |
| `engine_transport` | no | `stdio` or `pipe`; defaults per platform. |
| `engine_priority` | no | Where the engine sits among candidate sources. Default is below the compiled lexicon. |
| `engine_codepoints` | no | Hex range `LOW-HIGH`. Candidates containing anything outside it are rejected. |

The protocol **version** is deliberately not a profile field. The host knows
which versions it implements; the profile only declares which protocol the
engine must speak, and the host checks the reported version against its own
supported set.

A profile with no `engine` field uses only the built-in sources, which is how
every profile shipped so far behaves.

## 4. What the host guarantees, and enforces

These are not suggestions to the engine; they are limits the host applies, so
a misbehaving engine degrades instead of damaging the IME.

| Limit | Behaviour |
|---|---|
| **Hard timeout per exchange** | An engine that does not answer in time is dropped for the rest of the session: `IsAvailable()` turns false and candidates come from the other sources. The IME never waits on it. |
| **Maximum response length** | A response longer than the cap is treated as a protocol error. Without this a single huge line is a memory denial of service. |
| **Maximum candidates per response** | Surplus candidates are discarded even if the engine ignores `max`. |
| **Executable resolution** | `engine` is resolved **only** inside the install directory. A value containing a path separator or `..` is rejected, and `PATH` is never searched. Otherwise a writable profile file would be arbitrary code execution. |
| **Outbound validation** | A request field containing a control character is never sent. |
| **Inbound validation** | A candidate is rejected if it is invalid UTF-8, contains a control character or a bidirectional override, exceeds a length cap, or falls outside `engine_codepoints` when that is set. Candidates are committed into the user's document, so they are treated as untrusted input. |
| **Crash or EOF** | Treated as unavailability, not an error loop. The host does not restart the engine in a tight loop. |

Timeouts and caps are host constants rather than profile fields on purpose: a
profile that could raise them could reintroduce the UI hang the timeout exists
to prevent.

## 5. Writing a conforming engine

The reference implementation is `zcode-cpp` (GPL-2.0-or-later), whose
`service/` directory is a thin shim over a library that knows nothing about
this protocol. That is the recommended shape:

* Keep the engine a plain library.
* Put the protocol in a small separate layer, so it can be unit-tested
  without spawning a process.
* Ship the shim as its own executable.

A shim is typically a few hundred lines. Writing one for an existing engine is
less work than adapting Paiza, and it keeps the engine's licence off Paiza's
linkage.

### Checking an engine by hand

The protocol is line-based so that it can be driven from a shell:

```bash
printf 'HELLO\nTYPE 0 s fuzzy 3\nTYPE 0 a fuzzy 3\nUNTYPE 0 fuzzy 3\nQUIT\n' | your_engine
```

An engine that answers this correctly, one line per request, is most of the way
to conforming.
