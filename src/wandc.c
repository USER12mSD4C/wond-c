#define WANDC_VERSION "2.0.0"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen/ir.h"
#include "codegen/ir_gen.h"
#include "codegen/optimizer.h"
#include "codegen/regalloc_linear.h"
#include "codegen/asm_gen.h"
#include "preproc.h"
#include "platform.h"
#include "target_spec.h"

typedef struct {
    char** paths;
    int count;
    int cap;
} PathList;

static PathList inc_paths;
static int target_raw = 0;
static int target_uefi = 0;
static char* out_fmt = "elf";
static int opt_level = 2;
static int alloc_type = 0;
static int bits = 64;
static const TargetSpec* selected_target = NULL;
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

static void add_unique_path(PathList* list, const char* path) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->paths[i], path) == 0) return;
    }
    add_path(list, path);
}

static void free_path_list(PathList* list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        platform_free(list->paths[i]);
    }
    if (list->paths) platform_free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->cap = 0;
}

static void module_entry_name(const char* module_name, char* out, size_t out_sz) {
    size_t pos;
    if (!module_name || !*module_name) {
        snprintf(out, out_sz, "__jmpto_module_main");
        return;
    }
    pos = 0;
    pos += snprintf(out + pos, out_sz - pos, "__jmpto_");
    for (const char* p = module_name; *p && pos + 6 < out_sz; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) out[pos++] = (char)c;
        else out[pos++] = '_';
    }
    if (pos + 6 >= out_sz) pos = out_sz - 6;
    memcpy(out + pos, "_main", 6);
}

static int ends_with(const char* s, const char* suffix) {
    size_t ls, lf;
    if (!s || !suffix) return 0;
    ls = strlen(s);
    lf = strlen(suffix);
    if (lf > ls) return 0;
    return strcmp(s + ls - lf, suffix) == 0;
}

static char* join_path(const char* a, const char* b) {
    size_t la, lb;
    char* out;
    if (!a || !b) return NULL;
    la = strlen(a);
    lb = strlen(b);
    out = platform_malloc(la + lb + 2);
    memcpy(out, a, la);
    if (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\') {
        out[la++] = '/';
    }
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

static char* with_wexp_ext(const char* module_name) {
    char* out;
    if (ends_with(module_name, ".wexp")) return platform_strdup(module_name);
    out = platform_malloc(strlen(module_name) + 6);
    sprintf(out, "%s.wexp", module_name);
    return out;
}

static char* resolve_module_path(const char* input_file, const char* module_name) {
    char* candidate;
    char* with_ext;
    const char* slash;
    const char* bslash;
    const char* base;
    char* input_dir = NULL;

    if (!module_name || !*module_name) return NULL;

    if (platform_file_exists(module_name)) return platform_strdup(module_name);
    with_ext = with_wexp_ext(module_name);
    if (platform_file_exists(with_ext)) return with_ext;

    slash = strrchr(input_file, '/');
    bslash = strrchr(input_file, '\\');
    base = slash ? slash : bslash;
    if (slash && bslash) base = (slash > bslash) ? slash : bslash;
    if (base) {
        size_t dir_len = (size_t)(base - input_file);
        input_dir = platform_malloc(dir_len + 1);
        memcpy(input_dir, input_file, dir_len);
        input_dir[dir_len] = '\0';
    } else {
        input_dir = platform_strdup(".");
    }

    candidate = join_path(input_dir, module_name);
    if (platform_file_exists(candidate)) {
        platform_free(input_dir);
        platform_free(with_ext);
        return candidate;
    }
    platform_free(candidate);

    candidate = join_path(input_dir, with_ext);
    if (platform_file_exists(candidate)) {
        platform_free(input_dir);
        platform_free(with_ext);
        return candidate;
    }

    platform_free(candidate);
    platform_free(input_dir);
    platform_free(with_ext);
    return NULL;
}

static void rename_module_main(AstNode* module_ast, const char* entry_name) {
    if (!module_ast || module_ast->type != NODE_PROGRAM) return;
    for (int i = 0; i < module_ast->data.program.count; i++) {
        AstNode* item = module_ast->data.program.items[i];
        if (item->type == NODE_FUNCTION && item->data.function.name &&
            strcmp(item->data.function.name, "main") == 0) {
            platform_free(item->data.function.name);
            item->data.function.name = platform_strdup(entry_name);
        }
    }
}

static void collect_jmpto_modules(AstNode* node, PathList* out) {
    if (!node || !out) return;
    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->data.program.count; i++) {
                collect_jmpto_modules(node->data.program.items[i], out);
            }
            break;
        case NODE_FUNCTION:
            collect_jmpto_modules(node->data.function.body, out);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                collect_jmpto_modules(node->data.block.statements[i], out);
            }
            break;
        case NODE_IF:
            collect_jmpto_modules(node->data.if_stmt.then_branch, out);
            collect_jmpto_modules(node->data.if_stmt.else_branch, out);
            break;
        case NODE_WHILE:
            collect_jmpto_modules(node->data.while_loop.body, out);
            break;
        case NODE_FOR:
            collect_jmpto_modules(node->data.for_loop.body, out);
            break;
        case NODE_JMPTO:
            if (node->data.jmpto.filename) {
                add_unique_path(out, node->data.jmpto.filename);
            }
            break;
        default:
            break;
    }
}

