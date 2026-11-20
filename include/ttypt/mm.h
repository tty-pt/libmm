#ifndef TTYPT_MM_H
#define TTYPT_MM_H

/**
 * @file mm.h
 * @brief Public API for libmm — Pure-C Two-Tier Multi-Axis Memory Engine.
 *
 * Unifies persistent episodic lore (libcorm + libjoint + liblemma + libsepal),
 * live codebase symbol & call-graph indexing (libislet + liblemma), and
 * session transcript recall across a two-tier storage hierarchy:
 *   - Tier 1 (MM_TIER_GLOBAL): Operator-wide standing directives, anchors,
 *     and desktop/voice history (~/.local/share/mm/global)
 *   - Tier 2 (MM_TIER_PROJECT): Repository-scoped lore, codebase symbol graph,
 *     and coding session logs (`<repo>/.mm` or `~/.pi/agent/mm/<slug>`)
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. Core Enumerations & Constants
 * ============================================================================ */

typedef struct mm_ctx mm_ctx_t;

/** @brief Storage tier selection for federated two-tier memory operations. */
typedef enum {
	MM_TIER_AUTO    = 0, /**< Route writes to PROJECT if open (or GLOBAL if standing=true); query BOTH */
	MM_TIER_GLOBAL  = 1, /**< Tier 1: ~/.local/share/mm/global */
	MM_TIER_PROJECT = 2, /**< Tier 2: `<repo>/.mm` or `~/.pi/agent/mm/<slug>` */
	MM_TIER_BOTH    = 3  /**< Explicit federated read across GLOBAL + PROJECT */
} mm_tier_t;

typedef enum {
	MM_SCOPE_ALL        = 0,
	MM_SCOPE_CODE       = 1,
	MM_SCOPE_SESSION    = 2,
	MM_SCOPE_PERSISTENT = 3
} mm_scope_t;

typedef enum {
	MM_LEVEL_NONE = 0,
	MM_LEVEL_L0   = 1,
	MM_LEVEL_L1   = 2,
	MM_LEVEL_L2   = 3,
	MM_LEVEL_L3   = 4
} mm_level_t;

typedef enum {
	MM_STATUS_OBSERVED     = 0,
	MM_STATUS_HYPOTHESIZED = 1,
	MM_STATUS_TESTED       = 2,
	MM_STATUS_SUPPORTED    = 3,
	MM_STATUS_CONTRADICTED = 4,
	MM_STATUS_SUPERSEDED   = 5
} mm_epistemic_status_t;

typedef enum {
	MM_ORIGIN_LEMMA = 0,
	MM_ORIGIN_SEPAL = 1,
	MM_ORIGIN_BOTH  = 2
} mm_origin_t;

typedef enum {
	MM_SYM_FUNCTION  = 0,
	MM_SYM_METHOD    = 1,
	MM_SYM_CLASS     = 2,
	MM_SYM_STRUCT    = 3,
	MM_SYM_INTERFACE = 4,
	MM_SYM_TYPE      = 5,
	MM_SYM_ENUM      = 6,
	MM_SYM_MACRO     = 7,
	MM_SYM_ROUTE     = 8,
	MM_SYM_VARIABLE  = 9
} mm_sym_kind_t;

const char           *mm_level_to_str(mm_level_t level);
mm_level_t            mm_level_from_str(const char *str);
const char           *mm_status_to_str(mm_epistemic_status_t status);
mm_epistemic_status_t mm_status_from_str(const char *str);
const char           *mm_origin_to_str(mm_origin_t origin);
const char           *mm_sym_kind_to_str(mm_sym_kind_t kind);
mm_sym_kind_t         mm_sym_kind_from_str(const char *str);

/* ============================================================================
 * 2. Memory Envelope & HTOK v4 Payload Codec (libmm.c)
 * ============================================================================ */

#define MM_MAX_EVIDENCE    32
#define MM_MAX_TAGS        16
#define MM_MAX_CONSTRAINTS 16
#define MM_MAX_APPROACHES  8
#define MM_MAX_FINDINGS    8
#define MM_MAX_DIFFS       8
#define MM_MAX_HISTORY     8

