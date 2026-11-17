# `@tty-pt/mm`

Universal Node.js and Deno N-API bindings for **`libmm`** — the Pure-C Two-Tier Multi-Axis Memory Engine, Codebase Symbol/Call-Graph Indexer, and Universal CLI/MCP Substrate built on `@tty-pt/corm`, `@tty-pt/lemma`, `@tty-pt/sepal`, `@tty-pt/joint`, and `@tty-pt/islet`.

## Features

- **Two-Tier Federated Memory (`MmContext`)**:
  - **Tier 1 (`Global`)**: Persistent user-wide rules, standing restrictions, persona tenets, and cross-project operator facts (`~/.local/share/mm/global/mem.db`).
  - **Tier 2 (`Project`)**: Repository-scoped architectural lore, bug post-mortems, verification-attested root causes, and symbol-linked memory (`<repo>/.mm/mem.db`).
- **HTOK v4 Binary Envelope Codec (`payloadEncode`, `payloadDecode`)**: Zero-overhead binary serialization with legacy `MM_META` and raw-text fallback.
- **Hybrid RRF & Multi-Factor Ranking (`fuseRankingsRRF`, `computeRankingScore`, `filterAndEnrich`)**: Adaptive Reciprocal Rank Fusion, epistemic status multipliers, temporal decay, and hygiene gates.
- **Codebase Symbol & Call-Graph Indexer (`MmCodeIndex`, `scanSource`)**: Multi-language structural symbol scanner (C/C++, TypeScript/JS, Python, Go, Rust), call-graph tracer, git diff blast-radius analyzer, stack-trace fault localizer, and cross-boundary resolver.

## Usage

```js
import {
  MmContext,
  MmCodeIndex,
  fuseRankingsRRF,
  payloadEncode,
  payloadDecode,
} from "@tty-pt/mm";

const ctx = new MmContext({ cwd: process.cwd() });
const rec = ctx.record({
  summary: "Always use reactive wakeup notifications instead of polling loops",
  standing: true,
  tags: "workflow,concurrency",
});

console.log(ctx.recall({ scope: "persistent", query: "polling loops" }));
ctx.close();
```

## License

BSD-2-Clause
