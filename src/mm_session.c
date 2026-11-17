#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

/* Forward declaration from mm_ctx.c */
int mm_recall_persistent(mm_ctx_t *ctx, const mm_recall_req_t *req,
                         char **out_buf, size_t *out_len);

/* Access internal code_index of mm_ctx_t via helper in mm_session.c */
struct mm_ctx_prefix {
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

static mm_code_index_t *ensure_code_index(mm_ctx_t *ctx) {
	if (!ctx) return NULL;
	struct mm_ctx_prefix *p = (struct mm_ctx_prefix *)ctx;
	if (!p->code_index_loaded) {
		char manifest[PATH_MAX];
		const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
		if (tdir) {
			snprintf(manifest, sizeof(manifest), "%s/code_manifest.tsv", tdir);
			struct stat st;
			if (stat(manifest, &st) == 0 && st.st_size > 0) {
				if (mm_code_load_manifest(&p->code_index, manifest) == 0 && p->code_index.n_symbols > 0) {
					p->code_index_loaded = true;
					return &p->code_index;
				}
			}
		}
		mm_code_index_dir(&p->code_index, mm_get_project_root(ctx));
		if (tdir) {
			mm_code_save_manifest(&p->code_index, manifest);
		}
		p->code_index_loaded = true;
	}
	return &p->code_index;
}

/* ============================================================================
 * 1. Session Transcript Ingestion & Recall
 * ============================================================================ */

static void sanitize_session_id(const char *in, char *out, size_t cap) {
	size_t j = 0;
	for (const char *p = in ? in : "default"; *p && j + 1 < cap; p++) {
		if (isalnum((unsigned char)*p) || *p == '-' || *p == '_') {
			out[j++] = *p;
		}
	}
	if (j == 0) strcpy(out, "default");
	else out[j] = '\0';
}

static void write_json_escaped(FILE *fp, const char *s) {
	fputc('"', fp);
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		switch (*p) {
		case '"':  fputs("\\\"", fp); break;
		case '\\': fputs("\\\\", fp); break;
		case '\n': fputs("\\n", fp); break;
		case '\r': fputs("\\r", fp); break;
		case '\t': fputs("\\t", fp); break;
		default:
			if (*p >= 0x20) fputc(*p, fp);
			break;
		}
	}
	fputc('"', fp);
}

int mm_session_ingest_turn(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                           int turn_idx, const char *role, const char *text) {
	if (!ctx || !text) return -1;
	mm_tier_t eff_tier = tier;
	if (eff_tier == MM_TIER_AUTO || eff_tier == MM_TIER_BOTH) {
		eff_tier = mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL;
	}
	const char *tdir = mm_get_tier_dir(ctx, eff_tier);
	if (!tdir) return -1;

	char sid[128];
	sanitize_session_id(session_id, sid, sizeof(sid));

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/sessions/%s.jsonl", tdir, sid);

	FILE *fp = fopen(path, "a");
	if (!fp) return -1;

	time_t now = time(NULL);
	struct tm tm_utc;
	gmtime_r(&now, &tm_utc);
	char ts[32];
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

	fprintf(fp, "{\"session\":");
	write_json_escaped(fp, sid);
	fprintf(fp, ",\"turn\":%d,\"role\":", turn_idx > 0 ? turn_idx : 1);
	write_json_escaped(fp, (role && *role) ? role : "user");
	fprintf(fp, ",\"timestamp\":\"%s\",\"text\":", ts);
	write_json_escaped(fp, text);
	fprintf(fp, "}\n");
	fclose(fp);
	return 0;
}

static bool extract_jsonl_field_str(const char *line, const char *key, char *out, size_t cap) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	const char *p = strstr(line, pat);
	if (!p) return false;
	p += strlen(pat);
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

static int extract_jsonl_field_int(const char *line, const char *key, int def_val) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char *p = strstr(line, pat);
	if (!p) return def_val;
	p += strlen(pat);
	while (*p == ' ') p++;
	return atoi(p);
}

