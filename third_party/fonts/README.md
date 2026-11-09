# Vendored fonts

## NotoSansMongolian-Regular.ttf

Noto Sans Mongolian, version 3.103, the **hinted** TrueType build — hinting
is what keeps a vertical script legible at candidate-window sizes on
Windows, which is the only place this file is used so far.

* Upstream: <https://github.com/notofonts/mongolian>, release
  [`NotoSansMongolian-v3.103`](https://github.com/notofonts/mongolian/releases/tag/NotoSansMongolian-v3.103),
  the file `NotoSansMongolian/hinted/ttf/NotoSansMongolian-Regular.ttf`
  inside `NotoSansMongolian-v3.103.zip`
  (zip SHA-256 `ab4ef0e294a7784e4a418523b0b214cda27dc876c03f7501f49ca8a8ba36756c`).
* Licence: SIL Open Font License 1.1 — `NotoSansMongolian-OFL.txt`, taken
  from the same archive. The OFL permits redistribution, bundling and
  installation; it is why this can ship inside an installer at all.
* Family name as Windows registers it: **`Noto Sans Mongolian`**. That
  string is what `data/profiles/unicode.txt` names in its `font` key and
  what `installer/paiza.iss` passes to `FontInstall`, so the three have to
  agree — changing one means changing all three.

It is vendored rather than downloaded during the build so that an installer
can be rebuilt from a tag years from now without depending on a release
asset still being there.

The ZCode font (`z52haratig.ttf`) is **not** here: it belongs to the engine
and lives in the zcode-cpp repository, whose `NOTICE.md` records the
permission it is redistributed under.