typedef struct {
	char approach[256];
	char outcome[32]; /**< "worked", "failed", "abandoned" */
	char reason[256];
} mm_approach_t;

typedef struct {
	mm_epistemic_status_t from;
	mm_epistemic_status_t to;
	char                  timestamp[32];
	char                  reason[256];
	uint32_t              evidence_ref;
} mm_lifecycle_transition_t;

typedef struct {
	mm_level_t             level;
	mm_epistemic_status_t  lifecycle;
	char                   type[32];        /**< "note", "episode", "decision", "investigation", "rule" */
	char                   timestamp[32];   /**< ISO-8601 UTC timestamp */
	char                  *summary;         /**< Primary text/summary (heap-allocated) */
	bool                   is_anchor;       /**< Flashbulb salience boost (1.25x) */
	bool                   standing;        /**< Global standing operator directive */
	uint32_t               supersedes;      /**< 0 if none */
	uint32_t               superseded_by;   /**< 0 if none */
	uint32_t               evidence[MM_MAX_EVIDENCE];
	size_t                 n_evidence;
	char                  *tags[MM_MAX_TAGS];
	size_t                 n_tags;
	char                  *constraints[MM_MAX_CONSTRAINTS];
	size_t                 n_constraints;

	/* Context metadata */
	char                   ctx_project[128];
	char                   ctx_subsystem[128];
	char                   ctx_module[128];
	char                   ctx_path[256];
	char                   ctx_subject[128];
	bool                   has_grid;
	double                 grid_x, grid_y, grid_z;

	/* Episode metadata */
	char                  *ep_problem;
	char                  *ep_request;
	char                  *ep_outcome;
	char                   ep_session_id[128];
	int                    ep_turn_id;
	char                   ep_run_id[128];
	char                   ep_leaf_id[128];
	char                   ep_tool_call_id[128];
	mm_approach_t          ep_approaches[MM_MAX_APPROACHES];
	size_t                 n_approaches;
	char                  *ep_findings[MM_MAX_FINDINGS];
	size_t                 n_findings;
	char                  *ep_diffs[MM_MAX_DIFFS];
	size_t                 n_diffs;

	/* Epistemic lifecycle transition history */
	mm_lifecycle_transition_t history[MM_MAX_HISTORY];
	size_t                    n_history;

	/* Utility feedback counters */
	int                    used_count;
	int                    irrelevant_count;
	int                    obsolete_count;
} mm_envelope_t;

void mm_envelope_init(mm_envelope_t *env);
void mm_envelope_free(mm_envelope_t *env);
void mm_envelope_clone(mm_envelope_t *dst, const mm_envelope_t *src);

/**
 * Serialize an mm_envelope_t into a canonical JSON string.
 * Caller must free(*out_json).
 */
int mm_envelope_to_json(const mm_envelope_t *env, char **out_json, size_t *out_len);

/**
 * Parse a JSON string (or raw "<DATE>:<TEXT>" legacy string) into mm_envelope_t.
 */
int mm_envelope_from_json(const char *json_or_raw, size_t len, mm_envelope_t *out_env);

/**
 * Pack an mm_envelope_t into "<YYYY-MM-DD>:<HTOK v4 binary>" for corm storage,
 * and also return its canonical JSON string if out_json != NULL.
 */
int mm_payload_encode(const mm_envelope_t *env,
                      uint8_t **out_bytes, size_t *out_len,
                      char **out_json, size_t *out_json_len);

/**
 * Decode a stored record (either "<DATE>:<HTOK binary>", raw HTOK binary,
 * JSON string, or "<DATE>:<plain text>") into an mm_envelope_t.
 */
int mm_payload_decode(const void *data, size_t len, mm_envelope_t *out_env);

/**
 * Compute the libjoint temporal window [since, until] for a mipmap level
 * relative to a given UTC epoch timestamp.
 * Returns true if a bounded window applies (L1=1d, L2=month), false for L0/L3.
 */
bool mm_window_for_level(mm_level_t level, int64_t now_epoch_sec,
                         char since_out[16], char until_out[16]);