static bool text_matches_query_ci(const char *text, const char *query) {
	if (!query || !*query) return true;
	if (!text || !*text) return false;
	/* Match if any space-separated token (>=3 chars) or full query appears in text */
	size_t qlen = strlen(query);
	for (const char *p = text; *p; p++) {
		if (strncasecmp(p, query, qlen) == 0) return true;
	}
	char qcopy[256];
	snprintf(qcopy, sizeof(qcopy), "%s", query);
	char *saveptr = NULL;
	char *tok = strtok_r(qcopy, " \t\r\n", &saveptr);
	int matched_tokens = 0;
	int total_tokens = 0;
	while (tok) {
		size_t tlen = strlen(tok);
		if (tlen >= 3) {
			total_tokens++;
			for (const char *p = text; *p; p++) {
				if (strncasecmp(p, tok, tlen) == 0) {
					matched_tokens++;
					break;
				}
			}
		}
		tok = strtok_r(NULL, " \t\r\n", &saveptr);
	}
	return total_tokens > 0 && matched_tokens == total_tokens;
}

static void scan_session_dir(const char *sess_dir, const char *session_filter,
                             const char *query, int zoom_turn, int line_start, int line_end,
                             int limit, int *io_matches,
                             char **buf, size_t *pos, size_t *cap) {
	DIR *d = opendir(sess_dir);
	if (!d) return;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && *io_matches < limit) {
		const char *ext = strrchr(ent->d_name, '.');
		if (!ext || strcmp(ext, ".jsonl") != 0) continue;

		if (session_filter && *session_filter && strcmp(session_filter, "all") != 0 &&
		    strcmp(session_filter, "current") != 0) {
			if (strncmp(ent->d_name, session_filter, strlen(session_filter)) != 0)
				continue;
		}

		char fpath[PATH_MAX];
		snprintf(fpath, sizeof(fpath), "%s/%s", sess_dir, ent->d_name);
		FILE *fp = fopen(fpath, "r");
		if (!fp) continue;

		char line[8192];
		while (fgets(line, sizeof(line), fp) && *io_matches < limit) {
			int turn = extract_jsonl_field_int(line, "turn", 0);
			if (zoom_turn > 0 && turn != zoom_turn) continue;

			char sid[128] = {0};
			char role[32] = {0};
			char text[4096] = {0};
			extract_jsonl_field_str(line, "session", sid, sizeof(sid));
			extract_jsonl_field_str(line, "role", role, sizeof(role));
			if (!extract_jsonl_field_str(line, "text", text, sizeof(text))) continue;

			if (zoom_turn <= 0 && !text_matches_query_ci(text, query)) continue;

			(*io_matches)++;
			if (*pos + 2048 >= *cap) {
				*cap *= 2;
				*buf = (char *)realloc(*buf, *cap);
			}

			if (zoom_turn > 0) {
				int s_l = line_start > 0 ? line_start : 1;
				int e_l = line_end > 0 ? line_end : 9999;
				*pos += (size_t)snprintf(*buf + *pos, *cap - *pos,
					"=== Session %s · Turn %d (%s, lines %d-%d) ===\n",
					sid[0] ? sid : ent->d_name, turn, role[0] ? role : "user", s_l, e_l);
				int cur_l = 1;
				const char *lp = text;
				while (*lp) {
					const char *nl = strchr(lp, '\n');
					size_t llen = nl ? (size_t)(nl - lp) : strlen(lp);
					if (cur_l >= s_l && cur_l <= e_l) {
						*pos += (size_t)snprintf(*buf + *pos, *cap - *pos, "%4d: %.*s\n",
						                         cur_l, (int)llen, lp);
					}
					cur_l++;
					if (!nl) break;
					lp = nl + 1;
				}
			} else {
				/* Compact summary card with zoom hint */
				char snippet[240];
				size_t slen = strlen(text);
				if (slen > 200) slen = 200;
				memcpy(snippet, text, slen);
				snippet[slen] = '\0';
				for (size_t k = 0; k < slen; k++) {
					if (snippet[k] == '\n' || snippet[k] == '\r') snippet[k] = ' ';
				}
				*pos += (size_t)snprintf(*buf + *pos, *cap - *pos,
					"[Session %s · Turn %d · %s]\n"
					"  ├─ Summary: %s%s\n"
					"  └─ Zoom: memory_recall({ scope: \"session\", session: \"%s\", zoomTurn: %d, lineRange: [1, 50] })\n",
					sid[0] ? sid : "default", turn, role[0] ? role : "user",
					snippet, strlen(text) > 200 ? "..." : "",
					sid[0] ? sid : "default", turn);
			}
		}
		fclose(fp);
	}
	closedir(d);
}

