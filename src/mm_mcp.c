#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Native C JSON-RPC 2.0 MCP Server (mm mcp)
 * ============================================================================ */

static bool mcp_extract_str(const char *json, const char *key, char *out, size_t cap) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return false;
	p += strlen(pat);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
	if (*p != '"') return false;
	p++;
	size_t j = 0;
	while (*p && *p != '"' && j + 1 < cap) {
		if (*p == '\\' && p[1]) {
			p++;
			if (*p == 'n') out[j++] = '\n';
			else if (*p == 't') out[j++] = '\t';
			else if (*p == 'r') out[j++] = '\r';
			else out[j++] = *p;
		} else {
			out[j++] = *p;
		}
		p++;
	}
	out[j] = '\0';
	return true;
}

static bool mcp_extract_raw_id(const char *json, char *out, size_t cap) {
	const char *p = strstr(json, "\"id\"");
	if (!p) return false;
	p += 4;
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
	size_t j = 0;
	if (*p == '"') {
		out[j++] = *p++;
		while (*p && *p != '"' && j + 2 < cap) {
			if (*p == '\\' && p[1]) out[j++] = *p++;
			out[j++] = *p++;
		}
		if (*p == '"') out[j++] = '"';
	} else {
		while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\r' && *p != '\n' && j + 1 < cap) {
			out[j++] = *p++;
		}
	}
	out[j] = '\0';
	return j > 0;
}

static int mcp_extract_int(const char *json, const char *key, int def_val) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return def_val;
	p += strlen(pat);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
	if (!isdigit((unsigned char)*p) && *p != '-') return def_val;
	return atoi(p);
}

static bool mcp_extract_bool(const char *json, const char *key, bool def_val) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return def_val;
	p += strlen(pat);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
	if (strncmp(p, "true", 4) == 0) return true;
	if (strncmp(p, "false", 5) == 0) return false;
	return def_val;
}

static void mcp_print_escaped(const char *s) {
	fputc('"', stdout);
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		switch (*p) {
		case '"':  fputs("\\\"", stdout); break;
		case '\\': fputs("\\\\", stdout); break;
		case '\n': fputs("\\n", stdout); break;
		case '\r': fputs("\\r", stdout); break;
		case '\t': fputs("\\t", stdout); break;
		default:
			if (*p >= 0x20) fputc(*p, stdout);
			break;
		}
	}
	fputc('"', stdout);
}

static void mcp_send_tool_text(const char *id_raw, const char *text, bool is_error) {
	printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"content\":[{\"type\":\"text\",\"text\":", id_raw);
	mcp_print_escaped(text ? text : "");
	printf("}],\"isError\":%s}}\n", is_error ? "true" : "false");
	fflush(stdout);
}

