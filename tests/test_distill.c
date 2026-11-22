#define _GNU_SOURCE
#include "ttypt/mm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
	printf("=== test_distill: Zero-JSON Line-Delimited Memory Distillation ===\n");

	/* 1. Prompt generation */
	char *prompt = NULL;
	size_t prompt_len = 0;
	assert(mm_distill_prompt("User: How do we fix the race?\nAssistant: Use a mutex.", &prompt, &prompt_len) == 0);
	assert(prompt && strstr(prompt, "LINE-DELIMITED") && strstr(prompt, "How do we fix the race?"));
	free(prompt);

	/* 2. Zero-JSON parse */
	const char *distilled_sample =
		"PROBLEM: Fix socket leak in ren\n"
		"TYPE: episode\n"
		"STANDING: false\n"
		"PROJECT: axil-auren\n"
		"SUBSYSTEM: net\n"
		"PATH: src/libren.c\n"
		"APPROACH: Close fd on exit | worked | Prevents FD exhaustion\n"
		"FINDING: Socket was left open after client disconnect\n"
		"CONSTRAINT: Never exit loop without closing client FD\n";

	char storage[4096];
	mm_record_req_t req;
	assert(mm_distill_parse(distilled_sample, &req, storage, sizeof(storage)) == 0);

	assert(strcmp(req.problem, "Fix socket leak in ren") == 0);
	assert(strcmp(req.type, "episode") == 0);
	assert(!req.standing);
	assert(strcmp(req.project, "axil-auren") == 0);
	assert(strcmp(req.subsystem, "net") == 0);
	assert(strcmp(req.path, "src/libren.c") == 0);

	assert(req.n_approaches == 1);
	assert(strcmp(req.approaches[0].approach, "Close fd on exit") == 0);
	assert(strcmp(req.approaches[0].outcome, "worked") == 0);
	assert(strcmp(req.approaches[0].reason, "Prevents FD exhaustion") == 0);

	assert(req.n_findings == 1);
	assert(strcmp(req.findings[0], "Socket was left open after client disconnect") == 0);

	assert(req.n_constraints == 1);
	assert(strcmp(req.constraints[0], "Never exit loop without closing client FD") == 0);

	printf("ok - test_distill passed\n");
	return 0;
}
