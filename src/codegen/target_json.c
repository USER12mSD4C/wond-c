#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "target.h"
#include "../platform.h"

static FILE* json_out = NULL;
static int first_ins = 1;
static int first_global = 1;

static void json_escape(const char* str, FILE* out) {
    if (!str) {
        fprintf(out, "\"\"");
        return;
    }
    fprintf(out, "\"");
    while (*str) {
        if (*str == '"' || *str == '\\') fprintf(out, "\\");
        fprintf(out, "%c", *str);
        str++;
    }
    fprintf(out, "\"");
}

static void target_json_init(void) {
    if (json_out) {
        fclose(json_out);
        json_out = NULL;
    }
    json_out = fopen("output.json", "w");
    if (json_out) {
        fprintf(json_out, "{\n");
        fprintf(json_out, "  \"format\": \"standard46\",\n");
        fprintf(json_out, "  \"version\": \"1.3.1\",\n");
        fprintf(json_out, "  \"sections\": {\n");
        fprintf(json_out, "    \"instructions\": [\n");
    }
    first_ins = 1;
    first_global = 1;
}

static void target_json_finish(FILE* out) {
    (void)out;
    if (json_out) {
        fprintf(json_out, "\n    ],\n");
        fprintf(json_out, "    \"globals\": [\n");
        fprintf(json_out, "    ]\n");
        fprintf(json_out, "  }\n}\n");
        fclose(json_out);
        json_out = NULL;
    }
}

static void target_json_gen_ins(IRIns* ins, LinearRegAlloc* ra, FILE* out) {
    (void)ra;
    (void)out;
    
    if (!json_out) return;
    
    if (!first_ins) {
        fprintf(json_out, ",\n");
    }
    first_ins = 0;
    
    fprintf(json_out, "      {");
    
    switch (ins->op) {
        case IR_CONST:
            fprintf(json_out, "\"op\": \"const\", \"value\": %d", ins->const_val);
            break;
        case IR_GLOBAL:
            fprintf(json_out, "\"op\": \"global\", \"name\": ");
            json_escape(ins->var_name, json_out);
            if (ins->dest >= 0) {
                fprintf(json_out, ", \"dest\": %d", ins->dest);
            }
            break;
        case IR_STORE:
            fprintf(json_out, "\"op\": \"store\", \"name\": ");
            json_escape(ins->var_name, json_out);
            if (ins->src1 >= 0) {
                fprintf(json_out, ", \"src\": %d", ins->src1);
            }
            break;
        case IR_ADD:
            fprintf(json_out, "\"op\": \"add\"");
            if (ins->src1 >= 0) fprintf(json_out, ", \"src1\": %d", ins->src1);
            if (ins->src2 >= 0) fprintf(json_out, ", \"src2\": %d", ins->src2);
            if (ins->dest >= 0) fprintf(json_out, ", \"dest\": %d", ins->dest);
            break;
        case IR_SUB:
            fprintf(json_out, "\"op\": \"sub\"");
            if (ins->src1 >= 0) fprintf(json_out, ", \"src1\": %d", ins->src1);
            if (ins->src2 >= 0) fprintf(json_out, ", \"src2\": %d", ins->src2);
            if (ins->dest >= 0) fprintf(json_out, ", \"dest\": %d", ins->dest);
            break;
        case IR_MUL:
            fprintf(json_out, "\"op\": \"mul\"");
            if (ins->src1 >= 0) fprintf(json_out, ", \"src1\": %d", ins->src1);
            if (ins->src2 >= 0) fprintf(json_out, ", \"src2\": %d", ins->src2);
            if (ins->dest >= 0) fprintf(json_out, ", \"dest\": %d", ins->dest);
            break;
        case IR_DIV:
            fprintf(json_out, "\"op\": \"div\"");
            if (ins->src1 >= 0) fprintf(json_out, ", \"src1\": %d", ins->src1);
            if (ins->src2 >= 0) fprintf(json_out, ", \"src2\": %d", ins->src2);
            if (ins->dest >= 0) fprintf(json_out, ", \"dest\": %d", ins->dest);
            break;
        case IR_RETURN:
            fprintf(json_out, "\"op\": \"return\"");
            if (ins->src1 >= 0) fprintf(json_out, ", \"value\": %d", ins->src1);
            break;
        case IR_LABEL:
            fprintf(json_out, "\"op\": \"label\"");
            if (ins->var_name) {
                fprintf(json_out, ", \"name\": ");
                json_escape(ins->var_name, json_out);
            }
            if (ins->label >= 0) fprintf(json_out, ", \"label\": %d", ins->label);
            break;
        case IR_CALL:
            fprintf(json_out, "\"op\": \"call\", \"name\": ");
            json_escape(ins->var_name, json_out);
            if (ins->dest >= 0) fprintf(json_out, ", \"dest\": %d", ins->dest);
            break;
        case IR_PARAM:
            fprintf(json_out, "\"op\": \"param\", \"src\": %d, \"index\": %d", ins->src1, ins->dest);
            break;
        case IR_PUSH:
            fprintf(json_out, "\"op\": \"push\"");
            if (ins->src1 >= 0) fprintf(json_out, ", \"src\": %d", ins->src1);
            break;
        case IR_POP:
            fprintf(json_out, "\"op\": \"pop\"");
            if (ins->dest >= 0) fprintf(json_out, ", \"dest\": %d", ins->dest);
            break;
        case IR_JMP:
            fprintf(json_out, "\"op\": \"jmp\", \"label\": %d", ins->label);
            break;
        case IR_JZ:
            fprintf(json_out, "\"op\": \"jz\", \"src\": %d, \"label\": %d", ins->src1, ins->label);
            break;
        case IR_JNZ:
            fprintf(json_out, "\"op\": \"jnz\", \"src\": %d, \"label\": %d", ins->src1, ins->label);
            break;
        default:
            fprintf(json_out, "\"op\": \"unknown\", \"code\": %d", ins->op);
            break;
    }
    fprintf(json_out, "}");
}

static void target_json_gen_global(const char* name, int size, const char* init, FILE* out) {
    (void)out;
    if (!json_out) return;
    
    if (!first_global) {
        fprintf(json_out, ",\n");
    }
    first_global = 0;
    
    fprintf(json_out, "      {\"name\": ");
    json_escape(name, json_out);
    fprintf(json_out, ", \"size\": %d", size);
    if (init) {
        fprintf(json_out, ", \"init\": ");
        json_escape(init, json_out);
    }
    fprintf(json_out, "}");
}

Target target_json = {
    .name = "json",
    .reg_count = 0,
    .reg_names = NULL,
    .init = target_json_init,
    .prologue = NULL,
    .epilogue = NULL,
    .gen_ins = target_json_gen_ins,
    .gen_label = NULL,
    .finish = target_json_finish,
    .gen_global = target_json_gen_global,
    .gen_section_global = NULL,
    .gen_string = NULL
};
