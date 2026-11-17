# `libmm` & `mm` — Pure-C Two-Tier Multi-Axis Memory Engine & Universal Tooling Plan

## 1. Executive Summary & Goal

`libmm` extracts the cognitive, epistemic, codebase-indexing, and session-memory layer currently implemented in TypeScript inside [`.pi/extensions/pi-mm/`](../../.pi/extensions/pi-mm) into a **standalone, zero-JS-dependency pure-C library (`libmm.so` / `libmm.a`) and multi-modal binary (`mm`)** built on top of the `tty-pt` C stack:
- **`libcorm`** (`external/libcorm`): Multi-axis coordinate storage and resident daemon (`cormd`)
- **`liblemma`** (`external/liblemma`): Lexical FTS, tokenization, stemming, accent folding, and BM25 scoring
- **`libsepal`** (`external/libsepal`): Dense 384-dim vector storage (`.sepal`), SIMD cosine similarity, and HTTP embedding client for `axil-auren`
- **`libjoint`** (`external/libjoint`): Temporal mipmap windows (`L0`–`L3`) and Cartesian multi-axis intersection
- **`libislet`** (`external/libislet`): 1D/2D/3D spatial/interval bounding spans (source line ranges, diff hunks, spatial regions)

By moving the fusion, codebase symbol graph, episodic lifecycle, and transcript indexing into C under a **Two-Tier Storage Architecture**, `libmm` can be consumed identically across:
1. **`dwm-jarvis`** (`~/.local/src/dwm`: `dwm-whisperd.c`, `dwm-voice`, `dwm-auren`, `dwm-jarvis-pi`) with sub-millisecond native C / Unix-socket latency and zero Node/Deno runtime overhead.
2. **Any AI Coding Harness** (Pi, Claude Code, Cursor, Aider, OpenCode, Zed, custom agents) via either the **`mm` CLI** or the built-in **native C MCP server (`mm mcp`)**.
3. **`pi-mm`** via a thin **colocated N-API / WASM adapter** (`@tty-pt/mm`), preserving all Pi lifecycle hooks (`before_turn`, `after_turn`, `on_compaction`) while eliminating duplicated TypeScript storage/indexing logic.

---

## 2. Two-Tier Storage Architecture

To serve both a desktop-wide voice/window assistant (`dwm-jarvis`) and repository-scoped coding agents (`pi-mm`, MCP harnesses) without polluting project stores or losing operator identity, `libmm` implements a **Two-Tier Federated Store**:

```
┌────────────────────────────────────────────────────────────────────────────┐
│                        libmm Federated Context (mm_ctx_t)                  │
│                                                                            │
│   ┌──────────────────────────────────┐  ┌──────────────────────────────┐   │
│   │   Tier 1: Global Operator Tier   │  │ Tier 2: Project/Repo Tier    │   │
│   │  ~/.local/share/mm/global/       │  │ <repo-root>/.mm/             │   │
│   │  (or $XDG_DATA_HOME/mm/global/)  │  │ (or fallback ~/.pi/mm/...)   │   │
│   ├──────────────────────────────────┤  ├──────────────────────────────┤   │
│   │ • Standing operator directives   │  │ • Repo L0–L3 lore & episodes │   │
│   │ • Flashbulb anchors (isAnchor)   │  │ • Verified bug/test evidence │   │
│   │ • dwm-jarvis voice & desktop     │  │ • Codebase symbol & call     │   │
│   │   episodic history & habits      │  │   graph (code.db@islet,...)  │   │
│   │ • Cross-project L3 principles    │  │ • Repo session transcripts   │   │
│   │ • Global session transcripts     │  │ • Module stigmergy & dossiers│   │
│   └────────────────┬─────────────────┘  └──────────────┬───────────────┘   │
│                    │                                   │                   │
│                    └─────────────────┬─────────────────┘                   │
│                                      ▼                                     │
│             Stage 1: Two-Tier Reciprocal Rank Fusion (RRF)                 │
│             Stage 2: Epistemic, Citation DAG & Scope Re-Ranking            │
└────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Tier 1 — Global Operator Tier (`MM_TIER_GLOBAL`)
- **Default Path**: `$MM_GLOBAL_DIR` $\rightarrow$ `$XDG_DATA_HOME/mm/global` $\rightarrow$ `~/.local/share/mm/global/`
- **Contents**:
  - `mem.db@joint,lemma,sepal:a:b`: Operator-wide persistent memory space.
  - `sessions/`: Desktop / `dwm-jarvis` voice transcripts and global non-repo agent sessions.
  - `consolidation.journal`: L0 $\rightarrow$ L1 $\rightarrow$ L2 $\rightarrow$ L3 consolidation state for global episodes.
- **Write Routing Rules**:
  - Any `mm_record()` call with `standing = true` (global behavioral rules/habits), `tier = MM_TIER_GLOBAL`, or originating when no project tier is active (e.g. `dwm-jarvis` on the desktop) writes directly to Tier 1.
  - Cross-scope `L3` system principles synthesized from multiple projects are promoted to Tier 1.

### 2.2 Tier 2 — Project / Workspace Tier (`MM_TIER_PROJECT`)
- **Discovery Rules**:
  1. Explicit `--project <path>` or `MM_PROJECT_DIR` override.
  2. Walk upward from `cwd` looking for `.mm/`, `.pi/mm/`, or `.git/`.
  3. To maintain 100% backward compatibility with existing `pi-mm` installations, if `~/.pi/agent/mm/<project-slug>` or `.pi/mm/` exists, `libmm` mounts it transparently.
- **Contents**:
  - `mem.db@joint,lemma,sepal:a:b`: Project-scoped `L0`–`L3` lore, bug investigations, ADRs, and test verification records.
  - `code.db@islet,lemma,sepal`: Symbol definitions, file/line interval spans (`libislet`), caller/callee edges, include/import DAG, and module clusters.
  - `sessions/`: Project coding session transcripts and turn indexes.
  - `dossier.json` / stigmergy state: Per-module first-touch invariants and fault hot-spots.
- **Write Routing Rules**:
  - Default `mm_record()` inside a repository writes to Tier 2 (`MM_TIER_PROJECT`), unless `standing = true` or `tier = MM_TIER_GLOBAL` is explicitly specified.

### 2.3 Federated Read & RRF Merge Semantics
When `mm_recall()` executes:
- **`scope: "code"`**: Queries Tier 2 `code.db` (plus linked Tier 2 & Tier 1 lore bindings for matched symbols/files).
- **`scope: "session"`**: Queries Tier 2 `sessions/` (and Tier 1 `sessions/` if `session: "all"` or running in global desktop mode).
- **`scope: "persistent"` or `"all"`**:
  - Dispatches parallel/pipelined `lemma` (BM25) and `sepal` (384-dim cosine similarity) queries against **both** Tier 2 (`project`) and Tier 1 (`global`).
  - Merges candidate lists using namespaces `(tier, ref)` so a `ref` ID in Tier 1 never collides with a `ref` ID in Tier 2:
    - Qualified ref format in output cards: `[Ref P:1045]` (or `[Ref 1045]` in single-project view) vs `[Ref G:42]` for global operator memories.
  - Applies Stage 1 (`adaptiveRrfK`) and Stage 2 (`computeRankingScore`), where Tier 1 standing directives (`is_anchor` / `standing`) receive their $+25\%$ flashbulb salience multiplier and Tier 2 project-matched episodes receive their $+5\%$ project/subsystem alignment boost.

---

## 3. Component Mapping: `pi-mm` (TypeScript) $\rightarrow$ `libmm` (Pure C)

Every subsystem in `.pi/extensions/pi-mm/src/` maps cleanly to a cohesive C compilation unit in `external/libmm/src/`:

| `libmm` C Source File | Replaces in `pi-mm/src/` | Responsibilities |
| :--- | :--- | :--- |
| **`src/mm_ctx.c`** | `config.ts`, `lock.ts`, `daemon.ts`, `corm.ts` | Two-tier directory resolution, config loading (`mm.json` + env vars `MM_*` / `CORM_*`), file locks, direct `libcorm` API binding, and `cormd` Unix domain socket client. |
| **`src/mm_payload.c`** | `payload.ts`, `episode.ts`, `window.ts`, `types.ts`, `tools/ref_allocator.ts` | Zero-copy/arena JSON & binary payload codec for memory envelopes (`L0`–`L3`, `status`, `evidence[]`, `contradicts[]`, `isAnchor`, `standing`, `context`, `utility`), monotonic ref allocation per tier, and `libjoint` temporal window encoding. |
| **`src/mm_fusion.c`** | `associative.ts`, `tools/ranking.ts`, `tok.ts`, `vocab_base.ts` | Two-leg hybrid `lemma` + `sepal` query execution across Tier 1 & Tier 2, `adaptiveRrfK` Reciprocal Rank Fusion, epistemic lifecycle multipliers (`supported` $1.0\times$, `tested` $0.95\times$, `observed` $0.90\times$, `hypothesized` $0.70\times$, `contradicted` $0.10\times$, `superseded` $0.05\times$), citation DAG bonus, conversational code-noise pruning, and card formatting. |
| **`src/mm_ops.c`** | `tools/recall.ts`, `tools/record.ts`, `tools/review.ts`, `tools/forget.ts`, `tools/feedback.ts`, `tools/traverse.ts`, `tools/overview.ts` | High-level operations backing `recall`, `record`, `review` (`confirm`, `contradict`, `supersede`), `forget`, `feedback`, evidence DAG traversal, and persistent memory overview. |
| **`src/mm_code.c`** | `code/lexer.ts`, `code/scanner.ts`, `code/store.ts`, `code/graph.ts`, `code/recall.ts`, `code/diagnose.ts`, `code/dossier.ts`, `code/lore_binding.ts`, `code/stigmergy.ts`, `hooks/guardrails.ts` | Pure-C multi-language lexical/symbol scanner (C/H, TypeScript/JS, Python, Shell), `libislet` line-interval indexing, inbound/outbound call-graph tracer, L2/L3 architecture overview generator, `git diff` hunk blast-radius mapper, stack-trace fault localizer (`diagnose`), counterfactual guardrail check, and module dossier binding. |
| **`src/mm_session.c`** | `session/store.ts`, `session/recall.ts`, `session/manager.ts`, `session/lineage.ts`, `session/epoch.ts`, `session/compaction.ts`, `session_history.ts` | Harness-agnostic JSONL session transcript ingestion (supporting Pi transcripts, `dwm-jarvis` voice logs, and generic agent turns), turn-level lexical/semantic indexing, `zoomTurn` + `lineRange` slice extraction, and lineage tracking. |
| **`src/mm_judge.c`** | `judge/*.ts`, `synth/*.ts`, `tools/consolidate.ts`, `tools/consolidation_journal.ts`, `hooks/auto_recall.ts`, `hooks/reinforcement.ts` | HTTP client talking to `axil-auren` (`/v1/embeddings`, `/v1/classify`, `/v1/chat/completions`) for exemplar cosine intent classification (`captureEmbed`, `recallEmbed`, `directiveEmbed`), regex fallback when `auren` is offline, L0 $\rightarrow$ L1 $\rightarrow$ L2 $\rightarrow$ L3 consolidation queue draining, and `<mm_tool_priming>` prompt generation. |
| **`src/mm_cli.c`** | *(New universal frontend)* | Fast CLI binary (`mm`) exposing subcommands: `recall`, `record`, `review`, `code`, `session`, `prime`, `consolidate`, `status`, `daemon`, `mcp`. |
| **`src/mm_mcp.c`** | *(New universal MCP frontend)* | Zero-dependency C JSON-RPC 2.0 stdio server (`mm mcp`) exposing `memory_recall`, `memory_record`, and `memory_review` to any MCP client. |
| **`npm/src/mm_napi.c`** | `napi_runner.ts` + TS glue | Colocated Node.js N-API native addon (`@tty-pt/mm`) for `pi-mm` and JS consumers. |

---

## 4. Public C API Design (`include/ttypt/mm.h`)

The C library follows `tty-pt` conventions (`<ttypt/mm.h>`, opaque context handle, explicit error codes, caller-owned or arena-allocated buffers, and optional `ISO_EXPORT` symbols):

```c
#ifndef TTYPT_MM_H
#define TTYPT_MM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mm_ctx mm_ctx_t;

typedef enum {
	MM_TIER_AUTO    = 0, /* Route writes to PROJECT if open, else GLOBAL; query BOTH */
	MM_TIER_GLOBAL  = 1, /* ~/.local/share/mm/global */
	MM_TIER_PROJECT = 2, /* <repo>/.mm or ~/.pi/agent/mm/<slug> */
	MM_TIER_BOTH    = 3  /* Explicit federated read across GLOBAL + PROJECT */
} mm_tier_t;

