# UTMS Compiler

Low-level compiler that translates custom UTMS syntax into NASM assembly.
Targets: raw binary, ELF, UEFI.

## ⚠️ Project status

**Under active development. Not stable yet.**

The compiler currently:
- produces working assembly for basic cases
- may crash on invalid syntax
- has limited error reporting
- lacks optimization
- not recommended for production use

## Features (work in progress)

- bare metal mode (`sc.false`) and in-OS mode (`sc.true`)
- sections (`sect.name`)
- global variables
- functions with parameters and return values
- `if` / `else` / `while` / `for`
- inline NASM assembly (`::nasm::{}`)
- memory management: `mloc`, `bmloc`, `mfree`, `e820f`
- I/O ports: `inb` / `outb` etc.
- `jmpto` for module calls
- `printf` / `input` in OS mode

## Building

```
#make everything
make
#install command "utmc"
sudo make install
```

## Usage

```
utmc {filename.utms} {output_binary_name}


