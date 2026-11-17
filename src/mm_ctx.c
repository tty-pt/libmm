#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include <ttypt/mm.h>
#include <ttypt/corm.h>
#include <ttypt/stele.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#ifdef _WIN32
#ifndef strtok_r
#define strtok_r strtok_s
#endif
#endif

/* ============================================================================
 * 1. Internal Context Structure & Filesystem Helpers
 * ============================================================================ */

typedef struct {
	char   cmd[256];
	char   norm_cmd[256];
	char   err_snippet[256];
	char   mutated_files[8][256];
	size_t n_mutated;
	bool   active;
} mm_verif_fail_t;

struct mm_ctx {
	char              cwd[PATH_MAX];
	char              global_dir[PATH_MAX];
	char              project_dir[PATH_MAX];
	char              project_root[PATH_MAX];
	char              project_name[128];
	bool              has_project;
	char              corm_bin[PATH_MAX];
	char              axis_path[PATH_MAX * 2];
	char              auren_url[256];
	char              embed_model[64];
	bool              semantic_search;
	bool              prefer_daemon;
	uint32_t          req_counter;
	mm_code_index_t   code_index;
	bool              code_index_loaded;

	/* Verification-Attested Promotion Ledger */
	mm_verif_fail_t   verif_failures[8];
	size_t            n_verif_failures;
	char              passed_cmds[16][256];
	size_t            n_passed_cmds;
};

static int mkdir_p(const char *path) {
	if (!path || !*path) return -1;
	char tmp[PATH_MAX];
	snprintf(tmp, sizeof(tmp), "%s", path);
	size_t len = strlen(tmp);
	if (len == 0) return -1;
	if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (stl_mkdir(tmp) != 0 && errno != EEXIST) return -1;
			*p = '/';
		}
	}
	if (stl_mkdir(tmp) != 0 && errno != EEXIST) return -1;
	return 0;
}

static bool dir_exists(const char *path) {
	struct stat st;
	return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char *path) {
	struct stat st;
	return path && *path && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

static void now_iso_utc(char *out, size_t cap) {
	time_t now = time(NULL);
	struct tm tm_utc;
	stl_gmtime_r(&now, &tm_utc);
	strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

static void discover_repo_root(const char *start_cwd, char *root_out, size_t cap) {
	root_out[0] = '\0';
	char cur[PATH_MAX];
	if (!stl_realpath(start_cwd, cur)) {
		snprintf(cur, sizeof(cur), "%s", start_cwd);
	}
	while (cur[0] == '/' && cur[1] != '\0') {
		char check_mm[PATH_MAX];
		char check_git[PATH_MAX];
		snprintf(check_mm, sizeof(check_mm), "%s/.mm", cur);
		snprintf(check_git, sizeof(check_git), "%s/.git", cur);
		struct stat st;
		if (stat(check_mm, &st) == 0 || stat(check_git, &st) == 0) {
			snprintf(root_out, cap, "%s", cur);
			return;
		}
		char *slash = strrchr(cur, '/');
		if (!slash || slash == cur) break;
		*slash = '\0';
	}
}

static bool dir_has_any_axis_plugin(const char *dir) {
	if (!dir || !*dir) return false;
	static const char *const names[] = { "libjoint", "liblemma", "libsepal", "libislet", NULL };
	static const char *const exts[]  = { "so", "dylib", "dll", NULL };
	for (int i = 0; names[i]; i++) {
		for (int j = 0; exts[j]; j++) {
			char p[PATH_MAX];
			snprintf(p, sizeof(p), "%s/%s.%s", dir, names[i], exts[j]);
			if (file_exists(p)) return true;
		}
	}
	return false;
}

static void append_resolved_axis_dir(char *axis_path, size_t cap, const char *dir_cand) {
	if (!dir_cand || !*dir_cand) return;
	char rp[PATH_MAX];
	const char *use = stl_realpath(dir_cand, rp) ? rp : dir_cand;
	if (!dir_exists(use)) return;
	if (axis_path[0]) {
		strncat(axis_path, ":", cap - strlen(axis_path) - 1);
	}
	strncat(axis_path, use, cap - strlen(axis_path) - 1);
}

static void discover_corm_and_axes(mm_ctx_t *ctx, const mm_options_t *opts) {
	const char *env_corm = (opts && opts->corm_bin) ? opts->corm_bin : getenv("MM_CORM_BIN");
	if (env_corm && *env_corm && file_exists(env_corm)) {
		char rp[PATH_MAX];
		snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", stl_realpath(env_corm, rp) ? rp : env_corm);
	} else {
		const char *roots[2] = { ctx->project_root[0] ? ctx->project_root : ctx->cwd, ctx->cwd };
		bool found = false;
		for (int i = 0; i < 2 && !found; i++) {
			char cand[PATH_MAX];
			snprintf(cand, sizeof(cand), "%s/external/libcorm/bin/corm", roots[i]);
			if (file_exists(cand)) {
				char rp[PATH_MAX];
				snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", stl_realpath(cand, rp) ? rp : cand);
				found = true;
				break;
			}
			snprintf(cand, sizeof(cand), "%s/../libcorm/bin/corm", roots[i]);
			if (file_exists(cand)) {
				char rp[PATH_MAX];
				snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", stl_realpath(cand, rp) ? rp : cand);
				found = true;
				break;
			}
		}
		if (!found) {
			const char *home = getenv("HOME");
			char home_corm[PATH_MAX] = {0};
			if (home && *home) {
				snprintf(home_corm, sizeof(home_corm), "%s/.local/bin/corm", home);
			}
			const char *sys_bins[] = {
				home_corm[0] ? home_corm : NULL,
				"/usr/local/bin/corm",
				"/opt/homebrew/bin/corm",
				"/usr/bin/corm",
				NULL
			};
			for (int i = 0; i < 4; i++) {
				if (sys_bins[i] && file_exists(sys_bins[i])) {
					snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", sys_bins[i]);
					found = true;
					break;
				}
			}
		}
		if (!found) {
			snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "corm");
		}
	}

	ctx->axis_path[0] = '\0';
	const char *env_axis = (opts && opts->axis_path) ? opts->axis_path : getenv("CORM_AXIS_PATH");
	if (env_axis && *env_axis) {
		char copy[PATH_MAX * 2];
		snprintf(copy, sizeof(copy), "%s", env_axis);
		char *saveptr = NULL;
		char *tok = strtok_r(copy, ":", &saveptr);
		while (tok) {
			if (dir_has_any_axis_plugin(tok)) {
				append_resolved_axis_dir(ctx->axis_path, sizeof(ctx->axis_path), tok);
			}
			tok = strtok_r(NULL, ":", &saveptr);
		}
		if (ctx->axis_path[0]) return;
	}

	/* Auto-discover external/lib{joint,lemma,sepal,islet}/lib */
	static const char *const axes[] = { "libjoint", "liblemma", "libsepal", "libislet", NULL };
	const char *base = ctx->project_root[0] ? ctx->project_root : ctx->cwd;
	for (int i = 0; axes[i]; i++) {
		char cand[PATH_MAX];
		snprintf(cand, sizeof(cand), "%s/external/%s/lib", base, axes[i]);
		if (!dir_exists(cand)) {
			snprintf(cand, sizeof(cand), "%s/../%s/lib", base, axes[i]);
		}
		if (dir_has_any_axis_plugin(cand)) {
			append_resolved_axis_dir(ctx->axis_path, sizeof(ctx->axis_path), cand);
		}
	}
	if (ctx->axis_path[0]) return;

	/* Fallback to ~/.local/lib and system library directories where axis plugins are installed */
	const char *home = getenv("HOME");
	char home_lib[PATH_MAX] = {0};
	if (home && *home) {
		snprintf(home_lib, sizeof(home_lib), "%s/.local/lib", home);
	}
	const char *sys_libs[] = {
		home_lib[0] ? home_lib : NULL,
		"/opt/homebrew/lib",
		"/usr/local/lib",
		"/usr/lib",
		"/usr/lib64",
		"/mingw64/lib",
		NULL
	};
	for (int i = 0; i < 6; i++) {
		if (sys_libs[i] && dir_has_any_axis_plugin(sys_libs[i])) {
			append_resolved_axis_dir(ctx->axis_path, sizeof(ctx->axis_path), sys_libs[i]);
		}
	}
}

mm_ctx_t *mm_open(const mm_options_t *opts) {
	mm_ctx_t *ctx = (mm_ctx_t *)calloc(1, sizeof(mm_ctx_t));
	if (!ctx) return NULL;

	if (opts && opts->cwd && opts->cwd[0]) {
		if (!stl_realpath(opts->cwd, ctx->cwd)) {
			snprintf(ctx->cwd, sizeof(ctx->cwd), "%s", opts->cwd);
		}
	} else {
		if (!getcwd(ctx->cwd, sizeof(ctx->cwd))) {
			strcpy(ctx->cwd, ".");
		}
	}

	/* 1. Tier 1: Global Directory */
	const char *gdir = (opts && opts->global_dir) ? opts->global_dir : getenv("MM_GLOBAL_DIR");
	if (gdir && *gdir) {
		snprintf(ctx->global_dir, sizeof(ctx->global_dir), "%s", gdir);
	} else {
		const char *xdg = getenv("XDG_DATA_HOME");
		const char *home = getenv("HOME");
		if (xdg && *xdg) {
			snprintf(ctx->global_dir, sizeof(ctx->global_dir), "%s/mm/global", xdg);
		} else if (home && *home) {
			snprintf(ctx->global_dir, sizeof(ctx->global_dir), "%s/.local/share/mm/global", home);
		} else {
			snprintf(ctx->global_dir, sizeof(ctx->global_dir), "/tmp/mm-global");
		}
	}
	mkdir_p(ctx->global_dir);
	char gsess[PATH_MAX];
	snprintf(gsess, sizeof(gsess), "%s/sessions", ctx->global_dir);
	mkdir_p(gsess);

	/* 2. Tier 2: Project Directory */
	discover_repo_root(ctx->cwd, ctx->project_root, sizeof(ctx->project_root));
	const char *pdir = (opts && opts->project_dir) ? opts->project_dir : getenv("MM_PROJECT_DIR");
	if (pdir && *pdir) {
		snprintf(ctx->project_dir, sizeof(ctx->project_dir), "%s", pdir);
		ctx->has_project = true;
		if (!ctx->project_root[0]) {
			snprintf(ctx->project_root, sizeof(ctx->project_root), "%s", ctx->cwd);
		}
	} else if (ctx->project_root[0]) {
		snprintf(ctx->project_dir, sizeof(ctx->project_dir), "%s/.mm", ctx->project_root);
		ctx->has_project = true;
	}

	if (ctx->has_project) {
		mkdir_p(ctx->project_dir);
		char psess[PATH_MAX];
		snprintf(psess, sizeof(psess), "%s/sessions", ctx->project_dir);
		mkdir_p(psess);

		char tmp_root[PATH_MAX];
		snprintf(tmp_root, sizeof(tmp_root), "%s", ctx->project_root[0] ? ctx->project_root : ctx->cwd);
		const char *bname = basename(tmp_root);
		snprintf(ctx->project_name, sizeof(ctx->project_name), "%s", (bname && *bname) ? bname : "project");
	}

	discover_corm_and_axes(ctx, opts);

	const char *auren = (opts && opts->auren_url) ? opts->auren_url : getenv("CORM_SEPAL_EMBED_URL");
	snprintf(ctx->auren_url, sizeof(ctx->auren_url), "%s",
	         (auren && *auren) ? auren : "http://127.0.0.1:8081/v1/embeddings");

	const char *model = (opts && opts->embed_model) ? opts->embed_model : getenv("CORM_SEPAL_EMBED_MODEL");
	snprintf(ctx->embed_model, sizeof(ctx->embed_model), "%s",
	         (model && *model) ? model : "all-minilm");

	ctx->semantic_search = opts ? opts->semantic_search : false;
	ctx->prefer_daemon   = opts ? opts->prefer_daemon : true;

	mm_code_index_init(&ctx->code_index, ctx->project_root[0] ? ctx->project_root : ctx->cwd);
	return ctx;
}

void mm_close(mm_ctx_t *ctx) {
	if (!ctx) return;
	mm_code_index_free(&ctx->code_index);
	free(ctx);
}

const char *mm_get_tier_dir(const mm_ctx_t *ctx, mm_tier_t tier) {
	if (!ctx) return NULL;
	if (tier == MM_TIER_GLOBAL) return ctx->global_dir;
	if (tier == MM_TIER_PROJECT) return ctx->has_project ? ctx->project_dir : NULL;
	return ctx->has_project ? ctx->project_dir : ctx->global_dir;
}

const char *mm_get_project_root(const mm_ctx_t *ctx) {
	if (!ctx) return NULL;
	return ctx->project_root[0] ? ctx->project_root : ctx->cwd;
}

const char *mm_get_project_name(const mm_ctx_t *ctx) {
	if (!ctx) return NULL;
	return ctx->project_name[0] ? ctx->project_name : "global";
}

bool mm_has_project_tier(const mm_ctx_t *ctx) {
	return ctx && ctx->has_project;
}

/* ============================================================================
 * 2. Low-Level Corm Execution (cormd Unix Socket + CLI / flock Fallback)
 * ============================================================================ */

static bool is_daemon_lock_held(const char *tier_dir) {
	char lock_path[PATH_MAX];
	snprintf(lock_path, sizeof(lock_path), "%s/.cormd.lock", tier_dir);
	int fd = open(lock_path, O_RDWR);
	if (fd < 0) {
		if (errno == ENOENT) return false;
		return true; /* Fail closed on unexpected error */
	}
	if (stl_flock(fd, STL_LOCK_EX | STL_LOCK_NB) == 0) {
		stl_flock(fd, STL_LOCK_UN);
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

static int send_cormd_request(const char *sock_path, const char *header,
                              const void *body, size_t body_len,
                              char **out_stdout, size_t *out_len) {
#ifdef _WIN32
	(void)sock_path;
	(void)header;
	(void)body;
	(void)body_len;
	(void)out_stdout;
	(void)out_len;
	return -1;
#else
	if (!file_exists(sock_path)) return -1;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlen(sock_path) >= sizeof(addr.sun_path)) {
		close(fd);
		return -1;
	}
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}

	char hline[1024];
	int hlen = snprintf(hline, sizeof(hline), "%s\n", header);
	if (write(fd, hline, (size_t)hlen) != hlen) {
		close(fd);
		return -1;
	}
	if (body && body_len > 0) {
		const uint8_t *bp = (const uint8_t *)body;
		size_t rem = body_len;
		while (rem > 0) {
			ssize_t w = write(fd, bp, rem);
			if (w <= 0) {
				close(fd);
				return -1;
			}
			bp += w;
			rem -= (size_t)w;
		}
	}

	/* Read response header line: "<STATUS> <reqId> <bodyLen>\n" */
	char rhead[256];
	size_t rpos = 0;
	while (rpos + 1 < sizeof(rhead)) {
		char c;
		ssize_t r = read(fd, &c, 1);
		if (r <= 0) {
			close(fd);
			return -1;
		}
		if (c == '\n') break;
		rhead[rpos++] = c;
	}
	rhead[rpos] = '\0';

	char status[32] = {0};
	char req_id[64] = {0};
	unsigned long resp_len = 0;
	if (sscanf(rhead, "%31s %63s %lu", status, req_id, &resp_len) < 3) {
		close(fd);
		return -1;
	}

	char *rbuf = (char *)malloc(resp_len + 1);
	if (!rbuf) {
		close(fd);
		return -1;
	}
	size_t got = 0;
	while (got < resp_len) {
		ssize_t r = read(fd, rbuf + got, resp_len - got);
		if (r <= 0) break;
		got += (size_t)r;
	}
	rbuf[got] = '\0';
	close(fd);

	if (strcmp(status, "OK") != 0) {
		free(rbuf);
		return -1;
	}

	if (out_stdout) *out_stdout = rbuf;
	else free(rbuf);
	if (out_len) *out_len = got;
	return 0;
#endif
}

static int exec_corm_cli(mm_ctx_t *ctx, const char *tier_dir, bool is_mutation,
                         char *const argv[], const void *stdin_bytes, size_t stdin_len,
                         char **out_stdout, size_t *out_len) {
	if (is_mutation && is_daemon_lock_held(tier_dir)) {
		return -1; /* Fail closed: database is leased by resident cormd */
	}

#ifdef _WIN32
	(void)ctx;
	(void)tier_dir;
	(void)argv;
	(void)stdin_bytes;
	(void)stdin_len;
	(void)out_stdout;
	(void)out_len;
	return -1;
#else
	char lock_path[PATH_MAX];
	snprintf(lock_path, sizeof(lock_path), "%s/.mm.lock", tier_dir);
	int lock_fd = open(lock_path, O_CREAT | O_RDWR, 0644);
	if (lock_fd >= 0) {
		stl_flock(lock_fd, is_mutation ? STL_LOCK_EX : STL_LOCK_SH);
	}

	int in_pipe[2] = {-1, -1};
	int out_pipe[2] = {-1, -1};
	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
		if (in_pipe[0] >= 0) { close(in_pipe[0]); close(in_pipe[1]); }
		if (out_pipe[0] >= 0) { close(out_pipe[0]); close(out_pipe[1]); }
		if (lock_fd >= 0) { stl_flock(lock_fd, STL_LOCK_UN); close(lock_fd); }
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		if (lock_fd >= 0) { stl_flock(lock_fd, STL_LOCK_UN); close(lock_fd); }
		return -1;
	}

	if (pid == 0) {
		/* Child */
		if (lock_fd >= 0) close(lock_fd);
		dup2(in_pipe[0], STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);

		if (chdir(tier_dir) != 0) _exit(127);
		stl_setenv("CORM_QUIET", "1", 1);
		if (ctx->axis_path[0]) {
			stl_setenv("CORM_AXIS_PATH", ctx->axis_path, 1);
			const char *old_ld = getenv("LD_LIBRARY_PATH");
			char new_ld[PATH_MAX * 4];
			char corm_lib[PATH_MAX];
			char stele_lib[PATH_MAX];
			snprintf(corm_lib, sizeof(corm_lib), "%s/external/libcorm/lib",
			         ctx->project_root[0] ? ctx->project_root : ctx->cwd);
			snprintf(stele_lib, sizeof(stele_lib), "%s/external/libstele/lib",
			         ctx->project_root[0] ? ctx->project_root : ctx->cwd);
			if (!dir_exists(corm_lib) && ctx->corm_bin[0] == '/') {
				char bin_copy[PATH_MAX];
				snprintf(bin_copy, sizeof(bin_copy), "%s", ctx->corm_bin);
				char *bdir = dirname(bin_copy);
				char *cdir = dirname(bdir);
				snprintf(corm_lib, sizeof(corm_lib), "%s/lib", cdir);
				snprintf(stele_lib, sizeof(stele_lib), "%s/../libstele/lib", cdir);
			}
			if (old_ld && *old_ld) {
				snprintf(new_ld, sizeof(new_ld), "%s:%s:%s:%s", corm_lib, stele_lib, ctx->axis_path, old_ld);
			} else {
				snprintf(new_ld, sizeof(new_ld), "%s:%s:%s", corm_lib, stele_lib, ctx->axis_path);
			}
			stl_setenv("LD_LIBRARY_PATH", new_ld, 1);
			stl_setenv("DYLD_LIBRARY_PATH", new_ld, 1);
		}
		if (ctx->semantic_search && ctx->auren_url[0]) {
			stl_setenv("CORM_SEPAL_EMBED_URL", ctx->auren_url, 1);
			stl_setenv("CORM_SEPAL_EMBED_MODEL", ctx->embed_model, 1);
		}
		execvp(ctx->corm_bin, argv);
		_exit(127);
	}

	/* Parent */
	close(in_pipe[0]);
	close(out_pipe[1]);

	if (stdin_bytes && stdin_len > 0) {
		const uint8_t *p = (const uint8_t *)stdin_bytes;
		size_t rem = stdin_len;
		while (rem > 0) {
			ssize_t w = write(in_pipe[1], p, rem);
			if (w <= 0) break;
			p += w;
			rem -= (size_t)w;
		}
	}
	close(in_pipe[1]);

	size_t cap = 4096;
	size_t len = 0;
	char *buf = (char *)malloc(cap);
	if (buf) {
		for (;;) {
			if (len + 1024 + 1 > cap) {
				cap *= 2;
				char *nb = (char *)realloc(buf, cap);
				if (!nb) break;
				buf = nb;
			}
			ssize_t r = read(out_pipe[0], buf + len, 1024);
			if (r <= 0) break;
			len += (size_t)r;
		}
		buf[len] = '\0';
	}
	close(out_pipe[0]);

	int status = 0;
	waitpid(pid, &status, 0);
	if (lock_fd >= 0) {
		stl_flock(lock_fd, STL_LOCK_UN);
		close(lock_fd);
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		free(buf);
		return -1;
	}

	if (out_stdout) *out_stdout = buf;
	else free(buf);
	if (out_len) *out_len = len;
	return 0;
#endif
}

