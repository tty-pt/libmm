#include <ttypt/mm.h>
#include <ttypt/corm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>

/* ============================================================================
 * 1. Internal Context Structure & Filesystem Helpers
 * ============================================================================ */

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
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
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

static void discover_repo_root(const char *start_cwd, char *root_out, size_t cap) {
	root_out[0] = '\0';
	char cur[PATH_MAX];
	if (!realpath(start_cwd, cur)) {
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

static void append_resolved_axis_dir(char *axis_path, size_t cap, const char *dir_cand) {
	if (!dir_cand || !*dir_cand) return;
	char rp[PATH_MAX];
	const char *use = realpath(dir_cand, rp) ? rp : dir_cand;
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
		snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", realpath(env_corm, rp) ? rp : env_corm);
	} else {
		const char *roots[2] = { ctx->project_root[0] ? ctx->project_root : ctx->cwd, ctx->cwd };
		bool found = false;
		for (int i = 0; i < 2 && !found; i++) {
			char cand[PATH_MAX];
			snprintf(cand, sizeof(cand), "%s/external/libcorm/bin/corm", roots[i]);
			if (file_exists(cand)) {
				char rp[PATH_MAX];
				snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", realpath(cand, rp) ? rp : cand);
				found = true;
				break;
			}
			snprintf(cand, sizeof(cand), "%s/../libcorm/bin/corm", roots[i]);
			if (file_exists(cand)) {
				char rp[PATH_MAX];
				snprintf(ctx->corm_bin, sizeof(ctx->corm_bin), "%s", realpath(cand, rp) ? rp : cand);
				found = true;
				break;
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
			append_resolved_axis_dir(ctx->axis_path, sizeof(ctx->axis_path), tok);
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
		append_resolved_axis_dir(ctx->axis_path, sizeof(ctx->axis_path), cand);
	}
}

mm_ctx_t *mm_open(const mm_options_t *opts) {
	mm_ctx_t *ctx = (mm_ctx_t *)calloc(1, sizeof(mm_ctx_t));
	if (!ctx) return NULL;

	if (opts && opts->cwd && opts->cwd[0]) {
		if (!realpath(opts->cwd, ctx->cwd)) {
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
	if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
		flock(fd, LOCK_UN);
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

static int send_cormd_request(const char *sock_path, const char *header,
                              const void *body, size_t body_len,
                              char **out_stdout, size_t *out_len) {
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
}

static int exec_corm_cli(mm_ctx_t *ctx, const char *tier_dir, bool is_mutation,
                         char *const argv[], const void *stdin_bytes, size_t stdin_len,
                         char **out_stdout, size_t *out_len) {
	if (is_mutation && is_daemon_lock_held(tier_dir)) {
		return -1; /* Fail closed: database is leased by resident cormd */
	}

	char lock_path[PATH_MAX];
	snprintf(lock_path, sizeof(lock_path), "%s/.mm.lock", tier_dir);
	int lock_fd = open(lock_path, O_CREAT | O_RDWR, 0644);
	if (lock_fd >= 0) {
		flock(lock_fd, is_mutation ? LOCK_EX : LOCK_SH);
	}

	int in_pipe[2] = {-1, -1};
	int out_pipe[2] = {-1, -1};
	if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
		if (in_pipe[0] >= 0) { close(in_pipe[0]); close(in_pipe[1]); }
		if (out_pipe[0] >= 0) { close(out_pipe[0]); close(out_pipe[1]); }
		if (lock_fd >= 0) { flock(lock_fd, LOCK_UN); close(lock_fd); }
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(in_pipe[0]); close(in_pipe[1]);
		close(out_pipe[0]); close(out_pipe[1]);
		if (lock_fd >= 0) { flock(lock_fd, LOCK_UN); close(lock_fd); }
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
		setenv("CORM_QUIET", "1", 1);
		if (ctx->axis_path[0]) {
			setenv("CORM_AXIS_PATH", ctx->axis_path, 1);
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
			setenv("LD_LIBRARY_PATH", new_ld, 1);
		}
		if (ctx->semantic_search && ctx->auren_url[0]) {
			setenv("CORM_SEPAL_EMBED_URL", ctx->auren_url, 1);
			setenv("CORM_SEPAL_EMBED_MODEL", ctx->embed_model, 1);
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
		flock(lock_fd, LOCK_UN);
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

	/* Trim trailing newline added by corm -r -g if not part of binary */
	while (stdout_len > 0 && (stdout_buf[stdout_len - 1] == '\n' || stdout_buf[stdout_len - 1] == '\r')) {
		stdout_len--;
	}

	rc = mm_payload_decode(stdout_buf, stdout_len, out_env);
	free(stdout_buf);
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

int mm_record(mm_ctx_t *ctx, const mm_record_req_t *req,
              mm_tier_t *out_tier, uint32_t *out_ref) {
	if (!ctx || !req || !req->problem || !req->problem[0]) return -1;

	/* Route tier: standing=true or no project tier -> MM_TIER_GLOBAL */
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

	uint32_t ref = mm_next_ref(ctx, target_tier);
	int rc = mm_put_envelope(ctx, target_tier, ref, &env);
	mm_envelope_free(&env);
	if (rc != 0) return -1;

	/* If supersedes_ref > 0, mark the old record superseded */
	if (req->supersedes_ref > 0) {
		mm_envelope_t old_env;
		if (mm_get_envelope(ctx, target_tier, req->supersedes_ref, &old_env) == 0) {
			old_env.lifecycle = MM_STATUS_SUPERSEDED;
			old_env.superseded_by = ref;
			mm_put_envelope(ctx, target_tier, req->supersedes_ref, &old_env);
			mm_envelope_free(&old_env);
		}
	}

	if (out_tier) *out_tier = target_tier;
	if (out_ref)  *out_ref  = ref;
	return 0;
}

int mm_review(mm_ctx_t *ctx, const mm_review_req_t *req) {
	if (!ctx || !req || req->ref == 0 || !req->action) return -1;
	mm_tier_t tier = (req->tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
	                 (ctx->has_project ? MM_TIER_PROJECT : MM_TIER_GLOBAL);

	mm_envelope_t env;
	if (mm_get_envelope(ctx, tier, req->ref, &env) != 0) {
		/* Try the other tier if MM_TIER_AUTO was passed */
		if (req->tier == MM_TIER_AUTO) {
			tier = (tier == MM_TIER_PROJECT) ? MM_TIER_GLOBAL : MM_TIER_PROJECT;
			if (mm_get_envelope(ctx, tier, req->ref, &env) != 0) return -1;
		} else {
			return -1;
		}
	}

	if (!strcmp(req->action, "confirm") || !strcmp(req->action, "support")) {
		env.lifecycle = MM_STATUS_SUPPORTED;
	} else if (!strcmp(req->action, "contradict") || !strcmp(req->action, "refute")) {
		env.lifecycle = MM_STATUS_CONTRADICTED;
	} else if (!strcmp(req->action, "supersede")) {
		env.lifecycle = MM_STATUS_SUPERSEDED;
		if (req->superseded_by_ref > 0) env.superseded_by = req->superseded_by_ref;
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

	int rc = mm_put_envelope(ctx, tier, req->ref, &env);
	mm_envelope_free(&env);
	return rc;
}

/* ============================================================================
 * 4. Two-Tier Persistent Query & Recall Engine
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
				/* Check if next token is a float score followed by space */
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
		/* Hydrate full HTOK binary envelope for each matched ref so all fields are preserved */
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
	} else if (rc != 0 && (!query || !*query)) {
		/* Overview / empty query: enumerate all refs in tier */
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
		char *card = (char *)malloc(4096);
		if (!card) { mm_envelope_free(&env); return -1; }
		int n = snprintf(card, 4096,
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
		mm_envelope_free(&env);
		*out_buf = card;
		if (out_len) *out_len = (size_t)n;
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

	size_t cap = 4096 + n_ranked * 1024;
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
		}
	}

	mm_candidates_free(ranked, n_ranked);
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}
