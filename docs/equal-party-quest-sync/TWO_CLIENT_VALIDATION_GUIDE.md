# Two-Client Equal-Party Quest Sync Validation Guide (Task 13)

---

## 🇷🇺 РУССКАЯ ВЕРСИЯ (RUSSIAN VERSION)

### 1. Введение и главные правила безопасности (P0 Safety)

Данная инструкция предназначена для тестеров и игроков, проводящих валидацию первого этапа канонического применения стадий квестов (`TESQuest::SetStage`) в рамках архитектуры **Equal-Party Quest Sync**.

> [!CAUTION]
> **ПРАВИЛО ОДНОРАЗОВЫХ СОХРАНЕНИЙ (DISPOSABLE SAVES ONLY):**
> **НИКОГДА** не используйте ваши основные сохранения или рабочие сохранения персонажей!
> Для тестирования создаются исключительно новые, изолированные сохранения-пустышки.

> [!IMPORTANT]
> **КОНТРОЛИРУЕМАЯ АКТИВАЦИЯ (CONTROLLED ACTIVATION):**
> По умолчанию в релизных сборках нативная мутация квестов **ОТКЛЮЧЕНА**.
> Чтобы включить механизм синхронизации для тестирования, перед запуском игры необходимо выставить переменную окружения:
> `ST_EQUAL_PARTY_CONTROLLED_MUTATION=1`

---

### 2. Подготовка тестового стенда

#### Требования к окружению:
1. **Skyrim Special Edition 1.6.1170** (чистая установка без посторонних геймплейных модов).
2. **SKSE64 2.2.6** + **Address Library for SKSE Plugins**.
3. **Mod Organizer 2** (или две изолированные папки профилей):
   - Профиль **Client A**: отдельная папка сохранений `Saves_ClientA`.
   - Профиль **Client B**: отдельная папка сохранений `Saves_ClientB`.
4. Серверный бинарник: `SkyrimServerRunner.exe` (запускается локально на `127.0.0.1:10578`).

#### Подготовка переменных окружения (Client A и Client B):
В Mod Organizer 2: `Настройки` -> `Переменные окружения` (или создайте `.bat` файл для запуска):
```cmd
set ST_EQUAL_PARTY_CONTROLLED_MUTATION=1
skse64_loader.exe
```

---

### 3. Рецензированные квесты для тестирования (Reviewed Candidates)

В Task 12 были строго верифицированы два чистых vanilla-квеста без псевдонимов (aliases) и без скриптовых побочных эффектов на мир:

| Имя квеста в Creation Kit | FormID (hex) | Начальная стадия (Source) | Целевая стадия (Target) | Описание |
|---|---|---|---|---|
| `dunNorthwatchKeepQST` | `0x000C07C1` | `0` | `100` | Квест зачистки Северной сторожевой крепости |
| `dunKarthspireRedoubtQST` | `0x000357E5` | `10` | `100` | Квест лагеря Картспайр |

---

### 4. Пошаговый план тестирования (Матрица из 6 сценариев)

#### Сценарий 1: Прямая синхронизация (Initial Sync)
**Цель:** Проверить, что когда Клиент А переводит квест на новую стадию, Клиент Б автоматически и корректно переходит на ту же стадию.
1. Запустите `SkyrimServerRunner.exe`.
2. Запустите Клиент А и Клиент Б (в оконном режиме для удобства наблюдения).
3. На обоих клиентах загрузите тестовое сохранение (где персонаж стоит, например, в тестовой локации `coc Riverwood` или `coc Whiterun`).
4. На обоих клиентах в консоли (`~`) проверьте текущую стадию:
   ```text
   getstage 000C07C1
   ```
   *Ожидаемый ответ на обоих:* `0.00`
5. В интерфейсе Skyrim Together подключитесь обоими клиентами к серверу `127.0.0.1`.
6. Клиент А приглашает Клиента Б в группу (Party). Клиент Б принимает приглашение.
7. На **Клиенте А** в консоли выполните:
   ```text
   setstage 000C07C1 100
   ```
8. **Проверка на Клиенте Б:**
   - Подождите 1–2 секунды.
   - В консоли Клиента Б введите:
     ```text
     getstage 000C07C1
     ```
   - **Ожидаемый результат:** Стадия изменилась на `100.00` без вылетов и лагов!
   - В логах Клиента Б появится запись о верификации пост-условий (`postcondition verified stage=100`).

---

