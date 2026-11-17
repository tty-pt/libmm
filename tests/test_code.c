#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

int main(void) {
	printf("=== test_code: C/TS Symbol Scanner, Call Graph & Manifest ===\n");

	mm_code_index_t idx;
	mm_code_index_init(&idx, "/tmp");

	const char *c_src =
		"#include <stdio.h>\n"
		"#define MAX_ITEMS 64\n"
		"typedef struct {\n"
		"    int id;\n"
		"} item_t;\n"
		"static int helper_compute(int x) {\n"
		"    return x * 2;\n"
		"}\n"
		"int main_handler(int a) {\n"
		"    return helper_compute(a);\n"
		"}\n";

	const char *ts_src =
		"import { foo } from './bar.ts';\n"
		"export interface ConfigOpts {\n"
		"    port: number;\n"
		"}\n"
		"export function runApp(cfg: ConfigOpts) {\n"
		"    return main_handler(cfg.port);\n"
		"}\n";

	assert(mm_code_scan_source("src/engine.c", c_src, strlen(c_src), 1, &idx) == 0);
	assert(mm_code_scan_source("src/app.ts", ts_src, strlen(ts_src), 2, &idx) == 0);
	mm_code_resolve_call_graph(&idx);

	assert(idx.n_files == 2);
	assert(idx.n_symbols >= 5);

	/* Verify call graph: runApp -> main_handler -> helper_compute */
	char *trace_out = NULL;
	assert(mm_code_trace(&idx, "main_handler", "both", 2, &trace_out, NULL) == 0);
	assert(trace_out != NULL);
	assert(strstr(trace_out, "runApp") != NULL);
	assert(strstr(trace_out, "helper_compute") != NULL);
	free(trace_out);

	/* Verify overview */
	char *ov_out = NULL;
	assert(mm_code_overview(&idx, &ov_out, NULL) == 0);
	assert(ov_out != NULL);
	assert(strstr(ov_out, "main_handler") != NULL);
	free(ov_out);

	/* Verify fault diagnosis */
	char *diag_out = NULL;
	assert(mm_code_diagnose(&idx, "SEGV in helper_compute at src/engine.c:7", 2, &diag_out, NULL) == 0);
	assert(diag_out != NULL);
	assert(strstr(diag_out, "helper_compute") != NULL);
	assert(strstr(diag_out, "main_handler") != NULL);
	free(diag_out);

	/* Verify manifest round-trip */
	char mpath[] = "/tmp/mm_test_manifest_XXXXXX";
	int fd = mkstemp(mpath);
	assert(fd >= 0);
	close(fd);
	assert(mm_code_save_manifest(&idx, mpath) == 0);

	mm_code_index_t idx2;
	mm_code_index_init(&idx2, "/tmp");
	assert(mm_code_load_manifest(&idx2, mpath) == 0);
	assert(idx2.n_symbols == idx.n_symbols);
	unlink(mpath);

	mm_code_index_free(&idx2);
	mm_code_index_free(&idx);

	printf("ok - test_code passed\n");
	return 0;
}