int mm_session_recall(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                      const char *query, int zoom_turn, int line_start, int line_end,
                      int limit, char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	if (limit <= 0) limit = 10;

	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap, "=== Ephemeral Session Results ===\n");
	int matches = 0;

	bool check_proj = mm_has_project_tier(ctx) &&
	                  (tier == MM_TIER_AUTO || tier == MM_TIER_BOTH || tier == MM_TIER_PROJECT);
	bool check_glob = (tier == MM_TIER_AUTO || tier == MM_TIER_BOTH || tier == MM_TIER_GLOBAL || !mm_has_project_tier(ctx));

	if (check_proj) {
		char sdir[PATH_MAX];
		snprintf(sdir, sizeof(sdir), "%s/sessions", mm_get_tier_dir(ctx, MM_TIER_PROJECT));
		scan_session_dir(sdir, session_id, query, zoom_turn, line_start, line_end, limit, &matches, &buf, &pos, &cap);
	}
	if (check_glob && matches < limit) {
		char sdir[PATH_MAX];
		snprintf(sdir, sizeof(sdir), "%s/sessions", mm_get_tier_dir(ctx, MM_TIER_GLOBAL));
		scan_session_dir(sdir, session_id, query, zoom_turn, line_start, line_end, limit, &matches, &buf, &pos, &cap);
	}

	if (matches == 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "No matching session turns found.\n");
	}
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 2. Unified Multi-Axis Recall Dispatcher (Code + Session + Persistent)
 * ============================================================================ */

