# `@tty-pt/mm`

[![npm version](https://img.shields.io/npm/v/@tty-pt/mm.svg)](https://www.npmjs.com/package/@tty-pt/mm)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue.svg)](../LICENSE)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API%20(6%20prebuilds)-339933?logo=node.js)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)

> Universal Node.js and Deno N-API bindings for **`libmm`** — the Pure-C Two-Tier Multi-Axis Memory Engine, Codebase Symbol/Call-Graph Indexer, and Universal CLI/MCP Substrate built on `@tty-pt/corm`, `@tty-pt/lemma`, `@tty-pt/sepal`, `@tty-pt/joint`, `@tty-pt/islet`, and `@tty-pt/stele`. Canonical C header: `<ttypt/mm.h>`.

📖 **Full C & CLI/MCP Documentation**: See the main [**libmm GitHub README**](https://github.com/tty-pt/libmm#readme) for the two-tier architecture, `mm` CLI commands, `mm mcp` JSON-RPC 2.0 server, and C API reference.

---

## 1. Installation

```bash
npm install @tty-pt/mm
```

Ships prebuilt native Node-API (`mm.node`) addons for all 6 platforms: `linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`, and `win32-x64`.

**Native OS Packages (`tty.pt`)**: Prebuilt native C packages (`libmm`) for Linux (`apt`, `apk`, `pacman`, `dnf`), macOS (`brew`), Windows (`winget`), and OpenBSD (`pkg_add`) are available via the [**tty.pt package repositories**](https://github.com/tty-pt/ci/blob/main/docs/install.md).

---

## 2. Features

- **Two-Tier Federated Memory (`MmContext`)**:
  - **Tier 1 (`Global`)**: Persistent user-wide rules, standing restrictions, persona tenets, and cross-project operator facts (`~/.local/share/mm/global/mem.db`).
  - **Tier 2 (`Project`)**: Repository-scoped architectural lore, bug post-mortems, verification-attested root causes, and symbol-linked memory (`<repo>/.mm/mem.db`).
- **HTOK v4 Binary Envelope Codec (`payloadEncode`, `payloadDecode`)**: Zero-overhead binary serialization with legacy `MM_META` and raw-text fallback.
- **Hybrid RRF & Multi-Factor Ranking (`fuseRankingsRRF`, `computeRankingScore`, `filterAndEnrich`, `adaptiveRrfK`)**: Adaptive Reciprocal Rank Fusion, epistemic status multipliers, temporal decay, and hygiene gates (`isTechnicalInquiry`, `isCodeOrDocArtifact`, `isQuestionMemory`, `isAssistantEcho`, `isCleanSummaryText`).
- **Codebase Symbol & Call-Graph Indexer (`MmCodeIndex`, `scanSource`)**: Multi-language structural symbol scanner (C/C++, TypeScript/JS, Python, Go, Rust), call-graph tracer, git diff blast-radius analyzer, stack-trace fault localizer, `XY_DECL`/`XY_IMPL` contract checker, and cross-boundary resolver.

---

## 3. Quickstart

```ts
import {
  MmContext,
  MmCodeIndex,
  fuseRankingsRRF,
  computeRankingScore,
  payloadEncode,
  payloadDecode,
  scanSource,
} from "@tty-pt/mm";

// 1. Open federated two-tier memory context
const ctx = new MmContext({ cwd: process.cwd() });
const rec = ctx.record({
  summary: "Always use reactive wakeup notifications instead of polling loops",
  standing: true,
  anchor: true,
  tags: "workflow,concurrency",
});

console.log(ctx.recall({ scope: "persistent", query: "polling loops" }));
ctx.close();

// 2. Scan source code or query repository call graph in-process
const symbols = scanSource("src/example.c", "int add(int a, int b) { return a + b; }");
console.log("Scanned symbols:", symbols);
```

---

## 4. API Reference

### `MmContext`
- `new MmContext(opts?: MmContextOptions)`
- `.info()`: Returns `{ projectRoot, projectName, hasProjectTier, globalDir, projectDir }`.
- `.record(req: MmRecordRequest)`: Records a note, rule, decision, or episode into Tier 1 (`Global`) or Tier 2 (`Project`).
- `.recall(req: MmRecallRequest)`: Executes multi-resolution recall across `persistent`, `code`, `session`, or `all` scopes.
- `.review(req: MmReviewRequest)`: Inspects evidence hierarchy or transitions epistemic lifecycle state (`confirm`, `contradict`, `supersede`, `promote`, `forget`).
- `.overview(tier?: MmTier, maxPerBucket?: number)`: Synthesizes an `L3`/`L2`/`L1` memory overview.
- `.evaluateGuardrail(intentText: string, filePath?: string, symbolName?: string)`: Pre-flights a planned edit or command against contradicted memories and standing restrictions.
- `.observeVerification(command: string, exitCode: number, outputSnippet?: string)` / `.recordFileMutation(filePath: string)`: Verification-attested promotion ledger hooks.
- `.prime(prompt: string)` / `.consolidate(tier?: MmTier)` / `.close()`.

### `MmCodeIndex` & `scanSource`
- `new MmCodeIndex(rootDir?: string)`
- `.search(query?, fileFilter?, kindFilter?, limit?)`
- `.readSymbols(symbols, opts?)`
- `.trace(symbolName, direction?, depth?)`
- `.overview()` / `.diffImpact(depth?)` / `.diagnose(traceText, depth?)`
- `.detectXyMismatches()` / `.resolveCrossBoundary(symbolName?, filePath?)` / `.firstTouchDossier(fileOrSymbol)`
- `scanSource(filePath: string, content: string, fileId?: number): MmCodeSymbol[]`

### Stateless Fusion, Ranking & Envelope Helpers
- `adaptiveRrfK(candidateCount: number): number`
- `fuseRankingsRRF(lexical, semantic, kOverride?): MmRankedRef[]`
- `computeRankingScore(cand: MmCandidate, crit?: MmFilterCriteria): number`
- `filterAndEnrich(candidates: MmCandidate[], crit?: MmFilterCriteria): MmCandidate[]`
- `payloadEncode(envelopeOrJson: MmEnvelope | string): Uint8Array`
- `payloadDecode(bytesOrStr: Uint8Array | string): MmEnvelope`
- `windowForLevel(level: MmLevel | string, nowEpochSec?: number): { bounded: boolean; since: string; until: string }`
- `classifyPrompt(prompt: string): MmPromptProfile`
- `isTechnicalInquiry(query: string): boolean`
- `isCleanSummaryText(summary: string): boolean`
- `isCodeOrDocArtifact(record: string, envelope?: MmEnvelope | string): boolean`
- `isQuestionMemory(envelope?: MmEnvelope | string): boolean`
- `isAssistantEcho(envelope?: MmEnvelope | string): boolean`

---

## 5. C Development Headers & Native Integration

```makefile
CFLAGS += -I./node_modules/@tty-pt/mm/include
LDLIBS += -lmm
```

---

## License

BSD-2-Clause — Copyright (c) 2026, Paulo André Azevedo Quirino. See [LICENSE](../LICENSE).
