# libmm C API Reference (`<ttypt/mm.h>`)

Comprehensive reference for public data structures, lifecycle management, querying, and project indexing in `libmm`.

---

## 1. Context Lifecycle

### `mm_open`
```c
mm_ctx_t *mm_open(const mm_options_t *opts);
```
Initializes a federated two-tier memory context.
- **Parameters**: `opts` points to context configuration:
  - `global_dir`: Path to Tier 1 directory (defaults to `~/.local/share/mm/global` or `$MM_GLOBAL_DIR`).
  - `project_dir`: Path to Tier 2 directory (defaults to `<repo>/.mm`).
  - `cwd`: Active working directory.
  - `corm_bin`: Path to `corm` binary (optional; auto-discovered if NULL).
  - `axis_path`: Shared library path for `libjoint`, `liblemma`, `libsepal`, `libislet`.
  - `semantic_search`: Boolean flag to enable vector similarity search via `libsepal`.
  - `prefer_daemon`: Attempt connection to local `.cormd.sock` if active.
- **Returns**: Non-null `mm_ctx_t *` on success, `NULL` on initialization failure.

### `mm_close`
```c
void mm_close(mm_ctx_t *ctx);
```
Frees all in-memory indices, flushes pending database journals, and destroys the context.

---

## 2. Memory Record & Recall

### `mm_record`
```c
int mm_record(mm_ctx_t *ctx, const mm_record_req_t *req,
              mm_tier_t *out_tier, uint32_t *out_ref);
```
Stores an engineering episode, standing directive, invariant, or architectural decision.
- **Parameters**:
  - `req->problem`: Concise 1-line statement of the problem, invariant, or task.
  - `req->type`: String tag (`"episode"`, `"rule"`, `"decision"`, `"note"`).
  - `req->standing`: When `true`, automatically routes the record to `MM_TIER_GLOBAL`.
  - `req->findings`: Array of string observations or error signatures.
  - `req->approaches`: Array of `mm_approach_t` structures (`approach`, `outcome`, `reason`).
  - `req->constraints`: Array of explicit prohibitions or hard requirements.
  - `out_tier`: Receives the resolved tier (`MM_TIER_GLOBAL` or `MM_TIER_PROJECT`).
  - `out_ref`: Receives the assigned monotonic reference ID.
- **Returns**: `0` on success, non-zero on failure.

### `mm_recall`
```c
int mm_recall(mm_ctx_t *ctx, const mm_recall_req_t *req,
              char **out_buf, size_t *out_len);
```
Universal multi-resolution memory search across persistent lore, active sessions, and codebase indices.
- **Parameters**:
  - `req->scope`: `MM_SCOPE_ALL`, `MM_SCOPE_CODE`, `MM_SCOPE_SESSION`, or `MM_SCOPE_PERSISTENT`.
  - `req->query`: Natural language or lexical query string.
  - `req->limit`: Maximum candidate count.
  - `req->guardrail`: Pre-flight intent string for counterfactual checks.
  - `out_buf`: Pointer to allocated markdown buffer (caller must free with `free()`).
- **Returns**: `0` on success, non-zero on failure.

---

## 3. Multi-Project Discovery & Code Query

### `mm_project_resolve`
```c
int mm_project_resolve(const char *name_or_path, mm_project_info_t *out_info);
```
Lazily resolves a project name or path against the global registry or configured project search roots (`MM_PROJECT_ROOTS`).

### `mm_project_code_search`
```c
int mm_project_code_search(const char *project_or_null, const char *query,
                           const char *file_filter, const char *kind_filter,
                           int limit, char **out_buf, size_t *out_len);
```
Performs hybrid search across symbol signatures, qualified names, and docstrings.
- **`project_or_null`**: Exact project name, directory path, or `NULL` to federate across all registered projects.
- **Output**: Formatted markdown with `[project] file:line` anchors.

### `mm_project_code_symbol`
```c
int mm_project_code_symbol(const char *project_or_null, const char *symbol_name,
                           const char *file_filter,
                           char **out_buf, size_t *out_len);
```
Returns exact source definition, line ranges, and direct callers/callees.

### `mm_project_code_trace`
```c
int mm_project_code_trace(const char *project_or_null, const char *symbol_name,
                          const char *direction, int depth,
                          char **out_buf, size_t *out_len);
```
Traverses multi-hop call-graph edges (`"inbound"`, `"outbound"`, or `"both"`).

---

## 4. Zero-JSON Line-Delimited Distillation

### `mm_distill_prompt`
```c
int mm_distill_prompt(const char *conversation_text, char **out_prompt, size_t *out_len);
```
Generates a prompt instructing an LLM to distill dialogue into a compact, line-delimited key-value format.

### `mm_distill_parse`
```c
int mm_distill_parse(const char *distilled_text, mm_record_req_t *out_req,
                     char *storage_buf, size_t storage_cap);
```
Parses a line-delimited text block into an `mm_record_req_t` in a single pass without heap allocation.

### `mm_distill_commit`
```c
int mm_distill_commit(mm_ctx_t *ctx, const char *distilled_text,
                      mm_tier_t *out_tier, uint32_t *out_ref);
```
Parses and records a distilled block directly into the context database.