/* ============================================================================
 * 3. Multi-Axis RRF & Symbolic Epistemic Ranking (mm_fusion.c)
 * ============================================================================ */

typedef struct {
	mm_tier_t   tier;
	uint32_t    ref;
	double      score;
	char       *record; /**< Raw/JSON record string (heap-allocated) */
} mm_result_line_t;

typedef struct {
	mm_tier_t      tier;
	uint32_t       ref;
	char          *record;
	mm_origin_t    origin;
	double         rrf_score;
	int            lexical_rank;
	int            semantic_rank;
	double         lexical_score;
	double         semantic_score;
	double         ranking_score;
	mm_envelope_t  envelope;
	bool           has_envelope;
} mm_candidate_t;

typedef struct {
	mm_level_t  mipmap;       /**< MM_LEVEL_NONE for default (L1+) */
	const char *lifecycle;    /**< NULL for default (excludes superseded), "all", or specific state */
	const char *project;
	const char *subsystem;
	const char *query;
	int         limit;
	bool        dedupe;
	bool        include_l0;
} mm_filter_criteria_t;

int    mm_adaptive_rrf_k(size_t candidate_count);
bool   mm_is_technical_inquiry(const char *query);
bool   mm_is_code_or_doc_artifact(const char *record, const mm_envelope_t *env);
bool   mm_is_question_memory(const mm_envelope_t *env);
bool   mm_is_assistant_echo(const mm_envelope_t *env);
bool   mm_is_clean_summary_text(const char *summary);

int    mm_fuse_rankings_rrf(const mm_result_line_t *lexical, size_t n_lexical,
                            const mm_result_line_t *semantic, size_t n_semantic,
                            int k_override,
                            mm_candidate_t **out_candidates, size_t *out_count);

double mm_compute_ranking_score(const mm_candidate_t *cand, const mm_filter_criteria_t *crit);

int    mm_filter_and_enrich(mm_candidate_t *candidates, size_t n_candidates,
                            const mm_filter_criteria_t *crit,
                            mm_candidate_t **out_filtered, size_t *out_count);

void   mm_candidates_free(mm_candidate_t *candidates, size_t count);

/* ============================================================================
 * 4. Context & Two-Tier Lifecycle (mm_ctx.c)
 * ============================================================================ */

typedef struct {
	const char *global_dir;    /**< NULL => $MM_GLOBAL_DIR or ~/.local/share/mm/global */
	const char *project_dir;   /**< NULL => auto-detect <repo>/.mm or ~/.pi/agent/mm/<slug> */
	const char *cwd;           /**< NULL => getcwd() */
	const char *corm_bin;      /**< NULL => auto-discover external/libcorm/bin/corm or PATH */
	const char *axis_path;     /**< NULL => auto-discover libjoint/liblemma/libsepal/libislet */
	const char *auren_url;     /**< NULL => $CORM_SEPAL_EMBED_URL or http://127.0.0.1:8081 */
	const char *embed_model;   /**< NULL => default "all-minilm" */
	bool        semantic_search;
	bool        prefer_daemon; /**< Connect to resident .cormd.sock if present */
} mm_options_t;

mm_ctx_t   *mm_open(const mm_options_t *opts);
void        mm_close(mm_ctx_t *ctx);
const char *mm_get_tier_dir(const mm_ctx_t *ctx, mm_tier_t tier);
const char *mm_get_project_root(const mm_ctx_t *ctx);
const char *mm_get_project_name(const mm_ctx_t *ctx);
bool        mm_has_project_tier(const mm_ctx_t *ctx);

/** Allocate next monotonic reference ID in the given tier. */
uint32_t    mm_next_ref(mm_ctx_t *ctx, mm_tier_t tier);