static int generate_asm_from_ast(AstNode* ast, int safe_code, const char* asm_file) {
    IRProgram* ir = NULL;
    LinearRegAlloc* ra = NULL;
    AsmContext asm_ctx;

    ir = ir_generate(ast, safe_code, alloc_type);
    if (!ir) {
        platform_eprintf("IR generation failed\n");
        return 1;
    }

    optimize_ir(ir, opt_level);
    ra = linear_allocator_new(14);
    linear_allocate(ra, ir);

    asm_gen_set_target(selected_target);
    asm_gen_init(&asm_ctx, asm_file, bits, safe_code, opt_level);
    asm_gen_program(&asm_ctx, ir, ra);
    asm_gen_finish(&asm_ctx);

    linear_allocator_free(ra);
    ir_program_free(ir);
    return 0;
}

static int compile_one(const char* input, const char* output) {
    PathList modules = {0};
    PathList asm_files = {0};
    PathList obj_files = {0};
    char* src = NULL;
    PreprocOutput* pp = NULL;
    AstNode* ast = NULL;
    int safe_code = 0;
    int ret = 1;

    src = platform_read_file(input);
    if (!src) {
        platform_eprintf("cannot read: %s\n", input);
        return 1;
    }

    pp = preprocess(src, input, &inc_paths);
    if (!pp) goto cleanup;

    Lexer lex;
    lexer_init(&lex, pp->out);

    Parser parser;
    parser_init(&parser, &lex);
    ast = parser_parse(&parser);
    if (!ast) goto cleanup;
    safe_code = parser.safe_code;

    collect_jmpto_modules(ast, &modules);

    {
        char main_asm[512];
        char main_obj[512];
        snprintf(main_asm, sizeof(main_asm), "%s.main.asm", output);
        snprintf(main_obj, sizeof(main_obj), "%s.main.o", output);
        add_path(&asm_files, main_asm);
        add_path(&obj_files, main_obj);

        if (generate_asm_from_ast(ast, safe_code, main_asm) != 0) goto cleanup;
        if (platform_assemble_to_object(main_asm, main_obj, out_fmt, safe_code, selected_target) != 0) goto cleanup;
    }

    for (int i = 0; i < modules.count; i++) {
        char* mod_path = resolve_module_path(input, modules.paths[i]);
        char* mod_src = NULL;
        PreprocOutput* mod_pp = NULL;
        AstNode* mod_ast = NULL;
        Lexer mod_lex;
        Parser mod_parser;
        char entry_name[256];
        char mod_asm[512];
        char mod_obj[512];
        int mod_safe_code = 1;

        if (!mod_path) {
            platform_eprintf("jmpto module not found: %s\n", modules.paths[i]);
            goto cleanup;
        }

        mod_src = platform_read_file(mod_path);
        if (!mod_src) {
            platform_eprintf("cannot read module: %s\n", mod_path);
            platform_free(mod_path);
            goto cleanup;
        }
        mod_pp = preprocess(mod_src, mod_path, &inc_paths);
        if (!mod_pp) {
            platform_free(mod_src);
            platform_free(mod_path);
            goto cleanup;
        }

        lexer_init(&mod_lex, mod_pp->out);
        parser_init(&mod_parser, &mod_lex);
        mod_ast = parser_parse(&mod_parser);
        if (!mod_ast) {
            platform_eprintf("cannot parse module: %s\n", mod_path);
            preproc_free(mod_pp);
            platform_free(mod_src);
            platform_free(mod_path);
            goto cleanup;
        }

        module_entry_name(modules.paths[i], entry_name, sizeof(entry_name));
        rename_module_main(mod_ast, entry_name);
        mod_safe_code = mod_parser.safe_code;

        snprintf(mod_asm, sizeof(mod_asm), "%s.mod%d.asm", output, i);
        snprintf(mod_obj, sizeof(mod_obj), "%s.mod%d.o", output, i);
        add_path(&asm_files, mod_asm);
        add_path(&obj_files, mod_obj);

        if (generate_asm_from_ast(mod_ast, mod_safe_code, mod_asm) != 0) {
            ast_free(mod_ast);
            preproc_free(mod_pp);
            platform_free(mod_src);
            platform_free(mod_path);
            goto cleanup;
        }
        if (platform_assemble_to_object(mod_asm, mod_obj, out_fmt, mod_safe_code, selected_target) != 0) {
            ast_free(mod_ast);
            preproc_free(mod_pp);
            platform_free(mod_src);
            platform_free(mod_path);
            goto cleanup;
        }

        if (verbose) {
            platform_printf("Linked module %s as %s\n", modules.paths[i], entry_name);
        }

        ast_free(mod_ast);
        preproc_free(mod_pp);
        platform_free(mod_src);
        platform_free(mod_path);
    }

    ret = platform_link_objects((const char**)obj_files.paths, obj_files.count,
                                output, out_fmt, safe_code, selected_target);

    if (ret == 0) {
        platform_printf("Compiled %s -> %s\n", input, output);
    }

cleanup:
    for (int i = 0; i < asm_files.count; i++) {
        platform_remove(asm_files.paths[i]);
    }
    for (int i = 0; i < obj_files.count; i++) {
        platform_remove(obj_files.paths[i]);
    }
    if (ast) ast_free(ast);
    if (pp) preproc_free(pp);
    if (src) platform_free(src);
    free_path_list(&modules);
    free_path_list(&asm_files);
    free_path_list(&obj_files);
    return ret;
}

