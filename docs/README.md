# PlayersInfo — Documentation

A Metamod:Source plugin for CS2 that dumps server state and connected players as a single JSON string. Designed for RCON pulls or file-based ingestion by web dashboards.

## Index

- **[commands.md](commands.md)** — the three console commands and their behavior
- **[schema.md](schema.md)** — JSON structure and field types
- **[web-integration.md](web-integration.md)** — how to consume the output from a web app (RCON / file)
- **[install.md](install.md)** — build, install, and dependencies

## Quick start

From the server console or RCON:

```
mm_getinfo                          # full server JSON to console/RCON reply
mm_getinfo_slot 2                   # one player's JSON by slot
mm_getinfo_file /path/to/file.json  # atomic write of the full JSON to disk
```

The response is a single JSON line — see [schema.md](schema.md).
