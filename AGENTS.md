# AGENTS.md — arcana-embedded-esp32

Instructions for any AI coding agent (Claude Code, Codex, Cursor, Copilot, Gemini…) working
in this repository **or in any product derived from it**. `CLAUDE.md`, `GEMINI.md` and
`.github/copilot-instructions.md` all point here; this file is the single source.

> **You MUST NOT report a task as done until `tools/arch-check.sh` exits 0.** Violations are
> blocked twice: the pre-commit hook (`tools/githooks/pre-commit`) rejects new flat source
> files and `*Impl.hpp` includes outside the DI container, and the merge is blocked by CI
> (Jenkins stage "Architecture Qube") below the threshold. A flat `main/*.c` layout scans as
> a failure, not as "no violations".

## 0. First command in a fresh clone

Run `tools/install-hooks.sh` once. Git does not install hooks on clone; until you run it the
pre-commit check above does not exist and only CI will catch a flat layout — after the work
is written. `tools/new-app.sh` runs it for you.

## 1. Starting a new product from this repo

| Situation | Do this | Never do this |
|---|---|---|
| New firmware product (gateway, logger, bridge…) | `tools/new-app.sh <name> <dest-dir>` — copies the full layered skeleton **plus this file and the checks**, with the template demos removed (`--with-demo` keeps them) | Copy only `components/SdFat` or `main/share/db/arcanats` into a fresh `main/`; keep demo services "unwired" |
| New capability inside a product | `tools/new-service.sh <Name> [namespace]` — creates interface, impl, test and wires CMake | Write a new `foo.c` next to `App.cpp` |
| New peripheral chip | Add a class under `main/share/driver/` (or `main/<board>/` if board-specific) | Call `i2c_master_*` / `spi_device_*` from a service or view |

Cost of copying parts instead of the skeleton: the product loses this file, the hook and the
CI gate, and nothing tells the next agent the rules exist (iNAPLink v0.10.1, 2026-11-23:
5,900 lines of flat C, two shared structs written by the BLE task and read by three others
with no lock — found only at review). Keeping the demos unwired costs drift: they still
compile, still take RAM and nobody knows which are load-bearing. Demo code in this template
is fenced with `// demo:begin` / `// demo:end` (or a trailing `// demo`) and listed in
`tools/demo-files.txt`; a new demo feature must be fenced and listed, or `new-app.sh` breaks.

## 2. Layers — where a file goes

Every source file lives in exactly one of these. If you cannot name its layer, you have not
designed it yet.

| Layer | Path | Contains | May include |
|---|---|---|---|
| Entry + DI | `main/App.cpp`, `main/share/AppContainer.*` | `app_main`, construction and wiring | everything (only place allowed to include `*Impl.hpp`) |
| Board | `main/esp32/`, `main/esp32s3/` | pin maps, board-only drivers (`Board.cpp`, `BoardConfig.hpp`) | driver, core |
| View | `main/share/view/` | `*View`, `*ViewModel` (MVVM Input/Output) | service **interfaces**, core |
| Service | `main/share/service/<X>Service.hpp` + `service/impl/<X>ServiceImpl.{hpp,cpp}` | business logic, one responsibility per service | driver, transport interfaces, db, core |
| Transport | `main/share/transport/{ble,mqtt,wifi}/` (+ `impl/`) | radio / network I/O behind an interface | driver, core |
| Command | `main/share/command/` | command codec, dispatcher, crypto | core |
| DB | `main/share/db/arcanats/` | ArcanaTs storage | core |
| Driver | `main/share/driver/` | one class per chip, no business logic | core |
| Core | `main/share/core/{event,log,model}/` | `Observable<T>`, value types | nothing above it |

Direction is one way: view → service → transport/driver/db → core. A lower layer never
includes a higher one.

## 3. Rules