static void usage(const char* prog) {
    platform_eprintf("WandC v%s\n", WANDC_VERSION);
    platform_eprintf("Usage: %s [opts] <input> <output>\n", prog);
    platform_eprintf("  --target=raw|elf|uefi|<name>|<path.wtarget>\n");
    platform_eprintf("                     target profile; auto-loads default.wtarget if found\n");
    platform_eprintf("  --bits=16|32|64    set target bits (default: 64)\n");
    platform_eprintf("  -I<path>           add include path\n");
    platform_eprintf("  -O<0-2>            optimization level (default: 2)\n");
    platform_eprintf("  --alloc=linear|graph  register allocator\n");
    platform_eprintf("  -v                 verbose\n");
    platform_eprintf("  -h                 help\n");
}

int main(int argc, char** argv) {
    const char* input = NULL;
    const char* output = NULL;
    int bits_override = 0;
    TargetSpec* default_target = NULL;
    TargetSpec* cli_target = NULL;
    int ret;

    platform_init();
    init_paths();

    default_target = target_spec_load_default();
    if (default_target) {
        selected_target = default_target;
        if (default_target->format && *default_target->format) {
            out_fmt = default_target->format;
            target_raw = (strcmp(out_fmt, "raw") == 0);
            target_uefi = (strcmp(out_fmt, "uefi") == 0);
        }
        bits = default_target->bits;
    }
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-h") == 0) {
                usage(argv[0]);
                if (cli_target) target_spec_free(cli_target);
                if (default_target) target_spec_free(default_target);
                platform_cleanup();
                return 0;
            } else if (strcmp(argv[i], "-v") == 0) {
                verbose = 1;
            } else if (strncmp(argv[i], "--target=", 9) == 0) {
                const char* target_name = argv[i] + 9;
                if (strcmp(target_name, "raw") == 0 ||
                    strcmp(target_name, "elf") == 0 ||
                    strcmp(target_name, "uefi") == 0) {
                    out_fmt = (char*)target_name;
                    target_raw = (strcmp(out_fmt, "raw") == 0);
                    target_uefi = (strcmp(out_fmt, "uefi") == 0);
                    selected_target = NULL;
                    if (!bits_override) bits = 64;
                } else {
                    TargetSpec* loaded = target_spec_load(target_name);
                    if (!loaded) {
                        platform_eprintf("cannot load target profile: %s\n", target_name);
                        if (cli_target) target_spec_free(cli_target);
                        if (default_target) target_spec_free(default_target);
                        platform_cleanup();
                        return 1;
                    }
                    if (cli_target) target_spec_free(cli_target);
                    cli_target = loaded;
                    selected_target = cli_target;
                    if (cli_target->format && *cli_target->format) {
                        out_fmt = cli_target->format;
                        target_raw = (strcmp(out_fmt, "raw") == 0);
                        target_uefi = (strcmp(out_fmt, "uefi") == 0);
                    } else {
                        out_fmt = "elf";
                        target_raw = 0;
                        target_uefi = 0;
                    }
                    if (!bits_override) bits = cli_target->bits;
                    if (verbose) {
                        platform_printf("Using target: %s (%s)\n",
                                        cli_target->name ? cli_target->name : target_name,
                                        cli_target->source_path ? cli_target->source_path : "unknown");
                    }
                }
            } else if (strncmp(argv[i], "--bits=", 7) == 0) {
                bits = atoi(argv[i] + 7);
                if (bits != 16 && bits != 32 && bits != 64) bits = 64;
                bits_override = 1;
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
                if (cli_target) target_spec_free(cli_target);
                if (default_target) target_spec_free(default_target);
                platform_cleanup();
                return 1;
            }
        } else {
            if (!input) input = argv[i];
            else if (!output) output = argv[i];
            else {
                platform_eprintf("too many args\n");
                if (cli_target) target_spec_free(cli_target);
                if (default_target) target_spec_free(default_target);
                platform_cleanup();
                return 1;
            }
        }
    }
    
    if (!input || !output) {
        usage(argv[0]);
        if (cli_target) target_spec_free(cli_target);
        if (default_target) target_spec_free(default_target);
        platform_cleanup();
        return 1;
    }

    if (verbose && selected_target) {
        platform_printf("Target profile: %s (%s)\n",
                        selected_target->name ? selected_target->name : "default",
                        selected_target->source_path ? selected_target->source_path : "unknown");
    }
    
    ret = compile_one(input, output);
    if (cli_target) target_spec_free(cli_target);
    if (default_target) target_spec_free(default_target);
    platform_cleanup();
    return ret;
}