static void build_filespec(const mm_ctx_t *ctx, char *out, size_t cap) {
	if (ctx->semantic_search) {
		snprintf(out, cap, "mem.db@joint,lemma,sepal:a:b");
	} else {
		snprintf(out, cap, "mem.db@joint,lemma:a:b");
	}
}

/* ============================================================================
 * 3. Persistent Store Operations (List, Get, Put, Forget, Record, Review)
 * ============================================================================ */

int mm_list_refs(mm_ctx_t *ctx, mm_tier_t tier, uint32_t **out_refs, size_t *out_count) {
	if (!ctx || !out_refs || !out_count) return -1;
	*out_refs = NULL;
	*out_count = 0;

	const char *tdir = mm_get_tier_dir(ctx, tier);
	if (!tdir) return -1;

	char db_file[PATH_MAX];
	snprintf(db_file, sizeof(db_file), "%s/mem.db", tdir);
	if (!file_exists(db_file)) return 0;

	char fspec[128];
	build_filespec(ctx, fspec, sizeof(fspec));

	char *stdout_buf = NULL;
	size_t stdout_len = 0;
	int rc = -1;

	if (ctx->prefer_daemon) {
		char sock[PATH_MAX];
		snprintf(sock, sizeof(sock), "%s/.cormd.sock", tdir);
		char hdr[256];
		snprintf(hdr, sizeof(hdr), "LIST %u %s 0", ++ctx->req_counter, fspec);
		rc = send_cormd_request(sock, hdr, NULL, 0, &stdout_buf, &stdout_len);
	}
	if (rc != 0) {
		char *argv[] = { ctx->corm_bin, (char *)"-g", (char *)".", fspec, NULL };
		rc = exec_corm_cli(ctx, tdir, false, argv, NULL, 0, &stdout_buf, &stdout_len);
	}
	if (rc != 0 || !stdout_buf) {
		free(stdout_buf);
		return 0;
	}

	size_t cap = 64;
	size_t count = 0;
	uint32_t *refs = (uint32_t *)malloc(cap * sizeof(uint32_t));
	char *saveptr = NULL;
	char *line = strtok_r(stdout_buf, "\r\n", &saveptr);
	while (line) {
		while (*line && isspace((unsigned char)*line)) line++;
		if (isdigit((unsigned char)*line)) {
			unsigned long v = strtoul(line, NULL, 10);
			if (v > 0 && v < UINT32_MAX) {
				if (count >= cap) {
					cap *= 2;
					refs = (uint32_t *)realloc(refs, cap * sizeof(uint32_t));
				}
				refs[count++] = (uint32_t)v;
			}
		}
		line = strtok_r(NULL, "\r\n", &saveptr);
	}
	free(stdout_buf);

	*out_refs = refs;
	*out_count = count;
	return 0;
}