| # | Rule | Enforced by |
|---|---|---|
| R1 | A service is an abstract class `<X>Service` (pure virtual `init_HAL/init/start/stop`) in `service/`, implemented by `<X>ServiceImpl` in `service/impl/`. | arch-qube Interface/Impl, Naming |
| R2 | Only `AppContainer.cpp` includes `*Impl.hpp` or calls `*Impl::getInstance()`. Everyone else holds a pointer to the interface. | arch-qube "Only DI Container Imports Impl" |
| R3 | Services exchange data through `Observable<T>` declared in their `Input` / `Output` structs and wired in `AppContainer::wireServices()`. No `extern` mutable globals for cross-task data. | review + this file (on you) |
| R4 | Data written in one FreeRTOS task and read in another goes through `Observable<T>` (copied by value) or is guarded by a mutex / `portMUX` for **every** access. | review (on you) |
| R5 | Every new service has a GoogleTest in `Tests/` that builds on the host (`Tests/CMakeLists.txt`). | arch-qube Test Coverage, Sonar `new_coverage ≥ 80%` |
| R6 | New source files are C++ (`.cpp` / `.hpp`). A `.c` file is only allowed for generated code (`*.pb.c`) or a vendored component under `components/`. | arch-qube file scan |
| R7 | Functions stay under cognitive complexity 15; split a big `switch` into a table or one handler per case. | Sonar `cxx:FunctionCognitiveComplexity` |
| R8 | No credentials, PSKs or broker passwords in source. Use `sdkconfig.credentials` (git-ignored) or NVS. | Sonar secrets |
| R9 | A new `.cpp` under `main/` is added to `SRCS` in `main/CMakeLists.txt` in the section of its layer. `tools/new-service.sh` does this for you. | the build |
| R10 | Static internal RAM (`.data`+`.bss`) stays under the target's budget in `tools/ram-budget.conf`. Large CPU-only buffers and task stacks go to PSRAM via `core/memory/MemoryPlacement.hpp`; DMA buffers, atomics/`portMUX`, and stacks of tasks that write flash stay internal. Raising a budget needs a board boot log in the commit. | `tools/ram-budget.sh` (Jenkins "RAM Budget"); boot line `MEM internal_free=` + `LOW_INTERNAL_RAM` warning |
| R11 | Every long-running loop either subscribes to the task watchdog (`FreezeGuardService::watchCurrentTask()` + `feed()`, 10 s, panic) or is listed as exempt in §6 with its reason. Wrap a known-slow bounded call in `pause()`/`resume()`; never `esp_task_wdt_delete()` a loop for good. | `CONFIG_ESP_TASK_WDT_PANIC=y` (checked in the built sdkconfig by `tools/ram-budget.sh`); review against §6 |
| R12 | A board is flashed only with `tools/flash-safe.py`: full-flash backup first, DTR/RTS held low, and the flash counts only when the boot log shows the ready marker and `internal_free` ≥ threshold. | the script exits non-zero and prints the rollback command |

## 4. Before you say "done"

Run these, in this order, and paste the last line of each into your summary:

```bash
tools/arch-check.sh                      # arch-qube, same image and threshold as CI; must exit 0
docker compose -f docker-compose.test.yml build test     # host GoogleTests + coverage run inside the image build
idf.py build                             # firmware still builds
tools/ram-budget.sh build                # static internal RAM within budget + TWDT panic on; must print PASS
```

On a board, also: `tools/flash-safe.py -p <port>` must print `flash-safe: PASS`.

`arch-check.sh` printing `FAIL`, or a rule showing `NOT EVALUATED` for a layer you touched,
means not done. Do not lower `--threshold`, add exclusions, or move code into `components/`
to get a pass — fix the structure.

## 5. Reference

