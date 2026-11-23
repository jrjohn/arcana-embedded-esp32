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
| New firmware product (gateway, logger, bridge…) | `tools/new-app.sh <name> <dest-dir>` — copies the full layered skeleton **plus this file and the checks** | Copy only `components/SdFat` or `main/share/db/arcanats` into a fresh `main/` |
| New capability inside a product | `tools/new-service.sh <Name> [namespace]` — creates interface, impl, test and wires CMake | Write a new `foo.c` next to `App.cpp` |
| New peripheral chip | Add a class under `main/share/driver/` (or `main/<board>/` if board-specific) | Call `i2c_master_*` / `spi_device_*` from a service or view |

Cost of copying parts instead of the skeleton: the product loses this file, the hook and the
CI gate, and nothing tells the next agent the rules exist (iNAPLink v0.10.1, 2026-11-23:
5,900 lines of flat C, two shared structs written by the BLE task and read by three others
with no lock — found only at review).

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

## 4. Before you say "done"

Run these, in this order, and paste the last line of each into your summary:

```bash
tools/arch-check.sh                      # arch-qube, same image and threshold as CI; must exit 0
docker compose -f docker-compose.test.yml build test     # host GoogleTests + coverage run inside the image build
idf.py build                             # firmware still builds
```

`arch-check.sh` printing `FAIL`, or a rule showing `NOT EVALUATED` for a layer you touched,
means not done. Do not lower `--threshold`, add exclusions, or move code into `components/`
to get a pass — fix the structure.

## 5. Reference

- Detailed patterns and examples: `arcana-esp32-developer-skill`
  (https://github.com/jrjohn/arcana-skills/tree/main/arcana-esp32-developer-skill).
  Optional — everything you must follow is in this file.
- Worked example of every layer: `LedService` → `LedServiceImpl` → `RgbLed` driver,
  wired in `AppContainer::wireServices()`.
