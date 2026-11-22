#define _GNU_SOURCE
#include "ttypt/mm.h"
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void mm_mkdir_p(const char *path) {
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	size_t len = strlen(tmp);
	if (len == 0) return;
	if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

static void get_base_dir(char *out, size_t cap) {
	const char *env = getenv("MM_PROJECTS_DIR");
	if (env && *env) {
		snprintf(out, cap, "%s", env);
		return;
	}
	const char *home = getenv("HOME");
	if (!home || !*home) home = "/tmp";
	snprintf(out, cap, "%s/.local/share/mm", home);
}

static void get_registry_path(char *out, size_t cap) {
	char base[512];
	get_base_dir(base, sizeof(base));
	snprintf(out, cap, "%s/projects.tsv", base);
}

static void get_manifest_path(const char *name, char *out, size_t cap) {
	char base[512];
	get_base_dir(base, sizeof(base));
	char mdir[512];
	snprintf(mdir, sizeof(mdir), "%s/manifests", base);
	mm_mkdir_p(mdir);
	snprintf(out, cap, "%s/%s.manifest", mdir, name);
}

static bool path_exists(const char *p) {
	struct stat st;
	return stat(p, &st) == 0;
}

static bool is_project_dir(const char *dir) {
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
	char probe[600];
	snprintf(probe, sizeof(probe), "%s/.git", dir);
	if (path_exists(probe)) return true;
	snprintf(probe, sizeof(probe), "%s/Makefile", dir);
	if (path_exists(probe)) return true;
	return false;
}

int mm_project_list(mm_project_info_t **out_list, size_t *out_count) {
	*out_list = NULL;
	*out_count = 0;

	char reg[512];
	get_registry_path(reg, sizeof(reg));
	FILE *fp = fopen(reg, "r");
	if (!fp) return 0;

	size_t cap = 16, cnt = 0;
	mm_project_info_t *arr = calloc(cap, sizeof(mm_project_info_t));
	if (!arr) { fclose(fp); return -1; }

	char line[1280];
	while (fgets(line, sizeof(line), fp)) {
		size_t l = strlen(line);
		while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
		if (l == 0) continue;

		char *t1 = strchr(line, '\t');
		if (!t1) continue;
		*t1++ = '\0';
		char *t2 = strchr(t1, '\t');
		if (!t2) continue;
		*t2++ = '\0';
		char *t3 = strchr(t2, '\t');
		uint64_t ts = 0;
		if (t3) {
			*t3++ = '\0';
			ts = strtoull(t3, NULL, 10);
		}

		if (cnt == cap) {
			cap *= 2;
			mm_project_info_t *n = realloc(arr, cap * sizeof(mm_project_info_t));
			if (!n) { free(arr); fclose(fp); return -1; }
			arr = n;
		}
		snprintf(arr[cnt].name, sizeof(arr[cnt].name), "%s", line);
		snprintf(arr[cnt].root_path, sizeof(arr[cnt].root_path), "%s", t1);
		snprintf(arr[cnt].manifest_path, sizeof(arr[cnt].manifest_path), "%s", t2);
		arr[cnt].last_indexed = ts;
		cnt++;
	}
	fclose(fp);
	*out_list = arr;
	*out_count = cnt;
	return 0;
}

int mm_project_register(const char *name, const char *root_path) {
	if (!name || !*name || !root_path || !*root_path) return -1;

	char real[PATH_MAX];
	if (!realpath(root_path, real)) return -1;

	char base[512];
	get_base_dir(base, sizeof(base));
	mm_mkdir_p(base);

	mm_project_info_t *list = NULL;
	size_t count = 0;
	mm_project_list(&list, &count);

	char mpath[512];
	get_manifest_path(name, mpath, sizeof(mpath));

	bool found = false;
	for (size_t i = 0; i < count; i++) {
		if (strcmp(list[i].name, name) == 0) {
			snprintf(list[i].root_path, sizeof(list[i].root_path), "%s", real);
			snprintf(list[i].manifest_path, sizeof(list[i].manifest_path), "%s", mpath);
			found = true;
			break;
		}
	}

	char reg[512];
	get_registry_path(reg, sizeof(reg));
	FILE *fp = fopen(reg, "w");
	if (!fp) { free(list); return -1; }

	for (size_t i = 0; i < count; i++) {
		fprintf(fp, "%s\t%s\t%s\t%llu\n",
		        list[i].name, list[i].root_path, list[i].manifest_path,
		        (unsigned long long)list[i].last_indexed);
	}
	if (!found) {
		fprintf(fp, "%s\t%s\t%s\t0\n", name, real, mpath);
	}
	fclose(fp);
	free(list);
	return 0;
}

int mm_project_resolve(const char *name_or_path, mm_project_info_t *out_info) {
	if (!name_or_path || !*name_or_path || !out_info) return -1;
	memset(out_info, 0, sizeof(*out_info));

	/* 1. Check existing registry */
	mm_project_info_t *list = NULL;
	size_t count = 0;
	if (mm_project_list(&list, &count) == 0) {
		for (size_t i = 0; i < count; i++) {
			if (strcmp(list[i].name, name_or_path) == 0 ||
			    strcmp(list[i].root_path, name_or_path) == 0) {
				*out_info = list[i];
				free(list);
				return 0;
			}
		}
		free(list);
	}

	/* 2. Check if name_or_path is an explicit directory */
	if (is_project_dir(name_or_path)) {
		char real[PATH_MAX];
		if (realpath(name_or_path, real)) {
			char tmp[PATH_MAX];
			snprintf(tmp, sizeof(tmp), "%s", real);
			const char *b = basename(tmp);
			if (mm_project_register(b, real) == 0) {
				return mm_project_resolve(b, out_info);
			}
		}
	}

	/* 3. Lazy probe configured roots */
	char roots_buf[1024];
	const char *env_roots = getenv("MM_PROJECT_ROOTS");
	if (env_roots && *env_roots) {
		snprintf(roots_buf, sizeof(roots_buf), "%s", env_roots);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home) home = "/tmp";
		snprintf(roots_buf, sizeof(roots_buf), "%s:%s/site/external", home, home);
	}

	char *saveptr = NULL;
	for (char *tok = strtok_r(roots_buf, ":", &saveptr); tok; tok = strtok_r(NULL, ":", &saveptr)) {
		char cand[600];
		snprintf(cand, sizeof(cand), "%s/%s", tok, name_or_path);
		if (is_project_dir(cand)) {
			if (mm_project_register(name_or_path, cand) == 0) {
				return mm_project_resolve(name_or_path, out_info);
			}
		}
	}

	return -1;
}

