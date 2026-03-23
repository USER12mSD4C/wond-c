sc.true

// Структура с версионированием и рефлексией
struct syscall_args [[version(1)]] [[reflect]] {
    u32 fd [[version(1)]];
    u64 offset [[version(1)]];
    char* buf [[version(2)]];      // добавлено в версии 2
    u32 flags [[version(3)]];      // добавлено в версии 3
}

// Enum с версионированием
enum error_code [[version(2)]] {
    SUCCESS = 0 [[version(1)]];
    NOT_FOUND = 1 [[version(1)]];
    PERMISSION_DENIED = 2 [[version(2)]];
    TIMEOUT = 3 [[version(2)]];
    INTERNAL = 4 [[version(3)]];
}

// Функция, использующая структуру
fn process_args(struct syscall_args* args) -> u64 {
    u64 result = 0;
    
    if (args.fd == 0) {
        result = args.offset;
    } else {
        result = args.fd * 2;
    }
    
    return result;
}

// Основная функция с демонстрацией
fn main() -> u64 {
    struct syscall_args args;
    args.fd = 42;
    args.offset = 0x1000;
    
    u64 res = process_args(&args);
    
    // Проверяем версию структуры через рефлексию
    // (это будет сгенерировано компилятором)
    
    return res;
}
