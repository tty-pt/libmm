#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(void) {
	printf("=== test_payload: HTOK v4 binary + JSON round-trip ===\n");

	mm_envelope_t env;
	mm_envelope_init(&env);
	env.level     = MM_LEVEL_L2;
	env.lifecycle = MM_STATUS_SUPPORTED;
	snprintf(env.type, sizeof(env.type), "decision");
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
	assert(strcmp(dec.type, "decision") == 0);
	assert(dec.standing == true);
	assert(dec.is_anchor == true);
	assert(dec.summary != NULL && strcmp(dec.summary, "Always use two-tier federated storage in libmm") == 0);
	assert(strcmp(dec.ctx_project, "site") == 0);
	assert(strcmp(dec.ctx_subsystem, "mm") == 0);
	assert(dec.n_evidence == 2 && dec.evidence[0] == 10 && dec.evidence[1] == 25);
	assert(dec.n_tags == 2 && strcmp(dec.tags[0], "architecture") == 0 && strcmp(dec.tags[1], "corm") == 0);

	mm_envelope_free(&dec);

	/* Decode from JSON string */
	mm_envelope_t dec_json;
	rc = mm_payload_decode(json, json_len, &dec_json);
	assert(rc == 0);
	assert(dec_json.level == MM_LEVEL_L2);
	assert(dec_json.standing == true);
	assert(strcmp(dec_json.summary, "Always use two-tier federated storage in libmm") == 0);
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