uint32_t mm_next_ref(mm_ctx_t *ctx, mm_tier_t tier) {
	const char *tdir = mm_get_tier_dir(ctx, tier);
	if (!tdir) return 1;

	uint32_t max_ref = 0;
	uint32_t *refs = NULL;
	size_t n_refs = 0;
	if (mm_list_refs(ctx, tier, &refs, &n_refs) == 0 && refs) {
		for (size_t i = 0; i < n_refs; i++) {
			if (refs[i] > max_ref) max_ref = refs[i];
		}
		free(refs);
	}

	char seq_path[PATH_MAX];
	snprintf(seq_path, sizeof(seq_path), "%s/.next_ref", tdir);
	FILE *fp = fopen(seq_path, "r");
	if (fp) {
		unsigned long saved = 0;
		if (fscanf(fp, "%lu", &saved) == 1 && saved > max_ref) {
			max_ref = (uint32_t)saved;
		}
		fclose(fp);
	}

	uint32_t next = max_ref + 1;
	fp = fopen(seq_path, "w");
	if (fp) {
		fprintf(fp, "%u\n", next);
		fclose(fp);
	}
	return next;
}

int mm_put_envelope(mm_ctx_t *ctx, mm_tier_t tier, uint32_t ref, const mm_envelope_t *env) {
	if (!ctx || !env || ref == 0) return -1;
	const char *tdir = mm_get_tier_dir(ctx, tier);
	if (!tdir) return -1;

	uint8_t *payload_bytes = NULL;
	size_t payload_len = 0;
	char *payload_json = NULL;
	size_t json_len = 0;
	if (mm_payload_encode(env, &payload_bytes, &payload_len, &payload_json, &json_len) != 0) {
		return -1;
	}

	char fspec[128];
	build_filespec(ctx, fspec, sizeof(fspec));
	int rc = -1;

	if (ctx->prefer_daemon) {
		char sock[PATH_MAX];
		snprintf(sock, sizeof(sock), "%s/.cormd.sock", tdir);
		char hdr[256];
		snprintf(hdr, sizeof(hdr), "PUT %u %s %u %zu", ++ctx->req_counter, fspec, ref, payload_len);
		rc = send_cormd_request(sock, hdr, payload_bytes, payload_len, NULL, NULL);
	}
	if (rc != 0) {
		char ref_arg[64];
		snprintf(ref_arg, sizeof(ref_arg), "%u:-", ref);
		char *argv[] = { ctx->corm_bin, (char *)"-p", ref_arg, fspec, NULL };
		rc = exec_corm_cli(ctx, tdir, true, argv, payload_bytes, payload_len, NULL, NULL);
	}

	free(payload_bytes);
	free(payload_json);
	return rc;
}

int mm_get_envelope(mm_ctx_t *ctx, mm_tier_t tier, uint32_t ref, mm_envelope_t *out_env) {
	if (!ctx || !out_env || ref == 0) return -1;
	const char *tdir = mm_get_tier_dir(ctx, tier);
	if (!tdir) return -1;
	char db_file[PATH_MAX];
	snprintf(db_file, sizeof(db_file), "%s/mem.db", tdir);
	if (!file_exists(db_file)) return -1;

	char fspec[128];
	build_filespec(ctx, fspec, sizeof(fspec));

	char *stdout_buf = NULL;
	size_t stdout_len = 0;
	int rc = -1;

	if (ctx->prefer_daemon) {
		char sock[PATH_MAX];
		snprintf(sock, sizeof(sock), "%s/.cormd.sock", tdir);
		char hdr[256];
		snprintf(hdr, sizeof(hdr), "GET %u %s %u 0", ++ctx->req_counter, fspec, ref);
		rc = send_cormd_request(sock, hdr, NULL, 0, &stdout_buf, &stdout_len);
	}
	if (rc != 0) {
		char ref_str[32];
		snprintf(ref_str, sizeof(ref_str), "%u", ref);
		char *argv[] = { ctx->corm_bin, (char *)"-r", (char *)"-g", ref_str, fspec, NULL };
		rc = exec_corm_cli(ctx, tdir, false, argv, NULL, 0, &stdout_buf, &stdout_len);
	}
	if (rc != 0 || !stdout_buf || stdout_len == 0) {
		free(stdout_buf);
		return -1;
	}

	while (stdout_len > 0 && (stdout_buf[stdout_len - 1] == '\n' || stdout_buf[stdout_len - 1] == '\r')) {
		stdout_len--;
	}
	if (stdout_len == 0) {
		free(stdout_buf);
		return -1;
	}

	rc = mm_payload_decode(stdout_buf, stdout_len, out_env);
	free(stdout_buf);
	if (rc == 0 && (!out_env->summary || !out_env->summary[0]) && (!out_env->ep_problem || !out_env->ep_problem[0])) {
		mm_envelope_free(out_env);
		return -1;
	}
	return rc;
}

int mm_forget(mm_ctx_t *ctx, mm_tier_t tier, uint32_t ref) {
	if (!ctx || ref == 0) return -1;
	const char *tdir = mm_get_tier_dir(ctx, tier);
	if (!tdir) return -1;

	char fspec[128];
	build_filespec(ctx, fspec, sizeof(fspec));
	int rc = -1;

	if (ctx->prefer_daemon) {
		char sock[PATH_MAX];
		snprintf(sock, sizeof(sock), "%s/.cormd.sock", tdir);
		char hdr[256];
		snprintf(hdr, sizeof(hdr), "DEL %u %s %u 0", ++ctx->req_counter, fspec, ref);
		rc = send_cormd_request(sock, hdr, NULL, 0, NULL, NULL);
	}
	if (rc != 0) {
		char ref_str[32];
		snprintf(ref_str, sizeof(ref_str), "%u", ref);
		char *argv[] = { ctx->corm_bin, (char *)"-d", ref_str, fspec, NULL };
		rc = exec_corm_cli(ctx, tdir, true, argv, NULL, 0, NULL, NULL);
	}
	return rc;
}

/* ============================================================================
 * 4. Verification-Attested Promotion Ledger & Standing Directives Sync
 * ============================================================================ */

static void normalize_verif_cmd(const char *cmd, char *out, size_t cap) {
	if (!cmd || cap == 0) { if (cap > 0) out[0] = '\0'; return; }
	while (*cmd && isspace((unsigned char)*cmd)) cmd++;
	size_t j = 0;
	bool prev_sp = false;
	for (const char *p = cmd; *p && j + 1 < cap; p++) {
		if (isspace((unsigned char)*p)) {
			if (!prev_sp) { out[j++] = ' '; prev_sp = true; }
		} else {
			out[j++] = *p;
			prev_sp = false;
		}
	}
	while (j > 0 && out[j - 1] == ' ') j--;
	out[j] = '\0';
}

static bool is_verification_command(const char *cmd) {
	if (!cmd || !*cmd) return false;
	static const char *const patterns[] = {
		"make test", "make -C", "deno test", "npm test", "pytest",
		"cargo test", "go test", "ctest", "./bin/test", "test_", "check-", NULL
	};
	for (int i = 0; patterns[i]; i++) {
		if (strstr(cmd, patterns[i]) != NULL) return true;
	}
	return false;
}

static bool is_test_command_attested(const mm_ctx_t *ctx, const char *test_cmd) {
	if (!ctx || !test_cmd || !*test_cmd) return false;
	char norm[256];
	normalize_verif_cmd(test_cmd, norm, sizeof(norm));
	for (size_t i = 0; i < ctx->n_passed_cmds; i++) {
		if (strstr(ctx->passed_cmds[i], norm) || strstr(norm, ctx->passed_cmds[i]))
			return true;
	}
	return false;
}

int mm_record_file_mutation(mm_ctx_t *ctx, const char *file_path) {
	if (!ctx || !file_path || !*file_path) return -1;
	for (size_t i = 0; i < ctx->n_verif_failures; i++) {
		mm_verif_fail_t *vf = &ctx->verif_failures[i];
		if (!vf->active) continue;
		bool dup = false;
		for (size_t k = 0; k < vf->n_mutated; k++) {
			if (!strcmp(vf->mutated_files[k], file_path)) { dup = true; break; }
		}
		if (!dup && vf->n_mutated < 8) {
			snprintf(vf->mutated_files[vf->n_mutated++], 256, "%s", file_path);
		}
	}
	return 0;
}

int mm_observe_verification_command(mm_ctx_t *ctx, const char *command,
                                    int exit_code, const char *output_snippet,
                                    uint32_t *out_promoted_ref) {
	if (out_promoted_ref) *out_promoted_ref = 0;
	if (!ctx || !command || !is_verification_command(command)) return 0;

	char norm[256];
	normalize_verif_cmd(command, norm, sizeof(norm));

	if (exit_code != 0) {
		/* Record or update failure entry */
		mm_verif_fail_t *slot = NULL;
		for (size_t i = 0; i < ctx->n_verif_failures; i++) {
			if (ctx->verif_failures[i].active && !strcmp(ctx->verif_failures[i].norm_cmd, norm)) {
				slot = &ctx->verif_failures[i];
				break;
			}
		}
		if (!slot) {
			if (ctx->n_verif_failures < 8) {
				slot = &ctx->verif_failures[ctx->n_verif_failures++];
			} else {
				slot = &ctx->verif_failures[0];
			}
			memset(slot, 0, sizeof(*slot));
		}
		snprintf(slot->cmd, sizeof(slot->cmd), "%s", command);
		snprintf(slot->norm_cmd, sizeof(slot->norm_cmd), "%s", norm);
		snprintf(slot->err_snippet, sizeof(slot->err_snippet), "%s", output_snippet ? output_snippet : "test failure");
		slot->active = true;
		return 0;
	}

	/* Command succeeded (exit_code == 0): record in passed_cmds */
	bool already_passed = false;
	for (size_t i = 0; i < ctx->n_passed_cmds; i++) {
		if (!strcmp(ctx->passed_cmds[i], norm)) { already_passed = true; break; }
	}
	if (!already_passed && ctx->n_passed_cmds < 16) {
		snprintf(ctx->passed_cmds[ctx->n_passed_cmds++], 256, "%s", norm);
	}

	/* Check if this resolves an active failure that had file mutations */
	for (size_t i = 0; i < ctx->n_verif_failures; i++) {
		mm_verif_fail_t *vf = &ctx->verif_failures[i];
		if (vf->active && !strcmp(vf->norm_cmd, norm) && vf->n_mutated > 0) {
			char prob[512];
			char outc[512];
			char files_list[256] = {0};
			for (size_t k = 0; k < vf->n_mutated; k++) {
				if (k > 0) strncat(files_list, ", ", sizeof(files_list) - strlen(files_list) - 1);
				strncat(files_list, vf->mutated_files[k], sizeof(files_list) - strlen(files_list) - 1);
			}
			snprintf(prob, sizeof(prob), "Verification command `%s` failed (%s) and was resolved after modifying %s",
			         vf->norm_cmd, vf->err_snippet, files_list);
			snprintf(outc, sizeof(outc), "Verification passed (`%s`, exit 0) after editing %s",
			         vf->norm_cmd, files_list);

			const char *tags[2] = { "attested-fix", "fail-to-pass" };
			mm_record_req_t rreq;
			memset(&rreq, 0, sizeof(rreq));
			rreq.tier    = MM_TIER_PROJECT;
			rreq.level   = MM_LEVEL_L2;
			rreq.type    = "episode";
			rreq.status  = MM_STATUS_SUPPORTED;
			rreq.problem = prob;
			rreq.outcome = outc;
			rreq.path    = vf->mutated_files[0];
			rreq.tags    = tags;
			rreq.n_tags  = 2;

			uint32_t new_ref = 0;
			mm_record(ctx, &rreq, NULL, &new_ref);
			if (out_promoted_ref) *out_promoted_ref = new_ref;
			vf->active = false;
			break;
		}
	}
	return 0;
}

