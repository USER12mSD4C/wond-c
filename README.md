```
WandC — UTMS Compiler

Copyright (c) 2026 The UOPL Authors
```

Portable compiler for UTMS language.

== Quick Start ==

```
umk build
./wandc test.w test
```

== File Extensions ==

  .w      - main source
  .wlink  - project file
  .wexp   - extension module
  .wlib   - library

== Example ==

```
// hello.w
sc.true

fn main() {
    printf("Hello, World!\n");
}
```

== Build System ==

Use UMK (included):

```
umk build      # compile
umk clean      # clean
sudo umk install
```

== License ==

UOPL v1.6.2
