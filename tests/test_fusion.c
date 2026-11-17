#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

int main(void) {
	printf("=== test_fusion: RRF + Epistemic Re-Ranking & Noise Filters ===\n");

	assert(mm_adaptive_rrf_k(0) == 10);
	assert(mm_adaptive_rrf_k(1) == 10);
	assert(mm_adaptive_rrf_k(8) == 16);
	assert(mm_adaptive_rrf_k(50) == 60);

	assert(mm_is_technical_inquiry("nd_seven_gl.c") == true);
	assert(mm_is_technical_inquiry("buildCodeArchitectureOverview") == true);
	assert(mm_is_technical_inquiry("Why is the sky blue?") == false);

	/* Verify noise & artifact filters */
	mm_envelope_t q_env, c_env, a_env, e_env;
	mm_envelope_init(&q_env);
	q_env.summary = strdup("What is the port number for axil-auren?");
	assert(mm_is_question_memory(&q_env) == true);
	mm_envelope_free(&q_env);

	mm_envelope_init(&a_env);
	a_env.summary = strdup("#include <stdio.h>\nint main(void) { return 0; }");
	assert(mm_is_code_or_doc_artifact(NULL, &a_env) == true);
	mm_envelope_free(&a_env);

	mm_envelope_init(&c_env);
	c_env.summary = strdup("Port 8081 is used by axil-auren");
	assert(mm_is_question_memory(&c_env) == false);
	assert(mm_is_code_or_doc_artifact("Clean architectural rule for memory recall", &c_env) == false);
	mm_envelope_free(&c_env);

	mm_envelope_init(&e_env);
	e_env.summary = strdup("You mentioned: port 8081 is used by axil-auren");
	assert(mm_is_assistant_echo(&e_env) == true);
	mm_envelope_free(&e_env);

	assert(mm_is_clean_summary_text("Always validate lifecycle transitions before persisting") == true);
	assert(mm_is_clean_summary_text("\x01\x02\x03\x04\x05\x06\x07\x08") == false);

	mm_result_line_t lex[5] = {
		{ MM_TIER_PROJECT, 1, 0.85, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"decision\",\"project\":\"site\",\"subsystem\":\"mm\",\"usedCount\":4,\"summary\":\"Project decision alpha\"}" },
		{ MM_TIER_PROJECT, 2, 0.70, (char *)"{\"level\":\"L1\",\"lifecycle\":\"contradicted\",\"type\":\"episode\",\"project\":\"site\",\"subsystem\":\"mm\",\"summary\":\"Refuted hypothesis beta\"}" },
		{ MM_TIER_GLOBAL,  1, 0.60, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"rule\",\"standing\":true,\"isAnchor\":true,\"summary\":\"Global standing rule gamma\"}" },
		{ MM_TIER_PROJECT, 4, 0.55, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"decision\",\"project\":\"site\",\"subsystem\":\"mm\",\"summary\":\"Project decision alpha\"}" },
		{ MM_TIER_PROJECT, 5, 0.50, (char *)"{\"level\":\"L0\",\"lifecycle\":\"observed\",\"type\":\"note\",\"summary\":\"Raw L0 turn log\"}" },
	};
	mm_result_line_t sem[2] = {
		{ MM_TIER_PROJECT, 1, 0.92, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"decision\",\"project\":\"site\",\"subsystem\":\"mm\",\"usedCount\":4,\"summary\":\"Project decision alpha\"}" },
		{ MM_TIER_PROJECT, 3, 0.88, (char *)"{\"level\":\"L2\",\"lifecycle\":\"superseded\",\"type\":\"note\",\"summary\":\"Obsolete note delta\"}" },
	};

	mm_candidate_t *fused = NULL;
	size_t n_fused = 0;
	int rc = mm_fuse_rankings_rrf(lex, 5, sem, 2, 10, &fused, &n_fused);
	assert(rc == 0);
	assert(n_fused == 6);
	assert(fused[0].tier == MM_TIER_PROJECT && fused[0].ref == 1);
	assert(fused[0].origin == MM_ORIGIN_BOTH);
	assert(fabs(fused[0].rrf_score - (1.0 / 11.0 + 1.0 / 11.0)) < 1e-6);

	mm_filter_criteria_t crit;
	memset(&crit, 0, sizeof(crit));
	crit.mipmap     = MM_LEVEL_NONE;
	crit.limit      = 10;
	crit.query      = "decision rule";
	crit.project    = "site";
	crit.subsystem  = "mm";
	crit.dedupe     = true;
	crit.include_l0 = false;

	mm_candidate_t *ranked = NULL;
	size_t n_ranked = 0;
	rc = mm_filter_and_enrich(fused, n_fused, &crit, &ranked, &n_ranked);
	assert(rc == 0);
	/*
	 * Ref 3 is superseded -> filtered
	 * Ref 5 is L0 and include_l0=false -> filtered
	 * Ref 4 is duplicate summary of Ref 1 and dedupe=true -> filtered
	 * Remaining: Ref P:1, Ref G:1, Ref P:2 = 3 candidates
	 */
	assert(n_ranked == 3);

	/* Ref P:1 (both origin, supported, project+subsystem match, usedCount=4) and Ref G:1 (standing anchor) must outrank Ref P:2 (contradicted 0.1x) */
	assert(ranked[2].ref == 2 && ranked[2].tier == MM_TIER_PROJECT);
	assert(ranked[0].ranking_score > ranked[2].ranking_score * 5.0);

	mm_candidates_free(fused, n_fused);
	mm_candidates_free(ranked, n_ranked);

	printf("ok - test_fusion passed\n");
	return 0;
}
