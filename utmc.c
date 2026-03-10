#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"

typedef struct {
    char* name;
    char* path;
    int is_main;
    int is_library;
} ProjectFile;

typedef struct {
    ProjectFile* files;
    int count;
    char* target;
    char* output;
} Project;

static char* search_paths[10] = {
    ".",
    "/usr/lib/utms",
    NULL
};

static void usage(const char* prog) {
    fprintf(stderr, "UTMS Compiler v2.0\n");
    fprintf(stderr, "Usage: %s [options] <input.utms|input.utl> <output>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --target=raw    Generate raw binary (for bootloader)\n");
    fprintf(stderr, "  --target=elf    Generate ELF executable (default)\n");
    fprintf(stderr, "  --target=uefi   Generate UEFI-compatible PE/COFF\n");
    fprintf(stderr, "  -I<path>        Add library search path\n");
    exit(1);
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    if (read != (size_t)size) {
        free(content);
        fclose(f);
        return NULL;
    }
    
    content[size] = '\0';
    fclose(f);
    return content;
}

static char* find_library(const char* name) {
    // Проверяем <name> формат
    if (name[0] == '<' && name[strlen(name)-1] == '>') {
        char libname[256];
        strncpy(libname, name + 1, strlen(name) - 2);
        libname[strlen(name) - 2] = '\0';
        
        for (int i = 0; search_paths[i]; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.ut", search_paths[i], libname);
            struct stat st;
            if (stat(path, &st) == 0) {
                return strdup(path);
            }
        }
    } else {
        // Обычный импорт
        for (int i = 0; search_paths[i]; i++) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.ut", search_paths[i], name);
            struct stat st;
            if (stat(path, &st) == 0) {
                return strdup(path);
            }
        }
    }
    
    return NULL;
}

static void process_imports(AstNode* ast, const char* current_file) {
    if (!ast) return;
    
    if (ast->type == NODE_IMPORT) {
        char* lib_path = find_library(ast->data.import.module);
        if (!lib_path) {
            fprintf(stderr, "Library not found: %s\n", ast->data.import.module);
            exit(1);
        }
        
        char* lib_content = read_file(lib_path);
        if (!lib_content) {
            fprintf(stderr, "Failed to read library: %s\n", lib_path);
            exit(1);
        }
        
        // TODO: Вставить содержимое библиотеки в AST
        // Пока просто заглушка
        printf("Importing %s\n", lib_path);
        
        free(lib_content);
        free(lib_path);
    }
    
    // Рекурсивно обрабатываем дочерние узлы
    switch (ast->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < ast->data.program.count; i++) {
                process_imports(ast->data.program.items[i], current_file);
            }
            break;
        case NODE_FUNCTION:
            process_imports(ast->data.function.body, current_file);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < ast->data.block.count; i++) {
                process_imports(ast->data.block.statements[i], current_file);
            }
            break;
        case NODE_IF:
            process_imports(ast->data.if_stmt.then_branch, current_file);
            process_imports(ast->data.if_stmt.else_branch, current_file);
            break;
        case NODE_WHILE:
            process_imports(ast->data.while_loop.body, current_file);
            break;
        case NODE_FOR:
            process_imports(ast->data.for_loop.body, current_file);
            break;
        default:
            break;
    }
}

static Project* parse_project(const char* utl_file) {
    FILE* f = fopen(utl_file, "r");
    if (!f) {
        perror("fopen");
        return NULL;
    }
    
    Project* proj = malloc(sizeof(Project));
    proj->files = NULL;
    proj->count = 0;
    proj->target = strdup("elf");
    proj->output = NULL;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Убираем пробелы и \n
        char* end = line + strlen(line) - 1;
        while (end > line && (*end == '\n' || *end == '\r' || *end == ' ')) {
            *end = '\0';
            end--;
        }
        
        if (line[0] == '#' || line[0] == '\0') continue;
        
        if (line[0] == '-') {
            // Опция
            if (strcmp(line, "-raw") == 0) {
                free(proj->target);
                proj->target = strdup("raw");
            }
            continue;
        }
        
        // Это файл
        proj->count++;
        proj->files = realloc(proj->files, proj->count * sizeof(ProjectFile));
        ProjectFile* pf = &proj->files[proj->count - 1];
        
        pf->name = strdup(line);
        pf->path = strdup(line);
        pf->is_main = 0;
        pf->is_library = 0;
        
        if (strstr(line, ".utms")) {
            pf->is_main = (proj->count == 1); // Первый .utms - главный
        } else if (strstr(line, ".ut")) {
            pf->is_library = 1;
        }
    }
    
    fclose(f);
    return proj;
}

