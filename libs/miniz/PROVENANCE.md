# miniz provenance

Vendored third-party dependency used by `src/core/zipextractor.{h,cpp}` for the
built-in incremental updater (ZIP reading and, in tests, ZIP writing).

| Field | Value |
|---|---|
| Upstream | https://github.com/richgel999/miniz |
| Pinned release tag | `3.0.2` |
| Release archive | `miniz-3.0.2.zip` (attached to the `3.0.2` release) |
| Archive SHA-256 | `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5` |
| License | MIT (see `LICENSE`) |
| Vendored on | 2026-07-30 |

## Vendored file set

The GitHub *release archive* for miniz ships a **pre-amalgamated** source set,
which is not the same layout as a git checkout of the tag (a checkout splits the
implementation across `miniz_tdef.c`, `miniz_tinfl.c`, `miniz_zip.c` and a build
step amalgamates them). The complete set that the 3.0.2 release requires is:

| File | SHA-256 |
|---|---|
| `miniz.c` | `0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740` |
| `miniz.h` | `295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b` |

`LICENSE` and `ChangeLog.md` are copied verbatim from the same archive. The
`examples/` directory and `readme.md` are intentionally not vendored.

## Local modifications

**None.** `miniz.c` and `miniz.h` are byte-identical to the upstream release
archive. All configuration is done through compile definitions in
`CMakeLists.txt` (currently only `MINIZ_NO_TIME`), never by editing the sources.

If you ever need to change miniz behavior, add a `-D` in `CMakeLists.txt` or
wrap it in `src/core/zipextractor.cpp`. Keeping the vendored sources pristine is
what makes re-pinning a newer tag a mechanical operation.

## Re-pinning procedure

```pwsh
$ver = '3.0.3'
curl.exe -sSL -o "$env:TEMP\miniz.zip" `
  "https://github.com/richgel999/miniz/releases/download/$ver/miniz-$ver.zip"
Get-FileHash "$env:TEMP\miniz.zip" -Algorithm SHA256   # record in the table above
Expand-Archive "$env:TEMP\miniz.zip" -DestinationPath "$env:TEMP\miniz-$ver"
Copy-Item "$env:TEMP\miniz-$ver\miniz.c","$env:TEMP\miniz-$ver\miniz.h",`
          "$env:TEMP\miniz-$ver\LICENSE","$env:TEMP\miniz-$ver\ChangeLog.md" libs\miniz
```

Then update every hash and the tag in the tables above, and re-run
`ctest -R "^test_zipextractor$"`.
