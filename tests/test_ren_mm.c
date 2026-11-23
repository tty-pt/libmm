#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <ttypt/libren.h>
#include <ttypt/mm.h>
#include <ttypt/xy.h>

/* Mock ren tool registration */
static ren_tool_func g_recall_fn = NULL;
static ren_tool_func g_record_fn = NULL;

void ren_register_tool(const char *name, const char *usage, ren_tool_func fn) {
	(void)usage;
	if (strcmp(name, "recall") == 0) g_recall_fn = fn;
	if (strcmp(name, "record") == 0) g_record_fn = fn;
}

void ren_register_prompt_hint(const char *hint) {
	(void)hint;
}

/* Include ren-mm implementation directly */
#include "../mods/ren-mm/ren-mm.c"

static void cleanup_dir(const char *dir) {
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	if (system(cmd) != 0) {}
}

int main(void) {
	printf("=== test_ren_mm: ren-mm Desktop Module Tool Integration ===\n");

	const char *tmp_root = "/tmp/test_ren_mm_root";
	cleanup_dir(tmp_root);

	char pdir[1024];
	snprintf(pdir, sizeof(pdir), "%s/project", tmp_root);
	char mmdir[1024];
	snprintf(mmdir, sizeof(mmdir), "%s/.mm", pdir);
	char sessdir[1024];
	snprintf(sessdir, sizeof(sessdir), "%s/sessions", mmdir);

	char mkdir_cmd[2048];
	snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", sessdir);
	if (system(mkdir_cmd) != 0) {}

	/* Initialize ren-mm */
	if (chdir(pdir) != 0) {}
	xy_install();

	assert(g_recall_fn != NULL);
	assert(g_record_fn != NULL);
	printf("ok - xy_install registered 'recall' and 'record' tools\n");

	/* 1. Test record tool: PROBLEM::FINDINGS */
	char *rec_out = NULL;
	int rc = g_record_fn("Ren Desktop Problem::Ren Desktop Finding", &rec_out);
	assert(rc == 0);
	assert(rec_out != NULL);
	assert(strstr(rec_out, "Recorded as ref") != NULL);
	free(rec_out);
	printf("ok - record tool (problem::findings) succeeded\n");

	/* 2. Test record tool: Line-delimited distillation */
	const char *distill_block =
		"PROBLEM: Ren Distill Block\n"
		"APPROACH: Tried direct test\n"
		"FINDING: Distill works cleanly\n";
	rc = g_record_fn(distill_block, &rec_out);
	assert(rc == 0);
	assert(rec_out != NULL);
	assert(strstr(rec_out, "Distilled & recorded") != NULL);
	free(rec_out);
	printf("ok - record tool (distillation block) succeeded\n");

	/* 3. Test recall tool: projects listing */
	char *qout = NULL;
	rc = g_recall_fn("projects", &qout);
	assert(rc == 0);
	assert(qout != NULL);
	free(qout);
	printf("ok - recall projects handled\n");

	/* 4. Test recall tool: guardrail check */
	rc = g_recall_fn("guardrail:Ren Desktop Problem", &qout);
	assert(rc == 0);
	assert(qout != NULL);
	free(qout);
	printf("ok - recall guardrail handled\n");

	/* 5. Test recall tool: persistent memory query */
	rc = g_recall_fn("Ren Desktop Problem", &qout);
	assert(rc == 0);
	assert(qout != NULL);
	assert(strstr(qout, "Ren Desktop Problem") != NULL);
	free(qout);
	printf("ok - recall query retrieved recorded record\n");

	cleanup_dir(tmp_root);
	printf("test_ren_mm: all green\n");
	return 0;
}
