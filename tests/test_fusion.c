#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

int main(void) {
	printf("=== test_fusion: RRF + Epistemic Re-Ranking ===\n");

	assert(mm_adaptive_rrf_k(0) == 10);
	assert(mm_adaptive_rrf_k(1) == 10);
	assert(mm_adaptive_rrf_k(8) == 16);
	assert(mm_adaptive_rrf_k(50) == 60);

	assert(mm_is_technical_inquiry("nd_seven_gl.c") == true);
	assert(mm_is_technical_inquiry("buildCodeArchitectureOverview") == true);
	assert(mm_is_technical_inquiry("Why is the sky blue?") == false);

	mm_result_line_t lex[3] = {
		{ MM_TIER_PROJECT, 1, 0.85, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"decision\",\"summary\":\"Project decision alpha\"}" },
		{ MM_TIER_PROJECT, 2, 0.70, (char *)"{\"level\":\"L1\",\"lifecycle\":\"contradicted\",\"type\":\"episode\",\"summary\":\"Refuted hypothesis beta\"}" },
		{ MM_TIER_GLOBAL,  1, 0.60, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"rule\",\"standing\":true,\"isAnchor\":true,\"summary\":\"Global standing rule gamma\"}" },
	};
	mm_result_line_t sem[2] = {
		{ MM_TIER_PROJECT, 1, 0.92, (char *)"{\"level\":\"L2\",\"lifecycle\":\"supported\",\"type\":\"decision\",\"summary\":\"Project decision alpha\"}" },
		{ MM_TIER_PROJECT, 3, 0.88, (char *)"{\"level\":\"L2\",\"lifecycle\":\"superseded\",\"type\":\"note\",\"summary\":\"Obsolete note delta\"}" },
	};

	mm_candidate_t *fused = NULL;
	size_t n_fused = 0;
	int rc = mm_fuse_rankings_rrf(lex, 3, sem, 2, 10, &fused, &n_fused);
	assert(rc == 0);
	/* Notice (MM_TIER_PROJECT, 1) and (MM_TIER_GLOBAL, 1) are kept separate! Total unique = 4 */
	assert(n_fused == 4);
	assert(fused[0].tier == MM_TIER_PROJECT && fused[0].ref == 1);
	assert(fused[0].origin == MM_ORIGIN_BOTH);
	assert(fabs(fused[0].rrf_score - (1.0 / 11.0 + 1.0 / 11.0)) < 1e-6);

	mm_filter_criteria_t crit;
	memset(&crit, 0, sizeof(crit));
	crit.mipmap = MM_LEVEL_NONE;
	crit.limit  = 10;
	crit.query  = "decision rule";

	mm_candidate_t *ranked = NULL;
	size_t n_ranked = 0;
	rc = mm_filter_and_enrich(fused, n_fused, &crit, &ranked, &n_ranked);
	assert(rc == 0);
	/* Ref 3 is superseded so it must be filtered out; 3 candidates remain */
	assert(n_ranked == 3);

	/* Ref P:1 (both origin, supported) and Ref G:1 (standing anchor 1.25x) must outrank Ref P:2 (contradicted 0.1x) */
	assert(ranked[2].ref == 2 && ranked[2].tier == MM_TIER_PROJECT);
	assert(ranked[0].ranking_score > ranked[2].ranking_score * 5.0);

	mm_candidates_free(fused, n_fused);
	mm_candidates_free(ranked, n_ranked);

	printf("ok - test_fusion passed\n");
	return 0;
}
