#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

int main(void) {
	printf("=== test_code: Full Codebase Intelligence, XY/WASM, Lore Binding, Drift & Swarm Leases ===\n");

	char td[] = "/tmp/mm_test_code_XXXXXX";
	char tpl_g[] = "/tmp/mm_test_code_g_XXXXXX";
	char tpl_p[] = "/tmp/mm_test_code_p_XXXXXX";
	assert(mkdtemp(td) != NULL);
	assert(mkdtemp(tpl_g) != NULL);
	assert(mkdtemp(tpl_p) != NULL);

	/* Write actual source files on disk so snippet reading & drift checks work end-to-end */
	char f_engine[512], f_app[512], f_wasm[512];
	snprintf(f_engine, sizeof(f_engine), "%s/engine.c", td);
	snprintf(f_app, sizeof(f_app), "%s/app.ts", td);
	snprintf(f_wasm, sizeof(f_wasm), "%s/ux_client.c", td);

	const char *c_src =
		"#include <stdio.h>\n"
		"#define MAX_ITEMS 64\n"
		"XY_DECL(int, fetch_user, (int uid));\n"
		"XY_IMPL(int, fetch_user, (int uid, int extra)) {\n"
		"    return uid + extra;\n"
		"}\n"
		"XY_DECL(void, orphan_decl, (void));\n"
		"typedef struct {\n"
		"    int id;\n"
		"} item_t;\n"
		"/* Computes double of input */\n"
		"static int helper_compute(int x) {\n"
		"    return x * 2;\n"
		"}\n"
		"static void on_item_ready(int v) {\n"
		"    helper_compute(v);\n"
		"}\n"
		"int main_handler(int a) {\n"
		"    struct cfg c = { .detail = on_item_ready };\n"
		"    xy_load(\"ux_client\", \"#panel\");\n"
		"    return helper_compute(a);\n"
		"}\n"
		"void register_routes(void) {\n"
		"    http_route(\"GET\", \"/api/items\", main_handler);\n"
		"}\n";

	const char *ts_src =
		"import { foo } from './bar.ts';\n"
		"export interface ConfigOpts {\n"
		"    port: number;\n"
		"}\n"
		"export class AlphaWorker {\n"
		"    process(x: number) {\n"
		"        return helper_compute(x);\n"
		"    }\n"
		"}\n"
		"export class BetaWorker {\n"
		"    process(x: number) {\n"
		"        return main_handler(x);\n"
		"    }\n"
		"}\n"
		"export function runApp(cfg: ConfigOpts) {\n"
		"    app.post(\"/api/run\", main_handler);\n"
		"    return main_handler(cfg.port);\n"
		"}\n";

	const char *wasm_src =
		"STL_WASM_MODULE(\"ux_client\", 1);\n"
		"void ux_client_init(void) {\n"
		"    fetch_user(1, 2);\n"
		"}\n";

	FILE *fp = fopen(f_engine, "w"); assert(fp); fputs(c_src, fp); fclose(fp);
	fp = fopen(f_app, "w"); assert(fp); fputs(ts_src, fp); fclose(fp);
	fp = fopen(f_wasm, "w"); assert(fp); fputs(wasm_src, fp); fclose(fp);

	mm_code_index_t idx;
	mm_code_index_init(&idx, td);

	assert(mm_code_scan_source("engine.c", c_src, strlen(c_src), 100, &idx) == 0);
	assert(mm_code_scan_source("app.ts", ts_src, strlen(ts_src), 101, &idx) == 0);
	assert(mm_code_scan_source("ux_client.c", wasm_src, strlen(wasm_src), 102, &idx) == 0);
	mm_code_resolve_call_graph(&idx);

	assert(idx.n_files == 3);
	assert(idx.n_symbols >= 10);

	/* 1. Verify XY_DECL / XY_IMPL mismatch detection */
	char *mis_out = NULL;
	assert(mm_code_detect_xy_mismatches(&idx, &mis_out, NULL) == 0);
	assert(mis_out != NULL);
	assert(strstr(mis_out, "fetch_user") != NULL);
	assert(strstr(mis_out, "orphan_decl") != NULL);
	free(mis_out);

	/* 2. Verify cross-boundary pairs (XY_DECL <-> XY_IMPL) */
	char *pairs_out = NULL;
	assert(mm_code_resolve_cross_boundary_pairs(&idx, "fetch_user", NULL, &pairs_out, NULL) == 0);
	assert(pairs_out != NULL);
	assert(strstr(pairs_out, "XY Protocol Pair") != NULL);
	free(pairs_out);

	/* 3. Verify C struct initializer callback stitching (.detail = on_item_ready) and HTTP Route detection */
	char *trace_out = NULL;
	assert(mm_code_trace(&idx, "main_handler", "both", 2, &trace_out, NULL) == 0);
	assert(trace_out != NULL);
	assert(strstr(trace_out, "runApp") != NULL);
	assert(strstr(trace_out, "helper_compute") != NULL);
	assert(strstr(trace_out, "on_item_ready") != NULL);
	free(trace_out);

	/* 4. Verify fail-closed ambiguity for `process` without parent_symbol, and disambiguation WITH parent_symbol */
	char *ambig_out = NULL;
	assert(mm_code_read_symbol(&idx, "process", NULL, "both", 0, 0, &ambig_out, NULL) == 0);
	assert(ambig_out != NULL);
	assert(strstr(ambig_out, "Ambiguous symbol") != NULL);
	assert(strstr(ambig_out, "AlphaWorker") != NULL);
	assert(strstr(ambig_out, "BetaWorker") != NULL);
	free(ambig_out);

	char *disambig_out = NULL;
	const char *syms[1] = { "process" };
	assert(mm_code_read_symbols_ex(&idx, syms, 1, "AlphaWorker", NULL, "both", 0, 0, &disambig_out, NULL) == 0);
	assert(disambig_out != NULL);
	assert(strstr(disambig_out, "AlphaWorker") != NULL);
	free(disambig_out);

	/* 5. Verify First-Touch Symbol Dossier (with Tier-0 validation command) */
	char *dossier_out = NULL;
	assert(mm_code_first_touch_dossier(&idx, "helper_compute", &dossier_out, NULL) == 0);
	assert(dossier_out != NULL);
	assert(strstr(dossier_out, "First-Touch") != NULL);
	assert(strstr(dossier_out, "Tier-0") != NULL);
	free(dossier_out);

	/* 6. Verify Stack-Trace Fault Diagnosis with file:line frame resolution */
	char *diag_out = NULL;
	assert(mm_code_diagnose(&idx, "Assertion failed at engine.c:13 in helper_compute", 2, &diag_out, NULL) == 0);
	assert(diag_out != NULL);
	assert(strstr(diag_out, "helper_compute") != NULL);
	assert(strstr(diag_out, "main_handler") != NULL);
	free(diag_out);

	/* 7. Verify Living Lore-to-AST Symbol Binding, Context Drift & Swarm Leases via mm_ctx_t */
	mm_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.global_dir    = tpl_g;
	opts.project_dir   = tpl_p;
	opts.cwd           = td;
	opts.prefer_daemon = false;

	mm_ctx_t *ctx = mm_open(&opts);
	assert(ctx != NULL);

	/* Bind lore ref 1 to helper_compute */
	assert(mm_lore_bind_symbol(ctx, 1, "helper_compute must always multiply by 2", "helper_compute", "engine.c") == 0);

	char *drift_out = NULL;
	assert(mm_lore_verify_drift(ctx, "helper_compute", &drift_out, NULL) == 0);
	assert(drift_out != NULL && strstr(drift_out, "FRESH") != NULL);
	free(drift_out);

	/* Record context snapshot before mutating engine.c */
	assert(mm_context_record_snapshot(ctx, "sess-1", "engine.c", 1) == 0);

	/* Record Swarm lease from agent-A on helper_compute, then query from agent-B on main_handler */
	assert(mm_swarm_record_lease(ctx, "agent-A", "helper_compute", "engine.c", "write") == 0);
	char *lease_warn = NULL;
	assert(mm_swarm_query_leases(ctx, "agent-B", "main_handler", "engine.c", &lease_warn, NULL) == 0);
	assert(lease_warn != NULL && strstr(lease_warn, "agent-A") != NULL);
	free(lease_warn);

	/* Mutate helper_compute signature in engine.c */
	sleep(1);
	const char *c_src_mutated =
		"#include <stdio.h>\n"
		"static int helper_compute(int x, int scale) {\n"
		"    return x * scale;\n"
		"}\n";
	fp = fopen(f_engine, "w"); assert(fp); fputs(c_src_mutated, fp); fclose(fp);

	/* Verify Context Drift detects modified file/symbol */
	char *ctx_drift_warn = NULL;
	assert(mm_context_check_drift(ctx, "sess-1", "engine.c", &ctx_drift_warn, NULL) == 0);
	assert(ctx_drift_warn != NULL && strstr(ctx_drift_warn, "Drift") != NULL);
	free(ctx_drift_warn);

	/* Verify Lore Drift now reports STALE_SUSPECT */
	char *lore_stale = NULL;
	assert(mm_lore_verify_drift(ctx, "helper_compute", &lore_stale, NULL) == 0);
	assert(lore_stale != NULL && strstr(lore_stale, "STALE_SUSPECT") != NULL);
	free(lore_stale);

	mm_close(ctx);

	/* 8. Verify manifest v2 round-trip */
	char mpath[512];
	snprintf(mpath, sizeof(mpath), "%s/manifest.json", td);
	assert(mm_code_save_manifest(&idx, mpath) == 0);

	mm_code_index_t idx2;
	mm_code_index_init(&idx2, td);
	assert(mm_code_load_manifest(&idx2, mpath) == 0);
	assert(idx2.n_symbols == idx.n_symbols);

	mm_code_index_free(&idx2);
	mm_code_index_free(&idx);

	char rm_cmd[2048];
	snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\" \"%s\" \"%s\"", td, tpl_g, tpl_p);
	int sys_rc = system(rm_cmd);
	(void)sys_rc;

	printf("ok - test_code passed\n");
	return 0;
}
