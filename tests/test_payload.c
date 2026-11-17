#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(void) {
	printf("=== test_payload: HTOK v4 binary + JSON round-trip (Full Schema) ===\n");

	mm_envelope_t env;
	mm_envelope_init(&env);
	env.level     = MM_LEVEL_L2;
	env.lifecycle = MM_STATUS_SUPPORTED;
	snprintf(env.type, sizeof(env.type), "episode");
	snprintf(env.timestamp, sizeof(env.timestamp), "2026-11-17T12:00:00Z");
	env.summary   = strdup("Always use two-tier federated storage in libmm");
	env.standing  = true;
	env.is_anchor = true;
	snprintf(env.ctx_project, sizeof(env.ctx_project), "site");
	snprintf(env.ctx_subsystem, sizeof(env.ctx_subsystem), "mm");
	env.evidence[0] = 10;
	env.evidence[1] = 25;
	env.n_evidence  = 2;
	env.tags[0]     = strdup("architecture");
	env.tags[1]     = strdup("corm");
	env.n_tags      = 2;

	/* Rich episode fields */
	env.ep_problem = strdup("Memory recall needed cross-tier federation");
	env.ep_request = strdup("Ensure Global and Project stores work together");
	env.ep_outcome = strdup("Implemented Tier 1 Global + Tier 2 Project stores");
	snprintf(env.ep_session_id, sizeof(env.ep_session_id), "sess-abc-123");
	env.ep_turn_id = 7;
	snprintf(env.ep_run_id, sizeof(env.ep_run_id), "run-999");
	snprintf(env.ep_leaf_id, sizeof(env.ep_leaf_id), "leaf-42");
	snprintf(env.ep_tool_call_id, sizeof(env.ep_tool_call_id), "tc-01");

	env.n_approaches = 2;
	snprintf(env.ep_approaches[0].approach, sizeof(env.ep_approaches[0].approach), "Single store with prefix tags");
	snprintf(env.ep_approaches[0].outcome, sizeof(env.ep_approaches[0].outcome), "failed");
	snprintf(env.ep_approaches[0].reason, sizeof(env.ep_approaches[0].reason), "Ref collision across projects");
	snprintf(env.ep_approaches[1].approach, sizeof(env.ep_approaches[1].approach), "Two-tier federated CORM instances");
	snprintf(env.ep_approaches[1].outcome, sizeof(env.ep_approaches[1].outcome), "worked");
	env.ep_approaches[1].reason[0] = '\0';

	env.n_findings = 2;
	env.ep_findings[0] = strdup("Global rules must survive project switching");
	env.ep_findings[1] = strdup("Project episodes stay isolated in .pi/mm");

	env.n_diffs = 2;
	env.ep_diffs[0] = strdup("src/mm_ctx.c");
	env.ep_diffs[1] = strdup("src/mm_fusion.c");

	/* Lifecycle history transitions */
	env.n_history = 1;
	env.history[0].from = MM_STATUS_TESTED;
	env.history[0].to   = MM_STATUS_SUPPORTED;
	snprintf(env.history[0].timestamp, sizeof(env.history[0].timestamp), "2026-11-17T12:05:00Z");
	snprintf(env.history[0].reason, sizeof(env.history[0].reason), "Verified by unit test suite");
	env.history[0].evidence_ref = 10;

	/* Utility feedback counters */
	env.used_count       = 5;
	env.irrelevant_count = 1;
	env.obsolete_count   = 0;

	uint8_t *bytes = NULL;
	size_t bytes_len = 0;
	char *json = NULL;
	size_t json_len = 0;

	int rc = mm_payload_encode(&env, &bytes, &bytes_len, &json, &json_len);
	assert(rc == 0);
	assert(bytes != NULL && bytes_len > 15);
	assert(json != NULL && json_len > 20);

	/* Verify HTOK LE magic "KOTH" at byte 0 */
	assert(memcmp(bytes, "KOTH", 4) == 0);

	/* Decode from framed HTOK binary */
	mm_envelope_t dec;
	rc = mm_payload_decode(bytes, bytes_len, &dec);
	assert(rc == 0);
	assert(dec.level == MM_LEVEL_L2);
	assert(dec.lifecycle == MM_STATUS_SUPPORTED);
	assert(strcmp(dec.type, "episode") == 0);
	assert(dec.standing == true);
	assert(dec.is_anchor == true);
	assert(dec.summary != NULL && strcmp(dec.summary, "Always use two-tier federated storage in libmm") == 0);
	assert(strcmp(dec.ctx_project, "site") == 0);
	assert(strcmp(dec.ctx_subsystem, "mm") == 0);
	assert(dec.n_evidence == 2 && dec.evidence[0] == 10 && dec.evidence[1] == 25);
	assert(dec.n_tags == 2 && strcmp(dec.tags[0], "architecture") == 0 && strcmp(dec.tags[1], "corm") == 0);
	assert(dec.ep_problem && strcmp(dec.ep_problem, "Memory recall needed cross-tier federation") == 0);
	assert(dec.ep_outcome && strcmp(dec.ep_outcome, "Implemented Tier 1 Global + Tier 2 Project stores") == 0);
	assert(strcmp(dec.ep_session_id, "sess-abc-123") == 0);
	assert(dec.ep_turn_id == 7);
	assert(strcmp(dec.ep_run_id, "run-999") == 0);
	assert(strcmp(dec.ep_leaf_id, "leaf-42") == 0);
	assert(strcmp(dec.ep_tool_call_id, "tc-01") == 0);
	assert(dec.n_approaches == 2);
	assert(strcmp(dec.ep_approaches[0].outcome, "failed") == 0);
	assert(strcmp(dec.ep_approaches[0].reason, "Ref collision across projects") == 0);
	assert(strcmp(dec.ep_approaches[1].outcome, "worked") == 0);
	assert(dec.n_findings == 2 && strcmp(dec.ep_findings[0], "Global rules must survive project switching") == 0);
	assert(dec.n_diffs == 2 && strcmp(dec.ep_diffs[0], "src/mm_ctx.c") == 0);
	assert(dec.n_history == 1);
	assert(dec.history[0].from == MM_STATUS_TESTED);
	assert(dec.history[0].to == MM_STATUS_SUPPORTED);
	assert(dec.history[0].evidence_ref == 10);
	assert(dec.used_count == 5 && dec.irrelevant_count == 1);

	/* Test deep clone */
	mm_envelope_t cloned;
	mm_envelope_clone(&cloned, &dec);
	assert(cloned.n_approaches == 2);
	assert(cloned.n_findings == 2 && strcmp(cloned.ep_findings[1], "Project episodes stay isolated in .pi/mm") == 0);
	assert(cloned.n_history == 1 && strcmp(cloned.history[0].reason, "Verified by unit test suite") == 0);
	mm_envelope_free(&cloned);
	mm_envelope_free(&dec);

	/* Decode from JSON string */
	mm_envelope_t dec_json;
	rc = mm_payload_decode(json, json_len, &dec_json);
	assert(rc == 0);
	assert(dec_json.level == MM_LEVEL_L2);
	assert(dec_json.standing == true);
	assert(strcmp(dec_json.summary, "Always use two-tier federated storage in libmm") == 0);
	assert(dec_json.n_approaches == 2);
	assert(dec_json.n_findings == 2);
	assert(dec_json.n_diffs == 2);
	assert(dec_json.n_history == 1);
	mm_envelope_free(&dec_json);

	/* Decode legacy "<DATE>:<plain text>" */
	const char *legacy = "2026-09-13:Beacon Harbor lights";
	mm_envelope_t dec_leg;
	rc = mm_payload_decode(legacy, strlen(legacy), &dec_leg);
	assert(rc == 0);
	assert(strncmp(dec_leg.timestamp, "2026-09-13", 10) == 0);
	assert(strcmp(dec_leg.summary, "Beacon Harbor lights") == 0);
	mm_envelope_free(&dec_leg);

	/* Verify temporal windows */
	char since[16], until[16];
	assert(mm_window_for_level(MM_LEVEL_L0, 1763380800, since, until) == false);
	assert(mm_window_for_level(MM_LEVEL_L1, 1763380800, since, until) == true);
	assert(strlen(since) == 10 && strlen(until) == 10);
	assert(mm_window_for_level(MM_LEVEL_L2, 1763380800, since, until) == true);
	assert(strcmp(since + 8, "01") == 0);
	assert(mm_window_for_level(MM_LEVEL_L3, 1763380800, since, until) == false);

	free(bytes);
	free(json);
	mm_envelope_free(&env);

	printf("ok - test_payload passed\n");
	return 0;
}