typedef enum {
	MM_SCOPE_ALL        = 0,
	MM_SCOPE_CODE       = 1,
	MM_SCOPE_SESSION    = 2,
	MM_SCOPE_PERSISTENT = 3
} mm_scope_t;

typedef enum {
	MM_STATUS_OBSERVED     = 0,
	MM_STATUS_HYPOTHESIZED = 1,
	MM_STATUS_TESTED       = 2,
	MM_STATUS_SUPPORTED    = 3,
	MM_STATUS_CONTRADICTED = 4,
	MM_STATUS_SUPERSEDED   = 5
} mm_epistemic_status_t;

typedef struct {
	const char *global_dir;   /* NULL => default $XDG_DATA_HOME/mm/global */
	const char *project_dir;  /* NULL => auto-detect from cwd */
	const char *cwd;          /* NULL => getcwd() */
	const char *auren_url;    /* NULL => auto-discover axil-auren (:8081 / env) */
	const char *embed_model;  /* default "all-minilm" / "bge-base" */
	bool        read_only;
	bool        prefer_daemon;/* connect to resident cormd/mmd if available */
} mm_options_t;

/* Lifecycle */
mm_ctx_t *mm_open(const mm_options_t *opts);
void      mm_close(mm_ctx_t *ctx);

/* Unified Recall (Code + Session + Persistent across Two Tiers) */
typedef struct {
	mm_scope_t  scope;
	mm_tier_t   tier;
	const char *query;
	const char *symbol;
	const char *file;
	const char *kind;
	const char *trace;        /* symbol name for call-graph trace */
	const char *direction;    /* "inbound" | "outbound" | "both" */
	int         depth;        /* trace or diff depth (default 2-3) */
	bool        diff;         /* git diff blast radius mode */
	bool        overview;     /* L2/L3 architecture or lore overview */
	const char *diagnose;     /* stack trace or compiler error fault localization */
	const char *guardrail;    /* planned edit/command pre-flight check */
	int         zoom_ref;     /* >0 to zoom a persistent ref */
	mm_tier_t   zoom_tier;    /* tier of zoom_ref */
	int         zoom_turn;    /* >0 to zoom a session turn */
	int         line_start;
	int         line_end;
	int         limit;        /* max results (default 10) */
	bool        include_l0;
	bool        json_output;  /* true => JSON, false => LLM markdown cards */
} mm_recall_req_t;

