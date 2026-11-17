#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

int main(void) {
	printf("=== test_tiers_cli_mcp: Epistemic State Machine, Guardrails, Ledger, Overview, Sessions & Consolidation ===\n");

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

	/* 1. Epistemic Lifecycle State Machine validation */
	char err_buf[256];
	mm_envelope_t dummy_env;
	mm_envelope_init(&dummy_env);
	/* superseded is terminal */
	dummy_env.lifecycle = MM_STATUS_SUPERSEDED;
	assert(mm_validate_lifecycle_transition(ctx, MM_TIER_PROJECT, &dummy_env, MM_STATUS_SUPPORTED, "retry", 1, err_buf, sizeof(err_buf)) != 0);
	/* contradicted -> supported is forbidden directly (must go through tested) */
	dummy_env.lifecycle = MM_STATUS_CONTRADICTED;
	assert(mm_validate_lifecycle_transition(ctx, MM_TIER_PROJECT, &dummy_env, MM_STATUS_SUPPORTED, "fixed", 1, err_buf, sizeof(err_buf)) != 0);
	/* contradicted without reason or evidence_ref is rejected */
	dummy_env.lifecycle = MM_STATUS_HYPOTHESIZED;
	assert(mm_validate_lifecycle_transition(ctx, MM_TIER_PROJECT, &dummy_env, MM_STATUS_CONTRADICTED, "", 0, err_buf, sizeof(err_buf)) != 0);
	/* promotion to supported with non-existent evidence_ref is rejected */
	assert(mm_validate_lifecycle_transition(ctx, MM_TIER_PROJECT, &dummy_env, MM_STATUS_SUPPORTED, "looks good", 999, err_buf, sizeof(err_buf)) != 0);
	mm_envelope_free(&dummy_env);

	/* 2. Record a standing rule -> must route to MM_TIER_GLOBAL and sync standing_directives.json */
	mm_record_req_t r_global;
	memset(&r_global, 0, sizeof(r_global));
	r_global.tier     = MM_TIER_AUTO;
	r_global.standing = true;
	r_global.anchor   = true;
	r_global.problem  = "Never use raw polling loops; always yield for reactive wakeup";
	r_global.type     = "rule";
	r_global.status   = MM_STATUS_SUPPORTED;

	mm_tier_t tier_out = MM_TIER_PROJECT;
	uint32_t ref_g = 0;
	assert(mm_record(ctx, &r_global, &tier_out, &ref_g) == 0);
	assert(tier_out == MM_TIER_GLOBAL);
	assert(ref_g == 1);

	char sd_path[512];
	snprintf(sd_path, sizeof(sd_path), "%s/standing_directives.json", tpl_g);
	struct stat st;
	assert(stat(sd_path, &st) == 0 && st.st_size > 10);

	/* Verify reinforcement synthesis */
	char *reinf_out = NULL;
	assert(mm_synthesize_reinforcement(ctx, "compaction", "polling wakeup", &reinf_out, NULL) == 0);
	assert(reinf_out != NULL && strstr(reinf_out, "Never use raw polling loops") != NULL);
	free(reinf_out);

	/* 3. Record Evidence Ref P:1 and Dependent Supported Claim Ref P:2, then test Truth Maintenance Cascade! */
	mm_record_req_t r_ev;
	memset(&r_ev, 0, sizeof(r_ev));
	r_ev.tier      = MM_TIER_PROJECT;
	r_ev.problem   = "Benchmark showed spinlock faster on 2 cores";
	r_ev.type      = "episode";
	r_ev.status    = MM_STATUS_SUPPORTED;
	r_ev.subsystem = "sync";
	uint32_t ref_ev = 0;
	assert(mm_record(ctx, &r_ev, NULL, &ref_ev) == 0);
	assert(ref_ev == 1);

	uint32_t ev_list[1] = { ref_ev };
	mm_record_req_t r_claim;
	memset(&r_claim, 0, sizeof(r_claim));
	r_claim.tier          = MM_TIER_PROJECT;
	r_claim.problem       = "Use spinlocks in sync subsystem for contention";
	r_claim.type          = "decision";
	r_claim.status        = MM_STATUS_SUPPORTED;
	r_claim.subsystem     = "sync";
	r_claim.evidence_refs = ev_list;
	r_claim.n_evidence    = 1;
	uint32_t ref_claim = 0;
	assert(mm_record(ctx, &r_claim, NULL, &ref_claim) == 0);
	assert(ref_claim == 2);

	/* Traverse Evidence DAG downwards from ref_claim and upwards from ref_ev */
	char *dag_down = NULL, *dag_up = NULL;
	assert(mm_traverse(ctx, MM_TIER_PROJECT, ref_claim, "down", 4, &dag_down, NULL) == 0);
	assert(dag_down != NULL && strstr(dag_down, "[Ref #1]") != NULL);
	free(dag_down);

	assert(mm_traverse(ctx, MM_TIER_PROJECT, ref_ev, "up", 4, &dag_up, NULL) == 0);
	assert(dag_up != NULL && strstr(dag_up, "[Ref #2]") != NULL);
	free(dag_up);

	/* Contradict Ref P:1 -> Truth Maintenance Cascade must automatically downgrade Ref P:2 to hypothesized! */
	mm_review_req_t rev;
	memset(&rev, 0, sizeof(rev));
	rev.tier   = MM_TIER_PROJECT;
	rev.ref    = ref_ev;
	rev.action = "contradict";
	rev.reason = "Spinlock caused starvation under 16-thread load";
	char *rev_out = NULL;
	assert(mm_review_ex(ctx, &rev, &rev_out, NULL) == 0);
	assert(rev_out != NULL && strstr(rev_out, "Truth Maintenance Cascade downgraded 1 dependent") != NULL);
	free(rev_out);

	mm_envelope_t claim_after;
	assert(mm_get_envelope(ctx, MM_TIER_PROJECT, ref_claim, &claim_after) == 0);
	assert(claim_after.lifecycle == MM_STATUS_HYPOTHESIZED);
	assert(claim_after.n_history >= 1);
	mm_envelope_free(&claim_after);

	/* Inspect action */
	mm_review_req_t insp;
	memset(&insp, 0, sizeof(insp));
	insp.tier      = MM_TIER_PROJECT;
	insp.ref       = ref_claim;
	insp.action    = "inspect";
	insp.direction = "down";
	char *insp_out = NULL;
	assert(mm_review_ex(ctx, &insp, &insp_out, NULL) == 0);
	assert(insp_out != NULL && strstr(insp_out, "Evidence Tree") != NULL && strstr(insp_out, "[Ref #1]") != NULL);
	free(insp_out);

	/* 4. Counterfactual Pre-Flight Guardrail Engine */
	/* Record an episode with a failed approach */
	mm_approach_t failed_app;
	memset(&failed_app, 0, sizeof(failed_app));
	snprintf(failed_app.approach, sizeof(failed_app.approach), "Using spinlock in sync worker queue");
	snprintf(failed_app.outcome, sizeof(failed_app.outcome), "failed");
	snprintf(failed_app.reason, sizeof(failed_app.reason), "CPU livelock under high contention");

	mm_record_req_t r_ep_fail;
	memset(&r_ep_fail, 0, sizeof(r_ep_fail));
	r_ep_fail.tier         = MM_TIER_PROJECT;
	r_ep_fail.problem      = "Investigated sync worker queue locking";
	r_ep_fail.type         = "episode";
	r_ep_fail.status       = MM_STATUS_SUPPORTED;
	r_ep_fail.approaches   = &failed_app;
	r_ep_fail.n_approaches = 1;
	uint32_t ref_fail = 0;
	assert(mm_record(ctx, &r_ep_fail, NULL, &ref_fail) == 0);

	char *guard_out = NULL;
	assert(mm_evaluate_guardrail(ctx, "Using spinlock in sync benchmark", NULL, NULL, &guard_out, NULL) == 0);
	assert(guard_out != NULL);
	assert(strstr(guard_out, "MEMORY GUARDRAIL WARNING") != NULL);
	assert(strstr(guard_out, "contradicted_memory") != NULL);
	assert(strstr(guard_out, "failed_approach") != NULL);
	free(guard_out);

	/* 5. Verification-Attested Promotion Ledger (FAIL -> file edit -> PASS) */
	uint32_t promo_ref = 0;
	assert(mm_observe_verification_command(ctx, "make test", 1, "FAIL: test_parser boundary error at line 42", &promo_ref) == 0);
	assert(mm_record_file_mutation(ctx, "src/parser.c") == 0);
	/* Observe test pass -> must auto-synthesize an attested L2 fix episode! */
	assert(mm_observe_verification_command(ctx, "make test", 0, "PASS: all tests green", &promo_ref) == 0);
	assert(promo_ref > 0);

	mm_envelope_t attested_env;
	assert(mm_get_envelope(ctx, MM_TIER_PROJECT, promo_ref, &attested_env) == 0);
	assert(attested_env.lifecycle == MM_STATUS_SUPPORTED);
	mm_envelope_free(&attested_env);

	/* Also verify that a new record citing tests_passed={"make test"} auto-promotes from OBSERVED to SUPPORTED */
	const char *tp_cmds[1] = { "make test" };
	mm_record_req_t r_tp;
	memset(&r_tp, 0, sizeof(r_tp));
	r_tp.tier           = MM_TIER_PROJECT;
	r_tp.problem        = "Parser boundary fix verified by make test";
	r_tp.type           = "episode";
	r_tp.status         = MM_STATUS_OBSERVED;
	r_tp.tests_passed   = tp_cmds;
	r_tp.n_tests_passed = 1;
	uint32_t ref_tp = 0;
	assert(mm_record(ctx, &r_tp, NULL, &ref_tp) == 0);

	mm_envelope_t tp_env;
	assert(mm_get_envelope(ctx, MM_TIER_PROJECT, ref_tp, &tp_env) == 0);
	assert(tp_env.lifecycle == MM_STATUS_SUPPORTED);
	mm_envelope_free(&tp_env);

	/* 6. Hierarchical Memory Overview (Persona Tenets, L3, L2, L1, Chronological Timeline) */
	char *ov_out = NULL;
	assert(mm_build_memory_overview(ctx, MM_TIER_BOTH, 10, &ov_out, NULL) == 0);
	assert(ov_out != NULL);
	assert(strstr(ov_out, "Persona Tenets") != NULL);
	assert(strstr(ov_out, "Chronological Epoch Timeline") != NULL);
	free(ov_out);

	/* 7. Session Branching (lineage: active vs all), Tool Call Indexing, and Mipmap Session Compaction */
	mm_session_event_t ev1 = { "sess_branch", 1, "user", "Root turn on main branch", "e1", "", NULL, 0, 0, false, NULL, NULL };
	mm_session_event_t ev2 = { "sess_branch", 2, "assistant", "Abandoned branch experiment alpha", "e2_dead", "e1", "bash", 1, 0, true, NULL, NULL };
	mm_session_event_t ev3 = { "sess_branch", 3, "assistant", "Active branch implementation alpha", "e3_live", "e1", "bash", 1, 0, true, "src/parser.c", NULL };
	assert(mm_session_ingest_event(ctx, MM_TIER_PROJECT, &ev1) == 0);
	assert(mm_session_ingest_event(ctx, MM_TIER_PROJECT, &ev2) == 0);
	assert(mm_session_ingest_event(ctx, MM_TIER_PROJECT, &ev3) == 0);

	/* Active lineage must include e3_live and exclude e2_dead */
	char *lin_active = NULL;
	assert(mm_session_recall_ex(ctx, MM_TIER_PROJECT, "sess_branch", "alpha", 0, 0, "active", 0, 0, 10, &lin_active, NULL) == 0);
	assert(lin_active != NULL);
	assert(strstr(lin_active, "Active branch implementation alpha") != NULL);
	assert(strstr(lin_active, "Abandoned branch experiment alpha") == NULL);
	free(lin_active);

	/* All lineage must include both e3_live and e2_dead */
	char *lin_all = NULL;
	assert(mm_session_recall_ex(ctx, MM_TIER_PROJECT, "sess_branch", "alpha", 0, 0, "all", 0, 0, 10, &lin_all, NULL) == 0);
	assert(lin_all != NULL);
	assert(strstr(lin_all, "Active branch implementation alpha") != NULL);
	assert(strstr(lin_all, "Abandoned branch experiment alpha") != NULL);
	free(lin_all);

	/* Zoom into Turn 3 with tool_index=1 */
	char *tc_zoom = NULL;
	assert(mm_session_recall_ex(ctx, MM_TIER_PROJECT, "sess_branch", NULL, 3, 1, "all", 0, 0, 10, &tc_zoom, NULL) == 0);
	assert(tc_zoom != NULL && strstr(tc_zoom, "bash") != NULL);
	free(tc_zoom);

	/* Mipmap Session Compaction */
	for (int t = 4; t <= 9; t++) {
		char msg[64];
		snprintf(msg, sizeof(msg), "Turn %d detailed discussion", t);
		mm_session_ingest_turn(ctx, MM_TIER_PROJECT, "sess_branch", t, "user", msg);
	}
	char *compact_sum = NULL;
	assert(mm_session_compact(ctx, MM_TIER_PROJECT, "sess_branch", "Focus on parser fixes", &compact_sum, NULL) == 0);
	assert(compact_sum != NULL && strlen(compact_sum) > 10);
	free(compact_sum);

	/* 8. Multi-Stage Consolidation: L0->L1 with run_id successor evolution, plus L1->L2->L3 scope/principle synthesis */
	for (int i = 0; i < 3; i++) {
		mm_record_req_t r_l1;
		memset(&r_l1, 0, sizeof(r_l1));
		char prob[128];
		snprintf(prob, sizeof(prob), "Subsystem audio milestone %d completed cleanly", i + 1);
		r_l1.tier      = MM_TIER_PROJECT;
		r_l1.level     = MM_LEVEL_L1;
		r_l1.problem   = prob;
		r_l1.type      = "episode";
		r_l1.status    = MM_STATUS_SUPPORTED;
		r_l1.subsystem = "audio";
		uint32_t r_out = 0;
		assert(mm_record(ctx, &r_l1, NULL, &r_out) == 0);
	}
	int l2_created = 0, l3_created = 0;
	assert(mm_consolidate_scopes_and_principles(ctx, MM_TIER_PROJECT, &l2_created, &l3_created) == 0);
	assert(l2_created >= 1);

	mm_close(ctx);

	char cleanup_cmd[1024];
	snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf \"%s\" \"%s\"", tpl_g, tpl_p);
	int sys_rc = system(cleanup_cmd);
	(void)sys_rc;

	printf("ok - test_tiers_cli_mcp passed\n");
	return 0;
}