int mm_sync_standing_directives(mm_ctx_t *ctx) {
	if (!ctx) return -1;
	const char *gdir = mm_get_tier_dir(ctx, MM_TIER_GLOBAL);
	if (!gdir) return -1;

	uint32_t *refs = NULL;
	size_t n_refs = 0;
	if (mm_list_refs(ctx, MM_TIER_GLOBAL, &refs, &n_refs) != 0) return -1;

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/standing_directives.json", gdir);
	FILE *fp = fopen(path, "w");
	if (!fp) { free(refs); return -1; }

	char ts[32];
	now_iso_utc(ts, sizeof(ts));
	fprintf(fp, "{\"version\":1,\"updatedAt\":\"%s\",\"directives\":[", ts);
	int first = 1;
	for (size_t i = 0; i < n_refs; i++) {
		mm_envelope_t env;
		if (mm_get_envelope(ctx, MM_TIER_GLOBAL, refs[i], &env) == 0) {
			if (env.standing && env.lifecycle != MM_STATUS_CONTRADICTED &&
			    env.lifecycle != MM_STATUS_SUPERSEDED && env.summary && env.summary[0]) {
				fprintf(fp, "%s{\"ref\":%u,\"level\":\"%s\",\"lifecycle\":\"%s\",\"summary\":\"",
				        first ? "" : ",", refs[i], mm_level_to_str(env.level), mm_status_to_str(env.lifecycle));
				for (const char *p = env.summary; *p; p++) {
					if (*p == '"' || *p == '\\') fputc('\\', fp);
					if (*p != '\n' && *p != '\r') fputc(*p, fp);
				}
				fprintf(fp, "\"}");
				first = 0;
			}
			mm_envelope_free(&env);
		}
	}
	fprintf(fp, "]}\n");
	fclose(fp);
	free(refs);
	return 0;
}

int mm_synthesize_reinforcement(mm_ctx_t *ctx, const char *reason,
                                const char *milestone_summary,
                                char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	uint32_t *refs = NULL;
	size_t n_refs = 0;
	mm_list_refs(ctx, MM_TIER_GLOBAL, &refs, &n_refs);

	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	if (!buf) { free(refs); return -1; }

	const char *label = "SESSION START";
	if (reason && !strcmp(reason, "compaction")) label = "POST-COMPACTION";
	else if (reason && !strcmp(reason, "milestone")) label = "MILESTONE CHECKPOINT";

	size_t pos = (size_t)snprintf(buf, cap,
		"[STANDING DIRECTIVES REINFORCEMENT — %s]\n", label);
	if (milestone_summary && *milestone_summary) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "Milestone: %s\n", milestone_summary);
	}
	pos += (size_t)snprintf(buf + pos, cap - pos,
		"The following operator standing directives and invariants remain in force:\n");

	size_t active_count = 0;
	for (size_t i = 0; i < n_refs; i++) {
		mm_envelope_t env;
		if (mm_get_envelope(ctx, MM_TIER_GLOBAL, refs[i], &env) == 0) {
			if (env.standing && env.lifecycle != MM_STATUS_CONTRADICTED &&
			    env.lifecycle != MM_STATUS_SUPERSEDED && env.summary && env.summary[0]) {
				active_count++;
				pos += (size_t)snprintf(buf + pos, cap - pos,
					"  • [Ref G:%u] (%s, %s) %s\n",
					refs[i], mm_level_to_str(env.level), mm_status_to_str(env.lifecycle), env.summary);
				for (size_t c = 0; c < env.n_constraints; c++) {
					pos += (size_t)snprintf(buf + pos, cap - pos,
						"      Constraint: %s\n", env.constraints[c]);
				}
			}
			mm_envelope_free(&env);
		}
	}
	free(refs);

	if (active_count == 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "  (none active)\n");
	}
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 5. Record, Epistemic Lifecycle Validation & Truth Maintenance Cascade
 * ============================================================================ */

int mm_record(mm_ctx_t *ctx, const mm_record_req_t *req,
              mm_tier_t *out_tier, uint32_t *out_ref) {
	if (!ctx || !req || !req->problem || !req->problem[0]) return -1;

	mm_tier_t target_tier = req->tier;
	if (target_tier == MM_TIER_AUTO || target_tier == MM_TIER_BOTH) {
		if (req->standing || !ctx->has_project) {
			target_tier = MM_TIER_GLOBAL;
		} else {
			target_tier = MM_TIER_PROJECT;
		}
	}
	if (target_tier == MM_TIER_PROJECT && !ctx->has_project) {
		target_tier = MM_TIER_GLOBAL;
	}

	mm_envelope_t env;
	mm_envelope_init(&env);
	env.level = (req->level != MM_LEVEL_NONE)
		? req->level
		: ((req->standing || req->anchor) ? MM_LEVEL_L2 : MM_LEVEL_L1);
	env.lifecycle = req->status;
	snprintf(env.type, sizeof(env.type), "%s",
	         (req->type && req->type[0]) ? req->type : (req->standing ? "rule" : "episode"));
	env.standing  = req->standing;
	env.is_anchor = req->anchor || req->standing;
	env.summary   = strdup(req->problem);

	if (req->request && req->request[0]) env.ep_request = strdup(req->request);
	if (req->outcome && req->outcome[0]) env.ep_outcome = strdup(req->outcome);
	if (!strcmp(env.type, "episode")) {
		env.ep_problem = strdup(req->problem);
	}
	if (req->session_id && req->session_id[0]) {
		snprintf(env.ep_session_id, sizeof(env.ep_session_id), "%s", req->session_id);
	}
	env.ep_turn_id = req->turn_id;
	if (req->run_id && req->run_id[0]) {
		snprintf(env.ep_run_id, sizeof(env.ep_run_id), "%s", req->run_id);
	}
	if (req->leaf_id && req->leaf_id[0]) {
		snprintf(env.ep_leaf_id, sizeof(env.ep_leaf_id), "%s", req->leaf_id);
	}

	for (size_t i = 0; i < req->n_approaches && env.n_approaches < MM_MAX_APPROACHES; i++) {
		env.ep_approaches[env.n_approaches++] = req->approaches[i];
	}
	for (size_t i = 0; i < req->n_findings && env.n_findings < MM_MAX_FINDINGS; i++) {
		if (req->findings[i]) env.ep_findings[env.n_findings++] = strdup(req->findings[i]);
	}
	for (size_t i = 0; i < req->n_diffs && env.n_diffs < MM_MAX_DIFFS; i++) {
		if (req->diffs[i]) env.ep_diffs[env.n_diffs++] = strdup(req->diffs[i]);
	}

	const char *proj = (req->project && req->project[0])
		? req->project
		: (target_tier == MM_TIER_PROJECT ? ctx->project_name : "");
	if (proj && *proj) snprintf(env.ctx_project, sizeof(env.ctx_project), "%s", proj);
	if (req->subsystem && req->subsystem[0]) snprintf(env.ctx_subsystem, sizeof(env.ctx_subsystem), "%s", req->subsystem);
	if (req->module && req->module[0])       snprintf(env.ctx_module, sizeof(env.ctx_module), "%s", req->module);
	if (req->path && req->path[0])           snprintf(env.ctx_path, sizeof(env.ctx_path), "%s", req->path);

	for (size_t i = 0; i < req->n_tags && env.n_tags < MM_MAX_TAGS; i++) {
		if (req->tags[i]) env.tags[env.n_tags++] = strdup(req->tags[i]);
	}
	for (size_t i = 0; i < req->n_constraints && env.n_constraints < MM_MAX_CONSTRAINTS; i++) {
		if (req->constraints[i]) env.constraints[env.n_constraints++] = strdup(req->constraints[i]);
	}
	for (size_t i = 0; i < req->n_evidence && env.n_evidence < MM_MAX_EVIDENCE; i++) {
		env.evidence[env.n_evidence++] = req->evidence_refs[i];
	}
	env.supersedes = req->supersedes_ref;

	/* Verification-Attested Promotion: check tests_passed against ledger */
	if (req->n_tests_passed > 0 && req->tests_passed) {
		bool attested = false;
		for (size_t i = 0; i < req->n_tests_passed; i++) {
			if (is_test_command_attested(ctx, req->tests_passed[i])) {
				attested = true;
				break;
			}
		}
		if (attested) {
			env.lifecycle = MM_STATUS_SUPPORTED;
			if (env.n_tags < MM_MAX_TAGS) {
				env.tags[env.n_tags++] = strdup("test-attested");
			}
		} else if (req->status == MM_STATUS_OBSERVED || req->status == MM_STATUS_HYPOTHESIZED) {
			env.lifecycle = MM_STATUS_TESTED;
		}
	}

	uint32_t ref = mm_next_ref(ctx, target_tier);
	int rc = mm_put_envelope(ctx, target_tier, ref, &env);
	bool was_standing = env.standing;
	mm_epistemic_status_t final_lc = env.lifecycle;
	mm_envelope_free(&env);
	if (rc != 0) return -1;

	/* If supersedes_ref > 0, mark the old record superseded and run Truth Maintenance Cascade */
	if (req->supersedes_ref > 0) {
		mm_envelope_t old_env;
		if (mm_get_envelope(ctx, target_tier, req->supersedes_ref, &old_env) == 0) {
			if (old_env.n_history < MM_MAX_HISTORY) {
				mm_lifecycle_transition_t *tr = &old_env.history[old_env.n_history++];
				tr->from = old_env.lifecycle;
				tr->to   = MM_STATUS_SUPERSEDED;
				now_iso_utc(tr->timestamp, sizeof(tr->timestamp));
				snprintf(tr->reason, sizeof(tr->reason), "Superseded by Ref #%u", ref);
				tr->evidence_ref = ref;
			}
			old_env.lifecycle = MM_STATUS_SUPERSEDED;
			old_env.superseded_by = ref;
			mm_put_envelope(ctx, target_tier, req->supersedes_ref, &old_env);
			mm_envelope_free(&old_env);
			mm_propagate_truth_maintenance(ctx, target_tier, req->supersedes_ref, MM_STATUS_SUPERSEDED, NULL, NULL);
		}
	}

	if (was_standing) {
		mm_sync_standing_directives(ctx);
	}
	if ((final_lc == MM_STATUS_SUPPORTED || final_lc == MM_STATUS_TESTED) && target_tier == MM_TIER_PROJECT) {
		mm_lore_bind_symbol(ctx, ref, req->problem, req->module, req->path);
	}

	if (out_tier) *out_tier = target_tier;
	if (out_ref)  *out_ref  = ref;
	return 0;
}

