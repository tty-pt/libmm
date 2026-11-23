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
	printf("=== test_ipc_invalidation: Multi-Process Stigmergy & IPC Invalidation ===\n");

	const char *tmp_root = "/tmp/test_mm_ipc";
	cleanup_dir(tmp_root);

	char gdir[512], pdir[512];
	snprintf(gdir, sizeof(gdir), "%s/global", tmp_root);
	snprintf(pdir, sizeof(pdir), "%s/project", tmp_root);

	char mkdir_cmd[1024];
	snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/.mm/sessions %s", pdir, gdir);
	if (system(mkdir_cmd) != 0) {}

	/* 1. Context A (Writer, simulating Pi Agent CLI) */
	mm_options_t opts_a;
	memset(&opts_a, 0, sizeof(opts_a));
	opts_a.cwd = pdir;
	opts_a.global_dir = gdir;
	opts_a.project_dir = pdir;
	opts_a.semantic_search = false;
	mm_ctx_t *ctx_a = mm_open(&opts_a);
	assert(ctx_a != NULL);

	/* 2. Context B (Reader/Daemon, simulating resident jarvisd / ren) */
	mm_options_t opts_b;
	memset(&opts_b, 0, sizeof(opts_b));
	opts_b.cwd = pdir;
	opts_b.global_dir = gdir;
	opts_b.project_dir = pdir;
	opts_b.semantic_search = false;
	mm_ctx_t *ctx_b = mm_open(&opts_b);
	assert(ctx_b != NULL);

	/* Context B initial query (populates inmem cache) */
	mm_recall_req_t req_init;
	memset(&req_init, 0, sizeof(req_init));
	req_init.scope = MM_SCOPE_PERSISTENT;
	req_init.query = "Immediate";
	char *init_out = NULL;
	rc_assert:
	int rc = mm_recall(ctx_b, &req_init, &init_out, NULL);
	assert(rc == 0);
	free(init_out);
	printf("ok - Context B initialized and populated in-memory tier\n");

	/* 3. Context A writes a new memory */
	mm_record_req_t rreq;
	memset(&rreq, 0, sizeof(rreq));
	rreq.problem = "Immediate Invalidation Test Record";
	rreq.level = MM_LEVEL_L1;
	rreq.status = MM_STATUS_SUPPORTED;

	mm_tier_t tier;
	uint32_t ref = 0;
	rc = mm_record(ctx_a, &rreq, &tier, &ref);
	assert(rc == 0 && ref > 0);
	printf("ok - Context A recorded ref %u and broadcast IPC invalidation\n", ref);

	/* 4. Context B reads immediately without explicit file reload */
	/* Sleep 5ms to allow OS socket deliver datagram */
	usleep(5000);

	mm_envelope_t b_env;
	rc = mm_get_envelope(ctx_b, MM_TIER_PROJECT, ref, &b_env);
	assert(rc == 0);
	assert(b_env.summary != NULL);
	assert(strstr(b_env.summary, "Immediate Invalidation Test Record") != NULL);
	mm_envelope_free(&b_env);
	printf("ok - Context B received IPC invalidation and fetched fresh record immediately\n");

	mm_close(ctx_a);
	mm_close(ctx_b);
	cleanup_dir(tmp_root);
	printf("test_ipc_invalidation: all green\n");
	return 0;
}
