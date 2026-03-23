#define WANDC_VERSION "2.0.0"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen/ir.h"
#include "codegen/ir_gen.h"
#include "codegen/regalloc_linear.h"
#include "codegen/target.h"
#include "preproc.h"
#include "platform.h"

typedef struct {
    char** paths;
    int count;
    int cap;
} PathList;

static PathList inc_paths;
static int target_raw = 0;
static int target_uefi = 0;
static char* out_fmt = "elf";
static int opt_level = 0;
static int alloc_type = 0;
int verbose = 0;

static void add_path(PathList* list, const char* path) {
    if (list->count >= list->cap) {
        list->cap = list->cap ? list->cap * 2 : 16;
        list->paths = platform_realloc(list->paths, list->cap * sizeof(char*));
    }
    list->paths[list->count++] = platform_strdup(path);
}

static void init_paths(void) {
    add_path(&inc_paths, ".");
    add_path(&inc_paths, "/usr/lib/wandc");
    add_path(&inc_paths, "/usr/local/lib/wandc");
    
    const char* home = platform_get_home();
    if (home) {
        char* buf = platform_malloc(strlen(home) + 20);
        sprintf(buf, "%s/.wandc", home);
        add_path(&inc_paths, buf);
        platform_free(buf);
    }
}

static int compile_one(const char* input, const char* output) {
    (void)output;
    
    char* src = platform_read_file(input);
    if (!src) {
        platform_eprintf("cannot read: %s\n", input);
        return 1;
    }
    
    PreprocOutput* pp = preprocess(src, input, &inc_paths);
    if (!pp) {
        platform_free(src);
        return 1;
    }
    
    Lexer lex;
    lexer_init(&lex, pp->out);
    
    Parser parser;
    parser_init(&parser, &lex);
    
    AstNode* ast = parser_parse(&parser);
    if (!ast) {
        platform_free(src);
        preproc_free(pp);
        return 1;
    }
    
    IRProgram* ir = ir_generate(ast, parser.safe_code, alloc_type);
    if (!ir) {
        platform_eprintf("IR generation failed\n");
        platform_free(src);
        preproc_free(pp);
        ast_free(ast);
        return 1;
    }
    
    // Используем JSON таргет
    target_json.init();
    
    IRIns* ins = ir->head;
    while (ins) {
        target_json.gen_ins(ins, NULL, NULL);
        ins = ins->next;
    }
    
    target_json.finish(NULL);
    
    // НЕ освобождаем ir, так как в нём есть данные, которые могут быть использованы
    // или освобождаются дважды. Просто забываем о нём.
    
    platform_free(src);
    preproc_free(pp);
    ast_free(ast);
    
    platform_printf("Generated output.json\n");
    return 0;
}

static void usage(const char* prog) {
    platform_eprintf("WandC v%s\n", WANDC_VERSION);
    platform_eprintf("Usage: %s [opts] <input> <output>\n", prog);
    platform_eprintf("  --target=raw|elf|uefi\n");
    platform_eprintf("  -I<path>        add include path\n");
    platform_eprintf("  -O<0-2>         optimization level\n");
    platform_eprintf("  --alloc=linear|graph  register allocator\n");
    platform_eprintf("  -v              verbose\n");
    platform_eprintf("  -h              help\n");
}

int main(int argc, char** argv) {
    platform_init();
    init_paths();
    
    const char* input = NULL;
    const char* output = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-h") == 0) {
                usage(argv[0]);
                platform_cleanup();
                return 0;
            } else if (strcmp(argv[i], "-v") == 0) {
                verbose = 1;
            } else if (strncmp(argv[i], "--target=", 9) == 0) {
                out_fmt = argv[i] + 9;
                target_raw = (strcmp(out_fmt, "raw") == 0);
                target_uefi = (strcmp(out_fmt, "uefi") == 0);
            } else if (strncmp(argv[i], "-I", 2) == 0) {
                add_path(&inc_paths, argv[i] + 2);
            } else if (strncmp(argv[i], "-O", 2) == 0) {
                opt_level = atoi(argv[i] + 2);
                if (opt_level < 0) opt_level = 0;
                if (opt_level > 2) opt_level = 2;
            } else if (strncmp(argv[i], "--alloc=", 8) == 0) {
                if (strcmp(argv[i] + 8, "linear") == 0) {
                    alloc_type = 0;
                } else if (strcmp(argv[i] + 8, "graph") == 0) {
                    alloc_type = 1;
                    platform_eprintf("Graph allocator not implemented yet, using linear\n");
                }
            } else {
                platform_eprintf("unknown: %s\n", argv[i]);
                platform_cleanup();
                return 1;
            }
        } else {
            if (!input) input = argv[i];
            else if (!output) output = argv[i];
            else {
                platform_eprintf("too many args\n");
                platform_cleanup();
                return 1;
            }
        }
    }
    
    if (!input || !output) {
        usage(argv[0]);
        platform_cleanup();
        return 1;
    }
    
    int ret = compile_one(input, output);
    platform_cleanup();
    return ret;
}
