#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <ttypt/mm.h>
#include <ttypt/corm.h>

extern bool mm_is_clean_summary_text(const char *summary);
extern int mm_payload_decode(const void *data, size_t len, mm_envelope_t *out_env);

int main(void) {
	printf("=== test_token_decode: HTOK v4 BPE Token Reconstruction & Cleanliness ===\n");

	/* 1. Test clean summary string heuristics */
	assert(mm_is_clean_summary_text("A normal summary of an investigation"));
	assert(mm_is_clean_summary_text("Investigation: Fixed bug in corm_htok_unpack — verified ✓"));
	assert(!mm_is_clean_summary_text(""));
	assert(!mm_is_clean_summary_text(NULL));
	assert(!mm_is_clean_summary_text("[Corrupted payload: unexpected EOF]"));

	/* Mojibake with unprintable high bytes must be rejected */
	char bad_mojibake[] = { (char)0xC3, (char)0xA9, (char)0x80, (char)0x81, (char)0x82, (char)0x83, 0 };
	assert(!mm_is_clean_summary_text(bad_mojibake));
	printf("ok - Clean summary text validator distinguishes clean text from mojibake\n");

	/* 2. Test JSON payload decoding round-trip */
	const char *plain_json =
		"{\"level\":\"L1\",\"type\":\"episode\",\"lifecycle\":\"tested\","
		"\"timestamp\":\"2026-11-23T12:00:00Z\",\"summary\":\"Investigation: Token decoding test\","
		"\"context\":{\"project\":\"site\",\"subsystem\":\"libmm\"},"
		"\"episode\":{\"problem\":\"Verify token decoder\",\"outcome\":\"worked\"}}";

	mm_envelope_t env1;
	int rc = mm_payload_decode(plain_json, strlen(plain_json), &env1);
	assert(rc == 0);
	assert(env1.level == MM_LEVEL_L1);
	assert(env1.lifecycle == MM_STATUS_TESTED);
	assert(strcmp(env1.type, "episode") == 0);
	assert(strcmp(env1.summary, "Investigation: Token decoding test") == 0);
	assert(strcmp(env1.ctx_project, "site") == 0);
	assert(strcmp(env1.ctx_subsystem, "libmm") == 0);
	assert(strcmp(env1.ep_problem, "Verify token decoder") == 0);
	assert(strcmp(env1.ep_outcome, "worked") == 0);
	mm_envelope_free(&env1);
	printf("ok - Plain JSON payload decodes cleanly\n");

	/* 3. Test HTOK v4 binary container packing & decoding */
	uint8_t *htok_bytes = NULL;
	size_t htok_len = 0;
	rc = corm_htok_pack(plain_json, strlen(plain_json), &htok_bytes, &htok_len);
	assert(rc == 0);
	assert(htok_bytes != NULL && htok_len > 12);

	mm_envelope_t env2;
	rc = mm_payload_decode(htok_bytes, htok_len, &env2);
	assert(rc == 0);
	assert(env2.level == MM_LEVEL_L1);
	assert(env2.lifecycle == MM_STATUS_TESTED);
	assert(strcmp(env2.summary, "Investigation: Token decoding test") == 0);
	assert(strcmp(env2.ctx_project, "site") == 0);
	assert(strcmp(env2.ctx_subsystem, "libmm") == 0);
	mm_envelope_free(&env2);
	free(htok_bytes);
	printf("ok - HTOK v4 binary container unpacks and decodes with full fidelity\n");

	/* 4. Test synthetic HTOK v4 container with TYPE_TOKENS field (BPE encoded) */
	/* Magic: 'HTOK' (0x48, 0x54, 0x4f, 0x4b), Version: 4 (0x04, 0x00), Timestamp: 0 */
	uint8_t raw_htok[256];
	memset(raw_htok, 0, sizeof(raw_htok));
	raw_htok[0] = 0x48; raw_htok[1] = 0x54; raw_htok[2] = 0x4f; raw_htok[3] = 0x4b;
	raw_htok[4] = 0x04; raw_htok[5] = 0x00;
	/* triad: level=L1(1), lifecycle=tested(2), type=episode(0) -> (1<<6)|(2<<3)|0 = 0x50 */
	raw_htok[10] = 0x50;
	raw_htok[11] = 0x00; // flags

	/* 1 field: field count = 1 */
	raw_htok[12] = 1;
	/* Field 0: varTag = (0 << 3) | 1 (TYPE_TOKENS) = 0x01 */
	raw_htok[13] = 0x01;

	/* Token sequence for ASCII string "Hello": 'H'(72), 'e'(101), 'l'(108), 'l'(108), 'o'(111) */
	/* Since all < 128, packed bytes are simply [72, 101, 108, 108, 111] */
	raw_htok[14] = 5; // dlen = 5
	raw_htok[15] = 72;
	raw_htok[16] = 101;
	raw_htok[17] = 108;
	raw_htok[18] = 108;
	raw_htok[19] = 111;
	size_t raw_len = 20;

	mm_envelope_t env3;
	rc = mm_payload_decode(raw_htok, raw_len, &env3);
	assert(rc == 0);
	assert(env3.summary != NULL);
	assert(strcmp(env3.summary, "Hello") == 0);
	mm_envelope_free(&env3);
	printf("ok - Synthetic HTOK v4 TYPE_TOKENS unpacked through lemma tokenizer into 'Hello'\n");

	printf("test_token_decode: all green\n");
	return 0;
}
