#define _GNU_SOURCE
#include "ttypt/mm.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mm_distill_prompt(const char *conversation_text, char **out_prompt, size_t *out_len) {
	if (!out_prompt || !out_len) return -1;
	*out_prompt = NULL;
	*out_len = 0;

	const char *tmpl =
		"You are a cognitive memory distiller. Extract permanent engineering lore,\n"
		"architectural decisions, failed/working approaches, and standing rules from\n"
		"the following dialogue into a compact, LINE-DELIMITED key-value format (NO JSON).\n\n"
		"Format specification (each field on its own line):\n"
		"PROBLEM: <one-line summary of task, invariant, bug, or rule>\n"
		"TYPE: episode | decision | rule | note\n"
		"OUTCOME: worked | failed | abandoned (optional, for episodes)\n"
		"STANDING: true | false (true for standing operational directives or invariants)\n"
		"PROJECT: <project name, e.g. site, libmm, axil-auren>\n"
		"SUBSYSTEM: <subsystem or module name>\n"
		"PATH: <file path if applicable>\n"
		"APPROACH: <what was tried> | <worked|failed|abandoned> | <reason>\n"
		"FINDING: <key insight, causal observation, or exact error>\n"
		"CONSTRAINT: <hard prohibition or required invariant>\n\n"
		"Rules:\n"
		"- Repeat APPROACH, FINDING, CONSTRAINT lines if there are multiple.\n"
		"- Omit lines that do not apply.\n"
		"- Do NOT output markdown code fences, JSON, or explanatory preamble.\n\n"
		"=== Dialogue ===\n%s\n"
		"=== End Dialogue ===\n\n"
		"Distillation:\n";

	size_t conv_len = conversation_text ? strlen(conversation_text) : 0;
	size_t cap = strlen(tmpl) + conv_len + 128;
	char *buf = malloc(cap);
	if (!buf) return -1;

	int n = snprintf(buf, cap, tmpl, conversation_text ? conversation_text : "");
	if (n < 0) {
		free(buf);
		return -1;
	}
	*out_prompt = buf;
	*out_len = (size_t)n;
	return 0;
}

static char *trim_whitespace(char *str) {
	while (isspace((unsigned char)*str)) str++;
	if (!*str) return str;
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
	return str;
}

int mm_distill_parse(const char *distilled_text, mm_record_req_t *out_req,
                     char *storage_buf, size_t storage_cap) {
	if (!distilled_text || !out_req || !storage_buf || storage_cap < 512) return -1;
	memset(out_req, 0, sizeof(*out_req));

	/* Dynamic arrays pointing to storage_buf offsets */
	static const char *findings[32];
	static const char *constraints[32];
	static mm_approach_t approaches[16];
	size_t n_find = 0, n_const = 0, n_app = 0;

	out_req->level = MM_LEVEL_L1;
	out_req->status = MM_STATUS_TESTED;
	out_req->type = "episode";

	size_t s_off = 0;
	#define ALLOC_STR(src) ({ \
		char *_res = NULL; \
		size_t _slen = strlen(src); \
		if (s_off + _slen + 1 < storage_cap) { \
			_res = storage_buf + s_off; \
			memcpy(_res, src, _slen + 1); \
			s_off += _slen + 1; \
		} \
		_res; \
	})

	char *text_copy = strdup(distilled_text);
	if (!text_copy) return -1;

	char *saveptr = NULL;
	for (char *line = strtok_r(text_copy, "\r\n", &saveptr); line; line = strtok_r(NULL, "\r\n", &saveptr)) {
		line = trim_whitespace(line);
		if (!*line || *line == '#') continue;

		char *colon = strchr(line, ':');
		if (!colon) continue;
		*colon = '\0';
		char *key = trim_whitespace(line);
		char *val = trim_whitespace(colon + 1);
		if (!*val) continue;

		if (strcasecmp(key, "PROBLEM") == 0) {
			out_req->problem = ALLOC_STR(val);
		} else if (strcasecmp(key, "TYPE") == 0) {
			out_req->type = ALLOC_STR(val);
		} else if (strcasecmp(key, "OUTCOME") == 0) {
			out_req->outcome = ALLOC_STR(val);
		} else if (strcasecmp(key, "STANDING") == 0) {
			out_req->standing = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
			if (out_req->standing) out_req->tier = MM_TIER_GLOBAL;
		} else if (strcasecmp(key, "PROJECT") == 0) {
			out_req->project = ALLOC_STR(val);
		} else if (strcasecmp(key, "SUBSYSTEM") == 0) {
			out_req->subsystem = ALLOC_STR(val);
		} else if (strcasecmp(key, "PATH") == 0) {
			out_req->path = ALLOC_STR(val);
		} else if (strcasecmp(key, "FINDING") == 0) {
			if (n_find < 32) {
				char *alloc = ALLOC_STR(val);
				if (alloc) findings[n_find++] = alloc;
			}
		} else if (strcasecmp(key, "CONSTRAINT") == 0) {
			if (n_const < 32) {
				char *alloc = ALLOC_STR(val);
				if (alloc) constraints[n_const++] = alloc;
			}
		} else if (strcasecmp(key, "APPROACH") == 0) {
			if (n_app < 16) {
				char *p1 = strchr(val, '|');
				if (p1) {
					*p1++ = '\0';
					char *p2 = strchr(p1, '|');
					if (p2) *p2++ = '\0';
					char *app_desc = trim_whitespace(val);
					char *app_out = trim_whitespace(p1);
					char *app_reas = p2 ? trim_whitespace(p2) : "";
					snprintf(approaches[n_app].approach, sizeof(approaches[n_app].approach), "%s", app_desc);
					snprintf(approaches[n_app].outcome, sizeof(approaches[n_app].outcome), "%s", app_out);
					snprintf(approaches[n_app].reason, sizeof(approaches[n_app].reason), "%s", app_reas);
					n_app++;
				} else {
					snprintf(approaches[n_app].approach, sizeof(approaches[n_app].approach), "%s", val);
					snprintf(approaches[n_app].outcome, sizeof(approaches[n_app].outcome), "worked");
					approaches[n_app].reason[0] = '\0';
					n_app++;
				}
			}
		}
	}
	free(text_copy);

	if (!out_req->problem) return -1;

	if (n_find > 0) {
		out_req->findings = findings;
		out_req->n_findings = n_find;
	}
	if (n_const > 0) {
		out_req->constraints = constraints;
		out_req->n_constraints = n_const;
	}
	if (n_app > 0) {
		out_req->approaches = approaches;
		out_req->n_approaches = n_app;
	}

	return 0;
}

int mm_distill_commit(mm_ctx_t *ctx, const char *distilled_text,
                      mm_tier_t *out_tier, uint32_t *out_ref) {
	if (!ctx || !distilled_text || !out_tier || !out_ref) return -1;

	char storage[8192];
	mm_record_req_t req;
	if (mm_distill_parse(distilled_text, &req, storage, sizeof(storage)) != 0) {
		return -1;
	}

	return mm_record(ctx, &req, out_tier, out_ref);
}