static bool is_source_ext(const char *name) {
	const char *dot = strrchr(name, '.');
	if (!dot) return false;
	return strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0 ||
	       strcmp(dot, ".cc") == 0 || strcmp(dot, ".cpp") == 0 ||
	       strcmp(dot, ".ts") == 0 || strcmp(dot, ".js") == 0 ||
	       strcmp(dot, ".py") == 0;
}

static bool has_newer_file(const char *dir, time_t ref_mtime) {
	DIR *d = opendir(dir);
	if (!d) return false;
	struct dirent *ent;
	bool newer = false;
	while (!newer && (ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		if (strcmp(ent->d_name, "node_modules") == 0 ||
		    strcmp(ent->d_name, "bin") == 0 ||
		    strcmp(ent->d_name, "lib") == 0 ||
		    strcmp(ent->d_name, "build") == 0) continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		struct stat st;
		if (stat(path, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (has_newer_file(path, ref_mtime)) newer = true;
		} else if (S_ISREG(st.st_mode) && is_source_ext(ent->d_name)) {
			if (st.st_mtime > ref_mtime) newer = true;
		}
	}
	closedir(d);
	return newer;
}

static bool is_manifest_fresh(const mm_project_info_t *proj) {
	if (!proj->manifest_path[0]) return false;
	struct stat st;
	if (stat(proj->manifest_path, &st) != 0) return false;
	return !has_newer_file(proj->root_path, st.st_mtime);
}

int mm_project_open_index(const mm_project_info_t *proj, mm_code_index_t *out_idx) {
	if (!proj || !out_idx) return -1;
	mm_code_index_init(out_idx, proj->root_path);
	if (is_manifest_fresh(proj)) {
		if (mm_code_load_manifest(out_idx, proj->manifest_path) == 0 && out_idx->n_symbols > 0) {
			return 0;
		}
	}
	if (mm_code_index_dir(out_idx, proj->root_path) != 0) {
		mm_code_index_free(out_idx);
		return -1;
	}
	mm_code_resolve_call_graph(out_idx);
	if (proj->manifest_path[0]) {
		mm_code_save_manifest(out_idx, proj->manifest_path);
	}
	return 0;
}

static void append_prefixed(char **acc, size_t *acc_len, size_t *acc_cap,
                            const char *proj_name, const char *chunk) {
	if (!chunk || !*chunk) return;
	size_t plen = strlen(proj_name) + strlen(chunk) + 16;
	if (*acc_len + plen >= *acc_cap) {
		size_t ncap = (*acc_cap == 0 ? 4096 : *acc_cap * 2) + plen;
		char *nbuf = realloc(*acc, ncap);
		if (!nbuf) return;
		*acc = nbuf;
		*acc_cap = ncap;
	}
	int n = snprintf(*acc + *acc_len, *acc_cap - *acc_len, "[%s]\n%s\n", proj_name, chunk);
	if (n > 0) *acc_len += (size_t)n;
}

int mm_project_code_search(const char *project_or_null, const char *query,
                           const char *file_filter, const char *kind_filter,
                           int limit, char **out_buf, size_t *out_len) {
	*out_buf = NULL;
	*out_len = 0;

	if (project_or_null && *project_or_null) {
		mm_project_info_t proj;
		if (mm_project_resolve(project_or_null, &proj) != 0) return -1;
		mm_code_index_t idx;
		if (mm_project_open_index(&proj, &idx) != 0) return -1;
		char *raw = NULL;
		size_t rlen = 0;
		int rc = mm_code_search(&idx, query, file_filter, kind_filter, limit, &raw, &rlen);
		mm_code_index_free(&idx);
		if (rc == 0 && raw) {
			size_t cap = 0;
			append_prefixed(out_buf, out_len, &cap, proj.name, raw);
			free(raw);
		}
		return rc;
	}

	mm_project_info_t *list = NULL;
	size_t count = 0;
	if (mm_project_list(&list, &count) != 0 || count == 0) {
		free(list);
		return -1;
	}

	size_t cap = 0;
	for (size_t i = 0; i < count; i++) {
		mm_code_index_t idx;
		if (mm_project_open_index(&list[i], &idx) != 0) continue;
		char *raw = NULL;
		size_t rlen = 0;
		if (mm_code_search(&idx, query, file_filter, kind_filter, limit, &raw, &rlen) == 0 && raw) {
			append_prefixed(out_buf, out_len, &cap, list[i].name, raw);
			free(raw);
		}
		mm_code_index_free(&idx);
	}
	free(list);
	return (*out_buf && *out_len > 0) ? 0 : -1;
}

int mm_project_code_symbol(const char *project_or_null, const char *symbol_name,
                           const char *file_filter,
                           char **out_buf, size_t *out_len) {
	*out_buf = NULL;
	*out_len = 0;

	if (project_or_null && *project_or_null) {
		mm_project_info_t proj;
		if (mm_project_resolve(project_or_null, &proj) != 0) return -1;
		mm_code_index_t idx;
		if (mm_project_open_index(&proj, &idx) != 0) return -1;
		char *raw = NULL;
		size_t rlen = 0;
		int rc = mm_code_read_symbol(&idx, symbol_name, file_filter, "both", 0, 0, &raw, &rlen);
		mm_code_index_free(&idx);
		if (rc == 0 && raw) {
			size_t cap = 0;
			append_prefixed(out_buf, out_len, &cap, proj.name, raw);
			free(raw);
		}
		return rc;
	}

	mm_project_info_t *list = NULL;
	size_t count = 0;
	if (mm_project_list(&list, &count) != 0 || count == 0) {
		free(list);
		return -1;
	}

	size_t cap = 0;
	for (size_t i = 0; i < count; i++) {
		mm_code_index_t idx;
		if (mm_project_open_index(&list[i], &idx) != 0) continue;
		char *raw = NULL;
		size_t rlen = 0;
		if (mm_code_read_symbol(&idx, symbol_name, file_filter, "both", 0, 0, &raw, &rlen) == 0 && raw) {
			append_prefixed(out_buf, out_len, &cap, list[i].name, raw);
			free(raw);
		}
		mm_code_index_free(&idx);
	}
	free(list);
	return (*out_buf && *out_len > 0) ? 0 : -1;
}

int mm_project_code_trace(const char *project_or_null, const char *symbol_name,
                          const char *direction, int depth,
                          char **out_buf, size_t *out_len) {
	*out_buf = NULL;
	*out_len = 0;

	if (project_or_null && *project_or_null) {
		mm_project_info_t proj;
		if (mm_project_resolve(project_or_null, &proj) != 0) return -1;
		mm_code_index_t idx;
		if (mm_project_open_index(&proj, &idx) != 0) return -1;
		char *raw = NULL;
		size_t rlen = 0;
		int rc = mm_code_trace(&idx, symbol_name, direction, depth, &raw, &rlen);
		mm_code_index_free(&idx);
		if (rc == 0 && raw) {
			size_t cap = 0;
			append_prefixed(out_buf, out_len, &cap, proj.name, raw);
			free(raw);
		}
		return rc;
	}

	mm_project_info_t *list = NULL;
	size_t count = 0;
	if (mm_project_list(&list, &count) != 0 || count == 0) {
		free(list);
		return -1;
	}

	size_t cap = 0;
	for (size_t i = 0; i < count; i++) {
		mm_code_index_t idx;
		if (mm_project_open_index(&list[i], &idx) != 0) continue;
		char *raw = NULL;
		size_t rlen = 0;
		if (mm_code_trace(&idx, symbol_name, direction, depth, &raw, &rlen) == 0 && raw) {
			append_prefixed(out_buf, out_len, &cap, list[i].name, raw);
			free(raw);
		}
		mm_code_index_free(&idx);
	}
	free(list);
	return (*out_buf && *out_len > 0) ? 0 : -1;
}
