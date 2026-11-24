# Known releases

The game and `lv-export` only accept releases listed here (`kKnownReleases` in `src/assets/data_dat.hpp`, ADR-0001).
To add one: run `lv-export verify <dir>`; it prints the hashes of an unknown release. Then check that everything
decodes and add the release.

## vanilla-dos (reference)
The original vanilla DOS release of the game. File dates 1993/1994.

| File | Size | FNV-1a 64 | MD5 | SHA-256 |
|---|---|---|---|---|
| `VIKINGS.EXE` | 142,224 | `5149f850f14a39c3` | `8807a952565c13e428988d87021095c8` | `7eb81ec3c39bd96145671ec8616d48fbbc27c779485b61936db38f962f79b21b` |
| `DATA.DAT` | 1,570,444 | content: `f4368f1cf41b4d8b` | `74beea6aa1e49bfda07ad03ff9c2bbdf`* | `3701ec4d6fcbe0f7d0bd82249cad769c70c1d58d2a1a9467ee813ff0afeedf64`* |
| `SETUP.EXE` | 33,552 | n/a | `c50e11c253241cbeb79b342fd11f67f1` | `e5391b1cb7f0a7bfe8a53e14793010130287e77f44838361434cb97c5dd6c876` |

\* Whole-file hashes of `DATA.DAT` depend on the sound settings in entry 0 (see [`data-dat.md`](data-dat.md)).
Use the **content hash**: FNV-1a 64 of the file with entry 0's 32 bytes left out.

`VIKINGS.EXE` matches the MD5 that RyanMallon's tools were tested with. Their `DATA.DAT` MD5
(`d0f8a04202070a54d1909758ecd962b5`) differs from ours at the same size. That is
presumably only the sound settings in entry 0, but it is unconfirmed until someone compares the files.
