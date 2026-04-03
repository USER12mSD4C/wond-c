**WandC — UTMS Compiler**

**Copyright (c) 2026 The UOPL Authors**


Portable compiler for UTMS language.

== Quick Start ==

```
umk build
./wandc test.w test
```

== File Extensions ==

-  .w      - main source
-  .wlink  - linker
-  .wexp   - extension module
-  .wlib   - library

== Example ==

```
// hello.w
sc.true

fn main() {
    u64 x = 15;
    return x;
}
```

`printf` supports `%v` as a generic value placeholder:
- string arguments -> `%s`
- other arguments -> `%lld`

== Build System ==

Use UMK (included):

```
umk build      # compile
umk clean      # clean
sudo umk install
```

== Target Profiles ==

Compiler auto-loads `targets/default.wtarget` (or `default.wtarget` from lookup paths).

```
./wandc test.w test
./wandc --target=raw test.w test.bin
./wandc --target=linux_x86_64 test.w test
./wandc --target=targets/default.wtarget test.w test
```

`jmpto` can call `.wexp` modules directly:

```
jmpto test_e.wexp{
    abc;
}
```

== License ==

**UOPL v1.6.2**