typedef struct {
	mm_tier_t              tier;         /**< MM_TIER_AUTO routes standing=true to GLOBAL */
	mm_level_t             level;        /**< Default MM_LEVEL_L2 for anchors/decisions, L1 for episodes */
	const char            *problem;      /**< Primary summary / rule / finding */
	const char            *request;      /**< Optional episode request */
	const char            *outcome;      /**< Optional episode outcome */
	const char            *type;         /**< "note", "episode", "decision", "rule" */
	mm_epistemic_status_t  status;
	bool                   standing;     /**< Global standing directive -> routes to MM_TIER_GLOBAL */
	bool                   anchor;       /**< Flashbulb salience boost (1.25x) */
	const char            *project;
	const char            *subsystem;
	const char            *module;
	const char            *path;
	const char           **tags;
	size_t                 n_tags;
	const char           **constraints;
	size_t                 n_constraints;
	const uint32_t        *evidence_refs;
	size_t                 n_evidence;
	uint32_t               supersedes_ref;
	const char            *session_id;
	int                    turn_id;
	const char            *run_id;
	const char            *leaf_id;
	const mm_approach_t   *approaches;
	size_t                 n_approaches;
	const char           **findings;
	size_t                 n_findings;
	const char           **diffs;
	size_t                 n_diffs;
	const char           **tests_passed;
	size_t                 n_tests_passed;
} mm_record_req_t;

int mm_record(mm_ctx_t *ctx, const mm_record_req_t *req,
              mm_tier_t *out_tier, uint32_t *out_ref);

int mm_get_envelope(mm_ctx_t *ctx, mm_tier_t tier, uint32_t ref, mm_envelope_t *out_env);
int mm_put_envelope(mm_ctx_t *ctx, mm_tier_t tier, uint32_t ref, const mm_envelope_t *env);
int mm_forget(mm_ctx_t *ctx, mm_tier_t tier, uint32_t ref);
int mm_list_refs(mm_ctx_t *ctx, mm_tier_t tier, uint32_t **out_refs, size_t *out_count);

typedef struct {
	mm_tier_t              tier;
	uint32_t               ref;
	const char            *action; /**< "inspect" | "update_lifecycle" | "confirm" | "contradict" | "supersede" | "helpful" | "harmful" | "obsolete" */
	const char            *reason;
	uint32_t               superseded_by_ref;
	mm_epistemic_status_t  target_lifecycle;
	uint32_t               evidence_ref;
	const char            *direction; /**< "down" | "up" */
	int                    depth;
} mm_review_req_t;

int mm_review(mm_ctx_t *ctx, const mm_review_req_t *req);
int mm_review_ex(mm_ctx_t *ctx, const mm_review_req_t *req, char **out_buf, size_t *out_len);

int mm_validate_lifecycle_transition(mm_ctx_t *ctx, mm_tier_t tier,
                                     const mm_envelope_t *env,
                                     mm_epistemic_status_t target,
                                     const char *reason,
                                     uint32_t evidence_ref,
                                     char *err_buf, size_t err_cap);

int mm_propagate_truth_maintenance(mm_ctx_t *ctx, mm_tier_t tier,
                                   uint32_t changed_ref,
                                   mm_epistemic_status_t new_state,
                                   uint32_t **out_downgraded, size_t *out_count);

int mm_traverse(mm_ctx_t *ctx, mm_tier_t tier, uint32_t start_ref,
                const char *direction, int max_depth,
                char **out_buf, size_t *out_len);

int mm_build_memory_overview(mm_ctx_t *ctx, mm_tier_t tier, int max_per_bucket,
                             char **out_buf, size_t *out_len);

int mm_evaluate_guardrail(mm_ctx_t *ctx, const char *intent_text,
                          const char *file_path, const char *symbol_name,
                          char **out_buf, size_t *out_len);

int mm_observe_verification_command(mm_ctx_t *ctx, const char *command,
                                    int exit_code, const char *output_snippet,
                                    uint32_t *out_promoted_ref);
int mm_record_file_mutation(mm_ctx_t *ctx, const char *file_path);

int mm_sync_standing_directives(mm_ctx_t *ctx);
int mm_synthesize_reinforcement(mm_ctx_t *ctx, const char *reason,
                                const char *milestone_summary,
                                char **out_buf, size_t *out_len);

int mm_consolidate_scopes_and_principles(mm_ctx_t *ctx, mm_tier_t tier,
                                         int *out_l2_created, int *out_l3_created);