#### Сценарий 2: Примирение расходящихся сохранений (Divergent Reconciliation)
**Цель:** Проверить, что отстающий клиент при входе в группу догоняет лидера, а клиент, уже имеющий стадию 100, не выполняет повторный вызов движка (no-op).
1. На Клиенте А стадия `000C07C1` равна `0.00`.
2. На Клиенте Б стадия `000C07C1` уже равна `100.00`.
3. Оба клиента подключаются к серверу и объединяются в группу.
4. **Проверка:**
   - Клиент А автоматически получает каноническое состояние от сервера и нативно переходит на стадию `100.00`.
   - Клиент Б фиксирует `idempotent-no-op` (никаких повторных вызовов движка, стадия остается `100.00`).
   - На обоих клиентах `getstage 000C07C1` выводит `100.00`.

---

#### Сценарий 3: Одновременное изменение (Concurrent Update)
**Цель:** Проверить, что при одновременной попытке изменения обоими игроками сервер выбирает одну каноническую транзакцию, а дубликат безопасно отклоняется.
1. Оба клиента в группе, квест `dunKarthspireRedoubtQST` (`000357E5`) находится на стадии `10`.
2. По команде оба игрока почти одновременно вводят:
   ```text
   setstage 000357E5 100
   ```
3. **Проверка:**
   - Сервер регистрирует ровно одну выигрышную транзакцию.
   - Второй пакет признается дубликатом.
   - Ни один клиент не падает, оба остаются на стадии `100.00`.

---

#### Сценарий 4: Переподключение и защита от повторного вызова (Duplicate/Replay Protection)
**Цель:** Доказать, что повторно пришедший пакет обновления не вызывает `SetStage` второй раз.
1. Клиент Б, уже находясь на стадии `100.00`, нажимает "Disconnect" (отключается от сервера).
2. Клиент Б снова подключается к серверу и повторно входит в группу.
3. Сервер высылает текущее состояние квестов кампании.
4. **Проверка:**
   - Клиент Б распознает, что стадия `100` уже установлена.
   - В логе регистрируется `idempotent-no-op` или `duplicate-transaction-replayed`.
   - Ровно 0 повторных нативных вызовов `SetStage` в движок.

---

#### Сценарий 5: Отложенное применение при смене локации (Readiness Gating)
**Цель:** Проверить, что если клиент находится в процессе загрузки локации (loading screen), обновление ждет завершения загрузки и применяется только в безопасном состоянии игрового потока.
1. Клиент А начинает переход в другую ячейку (например, входит в дверь таверны, идет загрузочный экран).
2. В этот момент Клиент Б меняет стадию квеста.
3. **Проверка:**
   - Запрос Клиента А помещается в очередь `Deferred World Queue`.
   - Как только загрузка завершается и управление возвращается в главный поток обновления (Main Update Thread), операция выполняется чисто.
   - Игра не зависает на экране загрузки.

---

#### Сценарий 6: Изоляция посторонних квестов (Unrelated Quest Isolation)
**Цель:** Убедиться, что синхронизация рецензированного квеста никак не затрагивает другие квесты, инвентарь или мир.
1. На Клиенте А продвиньте любой другой квест (например, Когти дракона или Ветреный пик `MQ103` на стадию `50`):
   ```text
   setstage MQ103 50
   getstage MQ103
   ```
2. Выполните проверку Сценария 1 для квеста `000C07C1`.
3. После завершения проверьте:
   ```text
   getstage MQ103
   ```
4. **Ожидаемый результат:** Стадия `MQ103` строго равна `50.00` на Клиенте А и не изменилась на Клиенте Б. Никакие предметы инвентаря не пропали и не продублировались.

---

### 5. Чек-лист проверки логов

Файлы логов располагаются в:
`Documents\My Games\Skyrim Special Edition\SkyrimTogether\logs\tilted_online.log`

#### Зеленые токены (признаки успешной работы):
- `PartyQuestRuntime production bootstrap bound` — подсистема инициализирована.
- `PartyQuestStageMutationAdapter: native SetStage applied` — нативный переход выполнен.
- `PartyQuestStageMutationAdapter: postcondition verified` — пост-условия проверены и сошлись.
- `PartyQuestStageMutationAdapter: idempotent no-op` — безопасный пропуск при совпадении стадий.

#### Красные токены (сообщить разработчикам, если замечены):
- `unreviewed-quest` — попытка запустить синхронизацию нерецензированного квеста.
- `postcondition-mismatch` — стадия после вызова `SetStage` не совпала с целевой.
- `duplicate-transaction-replayed` — попытка повторного применения уже выполненной транзакции.
- `native-invocation-failed` — нативный вызов движка вернул false.

---

---

## 🇬🇧 ENGLISH VERSION

### 1. Introduction and Safety Rules (P0 Safety)