int mm_recall(mm_ctx_t *ctx, const mm_recall_req_t *req, char **out_buf, size_t *out_len);

/* Record a Memory / Standing Rule / Anchor / Episode */
typedef struct {
	mm_tier_t              tier;        /* AUTO routes standing=true to GLOBAL */
	const char            *problem;     /* core finding, rule, or episode summary */
	const char            *type;        /* "episode" | "decision" | "note" | "rule" */
	mm_epistemic_status_t  status;
	bool                   standing;    /* true => global standing directive */
	bool                   anchor;      /* true => flashbulb salience boost (1.25x) */
	const char            *project;
	const char            *subsystem;
	const char           **constraints;
	size_t                 n_constraints;
	const int             *evidence_refs;
	size_t                 n_evidence;
	const int             *contradicts_refs;
	size_t                 n_contradicts;
} mm_record_req_t;

int mm_record(mm_ctx_t *ctx, const mm_record_req_t *req, mm_tier_t *out_tier, int *out_ref);

/* Epistemic Review (confirm / contradict / supersede / utility feedback) */
typedef struct {
	mm_tier_t              tier;
	int                    ref;
	const char            *action;      /* "confirm" | "contradict" | "supersede" | "helpful" | "harmful" */
	const char            *reason;
	int                    superseded_by_ref;
} mm_review_req_t;

