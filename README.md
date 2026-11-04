# Yuka Agent

Консольный чат на C++17 поверх локальной LLM. Каждое сообщение отправляется в
`ollama run qwen2.5:7b`, ответ печатается в терминале. История диалога не сохраняется:
каждый запрос независим.

- - -

## 1. Что должно быть установлено

| Компонент | Зачем | Установка (macOS) |
|---|---|---|
| Xcode Command Line Tools (clang++ с C++17) | компиляция | `xcode-select --install` |
| CMake ≥ 3.15 | сборка | `brew install cmake` |
| Ollama | запуск модели | `brew install ollama` |
| Модель `qwen2.5:7b` (~4.7 ГБ) | сама LLM | `ollama pull qwen2.5:7b` |
| clang-format *(опционально)* | форматирование кода (`refmt.sh`) | `brew install clang-format` |

> Платформа: macOS. Программа использует `osascript` и Terminal.app.

- - -

## 2. Проверка окружения

```bash
clang++ --version        # компилятор
cmake --version          # должно быть >= 3.15
which ollama             # путь к ollama, например /opt/homebrew/bin/ollama
ollama --version
```

Проверка модели (нужен запущенный сервер Ollama):

```bash
ollama serve             # в отдельном окне терминала, если сервер ещё не запущен
ollama list              # в списке должна быть qwen2.5:7b
```

Если `ollama list` пишет `could not connect to ollama server`, значит сервер не запущен
(см. `ollama serve` выше).

- - -

## 3. Сборка

Из корня проекта:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Бинарник: `build/yuka`.

- - -

## 4. Запуск

```bash
./build/yuka
```

Как ведёт себя программа:

- Если `ollama serve` не запущен, она сама поднимает его в фоне. Лог пишется
  в `/tmp/yuka-ollama-serve.log`.
- Если запуск идёт **не из Terminal.app** (iTerm, VS Code, CLion и т.п.), она откроет
  новое окно Terminal.app и перезапустится там. Чтобы остаться в текущем терминале:

  ```bash
  YUKA_IN_SYSTEM_TERMINAL=1 ./build/yuka
  ```

Использование: после `me: >` введите сообщение и нажмите Enter. Первый ответ может
прийти с задержкой, пока модель загружается в память.

- - -

## 5. Остановка

- Выход из чата: `exit`, `quit`, `Ctrl+C` или `Ctrl+D`.
- Сервер Ollama после выхода **продолжает работать** в фоне. Остановить его:

  ```bash
  pkill -f "ollama serve"
  ```

  Если Ollama установлена как приложение или сервис brew: `brew services stop ollama`.

- - -

## Разработка

Форматирование исходников в `src/`:

```bash
./refmt.sh
```

Модель задана в коде (`src/main.cpp`, функция `run_ollama_and_print`). Чтобы сменить
модель, поменяйте `qwen2.5:7b` там и в приветственном баннере в `main()`.
