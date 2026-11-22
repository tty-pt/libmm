# libmm C Architecture Specification

`libmm` is a pure-C, two-tier, multi-axis cognitive memory engine and codebase intelligence kernel. It implements high-performance, deterministic storage, lexical and semantic retrieval, AST-bound code analysis, and session transcript indexing without runtime dependencies on Node.js or high-level interpreters.

---

## 1. Two-Tier Storage Topology

`libmm` organizes all memories, codebase indices, and conversational logs across two explicit tiers:

```
                          ┌───────────────────────────┐
                          │         libmm API         │
                          │        <ttypt/mm.h>       │
                          └─────────────┬─────────────┘
                                        │
                 ┌──────────────────────┴──────────────────────┐
                 ▼                                             ▼
    ┌─────────────────────────┐                   ┌─────────────────────────┐
    │         Tier 1          │                   │         Tier 2          │
    │     MM_TIER_GLOBAL      │                   │    MM_TIER_PROJECT      │
    │ ~/.local/share/mm/global│                   │     <repo>/.mm or       │
    │                         │                   │  ~/.pi/agent/mm/<slug>  │
    └────────────┬────────────┘                   └────────────┬────────────┘
                 │                                             │
      ┌──────────┴──────────┐                       ┌──────────┴──────────┐
      │ • Standing Rules    │                       │ • Episode Lore      │
      │ • Operator Anchors  │                       │ • Codebase AST Graph│
      │ • Voice Preferences │                       │ • Session Logs      │
      │ • Multi-Proj Index  │                       │ • Stigmergy Leases  │
      └─────────────────────┘                       └─────────────────────┘
```

1. **Tier 1 (`MM_TIER_GLOBAL`)**:
   - Location: `~/.local/share/mm/global` (or `$MM_GLOBAL_DIR`).
   - Scope: Universal operator-level preferences, standing invariants, cross-project registry, and audio/voice interaction logs.
2. **Tier 2 (`MM_TIER_PROJECT`)**:
   - Location: `<project_root>/.mm` or `~/.pi/agent/mm/<slug>`.
   - Scope: Project-specific architectural decisions, bug investigations, C/TypeScript symbol graph, and active coding session logs.

---

## 2. Multi-Axis Indexing & Query Pipeline

`libmm` leverages a modular suite of pure-C libraries to index and query information across multiple orthogonal axes:

```
[ Query ]
   │
   ├─── 1. Lexical Axis (liblemma) ───────► Accent-folded token matching & BM25
   │
   ├─── 2. Semantic Axis (libsepal) ──────► Cosine similarity over embeddings
   │
   ├─── 3. Structural Axis (libislet) ────► 2D interval trees & code AST spans
   │
   └─── 4. Graph Axis (libcorm) ──────────► Bidirectional evidence DAG traversal
   │
   ▼
[ Reciprocal Rank Fusion (RRF) + Epistemic Re-Ranking ]
   │
   ▼
[ Filtered & Enriched Result Envelope ]
```

### Fusion Algorithm
Rankings from lexical and semantic searches are fused using Reciprocal Rank Fusion:

$$RRF(d) = \sum_{m \in M} \frac{1}{k + r_m(d)}$$

Where $k$ is an adaptive smoothing parameter computed by `mm_adaptive_rrf_k(candidate_count)`. Candidate scores are subsequently modulated by epistemic weights:
- `supported`: 1.25x boost.
- `tested`: 1.0x baseline.
- `hypothesized`: 0.7x weight.
- `observed`: 0.4x weight.
- `contradicted` / `superseded`: Filtered out or heavily penalized.

---

## 3. Codebase Intelligence & Call-Graph Resolution

The code intelligence subsystem (`src/mm_code.c` and `src/mm_project.c`) parses source trees directly into contiguous arrays in pure C:

### In-Memory Layout
- **`mm_code_file_t`**: File path, language family (`C`, `JS`, etc.), and line offsets.
- **`mm_code_symbol_t`**:
  - `name`, `qualified_name`, `file_path`, `start_line`, `end_line`.
  - `kind`: `Function`, `Method`, `Class`, `Struct`, `Interface`, `Type`, `Enum`, `Macro`, `Route`.
  - `signature`, `doc_comment`.
  - `callers`: Array of `uint32_t` symbol reference IDs representing inbound calls.
  - `callees`: Array of `uint32_t` symbol reference IDs representing outbound calls.

### Lazy Multi-Project Discovery
`src/mm_project.c` manages multiple independent code repositories on demand:
- **Registry File**: `~/.local/share/mm/projects.tsv` maps project names to root paths, manifest locations, and last-indexed timestamps.
- **Freshness Detection**: `is_manifest_fresh()` inspects file modification times (`st_mtime`) across the project root. If any source file is newer than the cached `.manifest`, an incremental re-index is automatically triggered before query execution.
- **Root Probing**: If an unrecognized project is queried, `libmm` probes colon-separated paths defined in `MM_PROJECT_ROOTS` (default: `$HOME:$HOME/site/external`).

---

## 4. Zero-JSON Memory Distillation

To minimize token usage, avoid heap allocations, and eliminate JSON schema validation failures, `src/mm_distill.c` provides a compact, line-delimited distillation format:

```text
PROBLEM: Fix socket file descriptor leak on client disconnect
TYPE: episode
STANDING: false
PROJECT: axil-auren
SUBSYSTEM: net
PATH: src/libren.c
APPROACH: Close fd on exit | worked | Prevents FD exhaustion
FINDING: Socket remained open after abrupt client termination
CONSTRAINT: Never exit connection loop without closing client FD
```

- **Parsing Complexity**: $O(N)$ single-pass string scan with zero JSON parser dependencies.
- **Direct Commit**: Populates an `mm_record_req_t` and stores directly into `libcorm` in a single invocation (`mm_distill_commit`).
