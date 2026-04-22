# Tool display reference

This document lists every Claude Code tool the firmware may see during a
`PreToolUse` / `PostToolUse` hook, along with the fields available in
`tool_input` and what the OLED currently shows for each. Edit the **Shown**
column, then update `ToolFormat.cpp` — both `label()` and `detail()` live
there.

Label budget: ~21 chars total on the body row, of which the label takes 3–6
and the detail the remainder.

## Built-in tools

| Tool           | Label   | `tool_input` fields                                                   | Shown (current default)                      |
|----------------|---------|-----------------------------------------------------------------------|----------------------------------------------|
| `Edit`         | EDIT    | `file_path`, `old_string`, `new_string`, `replace_all`                | basename of `file_path`                      |
| `MultiEdit`    | MEDIT   | `file_path`, `edits[]`                                                | basename of `file_path`                      |
| `Write`        | WRITE   | `file_path`, `content`                                                | basename of `file_path`                      |
| `Read`         | READ    | `file_path`, `limit`, `offset`, `pages`                               | basename of `file_path`                      |
| `NotebookEdit` | NBEDT   | `notebook_path`, `new_source`, `cell_id`, `edit_mode`                 | basename of `notebook_path`                  |
| `Bash`         | BASH    | `command`, `description`, `timeout`, `run_in_background`              | full `command` (truncated to fit)            |
| `BashOutput`   | SHOUT   | `bash_id`, `filter`                                                   | `bash_id`                                    |
| `KillShell`    | KILL    | `shell_id`                                                            | `shell_id`                                   |
| `Glob`         | GLOB    | `pattern`, `path`                                                     | `pattern`                                    |
| `Grep`         | GREP    | `pattern`, `path`, `glob`, `type`, `output_mode`, `-i/-n/-A/-B/-C`    | `pattern`                                    |
| `WebFetch`     | FETCH   | `url`, `prompt`                                                       | `url` minus scheme                           |
| `WebSearch`    | SEARCH  | `query`, `allowed_domains`, `blocked_domains`                         | `query`                                      |
| `Task`         | TASK    | `subagent_type`, `description`, `prompt`                              | `subagent_type`                              |
| `TodoWrite`    | TODOS   | `todos[]` (each `{subject, description, activeForm}`)                 | `"N items"`                                  |
| `SlashCommand` | CMD     | `command`                                                             | `command`                                    |
| `ExitPlanMode` | PLAN    | `plan`                                                                | (empty — suggest: `plan[0:30]`?)             |

## MCP tools

Any tool whose name starts with `mcp__` is an MCP-server tool. Names are of
the form `mcp__<server>__<tool>`. The current default strips the `mcp__`
prefix and shows whatever remains, so `mcp__bridge__reply` renders as
`bridge__reply`. Replace with a custom formatter if a particular MCP server
produces something more informative.

## How to customize

Everything lives in `ToolFormat.cpp`:

1. **Pick new labels** by editing `ToolFormat::label()`. Stick to ≤6
   upper-case chars so the detail has room.
2. **Pick new detail formatting** by editing `ToolFormat::detail()`. You
   have the full `tool_input` blob as a `JsonVariantConst` — read any
   fields you like, then write up to `cap` ASCII chars into `out`.
3. If a tool isn't handled, the detail is empty and only the label shows.

Use `AsciiCopy::copy()` / `AsciiCopy::basename()` for any string that
originates from Claude — they fold UTF-8 punctuation to ASCII the OLED
font can render.

## Hook payload reference

Per Claude Code docs, every hook-event payload includes at minimum:

- `session_id` — stable across the turn
- `transcript_path` — local filesystem path to the JSONL transcript
- `cwd` — working directory

Tool hooks (`PreToolUse`, `PostToolUse`) additionally include:

- `tool_name` — the tool being / just called
- `tool_input` — the arguments Claude passed (shape varies per tool)
- `tool_response` (PostToolUse only) — whatever the tool returned

`Stop` / `PostToolUse` events forwarded by this bridge also carry an
`assistant_text: string[]` array — the new assistant text blocks extracted
from the transcript since the previous hook. The firmware captures the
**last** element of that array as the idle-view summary.

## Fill-me-in

If any of the **Shown** entries above aren't what you want, write your
preferred representation here and I'll wire them up:

- [ ] `Bash`: _e.g. first argv word only, last 20 chars, ..._
- [ ] `Edit`: _e.g. `basename + :linecount`?_
- [ ] `TodoWrite`: _e.g. title of first in-progress todo?_
- [ ] `Task`: _e.g. `subagent_type + description[0:12]`?_
- [ ] `ExitPlanMode`: _what to show, if anything?_
- [ ] MCP tools: _per-server overrides?_
