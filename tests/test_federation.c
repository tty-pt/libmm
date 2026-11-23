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
	printf("=== test_federation: Cross-Project Federated Recall Verification ===\n");

	const char *tmp_root = "/tmp/test_mm_federation";
	cleanup_dir(tmp_root);

	char root_alpha[512], root_beta[512];
	snprintf(root_alpha, sizeof(root_alpha), "%s/alpha", tmp_root);
	snprintf(root_beta, sizeof(root_beta), "%s/beta", tmp_root);

	char mkdir_cmd[1024];
	snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/.mm/sessions %s/.mm/sessions", root_alpha, root_beta);
	if (system(mkdir_cmd) != 0) {}

	/* 1. Register projects in temporary registry */
	char reg_dir[512];
	snprintf(reg_dir, sizeof(reg_dir), "%s/registry", tmp_root);
	char reg_mkdir[1024];
	snprintf(reg_mkdir, sizeof(reg_mkdir), "mkdir -p %s", reg_dir);
	if (system(reg_mkdir) != 0) {}

	setenv("MM_PROJECTS_DIR", reg_dir, 1);

	int rc = mm_project_register("proj_alpha", root_alpha);
	assert(rc == 0);
	rc = mm_project_register("proj_beta", root_beta);
	assert(rc == 0);
	printf("ok - Registered proj_alpha and proj_beta\n");

	/* 2. Record memory into proj_alpha */
	mm_options_t opts_a;
	memset(&opts_a, 0, sizeof(opts_a));
	opts_a.cwd = root_alpha;
	opts_a.semantic_search = false;
	mm_ctx_t *ctx_a = mm_open(&opts_a);
	assert(ctx_a != NULL);

	mm_record_req_t req_a;
	memset(&req_a, 0, sizeof(req_a));
	req_a.problem = "Alpha Unique Decision about Architecture";
	req_a.level = MM_LEVEL_L1;
	req_a.status = MM_STATUS_SUPPORTED;
	uint32_t ref_a = 0;
	rc = mm_record(ctx_a, &req_a, NULL, &ref_a);
	assert(rc == 0 && ref_a > 0);
	mm_close(ctx_a);
	printf("ok - Recorded memory in proj_alpha\n");

	/* 3. Record memory into proj_beta */
	mm_options_t opts_b;
	memset(&opts_b, 0, sizeof(opts_b));
	opts_b.cwd = root_beta;
	opts_b.semantic_search = false;
	mm_ctx_t *ctx_b = mm_open(&opts_b);
	assert(ctx_b != NULL);

	mm_record_req_t req_b;
	memset(&req_b, 0, sizeof(req_b));
	req_b.problem = "Beta Unique Finding about Network Sockets";
	req_b.level = MM_LEVEL_L1;
	req_b.status = MM_STATUS_SUPPORTED;
	uint32_t ref_b = 0;
	rc = mm_record(ctx_b, &req_b, NULL, &ref_b);
	assert(rc == 0 && ref_b > 0);
	mm_close(ctx_b);
	printf("ok - Recorded memory in proj_beta\n");

	/* 4. Federated recall query across all projects */
	char *out_buf = NULL;
	size_t out_len = 0;
	rc = mm_project_recall_all("Unique", 10, &out_buf, &out_len);
	assert(rc == 0);
	assert(out_buf != NULL);
	assert(strstr(out_buf, "proj_alpha") != NULL);
	assert(strstr(out_buf, "proj_beta") != NULL);
	assert(strstr(out_buf, "Alpha Unique Decision") != NULL);
	assert(strstr(out_buf, "Beta Unique Finding") != NULL);
	free(out_buf);
	printf("ok - mm_project_recall_all federated across proj_alpha and proj_beta\n");

	cleanup_dir(tmp_root);
	printf("test_federation: all green\n");
	return 0;
}
