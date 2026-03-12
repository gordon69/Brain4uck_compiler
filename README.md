# Brainfuck Compiler (CLI)

Консольный компилятор Brainfuck с внутренним backend (без внешних `nasm/gcc`).

## Что умеет

- `--emit asm`: генерирует ASM (`nasm`, `fasm`, `masm`)
- `--emit exe`: собирает PE `.exe` внутренним генератором
- архитектуры для `exe`: `x86` и `x64`
- все параметры задаются через аргументы командной строки

## Синтаксис

```powershell
bfcc <input.bf> [options]
```

## Опции

- `--emit <asm|exe>`: тип результата (по умолчанию `asm`)
- `--arch <x86|x64>`: целевая архитектура (по умолчанию `x64`)
- `--dialect <nasm|fasm|masm>`: диалект ASM (используется для `--emit asm`)
- `-o, --output <path>`: путь выходного файла
- `--tape-size <N>`: размер tape (по умолчанию `30000`)
- `-h, --help`: помощь

## Примеры

Сгенерировать NASM ASM:

```powershell
bfcc program.bf --emit asm --dialect nasm --arch x64 -o program.asm
```

Собрать x64 EXE внутренним backend:

```powershell
bfcc program.bf --emit exe --arch x64 -o program_x64.exe
```

Собрать x86 EXE внутренним backend:

```powershell
bfcc program.bf --emit exe --arch x86 -o program_x86.exe
```

## Структура проекта

- `main.cpp` — точка входа, orchestration
- `cli.h/.cpp` — парсинг аргументов и usage
- `compiler_types.h` — общие типы/enum/конфиг
- `brainfuck.h/.cpp` — парсинг Brainfuck в IR-операции
- `asm_emitter.h/.cpp` — генерация ASM
- `internal_pe.h/.cpp` — внутренний генератор PE EXE (`x86`/`x64`)

## Тесты

Добавлены smoke-тесты в `tests/`:

- `tests/cases/ab_loop.bf` — проверка циклов и вывода (`AB`)
- `tests/cases/hello_world.bf` — проверка более длинной программы
- `tests/cases/echo.bf` — проверка ввода/вывода
- `tests/run_tests.ps1` — автозапуск тестов

Запуск (из корня проекта):

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\run_tests.ps1 -CompilerPath "..\x64\Release\Compiler.exe"
```

Если у вас другой путь к собранному `Compiler.exe`, передайте его через `-CompilerPath`.