typedef struct {
	mm_scope_t         scope;
	mm_tier_t          tier;
	const char        *query;
	const char        *symbol;
	const char *const *symbols;
	size_t             n_symbols;
	const char        *parent_symbol;
	const char        *file;
	const char        *kind;
	const char        *neighbors;    /**< "none" | "callers" | "callees" | "both" */
	const char        *trace;        /**< symbol name for call-graph trace */
	const char        *direction;    /**< "inbound" | "outbound" | "both" */
	int                depth;        /**< trace or diff depth (default 2) */
	bool               diff;         /**< git diff blast-radius mode */
	bool               overview;     /**< architecture or persistent lore overview */
	const char        *diagnose;     /**< stack trace or compiler error fault localization */
	const char        *guardrail;    /**< counterfactual pre-flight check */
	mm_tier_t          zoom_tier;
	uint32_t           zoom_ref;
	const char        *session_id;
	int                zoom_turn;
	int                tool_index;   /**< 1-based tool output index in session zoomTurn */
	const char        *lineage;      /**< "active" | "all" */
	int                line_start;
	int                line_end;
	int                limit;
	bool               include_l0;
	bool               include_evidence;
	bool               dedupe;
	bool               json_output;
	const char        *subsystem;    /**< Optional ctx_subsystem filter (e.g. "jarvis") */
} mm_recall_req_t;

int mm_recall(mm_ctx_t *ctx, const mm_recall_req_t *req, char **out_buf, size_t *out_len);
int mm_recall_candidates(mm_ctx_t *ctx, const mm_recall_req_t *req,
                         mm_candidate_t **out_candidates, size_t *out_count);

/* ============================================================================
 * 5. Pure-C Codebase Symbol & Call-Graph Engine (mm_code.c)
 * ============================================================================ */

#define MM_CODE_MAX_CALLEES 32
#define MM_CODE_MAX_EDGES   64

typedef struct {
	uint32_t       ref;
	uint32_t       file_id;
	char           name[128];
	char           parent_symbol[128];
	char           qualified_name[256];
	mm_sym_kind_t  kind;
	char           file_path[256];
	int            start_line;
	int            end_line;
	char           signature[256];
	char           doc_comment[256];
	char           raw_callees[MM_CODE_MAX_CALLEES][64];
	size_t         n_raw_callees;
	uint32_t       callees[MM_CODE_MAX_EDGES];
	size_t         n_callees;
	uint32_t       callers[MM_CODE_MAX_EDGES];
	size_t         n_callers;
} mm_code_symbol_t;

typedef struct {
	uint32_t       file_id;
	char           file_path[256];
	int64_t        mtime_ms;
	size_t         byte_size;
	int            line_count;
	char           imports[16][128];
	size_t         n_imports;
} mm_code_file_t;

typedef struct {
	char              root_dir[512];
	mm_code_file_t   *files;
	size_t            n_files;
	size_t            cap_files;
	mm_code_symbol_t *symbols;
	size_t            n_symbols;
	size_t            cap_symbols;
} mm_code_index_t;

void mm_code_index_init(mm_code_index_t *idx, const char *root_dir);
void mm_code_index_free(mm_code_index_t *idx);

int  mm_code_scan_source(const char *file_path, const char *content, size_t len,
                         uint32_t file_id, mm_code_index_t *idx);
int  mm_code_index_dir(mm_code_index_t *idx, const char *root_dir);
void mm_code_resolve_call_graph(mm_code_index_t *idx);

int  mm_code_save_manifest(const mm_code_index_t *idx, const char *manifest_path);
int  mm_code_load_manifest(mm_code_index_t *idx, const char *manifest_path);

int  mm_code_search(const mm_code_index_t *idx, const char *query,
                    const char *file_filter, const char *kind_filter,
                    int limit, char **out_buf, size_t *out_len);
int  mm_code_read_symbol(const mm_code_index_t *idx, const char *symbol_name,
                         const char *file_filter, const char *neighbors,
                         int line_start, int line_end,
                         char **out_buf, size_t *out_len);
int  mm_code_read_symbols_ex(const mm_code_index_t *idx,
                             const char *const *symbols, size_t n_symbols,
                             const char *parent_symbol,
                             const char *file_filter, const char *neighbors,
                             int line_start, int line_end,
                             char **out_buf, size_t *out_len);