int mm_validate_lifecycle_transition(mm_ctx_t *ctx, mm_tier_t tier,
                                     const mm_envelope_t *env,
                                     mm_epistemic_status_t target,
                                     const char *reason,
                                     uint32_t evidence_ref,
                                     char *err_buf, size_t err_cap) {
	if (err_buf && err_cap > 0) err_buf[0] = '\0';
	if (!env) return -1;

	mm_epistemic_status_t cur = env->lifecycle;
	if (cur == target) return 0;

	/* 1. Superseded is terminal */
	if (cur == MM_STATUS_SUPERSEDED) {
		if (err_buf) snprintf(err_buf, err_cap, "Invalid transition: 'superseded' is a terminal state.");
		return -1;
	}

	/* 2. Contradicted cannot jump directly to Supported without re-testing */
	if (cur == MM_STATUS_CONTRADICTED && target == MM_STATUS_SUPPORTED) {
		if (err_buf) snprintf(err_buf, err_cap, "Invalid transition from 'contradicted' directly to 'supported'.");
		return -1;
	}

	/* 3. Supported can only transition to Contradicted or Superseded */
	if (cur == MM_STATUS_SUPPORTED && target != MM_STATUS_CONTRADICTED && target != MM_STATUS_SUPERSEDED) {
		if (err_buf) snprintf(err_buf, err_cap, "Invalid transition from 'supported' to '%s'.", mm_status_to_str(target));
		return -1;
	}

	/* 4. Contradicted / Superseded require reason or evidence_ref */
	if (target == MM_STATUS_CONTRADICTED || target == MM_STATUS_SUPERSEDED) {
		bool has_reason = reason && *reason;
		if (!has_reason && evidence_ref == 0) {
			if (err_buf) snprintf(err_buf, err_cap, "Transition to '%s' requires a reason or evidenceRef.", mm_status_to_str(target));
			return -1;
		}
	}

	/* 5. If evidence_ref > 0 is provided for 'supported', verify it exists and is not contradicted/superseded */
	if (target == MM_STATUS_SUPPORTED && evidence_ref > 0 && ctx) {
		mm_envelope_t ev_env;
		if (mm_get_envelope(ctx, tier, evidence_ref, &ev_env) != 0) {
			if (err_buf) snprintf(err_buf, err_cap, "Supporting evidence Ref #%u does not exist.", evidence_ref);
			return -1;
		}
		bool bad = (ev_env.lifecycle == MM_STATUS_CONTRADICTED || ev_env.lifecycle == MM_STATUS_SUPERSEDED);
		mm_envelope_free(&ev_env);
		if (bad) {
			if (err_buf) snprintf(err_buf, err_cap, "Supporting evidence Ref #%u is contradicted or superseded.", evidence_ref);
			return -1;
		}
	}

	return 0;
}

int mm_propagate_truth_maintenance(mm_ctx_t *ctx, mm_tier_t tier,
                                   uint32_t changed_ref,
                                   mm_epistemic_status_t new_state,
                                   uint32_t **out_downgraded, size_t *out_count) {
	if (out_downgraded) *out_downgraded = NULL;
	if (out_count) *out_count = 0;
	if (!ctx || (new_state != MM_STATUS_CONTRADICTED && new_state != MM_STATUS_SUPERSEDED)) {
		return 0;
	}

	uint32_t *refs = NULL;
	size_t n_refs = 0;
	if (mm_list_refs(ctx, tier, &refs, &n_refs) != 0 || !refs || n_refs == 0) {
		free(refs);
		return 0;
	}

	uint32_t *down = (uint32_t *)malloc(n_refs * sizeof(uint32_t));
	size_t n_down = 0;

	for (size_t i = 0; i < n_refs; i++) {
		uint32_t r = refs[i];
		if (r == changed_ref) continue;

		mm_envelope_t env;
		if (mm_get_envelope(ctx, tier, r, &env) != 0) continue;

		if (env.lifecycle != MM_STATUS_SUPPORTED && env.lifecycle != MM_STATUS_TESTED) {
			mm_envelope_free(&env);
			continue;
		}

		bool cites_changed = false;
		for (size_t e = 0; e < env.n_evidence; e++) {
			if (env.evidence[e] == changed_ref) {
				cites_changed = true;
				break;
			}
		}
		if (!cites_changed) {
			mm_envelope_free(&env);
			continue;
		}

		/* Check if any other supporting evidence ref remains valid */
		bool has_valid_other = false;
		for (size_t e = 0; e < env.n_evidence; e++) {
			uint32_t eref = env.evidence[e];
			if (eref == changed_ref) continue;
			mm_envelope_t other_env;
			if (mm_get_envelope(ctx, tier, eref, &other_env) == 0) {
				if (other_env.lifecycle != MM_STATUS_CONTRADICTED &&
				    other_env.lifecycle != MM_STATUS_SUPERSEDED) {
					has_valid_other = true;
				}
				mm_envelope_free(&other_env);
			}
			if (has_valid_other) break;
		}

		if (!has_valid_other) {
			if (env.n_history < MM_MAX_HISTORY) {
				mm_lifecycle_transition_t *tr = &env.history[env.n_history++];
				tr->from = env.lifecycle;
				tr->to   = MM_STATUS_HYPOTHESIZED;
				now_iso_utc(tr->timestamp, sizeof(tr->timestamp));
				snprintf(tr->reason, sizeof(tr->reason),
				         "Auto-downgraded: supporting evidence Ref #%u became %s",
				         changed_ref, mm_status_to_str(new_state));
				tr->evidence_ref = changed_ref;
			}
			env.lifecycle = MM_STATUS_HYPOTHESIZED;
			if (mm_put_envelope(ctx, tier, r, &env) == 0 && down) {
				down[n_down++] = r;
			}
		}
		mm_envelope_free(&env);
	}
	free(refs);

	if (out_downgraded) *out_downgraded = down;
	else free(down);
	if (out_count) *out_count = n_down;
	return 0;
}

/* ============================================================================
 * 6. Bidirectional Evidence DAG Traversal (mm_traverse)
 * ============================================================================ */

static void traverse_recursive(mm_ctx_t *ctx, mm_tier_t tier, uint32_t cur_ref,
                               bool go_up, int depth, int max_depth,
                               const uint32_t *all_refs, size_t n_all_refs,
                               uint32_t *visited, size_t *n_visited,
                               char **buf, size_t *pos, size_t *cap) {
	if (*pos + 1024 >= *cap) {
		*cap *= 2;
		*buf = (char *)realloc(*buf, *cap);
	}

	for (int d = 0; d < depth; d++) {
		*pos += (size_t)snprintf(*buf + *pos, *cap - *pos, "  ");
	}
	if (depth > 0) {
		*pos += (size_t)snprintf(*buf + *pos, *cap - *pos, "%s ", go_up ? "↑" : "└─");
	}

	for (size_t v = 0; v < *n_visited; v++) {
		if (visited[v] == cur_ref) {
			*pos += (size_t)snprintf(*buf + *pos, *cap - *pos, "[Ref #%u] [CYCLE DETECTED]\n", cur_ref);
			return;
		}
	}

	mm_envelope_t env;
	if (mm_get_envelope(ctx, tier, cur_ref, &env) != 0) {
		*pos += (size_t)snprintf(*buf + *pos, *cap - *pos, "[Ref #%u] [MISSING RECORD]\n", cur_ref);
		return;
	}

	*pos += (size_t)snprintf(*buf + *pos, *cap - *pos,
		"[Ref #%u] (%s, %s, %s): %s\n",
		cur_ref,
		mm_level_to_str(env.level),
		mm_status_to_str(env.lifecycle),
		env.type,
		env.summary ? env.summary : "");

	if (depth >= max_depth) {
		mm_envelope_free(&env);
		return;
	}

	if (*n_visited < 128) visited[(*n_visited)++] = cur_ref;

	if (!go_up) {
		/* Downward: follow env.evidence[] and env.supersedes */
		for (size_t e = 0; e < env.n_evidence; e++) {
			traverse_recursive(ctx, tier, env.evidence[e], false, depth + 1, max_depth,
			                   all_refs, n_all_refs, visited, n_visited, buf, pos, cap);
		}
		if (env.supersedes > 0) {
			traverse_recursive(ctx, tier, env.supersedes, false, depth + 1, max_depth,
			                   all_refs, n_all_refs, visited, n_visited, buf, pos, cap);
		}
	} else {
		/* Upward: find records whose evidence[] cites cur_ref or supersedes == cur_ref */
		for (size_t i = 0; i < n_all_refs; i++) {
			uint32_t cand_ref = all_refs[i];
			if (cand_ref == cur_ref) continue;
			mm_envelope_t cenv;
			if (mm_get_envelope(ctx, tier, cand_ref, &cenv) == 0) {
				bool cites = (cenv.supersedes == cur_ref);
				for (size_t e = 0; e < cenv.n_evidence && !cites; e++) {
					if (cenv.evidence[e] == cur_ref) cites = true;
				}
				mm_envelope_free(&cenv);
				if (cites) {
					traverse_recursive(ctx, tier, cand_ref, true, depth + 1, max_depth,
					                   all_refs, n_all_refs, visited, n_visited, buf, pos, cap);
				}
			}
		}
	}

	if (*n_visited > 0) (*n_visited)--;
	mm_envelope_free(&env);
}

