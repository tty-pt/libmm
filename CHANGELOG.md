# Changelog

All notable changes to `libmm` (`@tty-pt/mm`) will be documented in this file.

## [1.0.0] - 2026-11-17

### Added
- **Pure-C Two-Tier Federated Memory Engine (`libmm`)**:
  - Tier 1 (`Global`) user-wide persistent memory (`~/.local/share/mm/global/mem.db`) and Tier 2 (`Project`) repository-scoped memory (`<repo>/.mm/mem.db`) backed by `libcorm` (`@joint,lemma,sepal`).
  - Native Unix domain socket (`cormd`) client with automatic `corm` CLI + `flock` concurrency fallback (`src/mm_ctx.c`).
- **HTOK v4 Envelope Serialization & Temporal Mipmapping (`src/libmm.c`)**:
  - Full `mm_envelope_t` lifecycle, JSON serializer/parser, HTOK v4 binary payload pack/unpack (`mm_payload_encode`, `mm_payload_decode`), and `L0`–`L3` mipmap temporal windowing (`mm_window_for_level`).
- **Hybrid RRF & Epistemic Ranking Kernel (`src/mm_fusion.c`)**:
  - Adaptive Reciprocal Rank Fusion (`mm_fuse_rankings_rrf`), 8-factor composite scoring (`mm_compute_ranking_score`), hygiene & anti-echo filtering (`mm_filter_and_enrich`), and prompt intent classification (`mm_classify_prompt`).
- **Multi-Language Codebase Symbol & Call-Graph Indexer (`src/mm_code.c`)**:
  - Structural scanner for C/C++, TypeScript/JavaScript, Python, Go, and Rust (`mm_code_scan_source`, `mm_code_index_dir`), call-graph traversal (`mm_code_trace`), git diff blast-radius analysis (`mm_code_diff_impact`), stack-trace fault localization (`mm_code_diagnose`), X-Y mismatch detection, cross-boundary route/FFI resolution, and first-touch dossiers.
- **Session Transcript Ingestion, Guardrails & Sleep-Cycle Consolidation (`src/mm_session.c`)**:
  - Append-only JSONL session log ingestion and recall (`mm_session_ingest_event`, `mm_session_recall`), pre-flight counterfactual guardrail evaluation (`mm_evaluate_guardrail`), verification-attested promotion ledger (`mm_observe_verification`), and sleep-cycle memory consolidation (`mm_consolidate`).
- **Universal `mm` CLI & Zero-JS MCP Server (`src/mm.c`, `src/mm_mcp.c`)**:
  - Standalone `mm` CLI (`record`, `recall`, `review`, `prime`, `consolidate`, `ingest`, `index`) and `mm mcp` JSON-RPC 2.0 server over `stdio`.
- **Node.js & Deno N-API Bindings (`@tty-pt/mm`)**:
  - Full N-API addon (`npm/native/mm_napi.c`) and ESM wrapper (`npm/dist/index.js`, `npm/dist/index.d.ts`) exposing `MmContext`, `MmCodeIndex`, and all fusion/envelope primitives.
