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
	if (system(cmd) != 0) {}
}

int main(void) {
	printf("=== test_telemetry: Structured JSONL Logging Verification ===\n");

	const char *tmp_root = "/tmp/test_mm_telemetry";
	cleanup_dir(tmp_root);

	char pdir[512];
	snprintf(pdir, sizeof(pdir), "%s/project", tmp_root);
	char mmdir[512];
	snprintf(mmdir, sizeof(mmdir), "%s/.mm", pdir);
	char sessdir[512];
	snprintf(sessdir, sizeof(sessdir), "%s/sessions", mmdir);

	char mkdir_cmd[1024];
	snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", sessdir);
	if (system(mkdir_cmd) != 0) {}

	/* 1. Enable MM_TELEMETRY */
	setenv("MM_TELEMETRY", "1", 1);

	mm_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.cwd = pdir;
	opts.project_dir = mmdir;
	opts.semantic_search = false;

	mm_ctx_t *ctx = mm_open(&opts);
	assert(ctx != NULL);

	/* 2. Execute Record */
	mm_record_req_t rreq;
	memset(&rreq, 0, sizeof(rreq));
	rreq.problem = "Telemetry logging problem";
	rreq.level = MM_LEVEL_L1;
	rreq.status = MM_STATUS_SUPPORTED;

	mm_tier_t tier;
	uint32_t ref = 0;
	int rc = mm_record(ctx, &rreq, &tier, &ref);
	assert(rc == 0 && ref > 0);
	printf("ok - Recorded memory with telemetry active\n");

	/* 3. Execute Recall */
	mm_recall_req_t qreq;
	memset(&qreq, 0, sizeof(qreq));
	qreq.scope = MM_SCOPE_PERSISTENT;
	qreq.query = "Telemetry";
	char *qout = NULL;
	rc = mm_recall(ctx, &qreq, &qout, NULL);
	assert(rc == 0 && qout != NULL);
	free(qout);
	printf("ok - Recalled memory with telemetry active\n");

	mm_close(ctx);

	/* 4. Verify metrics.jsonl contents */
	char log_file[512];
	snprintf(log_file, sizeof(log_file), "%s/metrics.jsonl", mmdir);

	FILE *fp = fopen(log_file, "r");
	assert(fp != NULL);

	char line[1024];
	int record_found = 0;
	int recall_found = 0;

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, "\"op\":\"record\"")) record_found++;
		if (strstr(line, "\"op\":\"recall\"")) recall_found++;
	}
	fclose(fp);

	assert(record_found == 1);
	assert(recall_found == 1);
	printf("ok - metrics.jsonl recorded both 'record' and 'recall' JSON events cleanly\n");

	unsetenv("MM_TELEMETRY");
	cleanup_dir(tmp_root);
	printf("test_telemetry: all green\n");
	return 0;
}
