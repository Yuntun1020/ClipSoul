# ClipSoul — agent notes

## Code graph

This project has a CodeGraph index at `.codegraph/` (built by
[codegraph](https://github.com/colbymchenry/codegraph), auto-synced while a
`codegraph serve --mcp` process is running, e.g. any pi session in this
directory).

**Prefer graph queries over grep/read loops for structural questions.** The
graph already knows every symbol, call edge, and import in the 54 files under
`src/`, `include/`, and `tests/`.

### Use these (accurate on this C++ codebase)

| Question | Command |
|---|---|
| Where is symbol X, show me its source + who calls it | `codegraph node "<Name>"` |
| Who calls X | `codegraph callers "<Name>"` |
| What does X call | `codegraph callees "<Name>"` |
| What breaks if I change X | `codegraph impact "<Name>" -d 2` |
| Which tests cover a change | `git diff --name-only HEAD \| codegraph affected --stdin` |
| What's relevant before starting a task | `codegraph context "<task description>" -n 5` |
| Find symbols by name fragment | `codegraph query "<fragment>" -l 10` |

In an MCP-capable session, `codegraph_explore` is the equivalent single tool.
Treat its returned source as already read — do not re-read those files.

### Avoid / caveats

- **`codegraph explore` ranks poorly on this repo.** It returns unrelated
  symbols (e.g. `GetStartWithWindows`, `HistoryStore::get`) for questions like
  "how does paste get sent to the target window". Use it only for broad
  surveying, never for pinpointing. Fall back to `query` / `node`.
- **Use bare method names, not `Class::Method`.** `codegraph callers
  "SendPaste"` works; `codegraph callers "PasteController::Paste"` fails with
  "symbol not found". Disambiguate same-named methods with `-f <file>`.
- **Auto-sync requires a live MCP server.** Edits made with no pi session
  running stay pending until the next session starts (connect-time
  reconciliation) or you run `codegraph sync` manually.
- **Only this checkout is indexed.** The git worktrees under
  `ClipSoul-main-worktrees/` have no `.codegraph/` — run `codegraph init`
  there if you work in one.
- **`.codegraph/` is local machine state.** Its own `.gitignore` keeps the
  SQLite DB out of git; only `.codegraph/.gitignore` is meant to be committed.

## Build & test

- CMake project (`CMakeLists.txt`), Windows/Win32 API.
- Tests live in `tests/` (e.g. `HistoryStoreTests.cpp`, `AppSettingsTests.cpp`).
- `codegraph affected` resolves which of them are impacted by a source change.
