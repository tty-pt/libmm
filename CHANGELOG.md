# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **WebAssembly Reactor (`npm/wasm/mm_wasm.c`, `npm/dist/mm.wasm`)**:
  - Added standalone `mm.wasm` reactor build (`WASM_GLOBAL_BASE=131072` in `external/mk/include.mk`) exporting `mm_wasm_record`, `mm_wasm_recall_json`, and `mm_wasm_review` on `@tty-pt/stele`'s shared linear memory substrate.
- **In-Memory Tier Store & Structured Candidate Recall (`include/ttypt/mm.h`, `src/mm_ctx.c`, `src/mm_fusion.c`)**:
  - Added `mm_recall_candidates()`, optional `mm_recall_req_t.subsystem` filtering, and an in-process/WASM envelope store with accent-folded fallback search.
- **Doxygen Manpage Generation & Brand Assets (`Doxyfile`, `assets/logo.png`, `npm/assets/logo.png`)**:
  - Added `Doxyfile` for `mm.h.3` manpage generation via `make docs` / `make install`, cleaned Doxygen tag warnings in `include/ttypt/mm.h`, and added `libmm` brand assets.

### Changed
- **Browser-Safe ESM & Dual-Engine Runtime (`npm/dist/index.js`, `npm/dist/index.d.ts`, `npm/native/mm_napi.c`, `npm/package.json`)**:
  - Removed top-level `node:*` imports for browser/edge compatibility, added `getMmWasm()`, `wasmRecord()`, `wasmRecallJson()`, `wasmReview()`, and `MmContext` WASM fallback when native N-API is unavailable.
  - Added `summary` fallback alongside `problem` in `NapiRecord` (`npm/native/mm_napi.c`) and aligned TypeScript definitions (`MmRecordRequest`, `MmCodeIndex.readSymbols`).
  - Added `build:wasm` and `build` scripts and standardized `@tty-pt/corm` and `@tty-pt/stele` `peerDependencies` with zero runtime `dependencies`.
- **CI Multi-Environment Publishing & Documentation (`.github/workflows/ci.yml`, `README.md`, `npm/README.md`)**:
  - Added `pull_request` and `workflow_dispatch` CI triggers with tag-gated publishing across all supported environments (`deb`, `apk`, `rpm`, `pacman`, `brew`, `openbsd`, `winget`, and `npm`).
  - Aligned `CHANGELOG.md`, `README.md`, and `npm/README.md` with ecosystem formatting, badges, dual-engine runtime documentation, and package-manager installation conventions.

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