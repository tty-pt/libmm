# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Multi-Project Registry, Cross-Project Federation & IPC Invalidation (`include/ttypt/mm.h`, `src/mm_project.c`, `src/mm_ctx.c`, `tests/`)**:
  - Added multi-project registry, lazy `MM_PROJECT_ROOTS` discovery, manifest `mtime` freshness checks, and cross-project code queries (`mm_project_register()`, `mm_project_resolve()`, `mm_project_list()`, `mm_project_open_index()`, `mm_project_code_search()`, `mm_project_code_symbol()`, `mm_project_code_trace()`) (`fae4f92`, `554a403`).
  - Added cross-project federated recall (`mm_project_recall_all()`), multi-process IPC invalidation ring, and telemetry journaling (`tests/test_federation.c`, `tests/test_ipc_invalidation.c`, `tests/test_telemetry.c`) (`7669e88`).
  - Added canonical `.mm` / `.pi/mm` directory discovery and `liblemma` HTOK token decoding (`corm_htok_set_tok_decoder`) in `mm_payload_decode()` (`27808a5`).
- **Operational Metrics & Observability (`include/ttypt/mm.h`, `src/mm_ctx.c`, `src/mm_fusion.c`, `src/mm.c`, `tests/`)**:
  - Added `mm_metrics_t`, `mm_get_metrics()`, `mm_reset_metrics()`, `mm_format_metrics()`, CLI `mm status` / `mm metrics [--json]`, and unit tests (`tests/test_metrics.c`, `tests/test_token_decode.c`, `tests/test_ren_mm.c`) (`67c65c6`).
- **Zero-JSON Distillation & Extended N-API Bindings (`include/ttypt/mm.h`, `src/mm_distill.c`, `npm/native/mm_napi.c`, `tests/test_distill.c`)**:
  - Added zero-JSON line-delimited distillation (`mm_distill_prompt()`, `mm_distill_parse()`, `mm_distill_commit()`) (`ed5222b`).
  - Exported `projectCodeSearch`, `projectCodeSymbol`, `projectCodeTrace`, `distillPrompt`, `distillCommit`, `sessionIngestTurn`, `sessionRecall`, and `traverse` from `npm/native/mm_napi.c` (`554a403`, `ed5222b`).
- **`ren-mm` Adapter Module (`mods/ren-mm/ren-mm.c`, `Makefile`, `tests/test_ren_mm.c`)**:
  - Moved the `ren` memory adapter into `mods/ren-mm/ren-mm.c` (`201465b`), removed legacy `mod_mm.so` symlink installs (`bc0b5f7`), enabled `MM_TIER_BOTH` (`27808a5`), and added JSON schema and structured arguments support for `recall` and `record` (`7669e88`, `40972dd`).
- **Architecture & API Guides (`docs/ARCHITECTURE.md`, `docs/API.md`, `README.md`)**:
  - Added comprehensive `docs/ARCHITECTURE.md` and `docs/API.md` guides (`1fb2fd2`, `d88f8c1`).
- **WebAssembly Reactor (`npm/wasm/mm_wasm.c`, `npm/dist/mm.wasm`)**:
  - Added standalone `mm.wasm` reactor build (`WASM_GLOBAL_BASE=131072` in `external/mk/include.mk`) exporting `mm_wasm_record`, `mm_wasm_recall_json`, and `mm_wasm_review` on `@tty-pt/stele`'s shared linear memory substrate (`3798060`, `59be6d9`).
- **In-Memory Tier Store & Structured Candidate Recall (`include/ttypt/mm.h`, `src/mm_ctx.c`, `src/mm_fusion.c`)**:
  - Added `mm_recall_candidates()`, optional `mm_recall_req_t.subsystem` filtering, and an in-process/WASM envelope store with accent-folded fallback search (`3798060`).
- **Doxygen Manpage Generation & Brand Assets (`Doxyfile`, `assets/logo.png`, `npm/assets/logo.png`)**:
  - Added `Doxyfile` for `mm.h.3` manpage generation via `make docs` / `make install`, cleaned Doxygen tag warnings in `include/ttypt/mm.h`, and added `libmm` brand assets (`59be6d9`).

### Changed
- **Dynamic Formatting Buffers (`src/mm_ctx.c`, `src/mm_code.c`, `src/mm_session.c`)**:
  - Replaced fixed-buffer `snprintf` formatting across `mm_ctx.c`, `mm_code.c`, and `mm_session.c` with dynamically growing `mm_buf_appendf()` to prevent truncation on large envelopes, code reports, and reinforcement digests (`aa08149`).
- **Browser-Safe ESM & Dual-Engine Runtime (`npm/dist/index.js`, `npm/dist/index.d.ts`, `npm/native/mm_napi.c`, `npm/package.json`)**:
  - Removed top-level `node:*` imports for browser/edge compatibility, added `getMmWasm()`, `wasmRecord()`, `wasmRecallJson()`, `wasmReview()`, and `MmContext` WASM fallback when native N-API is unavailable (`59be6d9`).
  - Added `summary` fallback alongside `problem` in `NapiRecord` (`npm/native/mm_napi.c`) and aligned TypeScript definitions (`MmRecordRequest`, `MmCodeIndex.readSymbols`) (`59be6d9`).
  - Added `build:wasm` and `build` scripts and standardized `@tty-pt/corm` and `@tty-pt/stele` `peerDependencies` with zero runtime `dependencies` (`59be6d9`).
- **CI Multi-Environment Publishing & Documentation (`.github/workflows/ci.yml`, `README.md`, `npm/README.md`)**:
  - Added `pull_request` and `workflow_dispatch` CI triggers with tag-gated publishing across all supported environments (`deb`, `apk`, `rpm`, `pacman`, `brew`, `openbsd`, `winget`, and `npm`) (`59be6d9`).
  - Aligned `CHANGELOG.md`, `README.md`, and `npm/README.md` with ecosystem formatting, badges, dual-engine runtime documentation, and package-manager installation conventions (`5ab5c34`, `59be6d9`).

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