int  mm_code_trace(const mm_code_index_t *idx, const char *symbol_name,
                   const char *direction, int max_depth,
                   char **out_buf, size_t *out_len);
int  mm_code_overview(const mm_code_index_t *idx, char **out_buf, size_t *out_len);
int  mm_code_diff_impact(const mm_code_index_t *idx, int depth,
                         char **out_buf, size_t *out_len);
int  mm_code_diagnose(const mm_code_index_t *idx, const char *trace_text,
                      int depth, char **out_buf, size_t *out_len);

int  mm_code_resolve_cross_boundary_pairs(const mm_code_index_t *idx,
                                          const char *symbol_name,
                                          const char *file_path,
                                          char **out_buf, size_t *out_len);
int  mm_code_detect_xy_mismatches(const mm_code_index_t *idx,
                                  char **out_buf, size_t *out_len);
int  mm_code_first_touch_dossier(const mm_code_index_t *idx,
                                 const char *file_or_symbol,
                                 char **out_buf, size_t *out_len);

int  mm_lore_bind_symbol(mm_ctx_t *ctx, uint32_t mem_ref, const char *summary,
                         const char *symbol_name, const char *file_path);
int  mm_lore_verify_drift(mm_ctx_t *ctx, const char *symbol_or_file,
                          char **out_buf, size_t *out_len);

int  mm_context_record_snapshot(mm_ctx_t *ctx, const char *session_id,
                                const char *file_path, int turn_idx);
int  mm_context_check_drift(mm_ctx_t *ctx, const char *session_id,
                            const char *file_path,
                            char **out_buf, size_t *out_len);

int  mm_swarm_record_lease(mm_ctx_t *ctx, const char *session_id,
                           const char *symbol_name, const char *file_path,
                           const char *mode);
int  mm_swarm_query_leases(mm_ctx_t *ctx, const char *current_session_id,
                           const char *symbol_name, const char *file_path,
                           char **out_buf, size_t *out_len);

/* ============================================================================
 * 6. Session Transcript Engine, Intent Priming & MCP Server (mm_session.c)
 * ============================================================================ */

typedef struct {
	const char *session_id;
	int         turn_idx;
	const char *role;          /**< "user" | "assistant" | "tool" | "compaction" */
	const char *text;
	const char *entry_id;      /**< Branch DAG node ID */
	const char *parent_id;     /**< Branch DAG parent ID */
	const char *tool_name;
	int         tool_index;    /**< 1-based tool execution index within turn */
	int         exit_code;
	bool        has_exit_code;
	const char *files_touched; /**< Comma-separated paths */
	const char *error_snippet;
} mm_session_event_t;

int mm_session_ingest_turn(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                           int turn_idx, const char *role, const char *text);
int mm_session_ingest_event(mm_ctx_t *ctx, mm_tier_t tier, const mm_session_event_t *ev);
int mm_session_recall(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                      const char *query, int zoom_turn, int line_start, int line_end,
                      int limit, char **out_buf, size_t *out_len);
int mm_session_recall_ex(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                         const char *query, int zoom_turn, int tool_index,
                         const char *lineage, int line_start, int line_end,
                         int limit, char **out_buf, size_t *out_len);
int mm_session_compact(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                       const char *custom_instructions,
                       char **out_summary, size_t *out_len);

typedef enum {
	MM_INTENT_STANDALONE = 0,
	MM_INTENT_RECALL     = 1,
	MM_INTENT_DIRECTIVE  = 2,
	MM_INTENT_OVERVIEW   = 3,
	MM_INTENT_WONDERING  = 4
} mm_intent_t;

mm_intent_t mm_classify_prompt(const char *prompt);
int         mm_prime(mm_ctx_t *ctx, const char *prompt, char **out_buf, size_t *out_len);
int         mm_consolidate_drain(mm_ctx_t *ctx, mm_tier_t tier, int max_batch);

/** Run a native JSON-RPC 2.0 MCP server over stdin/stdout. */
int         mm_mcp_serve_stdio(mm_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TTYPT_MM_H */
