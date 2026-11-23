#include <ttypt/mm.h>
#include <ttypt/corm.h>
#include <ttypt/stele.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ============================================================================
 * 1. Enum Converters
 * ============================================================================ */

const char *mm_level_to_str(mm_level_t level) {
	switch (level) {
	case MM_LEVEL_L0: return "L0";
	case MM_LEVEL_L1: return "L1";
	case MM_LEVEL_L2: return "L2";
	case MM_LEVEL_L3: return "L3";
	default:          return "L2";
	}
}

mm_level_t mm_level_from_str(const char *str) {
	if (!str || !*str) return MM_LEVEL_L2;
	if (!strcmp(str, "L0") || !strcmp(str, "0")) return MM_LEVEL_L0;
	if (!strcmp(str, "L1") || !strcmp(str, "1")) return MM_LEVEL_L1;
	if (!strcmp(str, "L2") || !strcmp(str, "2")) return MM_LEVEL_L2;
	if (!strcmp(str, "L3") || !strcmp(str, "3")) return MM_LEVEL_L3;
	return MM_LEVEL_L2;
}

const char *mm_status_to_str(mm_epistemic_status_t status) {
	switch (status) {
	case MM_STATUS_OBSERVED:     return "observed";
	case MM_STATUS_HYPOTHESIZED: return "hypothesized";
	case MM_STATUS_TESTED:       return "tested";
	case MM_STATUS_SUPPORTED:    return "supported";
	case MM_STATUS_CONTRADICTED: return "contradicted";
	case MM_STATUS_SUPERSEDED:   return "superseded";
	default:                     return "supported";
	}
}

mm_epistemic_status_t mm_status_from_str(const char *str) {
	if (!str || !*str) return MM_STATUS_SUPPORTED;
	if (!strcmp(str, "observed"))     return MM_STATUS_OBSERVED;
	if (!strcmp(str, "hypothesized")) return MM_STATUS_HYPOTHESIZED;
	if (!strcmp(str, "tested"))       return MM_STATUS_TESTED;
	if (!strcmp(str, "supported"))    return MM_STATUS_SUPPORTED;
	if (!strcmp(str, "contradicted")) return MM_STATUS_CONTRADICTED;
	if (!strcmp(str, "superseded"))   return MM_STATUS_SUPERSEDED;
	return MM_STATUS_SUPPORTED;
}

const char *mm_origin_to_str(mm_origin_t origin) {
	switch (origin) {
	case MM_ORIGIN_LEMMA: return "lemma";
	case MM_ORIGIN_SEPAL: return "sepal";
	case MM_ORIGIN_BOTH:  return "both";
	default:              return "lemma";
	}
}

const char *mm_sym_kind_to_str(mm_sym_kind_t kind) {
	switch (kind) {
	case MM_SYM_FUNCTION:  return "Function";
	case MM_SYM_METHOD:    return "Method";
	case MM_SYM_CLASS:     return "Class";
	case MM_SYM_STRUCT:    return "Struct";
	case MM_SYM_INTERFACE: return "Interface";
	case MM_SYM_TYPE:      return "Type";
	case MM_SYM_ENUM:      return "Enum";
	case MM_SYM_MACRO:     return "Macro";
	case MM_SYM_ROUTE:     return "Route";
	case MM_SYM_VARIABLE:  return "Variable";
	default:               return "Function";
	}
}

mm_sym_kind_t mm_sym_kind_from_str(const char *str) {
	if (!str || !*str) return MM_SYM_FUNCTION;
	if (!strcasecmp(str, "Function"))  return MM_SYM_FUNCTION;
	if (!strcasecmp(str, "Method"))    return MM_SYM_METHOD;
	if (!strcasecmp(str, "Class"))     return MM_SYM_CLASS;
	if (!strcasecmp(str, "Struct"))    return MM_SYM_STRUCT;
	if (!strcasecmp(str, "Interface")) return MM_SYM_INTERFACE;
	if (!strcasecmp(str, "Type"))      return MM_SYM_TYPE;
	if (!strcasecmp(str, "Enum"))      return MM_SYM_ENUM;
	if (!strcasecmp(str, "Macro"))     return MM_SYM_MACRO;
	if (!strcasecmp(str, "Route"))     return MM_SYM_ROUTE;
	if (!strcasecmp(str, "Variable"))  return MM_SYM_VARIABLE;
	return MM_SYM_FUNCTION;
}

/* ============================================================================
 * 2. Envelope Lifecycle & Dynamic String Builder
 * ============================================================================ */

