#define _GNU_SOURCE
#include "ttypt/mm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
	printf("=== test_project: Multi-Project Registry, Lazy Discovery & Code Query ===\n");

	char tmp_dir[] = "/tmp/mm_test_proj_XXXXXX";
	assert(mkdtemp(tmp_dir) != NULL);

	char reg_dir[512], roots_dir[512], proj_dir[512];
	snprintf(reg_dir, sizeof(reg_dir), "%s/reg", tmp_dir);
	snprintf(roots_dir, sizeof(roots_dir), "%s/roots", tmp_dir);
	snprintf(proj_dir, sizeof(proj_dir), "%s/roots/myproj", tmp_dir);

	mkdir(reg_dir, 0755);
	mkdir(roots_dir, 0755);
	mkdir(proj_dir, 0755);

	char mk_path[600], c_path[600];
	snprintf(mk_path, sizeof(mk_path), "%s/Makefile", proj_dir);
	snprintf(c_path, sizeof(c_path), "%s/foo.c", proj_dir);

	FILE *fp = fopen(mk_path, "w");
	assert(fp);
	fprintf(fp, "all:\n\techo ok\n");
	fclose(fp);

	fp = fopen(c_path, "w");
	assert(fp);
	fprintf(fp,
	        "int my_helper_fn(int x) {\n"
	        "    return x + 1;\n"
	        "}\n\n"
	        "int my_caller_fn(int y) {\n"
	        "    return my_helper_fn(y);\n"
	        "}\n");
	fclose(fp);

	setenv("MM_PROJECTS_DIR", reg_dir, 1);
	setenv("MM_PROJECT_ROOTS", roots_dir, 1);

	mm_project_info_t info;
	assert(mm_project_resolve("myproj", &info) == 0);
	assert(strcmp(info.name, "myproj") == 0);

	mm_project_info_t *list = NULL;
	size_t count = 0;
	assert(mm_project_list(&list, &count) == 0);
	assert(count == 1);
	assert(strcmp(list[0].name, "myproj") == 0);
	free(list);

	char *buf = NULL;
	size_t len = 0;
	assert(mm_project_code_search("myproj", "my_helper_fn", NULL, NULL, 5, &buf, &len) == 0);
	assert(buf && strstr(buf, "[myproj]") && strstr(buf, "my_helper_fn"));
	free(buf);

	/* Cross-project search (project == NULL) */
	buf = NULL; len = 0;
	assert(mm_project_code_search(NULL, "my_helper_fn", NULL, NULL, 5, &buf, &len) == 0);
	assert(buf && strstr(buf, "[myproj]") && strstr(buf, "my_helper_fn"));
	free(buf);

	/* Symbol read */
	buf = NULL; len = 0;
	assert(mm_project_code_symbol("myproj", "my_helper_fn", NULL, &buf, &len) == 0);
	assert(buf && strstr(buf, "my_helper_fn"));
	free(buf);

	/* Call graph trace */
	buf = NULL; len = 0;
	assert(mm_project_code_trace("myproj", "my_helper_fn", "inbound", 2, &buf, &len) == 0);
	assert(buf && strstr(buf, "my_caller_fn"));
	free(buf);

	printf("ok - test_project passed\n");
	return 0;
}
