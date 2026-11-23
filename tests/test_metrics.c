#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <ttypt/mm.h>

static void cleanup_dir(const char *dir) {
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	system(cmd);
}

int main(void) {
	printf("=== test_metrics: Operational Telemetry & Measurement Verification ===\n");

	const char *tmp_root = "/tmp/test_mm_metrics";
	cleanup_dir(tmp_root);

	char gdir[512], pdir[512];
	snprintf(gdir, sizeof(gdir), "%s/global", tmp_root);
	snprintf(pdir, sizeof(pdir), "%s/project", tmp_root);

	mm_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.cwd = pdir;
	opts.global_dir = gdir;
	opts.project_dir = pdir;
	opts.semantic_search = false;

	mm_ctx_t *ctx = mm_open(&opts);
	assert(ctx != NULL);

	/* 1. Initial State: all counts zero */
	mm_metrics_t m0;
	int rc = mm_get_metrics(ctx, &m0);
	assert(rc == 0);
	assert(m0.total_recalls == 0);
	assert(m0.total_records == 0);
	assert(m0.total_reviews == 0);
	assert(m0.total_guardrail_checks == 0);
	assert(m0.global_ref_count == 0);
	assert(m0.project_ref_count == 0);
	printf("ok - Initial state zeroed\n");

	/* 2. Record operation metrics */
	mm_record_req_t rreq;
	memset(&rreq, 0, sizeof(rreq));
	rreq.problem = "Test Metrics Record Operation";
	rreq.level = MM_LEVEL_L1;
	rreq.status = MM_STATUS_OBSERVED;
	const char *f[1] = { "Testing latency recording" };
	rreq.findings = f;
	rreq.n_findings = 1;

	mm_tier_t tier;
	uint32_t ref1 = 0;
	rc = mm_record(ctx, &rreq, &tier, &ref1);
	assert(rc == 0);
	assert(ref1 > 0);

	mm_metrics_t m1;
	rc = mm_get_metrics(ctx, &m1);
	assert(rc == 0);
	assert(m1.total_records == 1);
	assert(m1.record_latency_us > 0);
	assert(m1.last_record_us > 0);
	assert(m1.project_ref_count == 1);
	printf("ok - Record metrics updated (records=1, latency=%lu us, ref_count=%zu)\n",
	       (unsigned long)m1.record_latency_us, m1.project_ref_count);

	/* 3. Recall operation metrics */
	mm_recall_req_t qreq;
	memset(&qreq, 0, sizeof(qreq));
	qreq.scope = MM_SCOPE_PERSISTENT;
	qreq.query = "Test Metrics Record";
	qreq.limit = 5;

	char *qout = NULL;
	rc = mm_recall(ctx, &qreq, &qout, NULL);
	assert(rc == 0);
	assert(qout != NULL);
	free(qout);

	mm_metrics_t m2;
	rc = mm_get_metrics(ctx, &m2);
	assert(rc == 0);
	assert(m2.total_recalls == 1);
	assert(m2.recall_latency_us > 0);
	assert(m2.last_recall_us > 0);
	assert(m2.candidates_fetched >= 1);
	assert(m2.candidates_filtered >= 1);
	printf("ok - Recall metrics updated (recalls=1, latency=%lu us, fetched=%lu, filtered=%lu)\n",
	       (unsigned long)m2.recall_latency_us, (unsigned long)m2.candidates_fetched,
	       (unsigned long)m2.candidates_filtered);

	/* 4. Guardrail metrics */
	char *gout = NULL;
	rc = mm_evaluate_guardrail(ctx, "Test Metrics Record", NULL, NULL, &gout, NULL);
	assert(rc == 0);
	free(gout);

	mm_metrics_t m3;
	rc = mm_get_metrics(ctx, &m3);
	assert(rc == 0);
	assert(m3.total_guardrail_checks == 1);
	printf("ok - Guardrail metrics updated (checks=1, warnings=%lu)\n",
	       (unsigned long)m3.guardrail_warnings);

	/* 5. Review metrics */
	mm_review_req_t rvreq;
	memset(&rvreq, 0, sizeof(rvreq));
	rvreq.ref = ref1;
	rvreq.action = "inspect";

	char *rvout = NULL;
	rc = mm_review_ex(ctx, &rvreq, &rvout, NULL);
	assert(rc == 0);
	free(rvout);

	mm_metrics_t m4;
	rc = mm_get_metrics(ctx, &m4);
	assert(rc == 0);
	assert(m4.total_reviews == 1);
	printf("ok - Review metrics updated (reviews=1)\n");

	/* 6. Formatting metrics (Human text & JSON) */
	char *txt_buf = NULL;
	rc = mm_format_metrics(&m4, false, &txt_buf, NULL);
	assert(rc == 0 && txt_buf != NULL);
	assert(strstr(txt_buf, "=== libmm Telemetry & Operational Metrics ===") != NULL);
	assert(strstr(txt_buf, "Recalls:           1") != NULL);
	assert(strstr(txt_buf, "Records:           1") != NULL);
	free(txt_buf);
	printf("ok - Human-readable metrics formatted\n");

	char *json_buf = NULL;
	rc = mm_format_metrics(&m4, true, &json_buf, NULL);
	assert(rc == 0 && json_buf != NULL);
	assert(json_buf[0] == '{');
	assert(strstr(json_buf, "\"total_recalls\":1") != NULL);
	assert(strstr(json_buf, "\"total_records\":1") != NULL);
	assert(strstr(json_buf, "\"candidates_fetched\":") != NULL);
	free(json_buf);
	printf("ok - JSON metrics formatted\n");

	/* 7. Reset metrics */
	mm_reset_metrics(ctx);
	mm_metrics_t m5;
	rc = mm_get_metrics(ctx, &m5);
	assert(rc == 0);
	assert(m5.total_recalls == 0);
	assert(m5.total_records == 0);
	assert(m5.candidates_fetched == 0);
	/* Footprint counts remain accurate after reset */
	assert(m5.project_ref_count == 1);
	printf("ok - Reset metrics preserves store footprint while clearing counters\n");

	mm_close(ctx);
	cleanup_dir(tmp_root);
	printf("test_metrics: all green\n");
	return 0;
}