- Detailed patterns and examples: `arcana-esp32-developer-skill`
  (https://github.com/jrjohn/arcana-skills/tree/main/arcana-esp32-developer-skill).
  Optional — everything you must follow is in this file.
- Worked example of a service: `DiagnosticService` → `DiagnosticServiceImpl`, fed by
  `TimerService` through `Observable<T>` in `AppContainer::wireServices()`.

## 6. Internal RAM, watchdog, flashing

Host tests and arch-check cannot see memory or timing. iNAPLink 0.3.0 (2026-11-24) passed
both at 100, then booted with 6.9 KB internal RAM and BLE + Wi-Fi failed to allocate; moving
big buffers and task stacks to PSRAM (static DIRAM 116,681 → 67,913 B) fixed it.

| Concern | Tool | Fails when |
|---|---|---|
| Static internal RAM | `tools/ram-budget.sh [build-dir]` (needs the IDF env; budgets in `tools/ram-budget.conf`) | `.data+.bss` > budget, or built sdkconfig lacks `CONFIG_ESP_TASK_WDT_PANIC=y` |
| Runtime internal RAM | `DiagnosticService::logBootMemory()` prints `MEM internal_free=… largest=… psram_free=…` | free < `CONFIG_ARCANA_INTERNAL_RAM_WARN_BYTES` (40 KB) logs `LOW_INTERNAL_RAM` |
| PSRAM placement | `Memory::allocLarge()`, `Memory::createTaskPsramStack()`; SdFat scratch via Kconfig `SDFAT_SCRATCH_IN_PSRAM` (on for S3) | — (falls back to internal RAM without PSRAM) |
| Frozen loop | `FreezeGuardService`; next boot prints `LC_FREEZE reset=… stage=…` | a subscribed loop does not feed within 10 s → panic, reboot |
| Flashing | `tools/flash-safe.py -p <port> [--dry-run] [--check-log FILE]`; backups in `flash-backups/` | no ready marker, `internal_free` below `--min-internal-free`, or a panic line |

Tasks and their watchdog status (R11). Add a row for every new long-running task:

| Task | Status | Why |
|---|---|---|
| `main` loop (`AppContainer::run`) | subscribed, feeds every 500 ms | Wi-Fi reconnect (+10 s SNTP) and the upload (ends in reboot) are wrapped in `pause()` |
| Observable async dispatch, `LcdView`, `bootsplash`, `rgb_cycle` | exempt | wait on a queue/notify/delay with no upper bound by design; a CPU hog still trips the IDLE watch |
| `IoService` button poll | exempt (template demo) | short `vTaskDelay` poll loop; a product that keeps it should subscribe it |
| `AtsStore`, `EcgSample` | exempt | SD writes can stall for seconds on a busy card; a reset there would lose data. Product code replacing them should subscribe with a measured bound |

The IDLE tasks stay on the watchdog too: a task that hogs a core for 10 s without yielding
now reboots instead of printing a warning.

## 7. Architecture check (arch-qube)

arch-qube (https://github.com/jrjohn/arcana-arch-qube; usage skill:
https://github.com/jrjohn/arcana-skills/tree/main/arch-qube-skill) scans the source for the
layer rules in §2–§3 and scores them. The version is pinned in `tools/arch-qube.version`;
`tools/arch-qube-image.sh` pulls `arcana.boo/arcana/arch-qube:<version>` (and warns, then
uses `:latest`, only if that pull fails). The Jenkinsfile uses the same script.

| Situation | Run |
|---|---|
| Docker available | `tools/arch-check.sh` → report in `arch-qube-reports/arch-qube.{md,json}` |
| No Docker | `pipx run --spec git+https://github.com/jrjohn/arcana-arch-qube@v$(cat tools/arch-qube.version) arch-qube scan . --framework esp32 --no-ai --ci --format json,markdown -o arch-qube-reports --threshold 90` |
| No pipx | `python3 -m venv .aq && .aq/bin/pip install git+https://github.com/jrjohn/arcana-arch-qube@v$(cat tools/arch-qube.version)`, then `.aq/bin/arch-qube scan …` with the same arguments |

Reading the report — the score alone is not a pass:

- A rule under "Not evaluated" (`"evaluated": false` in the JSON) was not checked. If it
  covers a layer you touched, that is not done.
- Check `files_checked` per rule. A rule that checked 0 files, or far fewer than you have,
  passed by looking at nothing.
- An unlayered (flat) project scores 0 on Layer Direction Enforcement; a 100 on a project you
  know is flat means the scan did not see your files.

On a false positive: open an issue on arcana-arch-qube with the report JSON and the file.
Never lower `--threshold`, add exclusions, or move code into `components/` to dodge a rule.
