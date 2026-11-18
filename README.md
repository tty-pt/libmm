# libmm

[![npm](https://img.shields.io/badge/npm-@tty--pt/mm-CB3837?logo=npm)](npm/README.md)
[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue.svg)](LICENSE)
[![Two-Tier Memory](https://img.shields.io/badge/memory-2--tier%20federated-16A34A)](#)
[![MCP Server](https://img.shields.io/badge/MCP-JSON--RPC%202.0%20stdio-blueviolet)](#)

> Pure-C two-tier multi-axis memory engine, codebase symbol & call-graph indexer, and universal CLI/MCP substrate built on [`libcorm`](https://github.com/tty-pt/libcorm), [`liblemma`](https://github.com/tty-pt/liblemma), [`libsepal`](https://github.com/tty-pt/libsepal), [`libjoint`](https://github.com/tty-pt/libjoint), [`libislet`](https://github.com/tty-pt/libislet), and [`libstele`](https://github.com/tty-pt/libstele). Canonical header is `<ttypt/mm.h>` and pkg-config package `mm.pc`.

`libmm` provides the cognitive, epistemic, and codebase-indexing engine behind `pi-mm` as a standalone C library (`libmm.so` / `libmm.a`), universal native CLI (`mm`), zero-JS Model Context Protocol server (`mm mcp`), and colocated Node.js/Deno N-API addon (`@tty-pt/mm`).

---

## 1. Quickstart

### C API (`<ttypt/mm.h>`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <ttypt/mm.h>

int main(void) {
    /* 1. Open federated two-tier memory context (Global + Project) */
    mm_options_t opts = {
        .cwd = ".",
        .semantic_search = true,
        .prefer_daemon = true
    };
    mm_ctx_t *ctx = mm_open(&opts);

    /* 2. Record a standing operator rule or verified episode */
    mm_tier_t tier;
    uint32_t ref = 0;
    mm_record_req_t rec = {
        .tier = MM_TIER_AUTO,
        .level = MM_LEVEL_L2,
        .problem = "Always use reactive wakeup notifications instead of polling loops",
        .type = "rule",
        .status = MM_STATUS_SUPPORTED,
        .standing = true,
        .anchor = true
    };
    mm_record(ctx, &rec, &tier, &ref);

    /* 3. Recall across persistent lore, session transcripts, or codebase symbols */
    char *report = NULL;
    size_t len = 0;
    mm_recall_req_t q = {
        .scope = MM_SCOPE_PERSISTENT,
        .tier = MM_TIER_BOTH,
        .query = "polling loops",
        .limit = 5
    };
    if (mm_recall(ctx, &q, &report, &len) == 0 && report) {
        printf("%s\n", report);
        free(report);
    }

    mm_close(ctx);
    return 0;
}
```

Compile and link:
```bash
cc app.c $(pkg-config --cflags --libs mm) -o app
```

### Universal `mm` CLI & Zero-JS MCP Server

```bash
# Record a standing global rule (routes automatically to Tier 1 Global)
mm record --standing --anchor --problem "Never poll status in a loop; await reactive notifications"

# Record a project episode (routes to Tier 2 Project)
mm record --type episode --status supported --problem "Fixed offscreen FBO viewport resize"

# Recall across persistent lore, codebase AST symbols, or session logs
mm recall --scope persistent "reactive notifications"
mm recall --scope code --symbol mm_fuse_rankings_rrf --neighbors both
mm recall --scope code --trace mm_recall --direction outbound --depth 2
mm recall --scope code --diff

# Run the native JSON-RPC 2.0 MCP server over stdio (Claude Code, Cursor, Zed, Pi)
mm mcp
```

---

## 2. Two-Tier Federated Architecture & Three Recall Scopes

`libmm` unifies three memory scopes across two physical storage tiers:

### Two Storage Tiers (`mm_tier_t`)
1. **Tier 1 — Global (`MM_TIER_GLOBAL`, `~/.local/share/mm/global/mem.db`)**:
   - Operator-wide standing directives (`standing: true`), persona tenets, flashbulb anchors (`anchor: true`), and desktop/voice history (`dwm-jarvis`).
2. **Tier 2 — Project (`MM_TIER_PROJECT`, `<repo>/.mm/mem.db` or `~/.pi/agent/mm/<slug>`)**:
   - Repository-scoped architectural invariants, debugging episodes, verification-attested root causes, symbol-bound lore, codebase AST graph, and coding session transcripts.
3. **Automatic Routing (`MM_TIER_AUTO` / `MM_TIER_BOTH`)**:
   - Writes with `standing = true` route automatically to Tier 1 (`Global`); project episodes route to Tier 2 (`Project`).
   - Reads federate both tiers via Adaptive Reciprocal Rank Fusion (`mm_fuse_rankings_rrf`).

### Three Recall Scopes (`mm_scope_t`)
| Scope | Subsystem | Capabilities |
| :--- | :--- | :--- |
| **`MM_SCOPE_PERSISTENT`** | `src/libmm.c`, `src/mm_ctx.c`, `src/mm_fusion.c` | HTOK v4 binary envelope storage (`mm_payload_encode`/`decode`), `L0`–`L3` temporal mipmapping (`mm_window_for_level`), hybrid lexical (`liblemma`) + semantic (`libsepal`) RRF fusion (`mm_fuse_rankings_rrf`), 8-factor epistemic scoring (`mm_compute_ranking_score`), evidence DAG truth maintenance (`mm_propagate_truth_maintenance`), and sleep-cycle consolidation (`mm_consolidate_scopes_and_principles`). |
| **`MM_SCOPE_CODE`** | `src/mm_code.c` | Multi-language zero-regex structural scanner (`mm_code_scan_source`) for C/C++, TypeScript/JavaScript, Python, Go, and Rust; symbol search (`mm_code_search`); concrete symbol/batch source read (`mm_code_read_symbols_ex`); call-graph traversal (`mm_code_trace`); architecture overview (`mm_code_overview`); `git diff` blast-radius impact (`mm_code_diff_impact`); stack-trace fault localization (`mm_code_diagnose`); `XY_DECL`/`XY_IMPL` parity checks (`mm_code_detect_xy_mismatches`); cross-boundary pairing (`mm_code_resolve_cross_boundary_pairs`); first-touch module dossiers (`mm_code_first_touch_dossier`); code-to-lore binding (`mm_lore_bind_symbol`, `mm_lore_verify_drift`); and swarm stigmergy leases (`mm_swarm_record_lease`). |
| **`MM_SCOPE_SESSION`** | `src/mm_session.c` | Append-only session transcript ingestion (`mm_session_ingest_event`), branch-aware 3-tier session recall (`mm_session_recall_ex`), mipmap session compaction (`mm_session_compact`), pre-flight counterfactual guardrails (`mm_evaluate_guardrail`), verification-attested promotion (`mm_observe_verification_command`), and prompt intent priming (`mm_classify_prompt`, `mm_prime`). |

---

## 3. Public C API Overview (`<ttypt/mm.h>`)

| Category | Key Functions |
| :--- | :--- |
| **Context Lifecycle** | `mm_open()`, `mm_close()`, `mm_get_tier_dir()`, `mm_get_project_root()`, `mm_get_project_name()`, `mm_has_project_tier()`, `mm_next_ref()` |
| **Envelope & HTOK v4 Codec** | `mm_envelope_init()`, `mm_envelope_free()`, `mm_envelope_clone()`, `mm_envelope_to_json()`, `mm_envelope_from_json()`, `mm_payload_encode()`, `mm_payload_decode()`, `mm_window_for_level()` |
| **RRF & Epistemic Ranking** | `mm_adaptive_rrf_k()`, `mm_fuse_rankings_rrf()`, `mm_compute_ranking_score()`, `mm_filter_and_enrich()`, `mm_is_technical_inquiry()`, `mm_is_code_or_doc_artifact()`, `mm_is_question_memory()`, `mm_is_assistant_echo()`, `mm_is_clean_summary_text()` |
| **Record, Recall & Review** | `mm_record()`, `mm_recall()`, `mm_review()`, `mm_review_ex()`, `mm_get_envelope()`, `mm_put_envelope()`, `mm_forget()`, `mm_list_refs()`, `mm_traverse()`, `mm_build_memory_overview()` |
| **Truth Maintenance & Guardrails** | `mm_validate_lifecycle_transition()`, `mm_propagate_truth_maintenance()`, `mm_evaluate_guardrail()`, `mm_observe_verification_command()`, `mm_record_file_mutation()`, `mm_consolidate_scopes_and_principles()` |
| **Codebase AST & Call Graph** | `mm_code_index_init()`, `mm_code_scan_source()`, `mm_code_index_dir()`, `mm_code_resolve_call_graph()`, `mm_code_search()`, `mm_code_read_symbol()`, `mm_code_read_symbols_ex()`, `mm_code_trace()`, `mm_code_overview()`, `mm_code_diff_impact()`, `mm_code_diagnose()`, `mm_code_detect_xy_mismatches()`, `mm_code_resolve_cross_boundary_pairs()`, `mm_code_first_touch_dossier()` |
| **Session & MCP Server** | `mm_session_ingest_turn()`, `mm_session_ingest_event()`, `mm_session_recall_ex()`, `mm_session_compact()`, `mm_classify_prompt()`, `mm_prime()`, `mm_consolidate_drain()`, `mm_mcp_serve_stdio()` |

---

## 4. Installation & Build

### Prebuilt OS Packages (`tty.pt`)
Prebuilt native packages (`libmm`) for all 7 supported OS package managers — Linux (`apt`, `apk`, `dnf`, `pacman`), macOS (`brew`), Windows (`winget`), and OpenBSD (`pkg_add`) — are distributed via [**tty.pt packages**](https://github.com/tty-pt/ci/blob/main/docs/install.md).

### Build from Source
```bash
make              # builds lib/libmm.so, lib/libmm.a, and bin/mm
make test         # runs all C unit suites (envelope, fusion, ctx, code, session/CLI/MCP)
make npm-build    # builds Node-API addon (npm/dist/mm_node.node)
make npm-test     # runs Node.js N-API integration test suite
sudo make install # installs headers, libraries, mm binary, and mm.pc
```

---

## 5. Node.js & Deno Bindings (`@tty-pt/mm`)

`@tty-pt/mm` ships prebuilt Node-API (`mm.node`) binaries across all 6 target platforms (`linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`, `win32-x64`):

```bash
npm install @tty-pt/mm
```

See [npm/README.md](npm/README.md) for the complete JavaScript/TypeScript API reference (`MmContext`, `MmCodeIndex`, `fuseRankingsRRF`, `computeRankingScore`, `payloadEncode`, `payloadDecode`, `scanSource`).

---

## License

BSD 2-Clause License — Copyright (c) 2026, Paulo André Azevedo Quirino. See [`LICENSE`](LICENSE).
