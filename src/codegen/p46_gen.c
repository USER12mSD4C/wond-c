#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "p46_gen.h"
#include "../platform.h"

extern int verbose;

typedef struct {
    uint32_t magic;           // 0x50343600
    uint16_t format_version;  // 0x0103
    uint16_t endianness;      // 0x0001 = little
    uint8_t pointer_size;     // 0=32bit, 1=64bit
    uint8_t reserved[3];
    uint32_t section_count;
    uint32_t strtab_offset;
    uint32_t strtab_size;
} P46Header;

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t type;
} P46Section;

static FILE* p46_out = NULL;
static uint32_t strtab_offset = 0;
static uint32_t strtab_size = 0;

void p46_gen_init(const char* filename) {
    p46_out = fopen(filename, "wb");
    if (!p46_out) {
        platform_eprintf("Cannot create %s\n", filename);
        return;
    }
    
    // Пишем заглушку для заголовка (заполним позже)
    P46Header header;
    memset(&header, 0, sizeof(header));
    header.magic = 0x50343600;
    header.format_version = 0x0103;
    header.endianness = 0x0001;
    header.pointer_size = 1; // 64-bit
    header.section_count = 3; // types, exports, strtab
    
    fwrite(&header, sizeof(header), 1, p46_out);
    
    // Пишем заглушки для секций
    P46Section sections[3];
    memset(sections, 0, sizeof(sections));
    fwrite(sections, sizeof(sections), 1, p46_out);
}

void p46_gen_finish(void) {
    if (p46_out) {
        fclose(p46_out);
        p46_out = NULL;
    }
}

void p46_gen_types(AstNode* ast) {
    if (!p46_out || !ast) return;
    
    // TODO: Запись типов в TLV формате
    // Для каждой структуры и enum в AST
}

void p46_gen_exports(AstNode* ast) {
    if (!p46_out || !ast) return;
    
    // TODO: Запись экспортируемых символов
    // Для каждой функции и глобальной переменной
}

void p46_gen_reflect(AstNode* ast) {
    if (!p46_out || !ast) return;
    
    // TODO: Запись рефлексионной информации
    // Для структур с атрибутом [[reflect]]
}