This guide is for testers and players conducting live validation of the narrow canonical quest stage mutation (`TESQuest::SetStage`) within the **Equal-Party Quest Sync** architecture.

> [!CAUTION]
> **DISPOSABLE SAVES ONLY:**
> **NEVER** use personal or main playthrough saves!
> All tests must use clean, disposable test saves created specifically for this verification.

> [!IMPORTANT]
> **CONTROLLED ACTIVATION:**
> Canonical runtime mutation is **DISABLED BY DEFAULT** in shipping binaries.
> To enable narrow stage mutation for this validation session, set the environment variable prior to launching:
> `ST_EQUAL_PARTY_CONTROLLED_MUTATION=1`

---

### 2. Test Setup

1. **Skyrim Special Edition 1.6.1170** (clean install without gameplay mods).
2. **SKSE64 2.2.6** + **Address Library for SKSE Plugins**.
3. **Mod Organizer 2** profiles:
   - **Client A**: Profile `TestClientA` (isolated save folder).
   - **Client B**: Profile `TestClientB` (isolated save folder).
4. **Server**: Run `SkyrimServerRunner.exe` locally (`127.0.0.1:10578`).
5. Launch clients with `ST_EQUAL_PARTY_CONTROLLED_MUTATION=1`.

---

### 3. Reviewed Quest Candidates

| Quest Identifier | FormID (hex) | Source Stage | Target Stage | Scope |
|---|---|---|---|---|
| `dunNorthwatchKeepQST` | `0x000C07C1` | `0` | `100` | Northwatch Keep clear quest |
| `dunKarthspireRedoubtQST` | `0x000357E5` | `10` | `100` | Karthspire Redoubt camp quest |

---

### 4. Step-by-Step Test Matrix

#### Scenario 1: Initial State & Forward Transition
1. Launch `SkyrimServerRunner.exe`.
2. Launch Client A and Client B; load disposable saves.
3. Check initial stage on both:
   ```text
   getstage 000C07C1
   ```
   Both must return `0.00`.
4. Connect both clients to `127.0.0.1` and form a party.
5. On **Client A**, trigger transition:
   ```text
   setstage 000C07C1 100
   ```
6. **Verify on Client B:**
   - Wait 1–2 seconds.
   - Run `getstage 000C07C1`. Stage must be `100.00`.
   - Client log confirms: `postcondition verified stage=100`.

#### Scenario 2: Divergent Reconciliation
1. Client A starts at stage `0`.
2. Client B starts at stage `100`.
3. Both connect and join party.
4. **Verify:**
   - Client A is brought to stage `100.00`.
   - Client B executes `idempotent-no-op` with 0 redundant engine calls.
   - Both clients report `getstage 000C07C1` -> `100.00`.

#### Scenario 3: Concurrent Operations
1. Both clients in party with `000357E5` at stage `10`.
2. Both players type `setstage 000357E5 100` simultaneously.
3. **Verify:**
   - Server orders transactions deterministically; exactly one transaction executes.
   - The competing request is deduplicated without error.
   - Both clients reach stage `100.00`.

#### Scenario 4: Replay & Reconnection Protection
1. Client B (at stage 100) disconnects and reconnects.
2. Server re-sends current party state.
3. **Verify:**
   - Client B recognizes stage 100 is already held.
   - No duplicate native `SetStage` call occurs.

#### Scenario 5: Deferred Readiness Gating
1. Client A enters a load door (loading screen active).
2. Client B advances quest stage.
3. **Verify:**
   - Client A queues update in `Deferred World Queue`.
   - Once loading completes on the main update thread, mutation applies safely.
   - No loading screen freeze or crash.

#### Scenario 6: Unrelated Quest Isolation
1. Advance unrelated quest on Client A (`setstage MQ103 50`).
2. Run Scenario 1 for `000C07C1`.
3. Check `getstage MQ103` on both clients.
4. **Verify:**
   - `MQ103` remains at `50.00` on Client A and unaffected on Client B.
   - No inventory items or world refs are altered.

---

### 5. Log Verification Checklist

Log file path:
`Documents\My Games\Skyrim Special Edition\SkyrimTogether\logs\tilted_online.log`

- **Success Tokens:**
  - `PartyQuestRuntime production bootstrap bound`
  - `PartyQuestStageMutationAdapter: native SetStage applied`
  - `PartyQuestStageMutationAdapter: postcondition verified`
  - `PartyQuestStageMutationAdapter: idempotent no-op`
- **Failure Tokens to Report:**
  - `unreviewed-quest`
  - `postcondition-mismatch`
  - `native-invocation-failed`
