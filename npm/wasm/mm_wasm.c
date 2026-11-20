#include <ttypt/iso.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __wasm__
#include <stdio.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static inline FILE *mm_wasm_popen(const char *cmd, const char *mode) { (void)cmd; (void)mode; return NULL; }
static inline int mm_wasm_pclose(FILE *fp) { (void)fp; return -1; }
#define popen mm_wasm_popen
#define pclose mm_wasm_pclose
#define TTYPT_STELE_H
static inline struct tm *stl_gmtime_r(const time_t *timep, struct tm *result) {
	return gmtime_r(timep, result);
}
static inline char *stl_realpath(const char *path, char *resolved_path) {
	if (!path || !resolved_path) return NULL;
	strcpy(resolved_path, path);
	return resolved_path;
}
static inline int stl_mkdir(const char *path) { (void)path; return 0; }
static inline int stl_flock(int fd, int op) { (void)fd; (void)op; return 0; }
static inline int stl_setenv(const char *n, const char *v, int o) { (void)n; (void)v; (void)o; return 0; }
#define STL_LOCK_SH 1
#define STL_LOCK_EX 2
#define STL_LOCK_NB 4
#define STL_LOCK_UN 8
#endif

#include "../../src/libmm.c"
#include "../../src/mm_fusion.c"
#include "../../src/mm_code.c"
#include "../../src/mm_session.c"
#include "../../src/mm_ctx.c"

static mm_ctx_t *g_wasm_mm = NULL;

static mm_ctx_t *ensure_wasm_mm(void)
{
	if (!g_wasm_mm) {
		mm_options_t opts;
		memset(&opts, 0, sizeof(opts));
		opts.global_dir    = "/tmp/mm-wasm-global";
		opts.prefer_daemon = false;
		g_wasm_mm = mm_open(&opts);
	}
	return g_wasm_mm;
}

ISO_EXPORT(uint32_t, mm_wasm_record, const char *problem, const char *outcome, const char *subsystem, int standing)
{
	mm_ctx_t *ctx = ensure_wasm_mm();
	if (!ctx || !problem || !*problem) return 0;

	mm_record_req_t req;
	memset(&req, 0, sizeof(req));
	req.tier      = standing ? MM_TIER_GLOBAL : MM_TIER_AUTO;
	req.level     = MM_LEVEL_L2;
	req.problem   = problem;
	req.outcome   = outcome;
	req.type      = "rule";
	req.status    = MM_STATUS_SUPPORTED;
	req.standing  = (standing != 0);
	req.subsystem = (subsystem && *subsystem) ? subsystem : "wasm";

	mm_tier_t tier = MM_TIER_GLOBAL;
	uint32_t ref = 0;
	if (mm_record(ctx, &req, &tier, &ref) != 0) {
		return 0;
	}
	return ref;
}

ISO_EXPORT(size_t, mm_wasm_recall_json, const char *query, const char *subsystem, int limit, char *out_json, size_t out_sz)
{
	mm_ctx_t *ctx = ensure_wasm_mm();
	if (!ctx || !out_json || out_sz == 0) return 0;

	mm_recall_req_t req;
	memset(&req, 0, sizeof(req));
	req.scope       = MM_SCOPE_PERSISTENT;
	req.tier        = MM_TIER_BOTH;
	req.query       = query;
	req.subsystem   = (subsystem && *subsystem) ? subsystem : NULL;
	req.limit       = limit > 0 ? limit : 5;
	req.dedupe      = true;
	req.json_output = true;

	char *buf = NULL;
	size_t len = 0;
	if (mm_recall(ctx, &req, &buf, &len) != 0 || !buf) {
		return 0;
	}
	if (len >= out_sz) {
		len = out_sz - 1;
	}
	memcpy(out_json, buf, len);
	out_json[len] = '\0';
	free(buf);
	return len;
}

ISO_EXPORT(int, mm_wasm_review, uint32_t ref, const char *action, const char *reason)
{
	mm_ctx_t *ctx = ensure_wasm_mm();
	if (!ctx || ref == 0 || !action) return -1;

	mm_review_req_t req;
	memset(&req, 0, sizeof(req));
	req.tier   = MM_TIER_GLOBAL;
	req.ref    = ref;
	req.action = action;
	req.reason = (reason && *reason) ? reason : "WASM review transition";
	return mm_review(ctx, &req);
}
