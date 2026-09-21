#!/usr/bin/env python3
"""Fill the Russian catalogue and mark the English one as an identity mapping.

Run after `cmake --build <dir> --target update_translations` has refreshed the
.ts files from source:

    python scripts/fill_translations.py

The script refuses to leave anything untranslated: an unmapped string is a hard
error, so a newly added qsTr() cannot silently ship as English in the Russian
interface.
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RU = ROOT / "i18n" / "jarvis_ru.ts"
EN = ROOT / "i18n" / "jarvis_en.ts"

# Translations keyed by source string. Technical names - JARVIS, llama.cpp,
# whisper.cpp, GGUF, Vulkan, config.json, CPU/GPU/RAM/VRAM - stay as they are.
RUSSIAN: dict[str, str] = {
    # --- shared vocabulary -------------------------------------------------
    "JARVIS": "JARVIS",
    "GPU": "GPU",
    "QT": "QT",
    "N/A": "Н/Д",
    "NONE": "НЕТ",
    "OFF": "ВЫКЛ",
    "READY": "ГОТОВО",
    "LIVE": "ЖИВЫЕ ДАННЫЕ",
    "PARTIAL": "ЧАСТИЧНО",
    "ACTIVE": "АКТИВНО",
    "STATUS": "СТАТУС",
    "FACTS": "ФАКТЫ",
    "REAL VALUES": "РЕАЛЬНЫЕ ЗНАЧЕНИЯ",
    "UNAVAILABLE": "НЕДОСТУПНО",
    "NOT AVAILABLE": "НЕДОСТУПНО",
    "NOT AVAILABLE · %1": "НЕДОСТУПНО · %1",
    "COLLECTING": "СБОР ДАННЫХ",
    "VISUAL PREVIEW": "ПРЕДПРОСМОТР",
    "PLANNED CAPABILITIES": "ПЛАНИРУЕМЫЕ ВОЗМОЖНОСТИ",

    # --- phases ------------------------------------------------------------
    "PHASE 3": "ФАЗА 3",
    "PHASE 4": "ФАЗА 4",
    "PHASE 5": "ФАЗА 5",
    "PHASE 7": "ФАЗА 7",
    "PHASE 8": "ФАЗА 8",
    "PHASE 9": "ФАЗА 9",
    "PHASE 10": "ФАЗА 10",
    "PHASE 4-6": "ФАЗЫ 4–6",

    # --- navigation and page titles ---------------------------------------
    "HOME": "ГЛАВНАЯ",
    "MEMORY": "ОЗУ",           # metric label; overridden for nav and page title
    "SYSTEM": "СИСТЕМА",
    "APPS": "ПРИЛОЖЕНИЯ",
    "APPLICATIONS": "ПРИЛОЖЕНИЯ",
    "FILES": "ФАЙЛЫ",
    "AUTOMATION": "АВТОМАТИЗАЦИЯ",
    "CONVERSATIONS": "ДИАЛОГИ",
    "MODELS": "МОДЕЛИ",
    "VOICE": "ГОЛОС",
    "SECURITY": "БЕЗОПАСНОСТЬ",
    "SETTINGS": "НАСТРОЙКИ",
    "OFFLINE · LOCAL ONLY": "НЕ В СЕТИ · ТОЛЬКО ЛОКАЛЬНО",

    # --- AI Core states ----------------------------------------------------
    "OFFLINE": "НЕ В СЕТИ",
    "IDLE": "ОЖИДАНИЕ",
    "LISTENING": "СЛУШАЮ",
    "THINKING": "ОБРАБАТЫВАЮ",
    "EXECUTING": "ВЫПОЛНЯЮ",
    "SPEAKING": "ОТВЕЧАЮ",
    "WARNING": "ВНИМАНИЕ",
    "ERROR": "ОШИБКА",

    # --- Command Center ----------------------------------------------------
    "COMMAND CENTER": "ЦЕНТР УПРАВЛЕНИЯ",
    "ACTIVE TASK": "ТЕКУЩАЯ ЗАДАЧА",
    "no tool engine until phase 7": "движок инструментов появится в фазе 7",
    "STARTUP WARNINGS": "ПРЕДУПРЕЖДЕНИЯ ЗАПУСКА",

    # --- home wings --------------------------------------------------------
    "SUBSYSTEMS": "ПОДСИСТЕМЫ",
    "Configuration": "Конфигурация",
    "Logging": "Журналирование",
    "Telemetry": "Телеметрия",
    "Interface": "Интерфейс",
    "LLM engine": "Движок LLM",
    "Speech to text": "Распознавание речи",
    "Text to speech": "Синтез речи",
    "Tool engine": "Движок инструментов",
    "Memory": "Память",
    "Permissions": "Разрешения",
    "MACHINE": "МАШИНА",
    "PROCESSOR": "ПРОЦЕССОР",
    "GRAPHICS": "ВИДЕОКАРТА",
    "NO NVML GPU": "НЕТ GPU NVML",

    # --- system page -------------------------------------------------------
    "Live telemetry, sampled once a second off the interface thread.":
        "Живая телеметрия: опрос раз в секунду вне потока интерфейса.",
    "HARDWARE PROFILE": "ПРОФИЛЬ ОБОРУДОВАНИЯ",
    "DRIVER": "ДРАЙВЕР",
    "GPU STATUS": "СОСТОЯНИЕ GPU",
    "OPERATING SYSTEM": "ОПЕРАЦИОННАЯ СИСТЕМА",
    "PROCESSOR LOAD": "ЗАГРУЗКА ПРОЦЕССОРА",
    "MEMORY LOAD": "ЗАГРУЗКА ОЗУ",
    "GRAPHICS LOAD": "ЗАГРУЗКА ВИДЕОКАРТЫ",
    "VIDEO MEMORY": "ВИДЕОПАМЯТЬ",
    "NO NVML-CAPABLE GPU": "НЕТ ВИДЕОКАРТЫ С ПОДДЕРЖКОЙ NVML",
    "NETWORK THROUGHPUT": "СЕТЕВОЙ ТРАФИК",
    "scaled to the busiest second observed in this session":
        "масштаб по самой нагруженной секунде за сеанс",
    " VRAM": " видеопамяти",

    # --- HUD ---------------------------------------------------------------
    "NETWORK": "СЕТЬ",
    "AI MODEL": "МОДЕЛЬ ИИ",
    "NOT LOADED · PHASE 3": "НЕ ЗАГРУЖЕНА · ФАЗА 3",

    # --- placeholder pages -------------------------------------------------
    "This subsystem is delivered in %1. Nothing on this page is simulated: "
    "until the backend exists, there is nothing here to show.":
        "Эта подсистема появится в %1. Ничего на этой странице не имитируется: "
        "пока нет бэкенда, показывать нечего.",
    "This subsystem is not implemented yet.":
        "Эта подсистема ещё не реализована.",

    # --- apps page ---------------------------------------------------------
    "Discovered applications JARVIS is allowed to launch, close and switch to.":
        "Обнаруженные приложения, которые JARVIS разрешено запускать, закрывать "
        "и переключать.",
    "Discover installed applications from the registry and Start menu":
        "Обнаружение установленных приложений через реестр и меню «Пуск»",
    "Launch and close applications by natural-language name":
        "Запуск и закрытие приложений по названию на естественном языке",
    "Switch, minimise and maximise windows":
        "Переключение, сворачивание и разворачивание окон",
    "Maintain a reviewed registry of safe application paths":
        "Ведение проверенного реестра безопасных путей к приложениям",

    # --- automation page ---------------------------------------------------
    "Multi-step routines built from registered tools.":
        "Многошаговые сценарии из зарегистрированных инструментов.",
    "Chain registered tools into named routines":
        "Объединение зарегистрированных инструментов в именованные сценарии",
    "Review every step and its permission level before running":
        "Просмотр каждого шага и его уровня доступа до запуска",
    "Trigger routines by voice or hotkey":
        "Запуск сценариев голосом или горячей клавишей",
    "Stop a running routine at any point":
        "Остановка выполняющегося сценария в любой момент",

    # --- conversations page ------------------------------------------------
    "Past exchanges with JARVIS, kept on this machine only.":
        "Прошлые диалоги с JARVIS, хранятся только на этом компьютере.",
    "Full transcript of past conversations":
        "Полная расшифровка прошлых диалогов",
    "Search across history": "Поиск по всей истории",
    "Continue an earlier conversation with its context restored":
        "Продолжение прошлого диалога с восстановленным контекстом",
    "Delete a conversation or the entire history":
        "Удаление отдельного диалога или всей истории",

    # --- files page --------------------------------------------------------
    "File search and management, constrained by the permission engine.":
        "Поиск файлов и работа с ними в рамках системы разрешений.",
    "Search, open, create, rename, move and copy files":
        "Поиск, открытие, создание, переименование, перемещение и копирование файлов",
    "Deletion gated behind a HIGH permission prompt":
        "Удаление только после подтверждения с уровнем HIGH",
    "Protected system paths that can never be modified":
        "Защищённые системные пути, которые нельзя изменить",
    "Every operation routed through a registered tool, never a shell":
        "Каждая операция идёт через зарегистрированный инструмент, а не через оболочку",

    # --- memory page -------------------------------------------------------
    "Conversation history, preferences and long-term memory, stored locally in SQLite.":
        "История диалогов, предпочтения и долговременная память — локально в SQLite.",
    "Browse short-term context and long-term memory":
        "Просмотр кратковременного контекста и долговременной памяти",
    "Delete an individual entry": "Удаление отдельной записи",
    "Clear conversation history": "Очистка истории диалогов",
    "Erase all memory": "Полное стирание памяти",
    "Disable history recording entirely": "Полное отключение записи истории",
    "DATABASE": "БАЗА ДАННЫХ",
    "(empty until Phase 9)": "(пусто до фазы 9)",

    # --- models page -------------------------------------------------------
    "Local GGUF models, their memory requirements and how they are loaded.":
        "Локальные модели GGUF, их требования к памяти и способ загрузки.",
    "Discover GGUF models placed in the models folder":
        "Обнаружение моделей GGUF в папке models",
    "Report size, context length and VRAM requirement per model":
        "Размер, длина контекста и требования к видеопамяти для каждой модели",
    "Choose CPU, GPU or hybrid execution and set GPU layer count":
        "Выбор режима CPU, GPU или гибридного и настройка числа слоёв на GPU",
    "Tune context size, temperature, top-p, threads and batch size":
        "Настройка размера контекста, температуры, top-p, потоков и размера батча",
    "Refuse to load a model that cannot fit in available memory":
        "Отказ от загрузки модели, которая не помещается в доступную память",
    "MODEL FOLDER": "ПАПКА МОДЕЛЕЙ",
    "no NVML-capable GPU detected":
        "видеокарта с поддержкой NVML не обнаружена",
    "PLANNED BACKEND": "ПЛАНИРУЕМЫЙ БЭКЕНД",
    "llama.cpp, Vulkan (no CUDA Toolkit required)":
        "llama.cpp, Vulkan (CUDA Toolkit не требуется)",

    # --- voice page --------------------------------------------------------
    "Microphone, speech recognition and speech synthesis - all running locally.":
        "Микрофон, распознавание и синтез речи — всё работает локально.",
    "Select and test a microphone": "Выбор и проверка микрофона",
    "Voice activity detection with adjustable sensitivity":
        "Детектор речевой активности с настраиваемой чувствительностью",
    "Local speech recognition with whisper.cpp":
        "Локальное распознавание речи через whisper.cpp",
    "Local speech synthesis with voice, speed, pitch and volume control":
        "Локальный синтез речи с настройкой голоса, скорости, тона и громкости",
    "Wake word, push-to-talk and continuous conversation modes":
        "Режимы активации по слову, удержания клавиши и непрерывного диалога",
    "AUDIO": "АУДИО",
    "Qt Multimedia is not linked yet; it arrives with the capture pipeline in Phase 4":
        "Qt Multimedia пока не подключён; появится вместе с конвейером захвата в фазе 4",
    "PRIVACY": "ПРИВАТНОСТЬ",
    "Audio is processed on this machine only. No audio leaves the device.":
        "Аудио обрабатывается только на этом компьютере. Ничего не покидает устройство.",

    # --- security page -----------------------------------------------------
    "What JARVIS is able to do, and what it is currently doing.":
        "Что JARVIS может делать и что он делает сейчас.",
    "Permission levels: SAFE, LOW, MEDIUM, HIGH, CRITICAL":
        "Уровни доступа: SAFE, LOW, MEDIUM, HIGH, CRITICAL",
    "Confirmation prompts before every HIGH and CRITICAL action":
        "Подтверждение перед каждым действием уровня HIGH и CRITICAL",
    "Master system control switch that disables all system tools":
        "Главный выключатель, отключающий все системные инструменты",
    "Protected paths that no tool can modify":
        "Защищённые пути, которые не может изменить ни один инструмент",
    "A full audit of microphone, file, network and startup access":
        "Полный аудит доступа к микрофону, файлам, сети и автозапуску",
    "SYSTEM TOOLS": "СИСТЕМНЫЕ ИНСТРУМЕНТЫ",
    "None exist. JARVIS cannot execute any system action in this build.":
        "Их нет. В этой сборке JARVIS не может выполнить ни одного системного действия.",
    "MICROPHONE": "МИКРОФОН",
    "Not accessed. No audio subsystem is linked.":
        "Не используется. Аудиоподсистема не подключена.",
    "No outbound connection is made. Telemetry counters are read locally.":
        "Исходящие соединения не создаются. Счётчики телеметрии читаются локально.",
    "AUTOSTART": "АВТОЗАПУСК",
    "No autostart entry and no persistence mechanism is created.":
        "Запись автозапуска и механизмы закрепления в системе не создаются.",
    "DATA": "ДАННЫЕ",
    "LOG CONTENT": "СОДЕРЖИМОЕ ЖУРНАЛА",
    "Identifiers and outcomes only. No user content is written to disk.":
        "Только идентификаторы и результаты. Пользовательский контент на диск не пишется.",

    # --- settings page -----------------------------------------------------
    "Every option here is written to config.json and takes effect as described.":
        "Каждая настройка записывается в config.json и действует так, как описано.",
    "LANGUAGE": "ЯЗЫК",
    "Interface and reply language": "Язык интерфейса и ответов",
    "Applied immediately, without restarting. The assistant will also answer in "
    "this language by default, while still understanding the others.":
        "Применяется сразу, без перезапуска. Ассистент будет отвечать на этом "
        "языке по умолчанию, продолжая понимать остальные.",
    "Application names, file names, paths and commands are never translated.":
        "Названия приложений, имена файлов, пути и команды никогда не переводятся.",
    "LOGGING": "ЖУРНАЛИРОВАНИЕ",
    "Log level": "Уровень журналирования",
    "Applied to the running logger immediately and saved to config.json.":
        "Применяется к работающему логгеру сразу и сохраняется в config.json.",
    "Write log to console": "Писать журнал в консоль",
    "Takes effect on the next start: sinks are attached during bootstrap.":
        "Вступит в силу при следующем запуске: приёмники подключаются при инициализации.",
    "RETENTION": "ХРАНЕНИЕ",
    "%1 days": "%1 дн.",
    "LOG FILE": "ФАЙЛ ЖУРНАЛА",
    "unavailable - file logging could not start":
        "недоступен — журналирование в файл не запустилось",
    "WINDOW": "ОКНО",
    "Remember window size and position": "Запоминать размер и положение окна",
    "Saved when the window closes, restored on the next start.":
        "Сохраняется при закрытии окна и восстанавливается при следующем запуске.",
    "AI CORE - VISUAL PREVIEW": "ЯДРО ИИ — ПРЕДПРОСМОТР СОСТОЯНИЙ",
    "PREVIEW ACTIVE": "ПРЕДПРОСМОТР ВКЛЮЧЁН",
    "A development tool. It forces the Core's visual state so the interface can "
    "be inspected before the engines exist. It does not start, stop or simulate "
    "any engine, and the Core shows a PREVIEW badge the whole time it is active.":
        "Инструмент разработчика. Он принудительно задаёт визуальное состояние "
        "ядра, чтобы интерфейс можно было проверить до появления движков. Он "
        "ничего не запускает, не останавливает и не имитирует, а ядро всё время "
        "показывает отметку ПРЕДПРОСМОТР.",
    "STOP PREVIEW": "ОСТАНОВИТЬ",
    "The engine state underneath is still OFFLINE.":
        "Реальное состояние движка по-прежнему НЕ В СЕТИ.",
    "Showing the real engine state.": "Показано реальное состояние движка.",
    "BUILD AND STORAGE": "СБОРКА И ХРАНИЛИЩЕ",
    "VERSION": "ВЕРСИЯ",
    "COMPILER": "КОМПИЛЯТОР",
    "STANDARD": "СТАНДАРТ",
    "BUILT": "СОБРАНО",
    "ROOT": "КОРЕНЬ",
    "CONFIG": "КОНФИГ",
    "OPEN LOG FOLDER": "ПАПКА ЖУРНАЛОВ",
    "OPEN CONFIG": "ОТКРЫТЬ КОНФИГ",
    "COPIED": "СКОПИРОВАНО",
    "COPY DIAGNOSTICS": "КОПИРОВАТЬ ДИАГНОСТИКУ",

    # --- models page -------------------------------------------------------
    "Local GGUF models. Nothing is downloaded: JARVIS lists what is already on this machine.":
        "Локальные модели GGUF. Ничего не скачивается: JARVIS показывает то, что уже есть на этом компьютере.",
    "ENGINE": "ДВИЖОК",
    "BACKEND": "БЭКЕНД",
    "GPU READY": "GPU ГОТОВ",
    "CPU ONLY": "ТОЛЬКО CPU",
    "GPU DEVICE": "УСТРОЙСТВО GPU",
    "CPU DEVICE": "УСТРОЙСТВО CPU",
    " free of ": " свободно из ",
    "LOADED": "ЗАГРУЖЕНА",
    "LOADED MODEL": "ЗАГРУЖЕННАЯ МОДЕЛЬ",
    "GPU ACCELERATED": "УСКОРЕНИЕ GPU",
    "CPU INFERENCE": "ИНФЕРЕНС НА CPU",
    "NAME": "НАЗВАНИЕ",
    "GPU LAYERS": "СЛОЁВ НА GPU",
    "CONTEXT": "КОНТЕКСТ",
    " tokens": " токенов",
    "FILE": "ФАЙЛ",
    "LAST RUN": "ПОСЛЕДНИЙ ЗАПУСК",
    "UNLOAD": "ВЫГРУЗИТЬ",
    "AVAILABLE MODELS": "ДОСТУПНЫЕ МОДЕЛИ",
    "SCANNING": "ПОИСК",
    "%1 FOUND": "НАЙДЕНО: %1",
    "RESCAN": "ОБНОВИТЬ",
    "Searched: the models folder and the Ollama store.":
        "Искали: папку models и хранилище Ollama.",
    "No GGUF language model found. Put one in the models folder, or pull one with Ollama.":
        "Языковых моделей GGUF не найдено. Положите модель в папку models или загрузите через Ollama.",
    " layers": " слоёв",
    "context ": "контекст ",
    "estimated VRAM at the configured context: ":
        "оценка видеопамяти при заданном контексте: ",
    "LOADING…": "ЗАГРУЗКА…",
    "LOAD": "ЗАГРУЗИТЬ",
    "NO MODEL": "НЕТ МОДЕЛИ",

    # --- conversation page -------------------------------------------------
    "CONVERSATION": "ДИАЛОГ",
    "GENERATING": "ГЕНЕРАЦИЯ",
    "%1 · %2 layers on %3": "%1 · слоёв %2 на %3",
    "Load a model on the MODELS page to start a conversation.":
        "Загрузите модель на странице МОДЕЛИ, чтобы начать диалог.",
    "YOU": "ВЫ",
    "Ask something. The model runs entirely on this machine.":
        "Спросите что-нибудь. Модель работает полностью на этом компьютере.",
    "No model is loaded.": "Модель не загружена.",
    "MESSAGE": "СООБЩЕНИЕ",
    "Type a message…": "Введите сообщение…",
    "Load a model first": "Сначала загрузите модель",
    "CLEAR": "ОЧИСТИТЬ",
    "STOP": "СТОП",
    "SEND": "ОТПРАВИТЬ",

    # --- HUD ---------------------------------------------------------------
    " · GPU": " · GPU",
    " · CPU": " · CPU",
    "NOT LOADED": "НЕ ЗАГРУЖЕНА",

    # --- voice page --------------------------------------------------------
    "VOICE": "ГОЛОС",
    "Microphone, speech recognition and speech synthesis — all running on this "
    "machine. No audio leaves the device.":
        "Микрофон, распознавание и синтез речи — всё работает на этом компьютере. "
        "Аудио не покидает устройство.",
    "PIPELINE": "КОНВЕЙЕР",
    "ON": "ВКЛ",
    "DISABLED": "ОТКЛЮЧЁН",
    "UNAVAILABLE": "НЕДОСТУПЕН",
    "TRANSCRIBING": "РАСПОЗНАЮ",
    "SYNTHESIZING": "СИНТЕЗИРУЮ",
    "VOICE ERROR": "ОШИБКА ГОЛОСА",
    "Voice enabled": "Голос включён",
    "Opens the microphone and starts the pipeline.":
        "Открывает микрофон и запускает конвейер.",
    "Microphone": "Микрофон",
    "Speaker": "Динамик",
    "unavailable": "недоступен",
    "model not loaded": "модель не загружена",
    "no voice installed": "голос не установлен",
    "LEVELS": "УРОВНИ",
    "REAL RMS": "РЕАЛЬНЫЙ RMS",
    "INPUT LEVEL": "УРОВЕНЬ ВХОДА",
    "OUTPUT LEVEL": "УРОВЕНЬ ВЫХОДА",
    "capturing": "идёт захват",
    "microphone closed": "микрофон закрыт",
    "NOT CAPTURING": "ЗАХВАТА НЕТ",
    "playing": "воспроизведение",
    "silent": "тишина",
    "NOT PLAYING": "НЕ ИГРАЕТ",
    "DEVICES": "УСТРОЙСТВА",
    "Selecting a device restarts capture immediately.":
        "Выбор устройства немедленно перезапускает захват.",
    "RECOGNITION AND SYNTHESIS": "РАСПОЗНАВАНИЕ И СИНТЕЗ",
    "Language": "Язык",
    "Recognition, the assistant's replies and the voice all follow the interface "
    "language.":
        "Распознавание, ответы ассистента и голос следуют языку интерфейса.",
    "Microphone sensitivity": "Чувствительность микрофона",
    "How loud speech must be before recognition starts. Lower is more sensitive.":
        "Насколько громкой должна быть речь, чтобы началось распознавание. "
        "Меньше — чувствительнее.",
    "LAST HEARD": "ПОСЛЕДНЕЕ УСЛЫШАННОЕ",
    "nothing recognised yet": "пока ничего не распознано",
    "VERIFIABLE": "ПРОВЕРЯЕМО",
    "open, audio kept in memory only": "открыт, аудио только в памяти",
    "closed": "закрыт",
    "Whisper, local": "Whisper, локально",
    "Piper, local": "Piper, локально",
    "no audio upload": "аудио никуда не отправляется",
    "RECORDINGS": "ЗАПИСИ",
    "nothing is written to disk": "на диск ничего не пишется",
    "NO VOICE": "НЕТ ГОЛОСА",

    # --- audio and voice errors -------------------------------------------
    "The selected microphone is no longer available.":
        "Выбранный микрофон больше недоступен.",
    "No microphone is available.": "Микрофон недоступен.",
    "The microphone reports no usable audio format.":
        "Микрофон не сообщает ни одного пригодного формата.",
    "The microphone could not be opened.": "Не удалось открыть микрофон.",
    "The microphone stopped responding.": "Микрофон перестал отвечать.",
    "The microphone was disconnected.": "Микрофон отключён.",
    "Audio input error.": "Ошибка аудиовхода.",
    "The selected speaker is no longer available.":
        "Выбранное устройство вывода больше недоступно.",
    "There is no audio to play.": "Нечего воспроизводить.",
    "No speaker is available.": "Устройство вывода недоступно.",
    "The speaker does not support the required audio format.":
        "Устройство вывода не поддерживает нужный формат аудио.",
    "The audio buffer could not be opened.": "Не удалось открыть аудиобуфер.",
    "Playback could not start.": "Не удалось начать воспроизведение.",
    "The speaker could not be opened.": "Не удалось открыть устройство вывода.",
    "The speaker stopped responding.": "Устройство вывода перестало отвечать.",
    "The speaker was disconnected.": "Устройство вывода отключено.",
    "Audio output error.": "Ошибка аудиовыхода.",
    "The speech recognition model is not loaded.":
        "Модель распознавания речи не загружена.",
    "No voice is installed for this language.":
        "Для этого языка не установлен голос.",
    "No language model is loaded.": "Языковая модель не загружена.",
    "The model could not answer.": "Модель не смогла ответить.",
    "The assistant is busy with another request.":
        "Ассистент занят другим запросом.",

    # --- C++ strings -------------------------------------------------------
    "CPU only": "Только CPU",
    "The local model engine is not available in this build.":
        "Движок локальной модели недоступен в этой сборке.",
    "Load a model before sending a message.":
        "Загрузите модель, прежде чем отправлять сообщение.",
    "%1 tokens · %2 tok/s · prompt %3 tokens":
        "%1 токенов · %2 ток/с · промпт %3 токенов",
    "AI engine not connected": "Движок ИИ не подключён",
    "The local model, speech and tool engines arrive in later phases.":
        "Локальная модель, речевые движки и инструменты появятся в следующих фазах.",
    "Ready": "Готов",
    "Waiting for a command.": "Ожидание команды.",
    "Listening": "Слушаю",
    "Capturing speech.": "Идёт запись речи.",
    "Understanding command": "Разбираю команду",
    "Working out what to do.": "Определяю, что нужно сделать.",
    "Executing": "Выполняю",
    "Running the requested action.": "Выполняю запрошенное действие.",
    "Responding": "Отвечаю",
    "Speaking the answer.": "Произношу ответ.",
    "Attention required": "Требуется внимание",
    "A subsystem needs your attention.": "Одна из подсистем требует вашего внимания.",
    "Error": "Ошибка",
    "Something failed. Check the log for details.":
        "Что-то не сработало. Подробности в журнале.",
    "Translations for this language are unavailable.":
        "Переводы для этого языка недоступны.",
    "GB": "ГБ",
    "MB": "МБ",
    "MB/s": "МБ/с",
    "KB/s": "КБ/с",
    "Unknown processor": "Неизвестный процессор",
    "%1 cores / %2 threads": "%1 ядер / %2 потоков",
    "No NVML-capable GPU": "Нет видеокарты с поддержкой NVML",

    # --- Phase 6: typographic keys, written as escapes ---------------------
    #
    # Ellipses, middle dots and curly quotes kept getting mangled by the
    # editing tools used on this file. Spelling them out removes the doubt.
    'FAILED': 'ОШИБКА',
    '×%1': '×%1',
    '%1 · %2 layers on %3': '%1 · %2 слоёв на %3',
    'Type a message…': 'Напишите сообщение…',
    'OFFLINE · LOCAL ONLY': 'АВТОНОМНО · ТОЛЬКО ЛОКАЛЬНО',
    'LOADING…': 'ЗАГРУЗКА…',
    'NOT AVAILABLE · %1': 'НЕДОСТУПНО · %1',
    ' · GPU': ' · GPU',
    ' · CPU': ' · CPU',
    'Actions marked “asks first” never run until you allow that exact request.': 'Действия с пометкой «спрашивает» не выполняются, пока вы не разрешите именно этот запрос.',
    'Reconsidering…': 'Пересматриваю…',
    'Working on it…': 'Работаю…',
    'Looking that up…': 'Уточняю…',
    '%1 tokens · %2 tok/s · prompt %3 tokens': '%1 токенов · %2 ток/с · промпт %3 токенов',

    # --- Phase 6: the agent page ---------------------------------------------
    "AGENT": "АГЕНТ",
    "CURRENT TASK": "ТЕКУЩАЯ ЗАДАЧА",
    "STEPS": "ШАГИ",
    "MEMORY": "ПАМЯТЬ",
    "READY": "ГОТОВ",
    "What the assistant is doing right now, and what it has been asked to do. "
    "Every step goes through the same checks as a single action.":
        "Что ассистент делает прямо сейчас и о чём его попросили. Каждый шаг "
        "проходит те же проверки, что и одиночное действие.",
    "No task is running. Ask a question to start one.":
        "Задач нет. Задайте вопрос, чтобы начать.",
    "The agent is switched off in the configuration.":
        "Агент выключен в конфигурации.",
    "retry %1": "повтор %1",
    "Step %1 of %2": "Шаг %1 из %2",
    "Stop": "Стоп",
    "×%1": "×%1",
    "KEPT": "СОХРАНЕНО",
    "AFTER RESTART": "ПОСЛЕ ПЕРЕЗАПУСКА",
    "Kept on disk.": "Хранится на диске.",
    "Forgotten. Nothing is written to disk.":
        "Забывается. На диск ничего не пишется.",
    "The assistant may keep a few facts between tasks. They are shown to the "
    "model as data and carry no permissions.":
        "Ассистент может сохранять отдельные факты между задачами. Модели они "
        "передаются как данные и не дают никаких прав.",
    "The assistant remembers nothing between tasks.":
        "Ассистент ничего не запоминает между задачами.",

    # Task statuses, as the page shows them.
    "CREATED": "СОЗДАНА",
    "RUNNING": "ВЫПОЛНЯЕТСЯ",
    "COMPLETED": "ЗАВЕРШЕНА",
    "STOPPED": "ОСТАНОВЛЕНА",

    # Step statuses.
    "waiting": "ожидает",
    "needs your confirmation": "нужно ваше подтверждение",
    "running": "выполняется",
    "done": "готово",
    "failed": "ошибка",
    "skipped": "пропущен",
    "stopped": "остановлен",

    # --- Phase 6: agent loop -------------------------------------------------
    "Understanding the request": "Разбираю запрос",
    "Waiting for a tool": "Жду инструмент",
    "Reading the result": "Читаю результат",
    "Answering": "Отвечаю",
    "Done": "Готово",
    "Stopped": "Остановлено",
    "Stopped.": "Остановлено.",
    "Unavailable": "Недоступно",
    "Reconsidering…": "Пересматриваю…",
    "Working on it…": "Работаю…",
    "The plan could not be started.": "Не удалось начать выполнение плана.",
    "A step could not be started.": "Не удалось начать шаг.",
    "The model did not produce an answer.": "Модель не выдала ответа.",
    "The model used its whole %1-token budget thinking and did not reach an "
    "answer. Raise the reply length limit.":
        "Модель израсходовала весь бюджет в %1 токенов на рассуждение и не "
        "дошла до ответа. Увеличьте предел длины ответа.",
    "This task reached its limit of %1 actions.":
        "Задача исчерпала предел в %1 действий.",
    "This task ran for longer than allowed.":
        "Задача выполнялась дольше допустимого.",
    "The previous request is still finishing. Try again in a moment.":
        "Предыдущий запрос ещё завершается. Попробуйте через мгновение.",
    "The conversation no longer fits in the model's context. Start a new "
    "conversation.":
        "Разговор больше не помещается в контекст модели. Начните новый разговор.",

    # Phase 6: the Destructive permission level.
    "Irreversible": "Необратимое",

    # --- Phase 6: agent state ------------------------------------------------
    #
    # The three states the Core gained when the agent arrived. The keys stay
    # English; only what the user reads is translated.
    "PLANNING": "ПЛАНИРОВАНИЕ",
    "CONFIRMING": "ПОДТВЕРЖДЕНИЕ",
    "RECOVERING": "ВОССТАНОВЛЕНИЕ",
    "STATE": "СОСТОЯНИЕ",
    "Planning": "Планирование",
    "Working out the steps.": "Определяю последовательность действий.",
    "Waiting for you": "Жду вас",
    "An action needs your confirmation.": "Действие требует вашего подтверждения.",
    "Recovering": "Восстановление",
    "A step failed. Trying to continue.":
        "Шаг не удался. Пытаюсь продолжить.",

    # --- Phase 5: tools, permissions, confirmation, audit --------------------
    #
    # Tool names (system_info, open_application) are identifiers and stay in
    # English, like every other technical name in this interface. What is
    # translated is what the user reads about them.
    "TOOLS": "ИНСТРУМЕНТЫ",
    "ACTIVITY": "АКТИВНОСТЬ",
    "AVAILABLE TOOLS": "ДОСТУПНЫЕ ИНСТРУМЕНТЫ",
    "ENFORCED IN CODE": "ОБЕСПЕЧЕНО КОДОМ",
    "AUDIT": "ЖУРНАЛ ДЕЙСТВИЙ",
    "ENABLED": "ВКЛЮЧЕНЫ",
    "DISABLED": "ВЫКЛЮЧЕНЫ",
    "The complete set of actions the assistant can take on this machine. "
    "Anything not listed here does not exist and cannot be requested.":
        "Полный перечень действий, доступных ассистенту на этой машине. "
        "Всё, чего нет в списке, не существует и не может быть запрошено.",
    "The assistant cannot run shell commands. No tool accepts a command line.":
        "Ассистент не может выполнять команды оболочки. Ни один инструмент не "
        "принимает командную строку.",
    "The assistant cannot name a file or a program by path. No tool accepts one.":
        "Ассистент не может указать файл или программу по пути. Ни один "
        "инструмент не принимает путь.",
    "The assistant cannot add tools, change permissions or confirm on your behalf.":
        "Ассистент не может добавлять инструменты, менять права или "
        "подтверждать действия за вас.",
    "Actions marked “asks first” never run until you allow that exact request.":
        "Действия с пометкой «спрашивает» не выполняются, пока вы не разрешите "
        "именно этот запрос.",
    "Results from tools are treated as data. Instructions inside them are ignored.":
        "Результаты инструментов считаются данными. Инструкции внутри них "
        "игнорируются.",
    "Nothing has been requested yet.": "Пока ничего не запрашивалось.",
    "Auditing is switched off in the configuration.":
        "Журналирование отключено в конфигурации.",
    "Clear the audit log": "Очистить журнал",
    "OFF": "ВЫКЛ",

    # Permission levels.
    "Read only": "Только чтение",
    "Safe action": "Безопасное действие",
    "Asks first": "Спрашивает",
    "Blocked": "Запрещено",

    # Audit events.
    "Request accepted": "Запрос принят",
    "Request rejected": "Запрос отклонён",
    "Permission denied": "Отказано в правах",
    "Confirmation requested": "Запрошено подтверждение",
    "Confirmed": "Подтверждено",
    "Declined": "Отклонено пользователем",
    "Confirmation expired": "Подтверждение просрочено",
    "Started": "Начато",
    "Completed": "Выполнено",
    "Failed": "Ошибка",
    "Cancelled": "Отменено",
    "Round limit reached": "Достигнут предел раундов",

    # Applications on the allowlist. These are the names Windows itself uses.
    "Calculator": "Калькулятор",
    "Notepad": "Блокнот",
    "File Explorer": "Проводник",
    "Windows Settings": "Параметры Windows",

    # Coordinator phases.
    "Checking the request": "Проверка запроса",
    "Checking permission": "Проверка прав",
    "Waiting for your confirmation": "Ожидание вашего подтверждения",
    "Running": "Выполняется",
    "Running %1": "Выполняется %1",

    # Confirmation dialog.
    "CONFIRMATION REQUIRED": "ТРЕБУЕТСЯ ПОДТВЕРЖДЕНИЕ",
    "Confirm this action": "Подтвердите действие",
    "JARVIS wants to open %1.": "JARVIS хочет открыть %1.",
    "JARVIS wants to run %1.": "JARVIS хочет выполнить %1.",
    "JARVIS wants to run %1 with %2.": "JARVIS хочет выполнить %1 с %2.",
    "This will not happen unless you allow it. The assistant cannot confirm on "
    "your behalf.":
        "Это не произойдёт, пока вы не разрешите. Ассистент не может "
        "подтвердить действие за вас.",
    "Allow": "Разрешить",
    "Cancel": "Отмена",
    "%1 s": "%1 с",

    # Conversation.
    "Looking that up…": "Уточняю…",
    "Used %1.": "Использован %1.",
    "I could not finish that within the allowed number of steps.":
        "Не удалось закончить за допустимое число шагов.",

    # Security page.
    "TOOLS ON": "ИНСТРУМЕНТЫ ВКЛ",
    "TOOLS OFF": "ИНСТРУМЕНТЫ ВЫКЛ",
    "BOUNDARIES": "ГРАНИЦЫ",
    "TRUST MODEL": "МОДЕЛЬ ДОВЕРИЯ",
    "NOT YET IMPLEMENTED": "ПОКА НЕ РЕАЛИЗОВАНО",
    "What JARVIS is able to do on this machine, and what it has actually done.":
        "Что JARVIS может делать на этой машине и что он действительно сделал.",
    "SHELL ACCESS": "ОБОЛОЧКА",
    "FILE ACCESS": "ФАЙЛЫ",
    "APPLICATIONS": "ПРИЛОЖЕНИЯ",
    "None. No tool accepts a command, a command line or a script.":
        "Нет. Ни один инструмент не принимает команду, командную строку или скрипт.",
    "None. No tool accepts a path, and none reads or writes user files.":
        "Нет. Ни один инструмент не принимает путь и не читает и не пишет "
        "пользовательские файлы.",
    "Four, by name, each requiring your confirmation. No other program can be "
    "started.":
        "Четыре, поимённо, каждое требует вашего подтверждения. Никакая другая "
        "программа запущена быть не может.",
    "Open while the voice pipeline is running. Audio stays in memory and is "
    "never written to disk.":
        "Открыт, пока работает голосовой конвейер. Звук остаётся в памяти и "
        "никогда не записывается на диск.",
    "Not opened. The voice pipeline is switched off.":
        "Не используется. Голосовой конвейер выключен.",
    "No outbound connection is made. Every model runs on this machine.":
        "Исходящие соединения не устанавливаются. Все модели работают на этой машине.",
    "The language model is treated as untrusted input. Its output is parsed, "
    "checked against a fixed schema and matched to a fixed list of tools before "
    "anything happens. None of those checks consult the model's instructions, "
    "so a model that ignored every one of them would reach exactly the same "
    "boundary.":
        "Языковая модель считается недоверенным вводом. Её вывод разбирается, "
        "проверяется по фиксированной схеме и сопоставляется с фиксированным "
        "списком инструментов до того, как что-либо произойдёт. Ни одна из этих "
        "проверок не обращается к инструкциям модели, поэтому модель, "
        "проигнорировавшая их все, упрётся ровно в ту же границу.",
    "The most a compromised model can obtain is a read-only fact about this "
    "computer, or an action you allowed by hand.":
        "Максимум, что может получить скомпрометированная модель, — факт о "
        "компьютере, доступный только для чтения, или действие, которое вы "
        "разрешили вручную.",
    "model output": "вывод модели",
    "strict validation": "строгая проверка",
    "permission check": "проверка прав",
    "your confirmation, if required": "ваше подтверждение, если требуется",
    "execution": "выполнение",
    "audit record": "запись в журнал",
    "File access with protected paths": "Доступ к файлам с защищёнными путями",
    "Per-tool permission editing from the interface":
        "Настройка прав для каждого инструмента из интерфейса",
    "A persistent audit log on disk": "Постоянный журнал действий на диске",
    "Scheduled and automated actions": "Отложенные и автоматические действия",
}

# Same source, different meaning depending on where it is used.
BY_CONTEXT: dict[tuple[str, str], str] = {
    ("Main", "MEMORY"): "ПАМЯТЬ",          # navigation: the memory subsystem
    ("MemoryPage", "MEMORY"): "ПАМЯТЬ",    # page title
    ("HomePage", "MEMORY"): "ОЗУ",         # metric label
    ("SystemPage", "MEMORY"): "ОЗУ",       # metric label

    # The subsystem panel. "Agent" here is the subsystem, not the page title,
    # and it sits beside Tool engine and Permissions rather than in the nav.
    ("HomePage", "Agent"): "Агент",
    ("HomePage", "OFF BY DEFAULT"): "ВЫКЛ ПО УМОЛЧАНИЮ",

    # The active-task readout under the control centre.
    ("HomePage", "the agent is switched off"): "агент выключен",
    ("HomePage", "waiting for a request"): "ожидание запроса",
    ("HomePage", "step %1 of %2%3"): "шаг %1 из %2%3",
}


def fill(path: Path, translate) -> tuple[int, list[str]]:
    tree = ET.parse(path)
    root = tree.getroot()

    count = 0
    missing: list[str] = []

    for context in root.findall("context"):
        name = context.findtext("name") or ""
        for message in context.findall("message"):
            source = message.findtext("source") or ""
            node = message.find("translation")
            if node is None:
                node = ET.SubElement(message, "translation")

            text = translate(name, source)
            if text is None:
                missing.append(f"{name} | {source}")
                continue

            node.text = text
            node.attrib.pop("type", None)  # clears type="unfinished"
            count += 1

    tree.write(path, encoding="utf-8", xml_declaration=True)

    # ElementTree drops the doctype; Qt's tools want it back.
    content = path.read_text(encoding="utf-8")
    if "<!DOCTYPE TS>" not in content:
        content = content.replace(
            "<TS ", "<!DOCTYPE TS>\n<TS ", 1)
        path.write_text(content, encoding="utf-8")

    return count, missing


def main() -> int:
    russian_count, russian_missing = fill(
        RU, lambda ctx, src: BY_CONTEXT.get((ctx, src), RUSSIAN.get(src)))

    # English is the source language: the catalogue is an identity mapping, which
    # keeps lrelease from reporting every string as untranslated.
    english_count, english_missing = fill(EN, lambda ctx, src: src)

    print(f"ru: {russian_count} translated, {len(russian_missing)} missing")
    print(f"en: {english_count} translated, {len(english_missing)} missing")

    if russian_missing:
        print("\nUNTRANSLATED - add these to RUSSIAN in this script:",
              file=sys.stderr)
        for entry in russian_missing:
            print(f"  {entry}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
