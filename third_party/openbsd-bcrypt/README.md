# OpenBSD bcrypt

PaneKit's PIN hashing (`app/PaneKit/Sources/PaneKit/Security.swift`) uses
OpenBSD's libc bcrypt, vendored unmodified as the `CBcrypt` target in
`app/PaneKit/Sources/CBcrypt/`:

| File | Origin | Revision |
| --- | --- | --- |
| `bcrypt.c` | `openbsd/src` `lib/libc/crypt/bcrypt.c` | `$OpenBSD: bcrypt.c,v 1.58 2020/07/06` |
| `blowfish.c` | `openbsd/src` `lib/libc/crypt/blowfish.c` | `$OpenBSD: blowfish.c,v 1.21 2022/08/28` |
| `blf.h` | `openbsd/src` `include/blf.h` | `$OpenBSD: blf.h,v 1.8 2021/11/29` |

Downloaded from `https://raw.githubusercontent.com/openbsd/src/master/` on
2026-10-26. `include/CBcrypt.h` is Pane's own and only declares the two
functions PaneKit calls. OpenBSD's libc internals the files use (`DEF_WEAK`,
`WRAP`, `explicit_bzero`) are compiler defines in `Package.swift`, so the
files stay as they came.

Licenses: `bcrypt.c` is ISC (Copyright (c) 2014 Ted Unangst, Copyright (c)
1997 Niels Provos); `blowfish.c` and `blf.h` are BSD-3-Clause (Copyright 1997
Niels Provos). The full texts are in each file's header and in `LICENSE`.
