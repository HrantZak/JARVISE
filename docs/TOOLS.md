# Tools

The set of things JARVIS can do on this machine. It is finite, compiled in, and
listed here in full.

## The catalogue

| Tool | Permission | What it reads |
|---|---|---|
| `system_info` | READ_ONLY | OS, processor, memory, graphics card, JARVIS version |
| `cpu_info` | READ_ONLY | Processor model, core count, current load |
| `memory_info` | READ_ONLY | Total, used and available system memory |
| `gpu_info` | READ_ONLY | Card, video memory, load, temperature |
| `disk_info` | READ_ONLY | Free and total space on the system drive |
| `network_info` | READ_ONLY | Throughput only — no addresses, names or credentials |
| `network_discovery` | READ_ONLY | Devices visible on the local IPv4 network: IP, MAC, hostname and reachability |
| `screen_analyze` | READ_ONLY | One explicit desktop or foreground-window snapshot, local PNG path and Windows OCR text |
| `battery_info` | READ_ONLY | Charge and whether the machine is on mains power |
| `time_info` | READ_ONLY | Local date and time |
| `device_info` | READ_ONLY | Build, compiler, Qt version, GPU backend |
| `open_application` | CONFIRM_REQUIRED | Opens one of four named applications |
| `spotify_search` | SAFE_ACTION | Opens a Spotify search for a query in the desktop app or web player |

The telemetry read-only tools take **no arguments at all**. `screen_analyze` has
one closed `target` enumeration (`screen` or `active_window`); it is only
called after the user explicitly asks JARVIS to look at the screen and it never
starts background monitoring.

`open_application` takes one argument, an enumeration of four values:
`calculator`, `notepad`, `explorer`, `settings`. It has no `path` field, no
`command` field and no `arguments` field — see [SECURITY.md](SECURITY.md) for why
that is a structural property rather than a check.

## Where the data comes from

Most read-only tools read `ISystemMetricsProvider` — the same object the HUD
samples once a second. `network_discovery` is the exception: it reads Windows'
IPv4 neighbour table and uses bounded ICMP reachability probes because those
devices are not part of the hardware telemetry provider. It never opens ports,
authenticates to a device, or changes network state.

When a reading is unavailable the tool fails with `UNAVAILABLE`. It never
substitutes a plausible number. A fabricated measurement is worse than an honest
gap, because the model will report it in a confident sentence.

Screen recognition passes the captured image directly through an in-memory stream to Windows OCR. No temporary screenshot file is required. OCR failures return a failed tool result.

## Calling convention

The model emits a single JSON object:

```json
{"tool": "memory_info", "arguments": {}}
```

Only `tool` and `arguments` are allowed. Any other field makes the whole call
invalid — that is what refuses `"confirmed": true`, and it refuses the next
invented field too, without new code.

The result comes back as flat text:

```
TOOL RESULT
tool: memory_info
status: OK
available: 12.87 GB
total: 31.89 GB
usage_percent: 59.6
used: 19.02 GB
```

A failure is unmistakably a failure:

```
TOOL RESULT
tool: gpu_info
status: FAILED
error: UNAVAILABLE
detail: no NVML-capable GPU is present
```

This shape is deliberate. A failure that reads like a measurement is how a model
ends up stating a number nobody measured.

## Adding a tool

1. Implement `ITool` in `src/tools`.
2. Give it a `ToolDefinition` with a permission level and a closed argument
   schema. There is no free-form string type; if a new tool seems to need one,
   that is the design telling you something.
3. Register it in `ToolCoordinator`'s constructor — the only place registration
   happens, and it happens before any model is loaded.
4. Add it to the table above, to `tst_tool_security`, and to the i18n
   dictionary if it needs a user-facing description.

The permission level is a property of the tool, fixed at registration. Nothing
in a tool call can raise it.

## Configuration

`config.json`, section `tools` (schema version 5):

| Key | Default | Effect |
|---|---|---|
| `enabled` | `true` | Master switch. Off means nothing runs. |
| `allowReadOnly` | `true` | Tools that only read state. |
| `allowSafeActions` | `true` | Reversible actions that run without asking. |
| `allowConfirmedActions` | `true` | Actions that ask first. Off refuses them outright — it never makes them silent. |
| `maxRounds` | `4` | Tool rounds per user message. Clamped to 1–16. |
| `confirmationTimeoutMs` | `60000` | How long a dialog stays valid. Clamped to 5000–300000. |
| `auditEnabled` | `true` | Record every call and refusal. |
| `auditCapacity` | `2000` | Records kept in memory. Clamped to 100–100000. |

A v4 config gains this section with these defaults on first load; the migration
is tested in `tst_config` and was observed on a real config file.

**No setting can enable a DENIED tool.** That level is not a configuration
value — it is a property of the tool, and the permission manager refuses it
under all sixteen combinations of the flags above (`tst_tools`).