void mm_envelope_init(mm_envelope_t *env) {
	if (!env) return;
	memset(env, 0, sizeof(*env));
	env->level = MM_LEVEL_L2;
	env->lifecycle = MM_STATUS_SUPPORTED;
	strncpy(env->type, "note", sizeof(env->type) - 1);
	time_t now = time(NULL);
	struct tm tm_utc;
	stl_gmtime_r(&now, &tm_utc);
	strftime(env->timestamp, sizeof(env->timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

void mm_envelope_free(mm_envelope_t *env) {
	if (!env) return;
	free(env->summary);
	env->summary = NULL;
	for (size_t i = 0; i < env->n_tags; i++) {
		free(env->tags[i]);
		env->tags[i] = NULL;
	}
	env->n_tags = 0;
	for (size_t i = 0; i < env->n_constraints; i++) {
		free(env->constraints[i]);
		env->constraints[i] = NULL;
	}
	env->n_constraints = 0;
	free(env->ep_problem);
	env->ep_problem = NULL;
	free(env->ep_request);
	env->ep_request = NULL;
	free(env->ep_outcome);
	env->ep_outcome = NULL;
	for (size_t i = 0; i < env->n_findings; i++) {
		free(env->ep_findings[i]);
		env->ep_findings[i] = NULL;
	}
	env->n_findings = 0;
	for (size_t i = 0; i < env->n_diffs; i++) {
		free(env->ep_diffs[i]);
		env->ep_diffs[i] = NULL;
	}
	env->n_diffs = 0;
}

void mm_envelope_clone(mm_envelope_t *dst, const mm_envelope_t *src) {
	if (!dst || !src) return;
	*dst = *src;
	dst->summary = src->summary ? strdup(src->summary) : NULL;
	for (size_t i = 0; i < src->n_tags; i++) {
		dst->tags[i] = src->tags[i] ? strdup(src->tags[i]) : NULL;
	}
	for (size_t i = 0; i < src->n_constraints; i++) {
		dst->constraints[i] = src->constraints[i] ? strdup(src->constraints[i]) : NULL;
	}
	dst->ep_problem = src->ep_problem ? strdup(src->ep_problem) : NULL;
	dst->ep_request = src->ep_request ? strdup(src->ep_request) : NULL;
	dst->ep_outcome = src->ep_outcome ? strdup(src->ep_outcome) : NULL;
	for (size_t i = 0; i < src->n_findings; i++) {
		dst->ep_findings[i] = src->ep_findings[i] ? strdup(src->ep_findings[i]) : NULL;
	}
	for (size_t i = 0; i < src->n_diffs; i++) {
		dst->ep_diffs[i] = src->ep_diffs[i] ? strdup(src->ep_diffs[i]) : NULL;
	}
}

typedef struct {
	char  *buf;
	size_t len;
	size_t cap;
} mm_strbuf_t;

static void sb_init(mm_strbuf_t *sb) {
	sb->cap = 512;
	sb->len = 0;
	sb->buf = (char *)malloc(sb->cap);
	if (sb->buf) sb->buf[0] = '\0';
}

static void sb_ensure(mm_strbuf_t *sb, size_t extra) {
	if (!sb->buf) return;
	if (sb->len + extra + 1 <= sb->cap) return;
	size_t ncap = sb->cap * 2;
	while (ncap < sb->len + extra + 1) ncap *= 2;
	char *nb = (char *)realloc(sb->buf, ncap);
	if (!nb) return;
	sb->buf = nb;
	sb->cap = ncap;
}

static void sb_append_n(mm_strbuf_t *sb, const char *s, size_t n) {
	if (!sb->buf || !s || n == 0) return;
	sb_ensure(sb, n);
	memcpy(sb->buf + sb->len, s, n);
	sb->len += n;
	sb->buf[sb->len] = '\0';
}

static void sb_append(mm_strbuf_t *sb, const char *s) {
	if (!s) return;
	sb_append_n(sb, s, strlen(s));
}

static void sb_append_json_str(mm_strbuf_t *sb, const char *s) {
	if (!s) {
		sb_append(sb, "\"\"");
		return;
	}
	sb_append_n(sb, "\"", 1);
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		unsigned char c = *p;
		switch (c) {
		case '"':  sb_append(sb, "\\\""); break;
		case '\\': sb_append(sb, "\\\\"); break;
		case '\b': sb_append(sb, "\\b"); break;
		case '\f': sb_append(sb, "\\f"); break;
		case '\n': sb_append(sb, "\\n"); break;
		case '\r': sb_append(sb, "\\r"); break;
		case '\t': sb_append(sb, "\\t"); break;
		default:
			if (c < 0x20) {
				char hex[8];
				snprintf(hex, sizeof(hex), "\\u%04x", (unsigned int)c);
				sb_append(sb, hex);
			} else {
				sb_append_n(sb, (const char *)p, 1);
			}
			break;
		}
	}
	sb_append_n(sb, "\"", 1);
}

/* ============================================================================
 * 3. JSON Serializer & Parser for mm_envelope_t
 * ============================================================================ */

int mm_envelope_to_json(const mm_envelope_t *env, char **out_json, size_t *out_len) {
	if (!env || !out_json) return -1;
	mm_strbuf_t sb;
	sb_init(&sb);
	if (!sb.buf) return -1;

	sb_append(&sb, "{\"level\":");
	sb_append_json_str(&sb, mm_level_to_str(env->level));
	sb_append(&sb, ",\"lifecycle\":");
	sb_append_json_str(&sb, mm_status_to_str(env->lifecycle));
	sb_append(&sb, ",\"type\":");
	sb_append_json_str(&sb, env->type[0] ? env->type : "note");

	if (env->is_anchor) {
		sb_append(&sb, ",\"isAnchor\":true");
	}
	if (env->standing) {
		sb_append(&sb, ",\"standing\":true");
	}
	if (env->timestamp[0]) {
		sb_append(&sb, ",\"timestamp\":");
		sb_append_json_str(&sb, env->timestamp);
	}
	if (env->supersedes > 0) {
		char nbuf[64];
		snprintf(nbuf, sizeof(nbuf), ",\"supersedes\":%u", env->supersedes);
		sb_append(&sb, nbuf);
	}
	if (env->superseded_by > 0) {
		char nbuf[64];
		snprintf(nbuf, sizeof(nbuf), ",\"supersededBy\":%u", env->superseded_by);
		sb_append(&sb, nbuf);
	}
	if (env->n_evidence > 0) {
		sb_append(&sb, ",\"evidence\":[");
		for (size_t i = 0; i < env->n_evidence; i++) {
			char nbuf[32];
			snprintf(nbuf, sizeof(nbuf), "%s%u", (i > 0) ? "," : "", env->evidence[i]);
			sb_append(&sb, nbuf);
		}
		sb_append(&sb, "]");
	}
	if (env->summary && env->summary[0]) {
		sb_append(&sb, ",\"summary\":");
		sb_append_json_str(&sb, env->summary);
	}
	if (env->n_tags > 0) {
		sb_append(&sb, ",\"tags\":[");
		for (size_t i = 0; i < env->n_tags; i++) {
			if (i > 0) sb_append(&sb, ",");
			sb_append_json_str(&sb, env->tags[i] ? env->tags[i] : "");
		}
		sb_append(&sb, "]");
	}
	if (env->n_constraints > 0) {
		sb_append(&sb, ",\"constraints\":[");
		for (size_t i = 0; i < env->n_constraints; i++) {
			if (i > 0) sb_append(&sb, ",");
			sb_append_json_str(&sb, env->constraints[i] ? env->constraints[i] : "");
		}
		sb_append(&sb, "]");
	}

	if (env->ctx_project[0] || env->ctx_subsystem[0] || env->ctx_module[0] ||
	    env->ctx_path[0] || env->ctx_subject[0] || env->has_grid) {
		sb_append(&sb, ",\"context\":{");
		int first = 1;
		if (env->ctx_project[0]) {
			sb_append(&sb, "\"project\":");
			sb_append_json_str(&sb, env->ctx_project);
			first = 0;
		}
		if (env->ctx_subsystem[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"subsystem\":");
			sb_append_json_str(&sb, env->ctx_subsystem);
			first = 0;
		}
		if (env->ctx_module[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"module\":");
			sb_append_json_str(&sb, env->ctx_module);
			first = 0;
		}
		if (env->ctx_path[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"path\":");
			sb_append_json_str(&sb, env->ctx_path);
			first = 0;
		}
		if (env->ctx_subject[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"subject\":");
			sb_append_json_str(&sb, env->ctx_subject);
			first = 0;
		}
		if (env->has_grid) {
			if (!first) sb_append(&sb, ",");
			char gbuf[96];
			snprintf(gbuf, sizeof(gbuf), "\"grid\":{\"x\":%g,\"y\":%g,\"z\":%g}",
			         env->grid_x, env->grid_y, env->grid_z);
			sb_append(&sb, gbuf);
		}
		sb_append(&sb, "}");
	}

	if ((env->ep_problem && env->ep_problem[0]) ||
	    (env->ep_request && env->ep_request[0]) ||
	    (env->ep_outcome && env->ep_outcome[0]) ||
	    env->ep_session_id[0] || env->ep_turn_id > 0 ||
	    env->ep_run_id[0] || env->ep_leaf_id[0] || env->ep_tool_call_id[0] ||
	    env->n_approaches > 0 || env->n_findings > 0 || env->n_diffs > 0) {
		sb_append(&sb, ",\"episode\":{");
		int first = 1;
		if (env->ep_problem && env->ep_problem[0]) {
			sb_append(&sb, "\"problem\":");
			sb_append_json_str(&sb, env->ep_problem);
			first = 0;
		}
		if (env->ep_request && env->ep_request[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"request\":");
			sb_append_json_str(&sb, env->ep_request);
			first = 0;
		}
		if (env->ep_outcome && env->ep_outcome[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"outcome\":");
			sb_append_json_str(&sb, env->ep_outcome);
			first = 0;
		}
		if (env->ep_session_id[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"sessionId\":");
			sb_append_json_str(&sb, env->ep_session_id);
			first = 0;
		}
		if (env->ep_turn_id > 0) {
			if (!first) sb_append(&sb, ",");
			char tbuf[48];
			snprintf(tbuf, sizeof(tbuf), "\"turnId\":%d", env->ep_turn_id);
			sb_append(&sb, tbuf);
			first = 0;
		}
		if (env->ep_run_id[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"runId\":");
			sb_append_json_str(&sb, env->ep_run_id);
			first = 0;
		}
		if (env->ep_leaf_id[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"leafId\":");
			sb_append_json_str(&sb, env->ep_leaf_id);
			first = 0;
		}
		if (env->ep_tool_call_id[0]) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"toolCallId\":");
			sb_append_json_str(&sb, env->ep_tool_call_id);
			first = 0;
		}
		if (env->n_approaches > 0) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"approaches\":[");
			for (size_t i = 0; i < env->n_approaches; i++) {
				if (i > 0) sb_append(&sb, ",");
				sb_append(&sb, "{\"approach\":");
				sb_append_json_str(&sb, env->ep_approaches[i].approach);
				sb_append(&sb, ",\"outcome\":");
				sb_append_json_str(&sb, env->ep_approaches[i].outcome[0] ? env->ep_approaches[i].outcome : "worked");
				if (env->ep_approaches[i].reason[0]) {
					sb_append(&sb, ",\"reason\":");
					sb_append_json_str(&sb, env->ep_approaches[i].reason);
				}
				sb_append(&sb, "}");
			}
			sb_append(&sb, "]");
			first = 0;
		}
		if (env->n_findings > 0) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"findings\":[");
			for (size_t i = 0; i < env->n_findings; i++) {
				if (i > 0) sb_append(&sb, ",");
				sb_append_json_str(&sb, env->ep_findings[i] ? env->ep_findings[i] : "");
			}
			sb_append(&sb, "]");
			first = 0;
		}
		if (env->n_diffs > 0) {
			if (!first) sb_append(&sb, ",");
			sb_append(&sb, "\"diffs\":[");
			for (size_t i = 0; i < env->n_diffs; i++) {
				if (i > 0) sb_append(&sb, ",");
				sb_append_json_str(&sb, env->ep_diffs[i] ? env->ep_diffs[i] : "");
			}
			sb_append(&sb, "]");
			first = 0;
		}
		sb_append(&sb, "}");
	}

	if (env->n_history > 0) {
		sb_append(&sb, ",\"history\":[");
		for (size_t i = 0; i < env->n_history; i++) {
			if (i > 0) sb_append(&sb, ",");
			sb_append(&sb, "{\"from\":");
			sb_append_json_str(&sb, mm_status_to_str(env->history[i].from));
			sb_append(&sb, ",\"to\":");
			sb_append_json_str(&sb, mm_status_to_str(env->history[i].to));
			sb_append(&sb, ",\"timestamp\":");
			sb_append_json_str(&sb, env->history[i].timestamp);
			if (env->history[i].reason[0]) {
				sb_append(&sb, ",\"reason\":");
				sb_append_json_str(&sb, env->history[i].reason);
			}
			if (env->history[i].evidence_ref > 0) {
				char ebuf[48];
				snprintf(ebuf, sizeof(ebuf), ",\"evidenceRef\":%u", env->history[i].evidence_ref);
				sb_append(&sb, ebuf);
			}
			sb_append(&sb, "}");
		}
		sb_append(&sb, "]");
	}

	if (env->used_count > 0 || env->irrelevant_count > 0 || env->obsolete_count > 0) {
		char ubuf[128];
		snprintf(ubuf, sizeof(ubuf),
		         ",\"utility\":{\"usedCount\":%d,\"irrelevantCount\":%d,\"obsoleteCount\":%d}",
		         env->used_count, env->irrelevant_count, env->obsolete_count);
		sb_append(&sb, ubuf);
	}

	sb_append(&sb, "}");
	*out_json = sb.buf;
	if (out_len) *out_len = sb.len;
	return 0;
}

