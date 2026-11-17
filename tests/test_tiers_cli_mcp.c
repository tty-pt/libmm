#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

int main(void) {
	printf("=== test_tiers_cli_mcp: Two-Tier Federated Store, Sessions & Priming ===\n");

	char tpl_g[] = "/tmp/mm_test_global_XXXXXX";
	char tpl_p[] = "/tmp/mm_test_proj_XXXXXX";
	assert(mkdtemp(tpl_g) != NULL);
	assert(mkdtemp(tpl_p) != NULL);

	mm_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.global_dir    = tpl_g;
	opts.project_dir   = tpl_p;
	opts.cwd           = ".";
	opts.prefer_daemon = false;

	mm_ctx_t *ctx = mm_open(&opts);
	assert(ctx != NULL);
	assert(mm_has_project_tier(ctx) == true);
	assert(strcmp(mm_get_tier_dir(ctx, MM_TIER_GLOBAL), tpl_g) == 0);
	assert(strcmp(mm_get_tier_dir(ctx, MM_TIER_PROJECT), tpl_p) == 0);

	/* 1. Record a standing rule -> must route to MM_TIER_GLOBAL */
	mm_record_req_t r_global;
	memset(&r_global, 0, sizeof(r_global));
	r_global.tier     = MM_TIER_AUTO;
	r_global.standing = true;
	r_global.anchor   = true;
	r_global.problem  = "Always prefer memory_recall over raw grep for symbol lookup";
	r_global.type     = "rule";
	r_global.status   = MM_STATUS_SUPPORTED;

	mm_tier_t tier_out = MM_TIER_PROJECT;
	uint32_t ref_g = 0;
	assert(mm_record(ctx, &r_global, &tier_out, &ref_g) == 0);
	assert(tier_out == MM_TIER_GLOBAL);
	assert(ref_g == 1);

	/* 2. Record a project episode -> must route to MM_TIER_PROJECT */
	mm_record_req_t r_proj;
	memset(&r_proj, 0, sizeof(r_proj));
	r_proj.tier     = MM_TIER_AUTO;
	r_proj.standing = false;
	r_proj.problem  = "Fixed shallow water optical extinction in nd_seven_gl symbol";
	r_proj.type     = "episode";
	r_proj.status   = MM_STATUS_SUPPORTED;

	uint32_t ref_p = 0;
	assert(mm_record(ctx, &r_proj, &tier_out, &ref_p) == 0);
	assert(tier_out == MM_TIER_PROJECT);
	assert(ref_p == 1);

	/* 3. Federated recall across BOTH tiers for 'symbol' */
	mm_recall_req_t qreq;
	memset(&qreq, 0, sizeof(qreq));
	qreq.scope = MM_SCOPE_PERSISTENT;
	qreq.tier  = MM_TIER_BOTH;
	qreq.query = "symbol";
	qreq.limit = 10;

	char *recall_out = NULL;
	assert(mm_recall(ctx, &qreq, &recall_out, NULL) == 0);
	assert(recall_out != NULL);
	assert(strstr(recall_out, "[Ref G:1]") != NULL);
	assert(strstr(recall_out, "[Ref P:1]") != NULL);
	free(recall_out);

	/* 4. Review action: contradict Ref P:1 and verify lifecycle update */
	mm_review_req_t rev;
	memset(&rev, 0, sizeof(rev));
	rev.tier   = MM_TIER_PROJECT;
	rev.ref    = ref_p;
	rev.action = "contradict";
	assert(mm_review(ctx, &rev) == 0);

	mm_envelope_t check_env;
	assert(mm_get_envelope(ctx, MM_TIER_PROJECT, ref_p, &check_env) == 0);
	assert(check_env.lifecycle == MM_STATUS_CONTRADICTED);
	mm_envelope_free(&check_env);

	/* 5. Session transcript ingest & zoom recall */
	assert(mm_session_ingest_turn(ctx, MM_TIER_PROJECT, "sess01", 1, "user",
	                              "Line 1 of turn\nLine 2 about water shader\nLine 3 end") == 0);
	char *sess_out = NULL;
	assert(mm_session_recall(ctx, MM_TIER_PROJECT, "sess01", "water", 0, 0, 0, 5, &sess_out, NULL) == 0);
	assert(sess_out != NULL && strstr(sess_out, "Turn 1") != NULL);
	free(sess_out);

	char *zoom_out = NULL;
	assert(mm_session_recall(ctx, MM_TIER_PROJECT, "sess01", NULL, 1, 2, 2, 5, &zoom_out, NULL) == 0);
	assert(zoom_out != NULL && strstr(zoom_out, "Line 2 about water shader") != NULL);
	free(zoom_out);

	/* 6. Prompt Intent Classification & Priming */
	assert(mm_classify_prompt("Jarvis, remember that port 8081 is axil-auren") == MM_INTENT_DIRECTIVE);
	assert(mm_classify_prompt("Do you remember how we fixed the chord picker?") == MM_INTENT_RECALL);
	assert(mm_classify_prompt("Give me an architecture overview") == MM_INTENT_OVERVIEW);

	char *prime_out = NULL;
	assert(mm_prime(ctx, "Remember that we always run targeted tests first", &prime_out, NULL) == 0);
	assert(prime_out != NULL);
	assert(strstr(prime_out, "intent=\"directive\"") != NULL);
	assert(strstr(prime_out, "[Ref G:1]") != NULL);
	free(prime_out);

	/* 7. L0 -> L1 Consolidation Drain */
	mm_record_req_t r_l0;
	memset(&r_l0, 0, sizeof(r_l0));
	r_l0.tier    = MM_TIER_PROJECT;
	r_l0.level   = MM_LEVEL_L0;
	r_l0.problem = "Observed raw L0 turn event in test";
	r_l0.type    = "note";
	r_l0.status  = MM_STATUS_OBSERVED;
	uint32_t ref_l0 = 0;
	assert(mm_record(ctx, &r_l0, NULL, &ref_l0) == 0);

	int drained = mm_consolidate_drain(ctx, MM_TIER_PROJECT, 10);
	assert(drained == 1);

	mm_envelope_t l0_after;
	assert(mm_get_envelope(ctx, MM_TIER_PROJECT, ref_l0, &l0_after) == 0);
	assert(l0_after.lifecycle == MM_STATUS_SUPERSEDED);
	assert(l0_after.superseded_by > ref_l0);
	mm_envelope_free(&l0_after);

	mm_close(ctx);

	char cleanup_cmd[1024];
	snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf \"%s\" \"%s\"", tpl_g, tpl_p);
	int sys_rc = system(cleanup_cmd);
	(void)sys_rc;

	printf("ok - test_tiers_cli_mcp passed\n");
	return 0;
}