static int compile_single_file(const char* input, const char* output, const char* target, int add_paths) {
    char* source = read_file(input);
    if (!source) {
        fprintf(stderr, "Failed to read input file: %s\n", input);
        return 1;
    }
    
    Lexer lexer;
    lexer_init(&lexer, source);
    
    Parser parser;
    parser_init(&parser, &lexer);
    
    AstNode* ast = parser_parse(&parser);
    printf("DEBUG: after parse, safe_code = %d\n", parser.safe_code); // ОТЛАДКА
    // Обрабатываем импорты
    if (add_paths) {
        process_imports(ast, input);
    }
    
    char asm_file[512];
    snprintf(asm_file, sizeof(asm_file), "%s.asm", output);
    
    FILE* out = fopen(asm_file, "w");
    if (!out) {
        perror("fopen");
        free(source);
        return 1;
    }
    
    CodeGen cg;
    codegen_init(&cg, out, parser.safe_code, strcmp(target, "raw") == 0);
    cg.target_uefi = (strcmp(target, "uefi") == 0);
    
    codegen_generate(&cg, ast);
    fclose(out);
    
    char cmd[4096];
    if (strcmp(target, "raw") == 0) {
        snprintf(cmd, sizeof(cmd), "nasm -f bin %s -o %s", asm_file, output);
    } else if (strcmp(target, "uefi") == 0) {
        snprintf(cmd, sizeof(cmd),
                "nasm -f win64 %s -o %s.obj && "
                "ld -m elf_x86_64 %s.obj -o %s --subsystem=efi_application",
                asm_file, output, output, output);
    } else {
        snprintf(cmd, sizeof(cmd), "nasm -f elf64 %s -o %s.o && ld %s.o -o %s",
                asm_file, output, output, output);
    }
    
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Assembly or linking failed\n");
        unlink(asm_file);
        if (strcmp(target, "raw") != 0 && strcmp(target, "uefi") != 0) {
            char obj_file[512];
            snprintf(obj_file, sizeof(obj_file), "%s.o", output);
            unlink(obj_file);
        }
        if (strcmp(target, "uefi") == 0) {
            char obj_file[512];
            snprintf(obj_file, sizeof(obj_file), "%s.obj", output);
            unlink(obj_file);
        }
        free(source);
        ast_free(ast);
        return 1;
    }
    
    unlink(asm_file);
    if (strcmp(target, "raw") != 0 && strcmp(target, "uefi") != 0) {
        char obj_file[512];
        snprintf(obj_file, sizeof(obj_file), "%s.o", output);
        unlink(obj_file);
    }
    if (strcmp(target, "uefi") == 0) {
        char obj_file[512];
        snprintf(obj_file, sizeof(obj_file), "%s.obj", output);
        unlink(obj_file);
    }
    
    printf("compiled %s -> %s\n", input, output);
    
    free(source);
    ast_free(ast);
    return 0;
}

static int compile_project(Project* proj, const char* output) {
    char** obj_files = malloc(proj->count * sizeof(char*));
    int obj_count = 0;
    
    // Компилируем каждый файл
    for (int i = 0; i < proj->count; i++) {
        if (proj->files[i].is_library) {
            // Библиотеки компилируются отдельно
            char obj_name[512];
            snprintf(obj_name, sizeof(obj_name), "/tmp/utmc_%d.o", i);
            
            if (compile_single_file(proj->files[i].path, obj_name, proj->target, 1) != 0) {
                fprintf(stderr, "Failed to compile %s\n", proj->files[i].path);
                return 1;
            }
            
            obj_files[obj_count++] = strdup(obj_name);
        } else {
            // Модули и main компилируем
            char obj_name[512];
            snprintf(obj_name, sizeof(obj_name), "/tmp/utmc_%d.o", i);
            
            if (compile_single_file(proj->files[i].path, obj_name, proj->target, 1) != 0) {
                fprintf(stderr, "Failed to compile %s\n", proj->files[i].path);
                return 1;
            }
            
            obj_files[obj_count++] = strdup(obj_name);
        }
    }
    
    // Линкуем всё вместе
    char cmd[16384] = "ld -nostdlib -static";
    
    for (int i = 0; i < obj_count; i++) {
        strcat(cmd, " ");
        strcat(cmd, obj_files[i]);
    }
    
    if (strcmp(proj->target, "raw") == 0) {
        strcat(cmd, " -Ttext=0x7C00 --oformat binary");
    }
    
    strcat(cmd, " -o ");
    strcat(cmd, output);
    
    int ret = system(cmd);
    
    // Чистим временные файлы
    for (int i = 0; i < obj_count; i++) {
        unlink(obj_files[i]);
        free(obj_files[i]);
    }
    free(obj_files);
    
    if (ret == 0) {
        printf("linked -> %s\n", output);
        return 0;
    } else {
        fprintf(stderr, "Linking failed\n");
        return 1;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) usage(argv[0]);
    
    const char* input_file = NULL;
    const char* output_file = NULL;
    char* target = strdup("elf");
    
    // Добавляем пути по умолчанию
    int path_count = 2;
    char* home = getenv("HOME");
    if (home) {
        char user_path[512];
        snprintf(user_path, sizeof(user_path), "%s/.utms", home);
        search_paths[path_count] = strdup(user_path);
        path_count++;
    }
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strncmp(argv[i], "--target=", 9) == 0) {
                free(target);
                target = strdup(argv[i] + 9);
            } else if (strncmp(argv[i], "-I", 2) == 0) {
                search_paths[path_count] = strdup(argv[i] + 2);
                path_count++;
            } else {
                usage(argv[0]);
            }
        } else {
            if (!input_file) {
                input_file = argv[i];
            } else if (!output_file) {
                output_file = argv[i];
            } else {
                usage(argv[0]);
            }
        }
    }
    
    if (!input_file || !output_file) usage(argv[0]);
    
    int ret;
    if (strstr(input_file, ".utl")) {
        // Режим проекта
        Project* proj = parse_project(input_file);
        if (!proj) {
            fprintf(stderr, "Failed to parse project file: %s\n", input_file);
            return 1;
        }
        proj->output = strdup(output_file);
        ret = compile_project(proj, output_file);
        
        // Очистка
        for (int i = 0; i < proj->count; i++) {
            free(proj->files[i].name);
            free(proj->files[i].path);
        }
        free(proj->files);
        free(proj->target);
        free(proj->output);
        free(proj);
    } else {
        // Режим одного файла
        ret = compile_single_file(input_file, output_file, target, 1);
    }
    
    free(target);
    return ret;
}
