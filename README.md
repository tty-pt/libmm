# `libmm` (`@tty-pt/mm`)

Pure-C two-tier multi-axis memory engine, codebase symbol/call-graph indexer, and universal CLI/MCP tool built on [`libcorm`](../libcorm), [`liblemma`](../liblemma), [`libsepal`](../libsepal), [`libjoint`](../libjoint), and [`libislet`](../libislet).

## Overview

`libmm` provides the cognitive, epistemic, and codebase-indexing substrate behind `pi-mm` as a standalone C library (`libmm.so` / `libmm.a`) and native binary (`mm`), reusable across:
- **`dwm-jarvis`**: Sub-millisecond desktop voice & operator memory (`Tier 1: Global`)
- **Coding Harnesses (Pi, Claude Code, Cursor, OpenCode, Zed, Aider)**: Native CLI (`mm`) and zero-JS MCP server (`mm mcp`) federating global operator rules (`Tier 1`) with repository code & lore (`Tier 2: Project`)
- **Node.js / TypeScript (`pi-mm`)**: Colocated N-API native addon (`@tty-pt/mm`)

## Architecture & Implementation Plan

See **[`PLAN.md`](./PLAN.md)** for the complete two-tier architecture, C API specification (`include/ttypt/mm.h`), component mapping from `pi-mm`, and phased TDD rollout plan.