/* --- Minimal JSON Field Helpers --- */

static const char *find_json_key(const char *json, size_t len, const char *key) {
	size_t klen = strlen(key);
	const char *end = json + len;
	const char *p = json;
	while (p + klen + 2 < end) {
		const char *q = (const char *)memchr(p, '"', (size_t)(end - p));
		if (!q || q + klen + 2 >= end) return NULL;
		if (memcmp(q + 1, key, klen) == 0 && q[klen + 1] == '"') {
			const char *colon = q + klen + 2;
			while (colon < end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n'))
				colon++;
			if (colon < end && *colon == ':') {
				colon++;
				while (colon < end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n'))
					colon++;
				return colon;
			}
		}
		p = q + 1;
	}
	return NULL;
}

static char *parse_json_string_val(const char *val_start, const char *end, const char **after_out) {
	if (!val_start || val_start >= end || *val_start != '"') return NULL;
	mm_strbuf_t sb;
	sb_init(&sb);
	const char *p = val_start + 1;
	while (p < end) {
		char c = *p++;
		if (c == '"') {
			if (after_out) *after_out = p;
			return sb.buf;
		}
		if (c == '\\' && p < end) {
			char esc = *p++;
			switch (esc) {
			case '"':  sb_append_n(&sb, "\"", 1); break;
			case '\\': sb_append_n(&sb, "\\", 1); break;
			case '/':  sb_append_n(&sb, "/", 1); break;
			case 'b':  sb_append_n(&sb, "\b", 1); break;
			case 'f':  sb_append_n(&sb, "\f", 1); break;
			case 'n':  sb_append_n(&sb, "\n", 1); break;
			case 'r':  sb_append_n(&sb, "\r", 1); break;
			case 't':  sb_append_n(&sb, "\t", 1); break;
			case 'u':
				if (p + 4 <= end) {
					char hex[5] = { p[0], p[1], p[2], p[3], 0 };
					unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
					if (cp < 0x80) {
						char ch = (char)cp;
						sb_append_n(&sb, &ch, 1);
					}
					p += 4;
				}
				break;
			default:
				sb_append_n(&sb, &esc, 1);
				break;
			}
		} else {
			sb_append_n(&sb, &c, 1);
		}
	}
	free(sb.buf);
	return NULL;
}

static void extract_str_into(const char *json, size_t len, const char *key, char *dst, size_t dst_cap) {
	const char *v = find_json_key(json, len, key);
	if (!v) return;
	char *s = parse_json_string_val(v, json + len, NULL);
	if (s) {
		strncpy(dst, s, dst_cap - 1);
		dst[dst_cap - 1] = '\0';
		free(s);
	}
}

static char *extract_str_alloc(const char *json, size_t len, const char *key) {
	const char *v = find_json_key(json, len, key);
	if (!v) return NULL;
	return parse_json_string_val(v, json + len, NULL);
}

static bool extract_bool(const char *json, size_t len, const char *key, bool def_val) {
	const char *v = find_json_key(json, len, key);
	if (!v) return def_val;
	if ((size_t)(json + len - v) >= 4 && !memcmp(v, "true", 4)) return true;
	if ((size_t)(json + len - v) >= 5 && !memcmp(v, "false", 5)) return false;
	return def_val;
}

static long extract_int(const char *json, size_t len, const char *key, long def_val) {
	const char *v = find_json_key(json, len, key);
	if (!v) return def_val;
	char *endp = NULL;
	long val = strtol(v, &endp, 10);
	if (endp == v) return def_val;
	return val;
}

static size_t extract_u32_array(const char *json, size_t len, const char *key, uint32_t *out, size_t cap) {
	const char *v = find_json_key(json, len, key);
	if (!v || *v != '[') return 0;
	const char *end = json + len;
	const char *p = v + 1;
	size_t n = 0;
	while (p < end && *p != ']' && n < cap) {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
			p++;
		if (p >= end || *p == ']') break;
		char *endp = NULL;
		unsigned long val = strtoul(p, &endp, 10);
		if (endp == p) break;
		out[n++] = (uint32_t)val;
		p = endp;
	}
	return n;
}

static size_t extract_str_array(const char *json, size_t len, const char *key, char **out, size_t cap) {
	const char *v = find_json_key(json, len, key);
	if (!v || *v != '[') return 0;
	const char *end = json + len;
	const char *p = v + 1;
	size_t n = 0;
	while (p < end && *p != ']' && n < cap) {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
			p++;
		if (p >= end || *p == ']') break;
		if (*p != '"') break;
		const char *after = NULL;
		char *item = parse_json_string_val(p, end, &after);
		if (!item) break;
		out[n++] = item;
		p = after;
	}
	return n;
}

static const char *find_matching_brace(const char *start, const char *end) {
	if (!start || start >= end || *start != '{') return NULL;
	int depth = 0;
	bool in_str = false;
	for (const char *p = start; p < end; p++) {
		if (in_str) {
			if (*p == '\\' && p + 1 < end) p++;
			else if (*p == '"') in_str = false;
		} else {
			if (*p == '"') in_str = true;
			else if (*p == '{') depth++;
			else if (*p == '}') {
				depth--;
				if (depth == 0) return p;
			}
		}
	}
	return NULL;
}

static size_t extract_approaches(const char *json, size_t len, mm_approach_t *out, size_t cap) {
	const char *v = find_json_key(json, len, "approaches");
	if (!v || *v != '[') return 0;
	const char *end = json + len;
	const char *p = v + 1;
	size_t n = 0;
	while (p < end && *p != ']' && n < cap) {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
			p++;
		if (p >= end || *p == ']') break;
		if (*p != '{') break;
		const char *obj_end = find_matching_brace(p, end);
		if (!obj_end) break;
		size_t olen = (size_t)(obj_end - p + 1);
		memset(&out[n], 0, sizeof(out[n]));
		extract_str_into(p, olen, "approach", out[n].approach, sizeof(out[n].approach));
		extract_str_into(p, olen, "outcome", out[n].outcome, sizeof(out[n].outcome));
		extract_str_into(p, olen, "reason", out[n].reason, sizeof(out[n].reason));
		if (out[n].approach[0]) n++;
		p = obj_end + 1;
	}
	return n;
}

static size_t extract_history(const char *json, size_t len, mm_lifecycle_transition_t *out, size_t cap) {
	const char *v = find_json_key(json, len, "history");
	if (!v || *v != '[') return 0;
	const char *end = json + len;
	const char *p = v + 1;
	size_t n = 0;
	while (p < end && *p != ']' && n < cap) {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
			p++;
		if (p >= end || *p == ']') break;
		if (*p != '{') break;
		const char *obj_end = find_matching_brace(p, end);
		if (!obj_end) break;
		size_t olen = (size_t)(obj_end - p + 1);
		memset(&out[n], 0, sizeof(out[n]));
		char from_s[32] = {0}, to_s[32] = {0};
		extract_str_into(p, olen, "from", from_s, sizeof(from_s));
		extract_str_into(p, olen, "to", to_s, sizeof(to_s));
		out[n].from = mm_status_from_str(from_s);
		out[n].to   = mm_status_from_str(to_s);
		extract_str_into(p, olen, "timestamp", out[n].timestamp, sizeof(out[n].timestamp));
		extract_str_into(p, olen, "reason", out[n].reason, sizeof(out[n].reason));
		out[n].evidence_ref = (uint32_t)extract_int(p, olen, "evidenceRef", 0);
		n++;
		p = obj_end + 1;
	}
	return n;
}

static bool is_iso_date_prefix(const char *s, size_t len) {
	if (len < 11) return false;
	return isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
	       isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) &&
	       s[4] == '-' &&
	       isdigit((unsigned char)s[5]) && isdigit((unsigned char)s[6]) &&
	       s[7] == '-' &&
	       isdigit((unsigned char)s[8]) && isdigit((unsigned char)s[9]) &&
	       s[10] == ':';
}

int mm_envelope_from_json(const char *json_or_raw, size_t len, mm_envelope_t *out_env) {
	if (!json_or_raw || !out_env) return -1;
	if (len == 0) len = strlen(json_or_raw);
	mm_envelope_init(out_env);

	const char *body = json_or_raw;
	size_t blen = len;
	if (is_iso_date_prefix(body, blen)) {
		snprintf(out_env->timestamp, sizeof(out_env->timestamp), "%.*sT00:00:00Z", 10, body);
		body += 11;
		blen -= 11;
	}

	while (blen > 0 && (*body == ' ' || *body == '\t' || *body == '\r' || *body == '\n')) {
		body++;
		blen--;
	}

	if (blen == 0 || *body != '{') {
		out_env->summary = (char *)malloc(blen + 1);
		if (!out_env->summary) return -1;
		memcpy(out_env->summary, body, blen);
		out_env->summary[blen] = '\0';
		return 0;
	}

	char lvl_str[16] = {0};
	extract_str_into(body, blen, "level", lvl_str, sizeof(lvl_str));
	if (lvl_str[0]) out_env->level = mm_level_from_str(lvl_str);

	char lc_str[32] = {0};
	extract_str_into(body, blen, "lifecycle", lc_str, sizeof(lc_str));
	if (lc_str[0]) out_env->lifecycle = mm_status_from_str(lc_str);

	extract_str_into(body, blen, "type", out_env->type, sizeof(out_env->type));
	extract_str_into(body, blen, "timestamp", out_env->timestamp, sizeof(out_env->timestamp));

	out_env->is_anchor = extract_bool(body, blen, "isAnchor", false);
	out_env->standing  = extract_bool(body, blen, "standing", false);

	out_env->supersedes    = (uint32_t)extract_int(body, blen, "supersedes", 0);
	out_env->superseded_by = (uint32_t)extract_int(body, blen, "supersededBy", 0);
	out_env->n_evidence    = extract_u32_array(body, blen, "evidence", out_env->evidence, MM_MAX_EVIDENCE);

	out_env->summary = extract_str_alloc(body, blen, "summary");
	if (!out_env->summary) {
		out_env->summary = extract_str_alloc(body, blen, "text");
	}

	out_env->n_tags = extract_str_array(body, blen, "tags", out_env->tags, MM_MAX_TAGS);
	out_env->n_constraints = extract_str_array(body, blen, "constraints", out_env->constraints, MM_MAX_CONSTRAINTS);

	extract_str_into(body, blen, "project", out_env->ctx_project, sizeof(out_env->ctx_project));
	extract_str_into(body, blen, "subsystem", out_env->ctx_subsystem, sizeof(out_env->ctx_subsystem));
	extract_str_into(body, blen, "module", out_env->ctx_module, sizeof(out_env->ctx_module));
	extract_str_into(body, blen, "path", out_env->ctx_path, sizeof(out_env->ctx_path));
	extract_str_into(body, blen, "subject", out_env->ctx_subject, sizeof(out_env->ctx_subject));

	out_env->ep_problem = extract_str_alloc(body, blen, "problem");
	out_env->ep_request = extract_str_alloc(body, blen, "request");
	out_env->ep_outcome = extract_str_alloc(body, blen, "outcome");
	extract_str_into(body, blen, "sessionId", out_env->ep_session_id, sizeof(out_env->ep_session_id));
	out_env->ep_turn_id = (int)extract_int(body, blen, "turnId", 0);
	extract_str_into(body, blen, "runId", out_env->ep_run_id, sizeof(out_env->ep_run_id));
	extract_str_into(body, blen, "leafId", out_env->ep_leaf_id, sizeof(out_env->ep_leaf_id));
	extract_str_into(body, blen, "toolCallId", out_env->ep_tool_call_id, sizeof(out_env->ep_tool_call_id));

	out_env->n_approaches = extract_approaches(body, blen, out_env->ep_approaches, MM_MAX_APPROACHES);
	out_env->n_findings   = extract_str_array(body, blen, "findings", out_env->ep_findings, MM_MAX_FINDINGS);
	out_env->n_diffs      = extract_str_array(body, blen, "diffs", out_env->ep_diffs, MM_MAX_DIFFS);
	out_env->n_history    = extract_history(body, blen, out_env->history, MM_MAX_HISTORY);

	out_env->used_count       = (int)extract_int(body, blen, "usedCount", 0);
	out_env->irrelevant_count = (int)extract_int(body, blen, "irrelevantCount", 0);
	out_env->obsolete_count   = (int)extract_int(body, blen, "obsoleteCount", 0);

	if (!out_env->summary && out_env->ep_problem) {
		out_env->summary = strdup(out_env->ep_problem);
	}

	return 0;
}

/* ============================================================================
 * 4. HTOK v4 Binary Payload Encode & Decode
 * ============================================================================ */

int mm_payload_encode(const mm_envelope_t *env,
                      uint8_t **out_bytes, size_t *out_len,
                      char **out_json, size_t *out_json_len) {
	if (!env) return -1;
	char *json_str = NULL;
	size_t json_len = 0;
	if (mm_envelope_to_json(env, &json_str, &json_len) != 0) {
		return -1;
	}

	if (out_bytes && out_len) {
#ifdef __wasm__
		uint8_t *copy = (uint8_t *)malloc(json_len + 1);
		if (!copy) {
			free(json_str);
			return -1;
		}
		memcpy(copy, json_str, json_len + 1);
		*out_bytes = copy;
		*out_len = json_len;
#else
		uint8_t *htok_buf = NULL;
		size_t htok_len = 0;
		if (corm_htok_pack(json_str, json_len, &htok_buf, &htok_len) != 0 || !htok_buf) {
			free(json_str);
			return -1;
		}
		uint8_t *combined = (uint8_t *)malloc(htok_len + 1 + json_len + 1);
		if (combined) {
			memcpy(combined, htok_buf, htok_len);
			combined[htok_len] = 0;
			memcpy(combined + htok_len + 1, json_str, json_len + 1);
			free(htok_buf);
			*out_bytes = combined;
			*out_len = htok_len + 1 + json_len;
		} else {
			*out_bytes = htok_buf;
			*out_len = htok_len;
		}
#endif
	}

	if (out_json) {
		*out_json = json_str;
		if (out_json_len) *out_json_len = json_len;
	} else {
		free(json_str);
	}
	return 0;
}

static bool has_htok_magic(const uint8_t *p, size_t rem) {
	if (rem < 5) return false;
	if (p[0] == 0x4b && p[1] == 0x4f && p[2] == 0x54 && p[3] == 0x48) return true;
	if (p[0] == 'H' && p[1] == 'T' && p[2] == 'O' && p[3] == 'K') return true;
	return false;
}

#ifndef __wasm__
typedef int (*mm_lemma_tok_decode_fn)(const void *vocab, const uint16_t *tokens, size_t n_tokens,
                                      char *dst, size_t dst_cap, size_t *bytes_written);

static char *mm_try_decode_token_field(const uint8_t *bytes, size_t len) {
	static void *s_lemma_handle = NULL;
	static mm_lemma_tok_decode_fn s_decode_fn = NULL;
	static int s_probed = 0;

	if (!s_probed) {
		s_probed = 1;
		s_lemma_handle = stl_dlopen("liblemma.so", 0);
		if (!s_lemma_handle) s_lemma_handle = stl_dlopen("/home/quirinpa/.local/lib/liblemma.so", 0);
		if (!s_lemma_handle) s_lemma_handle = stl_dlopen("/usr/local/lib/liblemma.so", 0);
		if (!s_lemma_handle) s_lemma_handle = stl_dlopen("/usr/lib/liblemma.so", 0);
		if (s_lemma_handle) {
			s_decode_fn = (mm_lemma_tok_decode_fn)stl_dlsym(s_lemma_handle, "lemma_tok_decode");
		}
	}
	if (!s_decode_fn) return NULL;

	uint16_t *tokens = (uint16_t *)malloc(len * sizeof(uint16_t));
	if (!tokens) return NULL;
	size_t n_tokens = 0;
	size_t i = 0;
	while (i < len) {
		uint8_t b = bytes[i++];
		if (!(b & 0x80)) {
			tokens[n_tokens++] = (uint16_t)b;
		} else {
			if (i >= len) break;
			uint8_t b2 = bytes[i++];
			tokens[n_tokens++] = (uint16_t)((b & 0x7f) | (b2 << 7));
		}
	}

	size_t cap = n_tokens * 32 + 64;
	char *out = (char *)malloc(cap);
	if (!out) { free(tokens); return NULL; }
	size_t bw = 0;
	if (s_decode_fn(NULL, tokens, n_tokens, out, cap, &bw) == 0 && bw > 0) {
		free(tokens);
		return out;
	}
	free(tokens);
	free(out);
	return NULL;
}
#endif

int mm_payload_decode(const void *data, size_t len, mm_envelope_t *out_env) {
	if (!data || !out_env) return -1;
	const uint8_t *bytes = (const uint8_t *)data;
	if (len == 0) len = strlen((const char *)data);

	char date_hint[11] = {0};
	const uint8_t *payload = bytes;
	size_t plen = len;

	if (is_iso_date_prefix((const char *)bytes, len)) {
		memcpy(date_hint, bytes, 10);
		date_hint[10] = '\0';
		payload = bytes + 11;
		plen = len - 11;
	}

	if (has_htok_magic(payload, plen)) {
		for (size_t i = 12; i + 2 < plen; i++) {
			if (payload[i] == 0xaa && payload[i + 1] == 0x00 && payload[i + 2] == '{') {
				const char *jptr = (const char *)(payload + i + 2);
				size_t jlen = plen - (i + 2);
				int rc = mm_envelope_from_json(jptr, jlen, out_env);
				if (rc == 0) {
					if (!out_env->timestamp[0] && date_hint[0]) {
						snprintf(out_env->timestamp, sizeof(out_env->timestamp), "%sT00:00:00Z", date_hint);
					}
					return 0;
				}
			}
		}
#ifndef __wasm__
		char *unpacked_json = NULL;
		size_t unpacked_len = 0;
		if (corm_htok_unpack(payload, plen, &unpacked_json, &unpacked_len) == 0 && unpacked_json) {
			int rc = mm_envelope_from_json(unpacked_json, unpacked_len, out_env);
			free(unpacked_json);
			if (rc == 0) {
				if (!out_env->timestamp[0] && date_hint[0]) {
					snprintf(out_env->timestamp, sizeof(out_env->timestamp), "%sT00:00:00Z", date_hint);
				}
				/* Direct token decoding for HTOK v4 token-packed fields */
				size_t offset = 12;
				uint8_t flags = payload[11];
				if (flags & 0x01) { while (offset < plen && (payload[offset++] & 0x80)); }
				if (flags & 0x02) { while (offset < plen && (payload[offset++] & 0x80)); }
				if (flags & 0x04) {
					uint64_t count = 0; size_t s = 0;
					while (offset < plen) {
						uint8_t b = payload[offset++];
						count |= (uint64_t)(b & 0x7f) << s;
						if (!(b & 0x80)) break;
						s += 7;
					}
					for (size_t k = 0; k < count && offset < plen; k++) {
						while (offset < plen && (payload[offset++] & 0x80));
					}
				}
				uint64_t fc = 0; size_t s = 0;
				while (offset < plen) {
					uint8_t b = payload[offset++];
					fc |= (uint64_t)(b & 0x7f) << s;
					if (!(b & 0x80)) break;
					s += 7;
				}
				for (size_t f = 0; f < fc && offset < plen; f++) {
					uint8_t varTag = payload[offset++];
					uint8_t typeTag = varTag & 0x07;
					uint8_t fieldId = (varTag >> 3) & 0x1f;
					if (fieldId == 31) {
						if (offset >= plen) break;
						uint8_t nlen = payload[offset++];
						offset += nlen;
					}
					uint64_t dlen = 0; s = 0;
					while (offset < plen) {
						uint8_t b = payload[offset++];
						dlen |= (uint64_t)(b & 0x7f) << s;
						if (!(b & 0x80)) break;
						s += 7;
					}
					if (typeTag == 1 && offset + dlen <= plen) {
						if (fieldId == 0) { // summary
							char *decoded = mm_try_decode_token_field(payload + offset, (size_t)dlen);
							if (decoded) {
								free(out_env->summary);
								out_env->summary = decoded;
							}
						} else if (fieldId == 2) { // ctx_project
							char *decoded = mm_try_decode_token_field(payload + offset, (size_t)dlen);
							if (decoded) {
								snprintf(out_env->ctx_project, sizeof(out_env->ctx_project), "%s", decoded);
								free(decoded);
							}
						} else if (fieldId == 3) { // ctx_subsystem
							char *decoded = mm_try_decode_token_field(payload + offset, (size_t)dlen);
							if (decoded) {
								snprintf(out_env->ctx_subsystem, sizeof(out_env->ctx_subsystem), "%s", decoded);
								free(decoded);
							}
						}
					}
					offset += (size_t)dlen;
				}
				return 0;
			}
		}
#endif
	}

	return mm_envelope_from_json((const char *)bytes, len, out_env);
}

/* ============================================================================
 * 5. Temporal Window Calculation (libjoint compatibility)
 * ============================================================================ */

bool mm_window_for_level(mm_level_t level, int64_t now_epoch_sec,
                         char since_out[16], char until_out[16]) {
	if (!since_out || !until_out) return false;
	since_out[0] = '\0';
	until_out[0] = '\0';

	time_t t = (now_epoch_sec > 0) ? (time_t)now_epoch_sec : time(NULL);
	struct tm tm_now;
#ifdef _WIN32
	localtime_s(&tm_now, &t);
#else
	localtime_r(&t, &tm_now);
#endif

	if (level == MM_LEVEL_L1) {
		struct tm tm_a = tm_now;
		struct tm tm_b = tm_now;
		tm_b.tm_mday += 1;
		mktime(&tm_b);
		strftime(since_out, 16, "%Y-%m-%d", &tm_a);
		strftime(until_out, 16, "%Y-%m-%d", &tm_b);
		return true;
	}
	if (level == MM_LEVEL_L2) {
		struct tm tm_a = tm_now;
		tm_a.tm_mday = 1;
		struct tm tm_b = tm_now;
		tm_b.tm_mday = 1;
		tm_b.tm_mon += 1;
		mktime(&tm_b);
		strftime(since_out, 16, "%Y-%m-%d", &tm_a);
		strftime(until_out, 16, "%Y-%m-%d", &tm_b);
		return true;
	}
	return false;
}