int mm_traverse(mm_ctx_t *ctx, mm_tier_t tier, uint32_t start_ref,
                const char *direction, int max_depth,
                char **out_buf, size_t *out_len) {
	if (!ctx || start_ref == 0 || !out_buf) return -1;
	if (max_depth <= 0) max_depth = 3;
	mm_tier_t eff_tier = (tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
	                     (ctx->has_project ? MM_TIER_PROJECT : MM_TIER_GLOBAL);

	mm_envelope_t probe;
	if (mm_get_envelope(ctx, eff_tier, start_ref, &probe) != 0) {
		if (tier == MM_TIER_AUTO) {
			eff_tier = (eff_tier == MM_TIER_PROJECT) ? MM_TIER_GLOBAL : MM_TIER_PROJECT;
			if (mm_get_envelope(ctx, eff_tier, start_ref, &probe) != 0) return -1;
		} else {
			return -1;
		}
	}
	mm_envelope_free(&probe);

	uint32_t *all_refs = NULL;
	size_t n_all = 0;
	mm_list_refs(ctx, eff_tier, &all_refs, &n_all);

	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;

	bool go_up = direction && (!strcmp(direction, "up") || !strcmp(direction, "inbound"));
	pos += (size_t)snprintf(buf + pos, cap - pos,
		"=== Evidence Tree (%s, depth=%d) ===\n",
		go_up ? "Upward / Dependent Conclusions" : "Downward / Supporting Evidence",
		max_depth);

	uint32_t visited[128];
	size_t n_vis = 0;
	traverse_recursive(ctx, eff_tier, start_ref, go_up, 0, max_depth,
	                   all_refs, n_all, visited, &n_vis, &buf, &pos, &cap);

	free(all_refs);
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_review_ex(mm_ctx_t *ctx, const mm_review_req_t *req, char **out_buf, size_t *out_len) {
	if (!ctx || !req || req->ref == 0 || !req->action) return -1;
	mm_tier_t tier = (req->tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
	                 (ctx->has_project ? MM_TIER_PROJECT : MM_TIER_GLOBAL);

	mm_envelope_t env;
	if (mm_get_envelope(ctx, tier, req->ref, &env) != 0) {
		if (req->tier == MM_TIER_AUTO) {
			tier = (tier == MM_TIER_PROJECT) ? MM_TIER_GLOBAL : MM_TIER_PROJECT;
			if (mm_get_envelope(ctx, tier, req->ref, &env) != 0) return -1;
		} else {
			return -1;
		}
	}

	if (!strcmp(req->action, "inspect")) {
		int rc = mm_traverse(ctx, tier, req->ref,
		                     (req->direction && *req->direction) ? req->direction : "down",
		                     req->depth > 0 ? req->depth : 3,
		                     out_buf, out_len);
		mm_envelope_free(&env);
		return rc;
	}

	mm_epistemic_status_t old_lc = env.lifecycle;
	bool lifecycle_changed = false;
	char err_msg[256] = {0};

	if (!strcmp(req->action, "update_lifecycle")) {
		const char *eff_reason = (req->reason && *req->reason) ? req->reason : "manual update";
		if (mm_validate_lifecycle_transition(ctx, tier, &env, req->target_lifecycle,
		                                     eff_reason, req->evidence_ref,
		                                     err_msg, sizeof(err_msg)) != 0) {
			mm_envelope_free(&env);
			if (out_buf) {
				*out_buf = strdup(err_msg);
				if (out_len) *out_len = strlen(err_msg);
			}
			return -1;
		}
		env.lifecycle = req->target_lifecycle;
		lifecycle_changed = (old_lc != env.lifecycle);
		if (req->evidence_ref > 0 && env.n_evidence < MM_MAX_EVIDENCE) {
			bool dup = false;
			for (size_t i = 0; i < env.n_evidence; i++) {
				if (env.evidence[i] == req->evidence_ref) { dup = true; break; }
			}
			if (!dup) env.evidence[env.n_evidence++] = req->evidence_ref;
		}
	} else if (!strcmp(req->action, "confirm") || !strcmp(req->action, "support")) {
		if (env.lifecycle == MM_STATUS_SUPERSEDED || env.lifecycle == MM_STATUS_CONTRADICTED) {
			mm_envelope_free(&env);
			return -1;
		}
		env.lifecycle = MM_STATUS_SUPPORTED;
		lifecycle_changed = (old_lc != env.lifecycle);
	} else if (!strcmp(req->action, "contradict") || !strcmp(req->action, "refute")) {
		if (env.lifecycle == MM_STATUS_SUPERSEDED) {
			mm_envelope_free(&env);
			return -1;
		}
		env.lifecycle = MM_STATUS_CONTRADICTED;
		lifecycle_changed = (old_lc != env.lifecycle);
	} else if (!strcmp(req->action, "supersede")) {
		env.lifecycle = MM_STATUS_SUPERSEDED;
		if (req->superseded_by_ref > 0) env.superseded_by = req->superseded_by_ref;
		else if (req->evidence_ref > 0) env.superseded_by = req->evidence_ref;
		lifecycle_changed = (old_lc != env.lifecycle);
	} else if (!strcmp(req->action, "helpful") || !strcmp(req->action, "used")) {
		env.used_count++;
	} else if (!strcmp(req->action, "harmful") || !strcmp(req->action, "irrelevant")) {
		env.irrelevant_count++;
	} else if (!strcmp(req->action, "obsolete")) {
		env.obsolete_count++;
	} else {
		mm_envelope_free(&env);
		return -1;
	}

	if (lifecycle_changed && env.n_history < MM_MAX_HISTORY) {
		mm_lifecycle_transition_t *tr = &env.history[env.n_history++];
		tr->from = old_lc;
		tr->to   = env.lifecycle;
		now_iso_utc(tr->timestamp, sizeof(tr->timestamp));
		snprintf(tr->reason, sizeof(tr->reason), "%s",
		         (req->reason && *req->reason) ? req->reason : req->action);
		tr->evidence_ref = req->evidence_ref > 0 ? req->evidence_ref : req->superseded_by_ref;
	}

	mm_epistemic_status_t new_lc = env.lifecycle;
	bool is_standing = env.standing;
	int rc = mm_put_envelope(ctx, tier, req->ref, &env);
	mm_envelope_free(&env);
	if (rc != 0) return -1;

	uint32_t *downgraded = NULL;
	size_t n_down = 0;
	if (lifecycle_changed && (new_lc == MM_STATUS_CONTRADICTED || new_lc == MM_STATUS_SUPERSEDED)) {
		mm_propagate_truth_maintenance(ctx, tier, req->ref, new_lc, &downgraded, &n_down);
	}
	if (is_standing) {
		mm_sync_standing_directives(ctx);
	}

	if (out_buf) {
		char *msg = (char *)malloc(2048);
		size_t pos = (size_t)snprintf(msg, 2048,
			"Updated [Ref %s:%u] lifecycle %s -> %s (action: %s)\n",
			tier == MM_TIER_GLOBAL ? "G" : "P", req->ref,
			mm_status_to_str(old_lc), mm_status_to_str(new_lc), req->action);
		if (n_down > 0) {
			pos += (size_t)snprintf(msg + pos, 2048 - pos,
				"Truth Maintenance Cascade downgraded %zu dependent record(s) to hypothesized:", n_down);
			for (size_t i = 0; i < n_down; i++) {
				pos += (size_t)snprintf(msg + pos, 2048 - pos, " #%u", downgraded[i]);
			}
			pos += (size_t)snprintf(msg + pos, 2048 - pos, "\n");
		}
		*out_buf = msg;
		if (out_len) *out_len = pos;
	}
	free(downgraded);
	return 0;
}

int mm_review(mm_ctx_t *ctx, const mm_review_req_t *req) {
	return mm_review_ex(ctx, req, NULL, NULL);
}

/* ============================================================================
 * 7. Structured Persistent Memory Overview & Counterfactual Guardrails
 * ============================================================================ */

typedef struct {
	mm_tier_t     tier;
	uint32_t      ref;
	mm_envelope_t env;
} mm_loaded_rec_t;

static void collect_active_tier_records(mm_ctx_t *ctx, mm_tier_t tier,
                                        mm_loaded_rec_t **io_recs, size_t *io_count, size_t *io_cap) {
	uint32_t *refs = NULL;
	size_t n_refs = 0;
	if (mm_list_refs(ctx, tier, &refs, &n_refs) != 0 || !refs) return;

	for (size_t i = 0; i < n_refs; i++) {
		mm_envelope_t env;
		if (mm_get_envelope(ctx, tier, refs[i], &env) == 0) {
			if (env.lifecycle != MM_STATUS_SUPERSEDED) {
				if (*io_count >= *io_cap) {
					*io_cap = (*io_cap == 0) ? 32 : (*io_cap * 2);
					*io_recs = (mm_loaded_rec_t *)realloc(*io_recs, (*io_cap) * sizeof(mm_loaded_rec_t));
				}
				mm_loaded_rec_t *slot = &(*io_recs)[(*io_count)++];
				slot->tier = tier;
				slot->ref  = refs[i];
				slot->env  = env;
			} else {
				mm_envelope_free(&env);
			}
		}
	}
	free(refs);
}

static bool has_tag_ci(const mm_envelope_t *env, const char *tag) {
	for (size_t i = 0; i < env->n_tags; i++) {
		if (env->tags[i] && !strcasecmp(env->tags[i], tag)) return true;
	}
	return false;
}

int mm_build_memory_overview(mm_ctx_t *ctx, mm_tier_t tier, int max_per_bucket,
                             char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	if (max_per_bucket <= 0) max_per_bucket = 10;

	mm_loaded_rec_t *recs = NULL;
	size_t n_recs = 0, cap_recs = 0;

	if (tier == MM_TIER_AUTO || tier == MM_TIER_BOTH || tier == MM_TIER_GLOBAL || !ctx->has_project) {
		collect_active_tier_records(ctx, MM_TIER_GLOBAL, &recs, &n_recs, &cap_recs);
	}
	if (ctx->has_project && (tier == MM_TIER_AUTO || tier == MM_TIER_BOTH || tier == MM_TIER_PROJECT)) {
		collect_active_tier_records(ctx, MM_TIER_PROJECT, &recs, &n_recs, &cap_recs);
	}

	size_t lvl_counts[5] = {0};
	char earliest[16] = {0};
	char latest[16] = {0};

	for (size_t i = 0; i < n_recs; i++) {
		const mm_envelope_t *e = &recs[i].env;
		if (e->level >= MM_LEVEL_L0 && e->level <= MM_LEVEL_L3) {
			lvl_counts[e->level]++;
		}
		if (strlen(e->timestamp) >= 10) {
			char d[11];
			memcpy(d, e->timestamp, 10);
			d[10] = '\0';
			if (!earliest[0] || strcmp(d, earliest) < 0) strcpy(earliest, d);
			if (!latest[0]   || strcmp(d, latest) > 0)   strcpy(latest, d);
		}
	}

	size_t cap = 8192 + n_recs * 256;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== Persistent Memory Hierarchical Overview ===\n"
		"Total Active Records: %zu | Span: %s .. %s\n"
		"Levels: L3=%zu | L2=%zu | L1=%zu | L0=%zu\n\n",
		n_recs,
		earliest[0] ? earliest : "N/A",
		latest[0] ? latest : "N/A",
		lvl_counts[MM_LEVEL_L3], lvl_counts[MM_LEVEL_L2],
		lvl_counts[MM_LEVEL_L1], lvl_counts[MM_LEVEL_L0]);

	/* Bucket 1: Persona Tenets */
	pos += (size_t)snprintf(buf + pos, cap - pos, "=== Persona Tenets ===\n");
	int b_cnt = 0;
	for (size_t i = 0; i < n_recs && b_cnt < max_per_bucket; i++) {
		const mm_envelope_t *e = &recs[i].env;
		if (e->lifecycle == MM_STATUS_CONTRADICTED) continue;
		if (!strcmp(e->type, "persona") || has_tag_ci(e, "persona-tenet")) {
			pos += (size_t)snprintf(buf + pos, cap - pos, "  • [Ref %s:%u] (%s) %s\n",
			                        recs[i].tier == MM_TIER_GLOBAL ? "G" : "P",
			                        recs[i].ref, mm_level_to_str(e->level), e->summary ? e->summary : "");
			b_cnt++;
		}
	}
	if (b_cnt == 0) pos += (size_t)snprintf(buf + pos, cap - pos, "  (none)\n");

	/* Bucket 2: Core Principles & Standing Directives (L3 / Standing) */
	pos += (size_t)snprintf(buf + pos, cap - pos, "\n=== Core Principles (L3 & Standing) ===\n");
	b_cnt = 0;
	for (size_t i = 0; i < n_recs && b_cnt < max_per_bucket; i++) {
		const mm_envelope_t *e = &recs[i].env;
		if (e->lifecycle == MM_STATUS_CONTRADICTED) continue;
		if (!strcmp(e->type, "persona") || has_tag_ci(e, "persona-tenet")) continue;
		if (e->level == MM_LEVEL_L3 || e->standing) {
			pos += (size_t)snprintf(buf + pos, cap - pos, "  • [Ref %s:%u] (%s, %s%s) %s\n",
			                        recs[i].tier == MM_TIER_GLOBAL ? "G" : "P",
			                        recs[i].ref, mm_level_to_str(e->level),
			                        mm_status_to_str(e->lifecycle),
			                        e->standing ? ", standing" : "",
			                        e->summary ? e->summary : "");
			b_cnt++;
		}
	}
	if (b_cnt == 0) pos += (size_t)snprintf(buf + pos, cap - pos, "  (none)\n");

	/* Bucket 3: Active Invariants & Decisions (L2) */
	pos += (size_t)snprintf(buf + pos, cap - pos, "\n=== Active Invariants & Decisions (L2) ===\n");
	b_cnt = 0;
	for (size_t i = 0; i < n_recs && b_cnt < max_per_bucket; i++) {
		const mm_envelope_t *e = &recs[i].env;
		if (e->lifecycle == MM_STATUS_CONTRADICTED) continue;
		if (!strcmp(e->type, "persona") || has_tag_ci(e, "persona-tenet")) continue;
		if (e->level == MM_LEVEL_L2 && !e->standing) {
			pos += (size_t)snprintf(buf + pos, cap - pos, "  • [Ref %s:%u] (%s) %s\n",
			                        recs[i].tier == MM_TIER_GLOBAL ? "G" : "P",
			                        recs[i].ref, mm_status_to_str(e->lifecycle),
			                        e->summary ? e->summary : "");
			b_cnt++;
		}
	}
	if (b_cnt == 0) pos += (size_t)snprintf(buf + pos, cap - pos, "  (none)\n");

	/* Bucket 4: Recent Milestones (L1) */
	pos += (size_t)snprintf(buf + pos, cap - pos, "\n=== Recent Milestones (L1) ===\n");
	b_cnt = 0;
	for (size_t i = 0; i < n_recs && b_cnt < max_per_bucket; i++) {
		const mm_envelope_t *e = &recs[i].env;
		if (e->lifecycle == MM_STATUS_CONTRADICTED) continue;
		if (e->level == MM_LEVEL_L1) {
			pos += (size_t)snprintf(buf + pos, cap - pos, "  • [Ref %s:%u] (%s) %s\n",
			                        recs[i].tier == MM_TIER_GLOBAL ? "G" : "P",
			                        recs[i].ref, mm_status_to_str(e->lifecycle),
			                        e->summary ? e->summary : "");
			b_cnt++;
		}
	}
	if (b_cnt == 0) pos += (size_t)snprintf(buf + pos, cap - pos, "  (none)\n");

	/* Bucket 5: Chronological Epoch Timeline (YYYY-MM) */
	pos += (size_t)snprintf(buf + pos, cap - pos, "\n=== Chronological Epoch Timeline ===\n");
	char months[32][8];
	size_t month_counts[32] = {0};
	const char *month_sample[32] = {0};
	size_t n_months = 0;
	for (size_t i = 0; i < n_recs; i++) {
		const mm_envelope_t *e = &recs[i].env;
		if (strlen(e->timestamp) >= 7) {
			char ym[8];
			memcpy(ym, e->timestamp, 7);
			ym[7] = '\0';
			ssize_t found = -1;
			for (size_t m = 0; m < n_months; m++) {
				if (!strcmp(months[m], ym)) { found = (ssize_t)m; break; }
			}
			if (found < 0 && n_months < 32) {
				found = (ssize_t)n_months++;
				strcpy(months[found], ym);
				month_counts[found] = 0;
				month_sample[found] = e->summary;
			}
			if (found >= 0) month_counts[found]++;
		}
	}
	for (size_t m = 0; m < n_months; m++) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "  • [%s] (%zu record%s) — %s\n",
		                        months[m], month_counts[m], month_counts[m] == 1 ? "" : "s",
		                        month_sample[m] ? month_sample[m] : "");
	}

	for (size_t i = 0; i < n_recs; i++) {
		mm_envelope_free(&recs[i].env);
	}
	free(recs);

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

static size_t count_keyword_overlap(const char *a, const char *b) {
	if (!a || !b) return 0;
	char copy[512];
	snprintf(copy, sizeof(copy), "%s", a);
	char *saveptr = NULL;
	char *tok = strtok_r(copy, " \t\r\n,.;:()[]{}\"'`/-", &saveptr);
	size_t hits = 0;
	while (tok) {
		size_t tlen = strlen(tok);
		if (tlen >= 4) {
			/* Case-insensitive substring check in b */
			for (const char *p = b; *p; p++) {
				if (strncasecmp(p, tok, tlen) == 0) {
					hits++;
					break;
				}
			}
		}
		tok = strtok_r(NULL, " \t\r\n,.;:()[]{}\"'`/-", &saveptr);
	}
	return hits;
}

static bool has_prohibitive_language(const char *s) {
	if (!s) return false;
	static const char *const prohib[] = {
		"never ", "must not", "do not", "don't", "forbidden", "prohibited", "avoid ", "disallow", NULL
	};
	for (int i = 0; prohib[i]; i++) {
		for (const char *p = s; *p; p++) {
			if (strncasecmp(p, prohib[i], strlen(prohib[i])) == 0) return true;
		}
	}
	return false;
}

int mm_evaluate_guardrail(mm_ctx_t *ctx, const char *intent_text,
                          const char *file_path, const char *symbol_name,
                          char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;

	mm_loaded_rec_t *recs = NULL;
	size_t n_recs = 0, cap_recs = 0;
	collect_active_tier_records(ctx, MM_TIER_GLOBAL, &recs, &n_recs, &cap_recs);
	if (ctx->has_project) {
		collect_active_tier_records(ctx, MM_TIER_PROJECT, &recs, &n_recs, &cap_recs);
	}

	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== [MEMORY GUARDRAIL WARNING — Pre-Flight Counterfactual Check] ===\n");
	size_t warnings = 0;

	char combined_target[1024];
	snprintf(combined_target, sizeof(combined_target), "%s %s %s",
	         intent_text ? intent_text : "",
	         file_path ? file_path : "",
	         symbol_name ? symbol_name : "");

	for (size_t i = 0; i < n_recs && warnings < 6; i++) {
		const mm_envelope_t *e = &recs[i].env;
		const char *sum = e->summary ? e->summary : "";
		bool file_match = (file_path && *file_path && e->ctx_path[0] && strstr(e->ctx_path, file_path));
		bool sym_match  = (symbol_name && *symbol_name && strstr(sum, symbol_name));
		size_t kw_hits  = count_keyword_overlap(combined_target, sum);

		/* 1. Contradicted memory warning */
		if (e->lifecycle == MM_STATUS_CONTRADICTED && (file_match || sym_match || kw_hits >= 2)) {
			warnings++;
			pos += (size_t)snprintf(buf + pos, cap - pos,
				"• [HIGH] [contradicted_memory] [Ref %s:%u] Contradicted hypothesis matches target: %s\n",
				recs[i].tier == MM_TIER_GLOBAL ? "G" : "P", recs[i].ref, sum);
		}

		/* 2. Failed or abandoned episode approach warning */
		for (size_t a = 0; a < e->n_approaches; a++) {
			const mm_approach_t *ap = &e->ep_approaches[a];
			if (!strcmp(ap->outcome, "failed") || !strcmp(ap->outcome, "abandoned")) {
				size_t ap_hits = count_keyword_overlap(combined_target, ap->approach);
				if (file_match || sym_match || ap_hits >= 2 || kw_hits >= 2) {
					warnings++;
					pos += (size_t)snprintf(buf + pos, cap - pos,
						"• [HIGH] [failed_approach] [Ref %s:%u] Previously %s approach: \"%s\" (Reason: %s)\n",
						recs[i].tier == MM_TIER_GLOBAL ? "G" : "P", recs[i].ref,
						ap->outcome, ap->approach, ap->reason[0] ? ap->reason : "see episode");
				}
			}
		}

		/* 3. Standing prohibitive directive warning */
		if ((e->standing || e->level == MM_LEVEL_L3) &&
		    e->lifecycle != MM_STATUS_CONTRADICTED &&
		    has_prohibitive_language(sum) &&
		    (file_match || sym_match || kw_hits >= 1)) {
			warnings++;
			pos += (size_t)snprintf(buf + pos, cap - pos,
				"• [HIGH] [standing_restriction] [Ref %s:%u] Standing restriction applies: %s\n",
				recs[i].tier == MM_TIER_GLOBAL ? "G" : "P", recs[i].ref, sum);
		}
	}

	/* 4. Check AST-Bound Lore Drift for target symbol or file */
	const char *check_target = (symbol_name && *symbol_name) ? symbol_name : file_path;
	if (check_target && *check_target) {
		char *drift_out = NULL;
		if (mm_lore_verify_drift(ctx, check_target, &drift_out, NULL) == 0 && drift_out) {
			if (strstr(drift_out, "STALE_SUSPECT")) {
				warnings++;
				pos += (size_t)snprintf(buf + pos, cap - pos,
					"• [MEDIUM] [stale_bound_invariant] Bound code invariant has drifted:\n%s",
					drift_out);
			}
			free(drift_out);
		}
	}

	for (size_t i = 0; i < n_recs; i++) {
		mm_envelope_free(&recs[i].env);
	}
	free(recs);

	if (warnings == 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "No counterfactual guardrail warnings triggered.\n");
	}
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 8. Two-Tier Persistent Query & Recall Engine
 * ============================================================================ */

static void parse_corm_query_lines(const char *stdout_buf, size_t stdout_len, mm_tier_t tier,
                                   mm_result_line_t **io_list, size_t *io_count, size_t *io_cap) {
	if (!stdout_buf || stdout_len == 0) return;
	const char *cur = stdout_buf;
	const char *end = stdout_buf + stdout_len;

	while (cur < end) {
		const char *nl = (const char *)memchr(cur, '\n', (size_t)(end - cur));
		const char *line_end = nl ? nl : end;
		if (cur < line_end && isdigit((unsigned char)*cur)) {
			char *end1 = NULL;
			unsigned long ref = strtoul(cur, &end1, 10);
			if (ref > 0 && end1 && end1 > cur && end1 < line_end && (*end1 == ' ' || *end1 == '\t')) {
				const char *p = end1;
				while (p < line_end && (*p == ' ' || *p == '\t')) p++;
				double score = 1.0;
				const char *rec_start = p;
				char *end2 = NULL;
				double maybe_score = strtod(p, &end2);
				if (end2 && end2 > p && end2 < line_end && (*end2 == ' ' || *end2 == '\t') &&
				    !(end2 - p == 10 && p[4] == '-' && p[7] == '-')) {
					score = maybe_score;
					rec_start = end2;
					while (rec_start < line_end && (*rec_start == ' ' || *rec_start == '\t')) rec_start++;
				}

				if (*io_count >= *io_cap) {
					*io_cap = (*io_cap == 0) ? 32 : (*io_cap * 2);
					*io_list = (mm_result_line_t *)realloc(*io_list, (*io_cap) * sizeof(mm_result_line_t));
				}
				mm_result_line_t *item = &(*io_list)[(*io_count)++];
				item->tier   = tier;
				item->ref    = (uint32_t)ref;
				item->score  = score;
				item->record = strdup("");
				(void)rec_start;
			}
		}
		cur = nl ? (nl + 1) : end;
	}
}

static void query_tier_axis(mm_ctx_t *ctx, mm_tier_t tier, const char *axis_expr,
                            const char *query, int limit,
                            mm_result_line_t **io_list, size_t *io_count, size_t *io_cap) {
	const char *tdir = mm_get_tier_dir(ctx, tier);
	if (!tdir) return;
	char db_file[PATH_MAX];
	snprintf(db_file, sizeof(db_file), "%s/mem.db", tdir);
	if (!file_exists(db_file)) return;

	if (!query || !*query) {
		uint32_t *refs = NULL;
		size_t n_refs = 0;
		if (mm_list_refs(ctx, tier, &refs, &n_refs) == 0 && refs) {
			for (size_t i = 0; i < n_refs; i++) {
				mm_envelope_t full_env;
				if (mm_get_envelope(ctx, tier, refs[i], &full_env) == 0) {
					char *json_str = NULL;
					if (mm_envelope_to_json(&full_env, &json_str, NULL) == 0 && json_str) {
						if (*io_count >= *io_cap) {
							*io_cap = (*io_cap == 0) ? 32 : (*io_cap * 2);
							*io_list = (mm_result_line_t *)realloc(*io_list, (*io_cap) * sizeof(mm_result_line_t));
						}
						mm_result_line_t *item = &(*io_list)[(*io_count)++];
						item->tier   = tier;
						item->ref    = refs[i];
						item->score  = 1.0;
						item->record = json_str;
					}
					mm_envelope_free(&full_env);
				}
			}
			free(refs);
		}
		return;
	}

	char fspec[128];
	build_filespec(ctx, fspec, sizeof(fspec));
	char lim_str[32];
	snprintf(lim_str, sizeof(lim_str), "%d", limit > 0 ? limit : 32);

	char q_arg[512];
	snprintf(q_arg, sizeof(q_arg), "--query=%s", query);

	char *stdout_buf = NULL;
	size_t stdout_len = 0;
	int rc = -1;

	if (ctx->prefer_daemon) {
		char sock[PATH_MAX];
		snprintf(sock, sizeof(sock), "%s/.cormd.sock", tdir);
		char body[1024];
		int blen = snprintf(body, sizeof(body), "%s\n--field=text\n%s\n--matched=1", axis_expr, q_arg);
		char hdr[256];
		snprintf(hdr, sizeof(hdr), "QUERY %u %s %s %d", ++ctx->req_counter, fspec, lim_str, blen);
		rc = send_cormd_request(sock, hdr, body, (size_t)blen, &stdout_buf, &stdout_len);
	}
	if (rc != 0) {
		char *argv[] = {
			ctx->corm_bin,
			(char *)"-X", (char *)axis_expr,
			(char *)"-g", (char *)".",
			(char *)"--field=text",
			q_arg,
			(char *)"--matched=1",
			fspec,
			(char *)"-t", lim_str,
			NULL
		};
		rc = exec_corm_cli(ctx, tdir, false, argv, NULL, 0, &stdout_buf, &stdout_len);
	}

	if (rc == 0 && stdout_buf) {
		parse_corm_query_lines(stdout_buf, stdout_len, tier, io_list, io_count, io_cap);
		for (size_t i = 0; i < *io_count; i++) {
			mm_result_line_t *it = &(*io_list)[i];
			if (it->tier == tier) {
				mm_envelope_t full_env;
				if (mm_get_envelope(ctx, tier, it->ref, &full_env) == 0) {
					char *json_str = NULL;
					if (mm_envelope_to_json(&full_env, &json_str, NULL) == 0 && json_str) {
						free(it->record);
						it->record = json_str;
					}
					mm_envelope_free(&full_env);
				}
			}
		}
	}
	free(stdout_buf);
}

static void free_result_lines(mm_result_line_t *list, size_t count) {
	if (!list) return;
	for (size_t i = 0; i < count; i++) free(list[i].record);
	free(list);
}

int mm_recall_persistent(mm_ctx_t *ctx, const mm_recall_req_t *req,
                         char **out_buf, size_t *out_len) {
	if (!ctx || !req || !out_buf) return -1;

	/* Overview mode on persistent memory */
	if (req->overview || (req->query && (!strcmp(req->query, "overview") || !strcmp(req->query, "*")))) {
		return mm_build_memory_overview(ctx, req->tier, req->limit > 0 ? req->limit : 10, out_buf, out_len);
	}

	/* Direct Zoom Ref mode */
	if (req->zoom_ref > 0) {
		mm_tier_t ztier = (req->zoom_tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
		                  (ctx->has_project ? MM_TIER_PROJECT : MM_TIER_GLOBAL);
		mm_envelope_t env;
		if (mm_get_envelope(ctx, ztier, req->zoom_ref, &env) != 0) {
			if (req->zoom_tier == MM_TIER_AUTO) {
				ztier = (ztier == MM_TIER_PROJECT) ? MM_TIER_GLOBAL : MM_TIER_PROJECT;
				if (mm_get_envelope(ctx, ztier, req->zoom_ref, &env) != 0) return -1;
			} else {
				return -1;
			}
		}
		if (req->json_output) {
			int rc = mm_envelope_to_json(&env, out_buf, out_len);
			mm_envelope_free(&env);
			return rc;
		}
		size_t cap = 8192;
		char *card = (char *)malloc(cap);
		if (!card) { mm_envelope_free(&env); return -1; }
		size_t pos = (size_t)snprintf(card, cap,
			"=== Persistent Memory [Ref %s:%u] (%s, %s%s) ===\n"
			"Timestamp: %s\n"
			"Type: %s\n"
			"Project: %s\n"
			"Summary: %s\n",
			ztier == MM_TIER_GLOBAL ? "G" : "P",
			req->zoom_ref,
			mm_level_to_str(env.level),
			mm_status_to_str(env.lifecycle),
			env.standing ? ", standing" : (env.is_anchor ? ", anchor" : ""),
			env.timestamp,
			env.type,
			env.ctx_project[0] ? env.ctx_project : "global",
			env.summary ? env.summary : "");

		if (env.n_approaches > 0) {
			pos += (size_t)snprintf(card + pos, cap - pos, "Approaches (%zu):\n", env.n_approaches);
			for (size_t i = 0; i < env.n_approaches; i++) {
				pos += (size_t)snprintf(card + pos, cap - pos, "  • [%s] %s%s%s\n",
				                        env.ep_approaches[i].outcome,
				                        env.ep_approaches[i].approach,
				                        env.ep_approaches[i].reason[0] ? " — " : "",
				                        env.ep_approaches[i].reason);
			}
		}
		if (env.n_findings > 0) {
			pos += (size_t)snprintf(card + pos, cap - pos, "Findings (%zu):\n", env.n_findings);
			for (size_t i = 0; i < env.n_findings; i++) {
				pos += (size_t)snprintf(card + pos, cap - pos, "  • %s\n", env.ep_findings[i]);
			}
		}
		if (env.n_history > 0) {
			pos += (size_t)snprintf(card + pos, cap - pos, "Lifecycle History (%zu):\n", env.n_history);
			for (size_t i = 0; i < env.n_history; i++) {
				pos += (size_t)snprintf(card + pos, cap - pos, "  • %s -> %s (%s): %s\n",
				                        mm_status_to_str(env.history[i].from),
				                        mm_status_to_str(env.history[i].to),
				                        env.history[i].timestamp,
				                        env.history[i].reason);
			}
		}
		if (env.n_evidence > 0 || req->include_evidence) {
			char *tree_buf = NULL;
			if (mm_traverse(ctx, ztier, req->zoom_ref, "down", 3, &tree_buf, NULL) == 0 && tree_buf) {
				pos += (size_t)snprintf(card + pos, cap - pos, "%s", tree_buf);
				free(tree_buf);
			}
		}
		mm_envelope_free(&env);
		*out_buf = card;
		if (out_len) *out_len = pos;
		return 0;
	}

	bool query_global  = (req->tier == MM_TIER_AUTO || req->tier == MM_TIER_BOTH || req->tier == MM_TIER_GLOBAL || !ctx->has_project);
	bool query_project = ctx->has_project && (req->tier == MM_TIER_AUTO || req->tier == MM_TIER_BOTH || req->tier == MM_TIER_PROJECT);

	int scan_limit = (req->limit > 0 ? req->limit * 4 : 32);
	mm_result_line_t *lex_list = NULL;
	size_t lex_count = 0, lex_cap = 0;
	mm_result_line_t *sem_list = NULL;
	size_t sem_count = 0, sem_cap = 0;

	if (query_project) {
		query_tier_axis(ctx, MM_TIER_PROJECT, "lemma", req->query, scan_limit, &lex_list, &lex_count, &lex_cap);
		if (ctx->semantic_search && req->query && *req->query) {
			query_tier_axis(ctx, MM_TIER_PROJECT, "sepal", req->query, scan_limit, &sem_list, &sem_count, &sem_cap);
		}
	}
	if (query_global) {
		query_tier_axis(ctx, MM_TIER_GLOBAL, "lemma", req->query, scan_limit, &lex_list, &lex_count, &lex_cap);
		if (ctx->semantic_search && req->query && *req->query) {
			query_tier_axis(ctx, MM_TIER_GLOBAL, "sepal", req->query, scan_limit, &sem_list, &sem_count, &sem_cap);
		}
	}

	mm_candidate_t *fused = NULL;
	size_t n_fused = 0;
	mm_fuse_rankings_rrf(lex_list, lex_count, sem_list, sem_count, 0, &fused, &n_fused);
	free_result_lines(lex_list, lex_count);
	free_result_lines(sem_list, sem_count);

	mm_filter_criteria_t crit;
	memset(&crit, 0, sizeof(crit));
	crit.mipmap     = MM_LEVEL_NONE;
	crit.query      = req->query;
	crit.limit      = req->limit > 0 ? req->limit : 10;
	crit.dedupe     = req->dedupe;
	crit.include_l0 = req->include_l0;
	crit.project    = ctx->has_project ? ctx->project_name : NULL;

	mm_candidate_t *ranked = NULL;
	size_t n_ranked = 0;
	mm_filter_and_enrich(fused, n_fused, &crit, &ranked, &n_ranked);
	mm_candidates_free(fused, n_fused);

	size_t cap = 4096 + n_ranked * 1536;
	char *buf = (char *)malloc(cap);
	if (!buf) {
		mm_candidates_free(ranked, n_ranked);
		return -1;
	}
	size_t pos = 0;

	if (req->json_output) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "[");
		for (size_t i = 0; i < n_ranked; i++) {
			char *jstr = NULL;
			if (mm_envelope_to_json(&ranked[i].envelope, &jstr, NULL) == 0 && jstr) {
				pos += (size_t)snprintf(buf + pos, cap - pos,
					"%s{\"tier\":\"%s\",\"ref\":%u,\"origin\":\"%s\",\"score\":%.6f,\"envelope\":%s}",
					(i > 0 ? "," : ""),
					ranked[i].tier == MM_TIER_GLOBAL ? "global" : "project",
					ranked[i].ref,
					mm_origin_to_str(ranked[i].origin),
					ranked[i].ranking_score,
					jstr);
				free(jstr);
			}
		}
		pos += (size_t)snprintf(buf + pos, cap - pos, "]");
	} else {
		pos += (size_t)snprintf(buf + pos, cap - pos,
			"=== Persistent Memory Results (%zu match%s) ===\n",
			n_ranked, n_ranked == 1 ? "" : "es");
		for (size_t i = 0; i < n_ranked; i++) {
			const mm_candidate_t *c = &ranked[i];
			const mm_envelope_t *e = &c->envelope;
			pos += (size_t)snprintf(buf + pos, cap - pos,
				"[Ref %s:%u] (%s, %s%s%s): %s\n"
				"  Origin: %s (score: %.6f)\n",
				c->tier == MM_TIER_GLOBAL ? "G" : "P",
				c->ref,
				mm_level_to_str(e->level),
				mm_status_to_str(e->lifecycle),
				e->standing ? ", standing" : (e->is_anchor ? ", anchor" : ""),
				e->ctx_project[0] ? " [project]" : "",
				e->summary ? e->summary : "",
				mm_origin_to_str(c->origin),
				c->ranking_score);
			if (req->include_evidence && e->n_evidence > 0) {
				char *tree_buf = NULL;
				if (mm_traverse(ctx, c->tier, c->ref, "down", 2, &tree_buf, NULL) == 0 && tree_buf) {
					pos += (size_t)snprintf(buf + pos, cap - pos, "%s", tree_buf);
					free(tree_buf);
				}
			}
		}
	}

	mm_candidates_free(ranked, n_ranked);
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}
