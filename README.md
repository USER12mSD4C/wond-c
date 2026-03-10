# UTMS Compiler

Low-level compiler that translates custom UTMS syntax into NASM assembly.
Targets: raw binary, ELF, UEFI.

## ⚠️ Project status

**Under active development. Not stable yet.**

currently even basic functional dont want to work, so i dont recommend using it

## Features (work in progress)

- bare metal mode (`sc.false`) and in-OS mode (`sc.true`)
- sections (`sect.name`)
- global variables
- functions with parameters and return values
- `if` / `else` / `while` / `for`
- inline NASM and C (`::C::{}` ; `::nasm::{}`)
- memory management: `mloc`, `bmloc`, `mfree`, `e820f`
- I/O ports: `inb` / `outb`
- `jmpto` for modules/extensions call

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
```

## multi-file

**utl linker file**
```
//listname.utl
-raw

main.utms
hashsum.utm
```
**utms main file**

```
//main.utms
sc.true

fn main() {
    u64 locate buf = mloc(16);
    
    ::nasm::{
        mov rax, [buf]
        mov byte [rax],   1
        mov byte [rax+1], 2
        mov byte [rax+2], 3
        mov byte [rax+3], 4
    }
    
    u64 hash;
    jmpto "hashsum" {
        u64 data = buf;
        u64 len  = 4;
        hash = return;
    }
    
    printf("hash = %v\n", hash);  // 1 xor 2 xor 3 xor 4 = 4
    
    mfree(buf);
}
```
**utm extension file (module)**
```
//hashsum.utm
fn hashsum_entry(u64 data_ptr, u64 length) {
    u64 result = 0;
    u64 locate ptr = data_ptr;
    
    for (u64 i = 0; i < length; i = i + 1) {
        ::nasm::{
            mov rax, [ptr]
            add rax, [i]
            movzx rbx, byte [rax]
            xor [result], rbx
        }
    }
    
    return u64 result;
}
```
**usage (how to compile it)**
```
utmc listname.utl hashsum_2
```
##