int mm_recall(mm_ctx_t *ctx, const mm_recall_req_t *req, char **out_buf, size_t *out_len) {
	if (!ctx || !req || !out_buf) return -1;

	/* 1. Direct Persistent Ref Zoom */
	if (req->zoom_ref > 0) {
		return mm_recall_persistent(ctx, req, out_buf, out_len);
	}

	/* 2. Direct Session Turn Zoom */
	if (req->zoom_turn > 0 || req->scope == MM_SCOPE_SESSION) {
		return mm_session_recall(ctx, req->tier, req->session_id, req->query,
		                         req->zoom_turn, req->line_start, req->line_end,
		                         req->limit, out_buf, out_len);
	}

	/* 3. Codebase Scope Modes */
	if (req->scope == MM_SCOPE_CODE || req->symbol || req->trace || req->diff || req->diagnose) {
		mm_code_index_t *idx = ensure_code_index(ctx);
		if (!idx) return -1;
		if (req->diff) {
			return mm_code_diff_impact(idx, req->depth, out_buf, out_len);
		}
		if (req->diagnose && *req->diagnose) {
			return mm_code_diagnose(idx, req->diagnose, req->depth, out_buf, out_len);
		}
		if (req->trace && *req->trace) {
			return mm_code_trace(idx, req->trace, req->direction, req->depth, out_buf, out_len);
		}
		if (req->overview) {
			return mm_code_overview(idx, out_buf, out_len);
		}
		if (req->symbol && *req->symbol) {
			return mm_code_read_symbol(idx, req->symbol, req->file, req->neighbors,
			                           req->line_start, req->line_end, out_buf, out_len);
		}
		return mm_code_search(idx, req->query, req->file, req->kind, req->limit, out_buf, out_len);
	}

	/* 4. Persistent Scope Mode */
	if (req->scope == MM_SCOPE_PERSISTENT) {
		return mm_recall_persistent(ctx, req, out_buf, out_len);
	}

	/* 5. Unified Scope (MM_SCOPE_ALL): Combine Session + Code + Persistent */
	char *sess_buf = NULL, *code_buf = NULL, *pers_buf = NULL;
	size_t sess_len = 0, code_len = 0, pers_len = 0;

	mm_session_recall(ctx, req->tier, req->session_id, req->query, 0, 0, 0,
	                  req->limit > 0 ? req->limit : 5, &sess_buf, &sess_len);
	if (mm_has_project_tier(ctx)) {
		mm_code_index_t *idx = ensure_code_index(ctx);
		if (idx) {
			mm_code_search(idx, req->query, req->file, req->kind, 5, &code_buf, &code_len);
		}
	}
	mm_recall_persistent(ctx, req, &pers_buf, &pers_len);

	size_t total = sess_len + code_len + pers_len + 64;
	char *combined = (char *)malloc(total);
	if (!combined) {
		free(sess_buf);
		free(code_buf);
		free(pers_buf);
		return -1;
	}
	size_t pos = 0;
	if (sess_buf) {
		memcpy(combined + pos, sess_buf, sess_len);
		pos += sess_len;
		combined[pos++] = '\n';
		free(sess_buf);
	}
	if (code_buf) {
		memcpy(combined + pos, code_buf, code_len);
		pos += code_len;
		combined[pos++] = '\n';
		free(code_buf);
	}
	if (pers_buf) {
		memcpy(combined + pos, pers_buf, pers_len);
		pos += pers_len;
		free(pers_buf);
	}
	combined[pos] = '\0';
	*out_buf = combined;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 3. Fast Intent Classifier, Priming Hook & L0 Consolidation Drain
 * ============================================================================ */

static bool prompt_has_ci(const char *prompt, const char *phrase) {
	if (!prompt || !phrase) return false;
	size_t plen = strlen(phrase);
	for (const char *p = prompt; *p; p++) {
		if (strncasecmp(p, phrase, plen) == 0) return true;
	}
	return false;
}

mm_intent_t mm_classify_prompt(const char *prompt) {
	if (!prompt || !*prompt) return MM_INTENT_STANDALONE;

	static const char *const directive_phrases[] = {
		"remember that", "always use", "never use", "from now on",
		"standing rule", "make sure you always", "keep in mind forever", NULL
	};
	for (int i = 0; directive_phrases[i]; i++) {
		if (prompt_has_ci(prompt, directive_phrases[i])) return MM_INTENT_DIRECTIVE;
	}

	static const char *const recall_phrases[] = {
		"do you remember", "what did we decide", "how did we fix",
		"last time we", "in our previous session", "why did we",
		"what was the rule", "recall", NULL
	};
	for (int i = 0; recall_phrases[i]; i++) {
		if (prompt_has_ci(prompt, recall_phrases[i])) return MM_INTENT_RECALL;
	}

	static const char *const overview_phrases[] = {
		"architecture overview", "how is this repo structured",
		"module graph", "what modules exist", NULL
	};
	for (int i = 0; overview_phrases[i]; i++) {
		if (prompt_has_ci(prompt, overview_phrases[i])) return MM_INTENT_OVERVIEW;
	}

	static const char *const wondering_phrases[] = {
		"i wonder why", "how does", "where is", "who calls", NULL
	};
	for (int i = 0; wondering_phrases[i]; i++) {
		if (prompt_has_ci(prompt, wondering_phrases[i])) return MM_INTENT_WONDERING;
	}

	return MM_INTENT_STANDALONE;
}

int mm_prime(mm_ctx_t *ctx, const char *prompt, char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	mm_intent_t intent = mm_classify_prompt(prompt);

	/* Query standing directives from Tier 1 (Global) */
	mm_recall_req_t greq;
	memset(&greq, 0, sizeof(greq));
	greq.scope = MM_SCOPE_PERSISTENT;
	greq.tier  = MM_TIER_GLOBAL;
	greq.query = "";
	greq.limit = 5;

	char *global_rules = NULL;
	size_t global_len = 0;
	mm_recall_persistent(ctx, &greq, &global_rules, &global_len);

	size_t cap = 2048 + global_len;
	char *buf = (char *)malloc(cap);
	if (!buf) {
		free(global_rules);
		return -1;
	}
	size_t pos = (size_t)snprintf(buf, cap,
		"<mm_priming intent=\"%s\" tier1=\"%s\" tier2=\"%s\">\n",
		intent == MM_INTENT_DIRECTIVE ? "directive" :
		intent == MM_INTENT_RECALL    ? "recall" :
		intent == MM_INTENT_OVERVIEW  ? "overview" :
		intent == MM_INTENT_WONDERING ? "wondering" : "standalone",
		mm_get_tier_dir(ctx, MM_TIER_GLOBAL),
		mm_has_project_tier(ctx) ? mm_get_tier_dir(ctx, MM_TIER_PROJECT) : "none");

	if (intent == MM_INTENT_DIRECTIVE) {
		pos += (size_t)snprintf(buf + pos, cap - pos,
			"• Standing Directive Detected: Persist via `mm record --standing \"<rule>\"` (or `memory_record({ standing: true, problem: \"...\" })`).\n");
	} else if (intent == MM_INTENT_RECALL || intent == MM_INTENT_WONDERING) {
		pos += (size_t)snprintf(buf + pos, cap - pos,
			"• Recall / Investigation Intent Detected: Prefer `mm recall` / `memory_recall` over grep.\n");
	}
	if (global_rules && global_len > 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "%s", global_rules);
	}
	pos += (size_t)snprintf(buf + pos, cap - pos, "</mm_priming>\n");
	free(global_rules);

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_consolidate_drain(mm_ctx_t *ctx, mm_tier_t tier, int max_batch) {
	if (!ctx) return -1;
	if (max_batch <= 0) max_batch = 20;
	mm_tier_t eff_tier = (tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
	                     (mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL);

	uint32_t *refs = NULL;
	size_t n_refs = 0;
	if (mm_list_refs(ctx, eff_tier, &refs, &n_refs) != 0 || !refs || n_refs == 0) {
		free(refs);
		return 0;
	}

	uint32_t l0_refs[32];
	size_t n_l0 = 0;
	char combined_summary[2048] = {0};
	size_t cpos = 0;

	/* Order newest-first */
	for (ssize_t i = (ssize_t)n_refs - 1; i >= 0 && (int)n_l0 < max_batch && n_l0 < 32; i--) {
		mm_envelope_t env;
		if (mm_get_envelope(ctx, eff_tier, refs[i], &env) == 0) {
			if (env.level == MM_LEVEL_L0 && env.lifecycle != MM_STATUS_SUPERSEDED &&
			    env.superseded_by == 0 && env.summary && env.summary[0]) {
				l0_refs[n_l0++] = refs[i];
				if (cpos + strlen(env.summary) + 8 < sizeof(combined_summary)) {
					cpos += (size_t)snprintf(combined_summary + cpos, sizeof(combined_summary) - cpos,
					                         "%s%s", (cpos > 0 ? "; " : "Consolidated L1 episode: "),
					                         env.summary);
				}
			}
			mm_envelope_free(&env);
		}
	}
	free(refs);

	if (n_l0 == 0) return 0;

	mm_record_req_t req;
	memset(&req, 0, sizeof(req));
	req.tier          = eff_tier;
	req.level         = MM_LEVEL_L1;
	req.problem       = combined_summary;
	req.type          = "episode";
	req.status        = MM_STATUS_TESTED;
	req.evidence_refs = l0_refs;
	req.n_evidence    = n_l0;

	uint32_t l1_ref = 0;
	if (mm_record(ctx, &req, NULL, &l1_ref) == 0 && l1_ref > 0) {
		for (size_t i = 0; i < n_l0; i++) {
			mm_envelope_t l0_env;
			if (mm_get_envelope(ctx, eff_tier, l0_refs[i], &l0_env) == 0) {
				l0_env.superseded_by = l1_ref;
				l0_env.lifecycle     = MM_STATUS_SUPERSEDED;
				mm_put_envelope(ctx, eff_tier, l0_refs[i], &l0_env);
				mm_envelope_free(&l0_env);
			}
		}
	}
	return (int)n_l0;
}