int mm_mcp_serve_stdio(mm_ctx_t *ctx) {
	if (!ctx) return -1;
	size_t cap = 65536;
	char *line = (char *)malloc(cap);
	if (!line) return -1;

	while (fgets(line, (int)cap, stdin)) {
		char method[128] = {0};
		char id_raw[64] = "null";
		bool has_id = mcp_extract_raw_id(line, id_raw, sizeof(id_raw));
		if (!mcp_extract_str(line, "method", method, sizeof(method))) {
			continue;
		}

		if (strcmp(method, "initialize") == 0) {
			printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{"
			       "\"protocolVersion\":\"2024-11-05\","
			       "\"capabilities\":{\"tools\":{}},"
			       "\"serverInfo\":{\"name\":\"mm\",\"version\":\"1.0.0\"}"
			       "}}\n", id_raw);
			fflush(stdout);
		} else if (strcmp(method, "notifications/initialized") == 0) {
			/* Notification: no response */
		} else if (strcmp(method, "ping") == 0 && has_id) {
			printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}\n", id_raw);
			fflush(stdout);
		} else if (strcmp(method, "tools/list") == 0 && has_id) {
			printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":["
			       "{\"name\":\"memory_recall\",\"description\":\"Multi-axis codebase, session, and two-tier persistent memory recall.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\",\"enum\":[\"all\",\"code\",\"session\",\"persistent\"]},\"tier\":{\"type\":\"string\",\"enum\":[\"auto\",\"global\",\"project\",\"both\"]},\"symbol\":{\"type\":\"string\"},\"file\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"trace\":{\"type\":\"string\"},\"direction\":{\"type\":\"string\"},\"depth\":{\"type\":\"integer\"},\"diff\":{\"type\":\"boolean\"},\"overview\":{\"type\":\"boolean\"},\"diagnose\":{\"type\":\"string\"},\"zoomRef\":{\"type\":\"integer\"},\"zoomTurn\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\"}}}},"
			       "{\"name\":\"memory_record\",\"description\":\"Record a persistent episode, decision, anchor, or global standing directive.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"problem\":{\"type\":\"string\"},\"type\":{\"type\":\"string\"},\"standing\":{\"type\":\"boolean\"},\"anchor\":{\"type\":\"boolean\"},\"tier\":{\"type\":\"string\",\"enum\":[\"auto\",\"global\",\"project\"]}},\"required\":[\"problem\"]}},"
			       "{\"name\":\"memory_review\",\"description\":\"Confirm, contradict, supersede, or rate a persistent memory reference.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"ref\":{\"type\":\"integer\"},\"action\":{\"type\":\"string\",\"enum\":[\"confirm\",\"contradict\",\"supersede\",\"helpful\",\"harmful\",\"obsolete\"]},\"tier\":{\"type\":\"string\",\"enum\":[\"auto\",\"global\",\"project\"]}},\"required\":[\"ref\",\"action\"]}}"
			       "]}}\n", id_raw);
			fflush(stdout);
		} else if (strcmp(method, "tools/call") == 0 && has_id) {
			char tool_name[64] = {0};
			mcp_extract_str(line, "name", tool_name, sizeof(tool_name));
			const char *args_sub = strstr(line, "\"arguments\"");
			const char *args_json = args_sub ? args_sub : line;

			if (strcmp(tool_name, "memory_recall") == 0) {
				mm_recall_req_t req;
				memset(&req, 0, sizeof(req));
				char scope_s[32] = {0}, tier_s[32] = {0};
				char query_s[512] = {0}, sym_s[128] = {0}, file_s[256] = {0};
				char kind_s[64] = {0}, trace_s[128] = {0}, dir_s[32] = {0}, diag_s[512] = {0};

				mcp_extract_str(args_json, "scope", scope_s, sizeof(scope_s));
				mcp_extract_str(args_json, "tier", tier_s, sizeof(tier_s));
				mcp_extract_str(args_json, "query", query_s, sizeof(query_s));
				mcp_extract_str(args_json, "symbol", sym_s, sizeof(sym_s));
				mcp_extract_str(args_json, "file", file_s, sizeof(file_s));
				mcp_extract_str(args_json, "kind", kind_s, sizeof(kind_s));
				mcp_extract_str(args_json, "trace", trace_s, sizeof(trace_s));
				mcp_extract_str(args_json, "direction", dir_s, sizeof(dir_s));
				mcp_extract_str(args_json, "diagnose", diag_s, sizeof(diag_s));

				if (!strcmp(scope_s, "code")) req.scope = MM_SCOPE_CODE;
				else if (!strcmp(scope_s, "session")) req.scope = MM_SCOPE_SESSION;
				else if (!strcmp(scope_s, "persistent")) req.scope = MM_SCOPE_PERSISTENT;
				else req.scope = MM_SCOPE_ALL;

				if (!strcmp(tier_s, "global")) req.tier = MM_TIER_GLOBAL;
				else if (!strcmp(tier_s, "project")) req.tier = MM_TIER_PROJECT;
				else req.tier = MM_TIER_BOTH;

				if (query_s[0]) req.query     = query_s;
				if (sym_s[0])   req.symbol    = sym_s;
				if (file_s[0])  req.file      = file_s;
				if (kind_s[0])  req.kind      = kind_s;
				if (trace_s[0]) req.trace     = trace_s;
				if (dir_s[0])   req.direction = dir_s;
				if (diag_s[0])  req.diagnose  = diag_s;

				req.depth     = mcp_extract_int(args_json, "depth", 2);
				req.diff      = mcp_extract_bool(args_json, "diff", false);
				req.overview  = mcp_extract_bool(args_json, "overview", false);
				req.zoom_ref  = (uint32_t)mcp_extract_int(args_json, "zoomRef", 0);
				req.zoom_turn = mcp_extract_int(args_json, "zoomTurn", 0);
				req.limit     = mcp_extract_int(args_json, "limit", 10);

				char *out = NULL;
				if (mm_recall(ctx, &req, &out, NULL) == 0 && out) {
					mcp_send_tool_text(id_raw, out, false);
					free(out);
				} else {
					mcp_send_tool_text(id_raw, "memory_recall failed", true);
				}
			} else if (strcmp(tool_name, "memory_record") == 0) {
				char prob_s[2048] = {0}, type_s[32] = {0}, tier_s[32] = {0};
				mcp_extract_str(args_json, "problem", prob_s, sizeof(prob_s));
				mcp_extract_str(args_json, "type", type_s, sizeof(type_s));
				mcp_extract_str(args_json, "tier", tier_s, sizeof(tier_s));

				mm_record_req_t req;
				memset(&req, 0, sizeof(req));
				req.problem  = prob_s;
				req.type     = type_s[0] ? type_s : "note";
				req.status   = MM_STATUS_SUPPORTED;
				req.standing = mcp_extract_bool(args_json, "standing", false);
				req.anchor   = mcp_extract_bool(args_json, "anchor", false);
				req.level    = MM_LEVEL_NONE;
				if (!strcmp(tier_s, "global")) req.tier = MM_TIER_GLOBAL;
				else if (!strcmp(tier_s, "project")) req.tier = MM_TIER_PROJECT;
				else req.tier = MM_TIER_AUTO;

				mm_tier_t out_tier = MM_TIER_PROJECT;
				uint32_t out_ref = 0;
				if (mm_record(ctx, &req, &out_tier, &out_ref) == 0) {
					char msg[256];
					snprintf(msg, sizeof(msg), "Recorded [Ref %s:%u] (%s)",
					         out_tier == MM_TIER_GLOBAL ? "G" : "P",
					         out_ref, out_tier == MM_TIER_GLOBAL ? "global" : "project");
					mcp_send_tool_text(id_raw, msg, false);
				} else {
					mcp_send_tool_text(id_raw, "memory_record failed", true);
				}
			} else if (strcmp(tool_name, "memory_review") == 0) {
				mm_review_req_t req;
				memset(&req, 0, sizeof(req));
				char act_s[32] = {0}, tier_s[32] = {0};
				mcp_extract_str(args_json, "action", act_s, sizeof(act_s));
				mcp_extract_str(args_json, "tier", tier_s, sizeof(tier_s));
				req.ref    = (uint32_t)mcp_extract_int(args_json, "ref", 0);
				req.action = act_s;
				if (!strcmp(tier_s, "global")) req.tier = MM_TIER_GLOBAL;
				else if (!strcmp(tier_s, "project")) req.tier = MM_TIER_PROJECT;
				else req.tier = MM_TIER_AUTO;

				if (mm_review(ctx, &req) == 0) {
					char msg[128];
					snprintf(msg, sizeof(msg), "Reviewed ref %u with action '%s'", req.ref, act_s);
					mcp_send_tool_text(id_raw, msg, false);
				} else {
					mcp_send_tool_text(id_raw, "memory_review failed", true);
				}
			} else {
				mcp_send_tool_text(id_raw, "Unknown tool", true);
			}
		}
	}

	free(line);
	return 0;
}