int mm_review(mm_ctx_t *ctx, const mm_review_req_t *req);

/* Fast Prompt Intent Classification & Priming (for dwm-jarvis & harness before_turn) */
int mm_prime(mm_ctx_t *ctx, const char *user_prompt, char **out_priming_block, size_t *out_len);

/* Incremental Codebase & Session Indexing */
int mm_code_sync(mm_ctx_t *ctx, bool force_full);
int mm_session_ingest_turn(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                           int turn_idx, const char *role, const char *content_json);
int mm_consolidate_drain(mm_ctx_t *ctx, int max_batch, int budget_ms);

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_MM_H */
```

---

## 5. Multi-Context Integration Blueprints

### 5.1 `dwm-jarvis` Integration (`~/.local/src/dwm`)
`dwm-jarvis` already has a pure-C voice daemon (`dwm-whisperd.c`), IPC (`dwm-msg.c`), and action/assistant scripts (`scripts/dwm-voice`, `scripts/dwm-auren`, `scripts/dwm-act`, `scripts/dwm-jarvis-pi`). `libmm` integrates at three points:
1. **Direct C / CLI Recall on Voice Utterances (`dwm-whisperd.c` / `dwm-auren`)**:
   - Before routing a spoken utterance to local `axil-auren` or `dwm-jarvis-pi`, `mm prime --prompt "$UTTERANCE"` (or `mm_prime()` linked directly in `dwm-whisperd`) checks in **< 2 ms** whether the utterance references operator preferences, past desktop actions, or project lore, and injects compact Tier 1 (`global`) memories.
2. **Voice-Driven Standing Rule & Memory Capture (`dwm-voice` / `intents.conf`)**:
   - Utterances matching `"Jarvis, remember that ..."` or `"always use ..."` invoke `mm record --standing --anchor "$FACT"`, persisting directly into `~/.local/share/mm/global/`.
   - Because Coding Harnesses (`pi-mm`, `mm mcp`) federate Tier 1 (`global`) on every query, anything told to Jarvis by voice is immediately known to every coding agent session across all repositories.
3. **Desktop Episodic History**:
   - `dwm-voice` / `dwm-jarvis-pi` logs spoken Q&A turns into Tier 1 `sessions/` via `mm session ingest`, enabling *"Jarvis, what was that command we ran an hour ago?"* via `mm recall --scope session`.

### 5.2 Other Harnesses (Claude Code, Cursor, OpenCode, Zed, Aider)
- **MCP Mode (`mm mcp`)**:
  - Adding `{"command": "mm", "args": ["mcp"]}` to any MCP client configuration exposes `memory_recall`, `memory_record`, and `memory_review` with identical schemas to `pi-mm`.
  - Runs as a single ~200 KB native C binary with zero Node.js startup lag or npm dependency drift.
- **CLI Mode (`mm recall` / `mm record`)**:
  - Harnesses without MCP support can call `mm recall --scope code --symbol ...` or `mm recall "query"` directly from bash.

### 5.3 `pi-mm` Extension Convergence (`.pi/extensions/pi-mm`)
- Following [`docs/COLOCATED_NODE_WASM_ADAPTERS.md`](../../docs/COLOCATED_NODE_WASM_ADAPTERS.md), `external/libmm/npm` publishes `@tty-pt/mm` with a colocated N-API native binding (`mm.node`) and WASM fallback.
- `.pi/extensions/pi-mm` retains its Pi-specific event wiring (`before_turn`, `after_turn`, `on_compaction`, tool registration), while replacing its internal TypeScript `code/`, `payload.ts`, `associative.ts`, and `tools/ranking.ts` implementations with calls into `@tty-pt/mm`.

---

## 6. Phased Implementation Roadmap (Abstraction-First TDD)

In accordance with **Guideline 9 (Abstraction-First Test-Driven Development & Integration Sequencing)**, each C module will be built and verified with standalone C unit tests before wiring into higher-level consumers:

### Phase 1: Foundation, Two-Tier Context & Payload Codec (`mm_ctx.c`, `mm_payload.c`)
1. Set up `external/libmm/Makefile` using `../mk/include.mk`, linking `libcorm`, `liblemma`, `libsepal`, `libjoint`, `libislet`.
2. Implement `mm_payload.c` with 100% wire-compatible reading and writing of existing `pi-mm` `mem.db` records (JSON envelopes, L0–L3 mipmap windows via `libjoint`, monotonic ref allocation, and epistemic metadata).
3. Implement `mm_ctx.c` supporting Tier 1 (`~/.local/share/mm/global`) and Tier 2 (`<repo>/.mm` + backward-compatible `~/.pi/agent/mm/<slug>`) directory resolution and `libcorm` / `cormd` handle management.
4. **Verification Gate**: `tests/test_payload.c` and `tests/test_tiers.c` verifying round-trip compatibility against real `pi-mm` fixtures and two-tier routing (`standing=true` $\rightarrow$ Tier 1, repo findings $\rightarrow$ Tier 2).

### Phase 2: Multi-Axis Hybrid Fusion & Epistemic Ranking (`mm_fusion.c`, `mm_ops.c`)
1. Port `adaptiveRrfK` and two-leg `lemma` + `sepal` candidate retrieval across Tier 1 + Tier 2 into `mm_fusion.c`.
2. Port `computeRankingScore` (epistemic status multipliers, citation DAG bonus, flashbulb `isAnchor` boost, project/subsystem scope match, conversational noise penalty) and markdown/JSON card formatting.
3. Implement `mm_record`, `mm_review` (`confirm`, `contradict`, `supersede`, utility feedback), and `mm_recall` for `scope: "persistent"`.
4. **Verification Gate**: `tests/test_fusion.c` verifying deterministic RRF scores, epistemic ordering (`supported` > `tested` > `hypothesized` > `contradicted`), and federated Tier 1 + Tier 2 merging.

### Phase 3: Pure-C Codebase Symbol & Call-Graph Engine (`mm_code.c`)
1. Implement the fast C lexical/AST scanner (`mm_code.c`) indexing C/H, TypeScript/JS, Python, and Shell symbols, storing line spans in `libislet` and tokens in `liblemma`.
2. Implement inbound/outbound call-graph tracing (`trace`), `git diff` hunk impact mapping (`diff: true`), stack-trace fault localization (`diagnose`), and L2/L3 codebase architecture overview (`overview: true`).
3. Bind persistent lore invariants (`Ref` cards) to files/symbols for first-touch module dossiers and counterfactual guardrails.
4. **Verification Gate**: `tests/test_code.c` indexing a fixture C/TS repository and verifying symbol lookup, call-graph depth traversal, and `git diff` hunk intersection.

### Phase 4: Session Transcript Engine & `axil-auren` Intent/Synthesis Client (`mm_session.c`, `mm_judge.c`)
1. Implement `mm_session.c` to index JSONL session turns (both Pi session format and `dwm-jarvis` voice turn format) and support `zoomTurn` + `lineRange` retrieval.
2. Implement `mm_judge.c` with HTTP client support for `axil-auren` (`/v1/embeddings`, `/v1/classify`, `/v1/chat/completions`), exemplar cosine margin classification (`mm_prime`), regex offline fallback, and bounded L0 $\rightarrow$ L1 $\rightarrow$ L2 $\rightarrow$ L3 consolidation draining.
3. **Verification Gate**: `tests/test_session.c` and `tests/test_judge.c` verifying transcript search, turn zooming, offline regex fallback, and L0 consolidation ordering.

### Phase 5: Universal Binaries (`mm` CLI & `mm mcp`) and `dwm-jarvis` / `pi-mm` Wiring
1. Implement `src/mm_cli.c` (`mm recall|record|review|prime|code|session|consolidate`) and `src/mm_mcp.c` (`mm mcp` JSON-RPC 2.0 stdio server).
2. Implement `npm/src/mm_napi.c` (`@tty-pt/mm`) and migrate `.pi/extensions/pi-mm` to delegate core operations to `libmm`.
3. Wire `dwm-jarvis` (`~/.local/src/dwm/scripts/dwm-auren`, `dwm-voice`, `dwm-jarvis-pi`) to Tier 1 global memory via `mm prime`, `mm recall`, and `mm record --standing`.
4. **Verification Gate**: End-to-end CLI, MCP stdio, N-API, and `dwm-jarvis` integration tests.
