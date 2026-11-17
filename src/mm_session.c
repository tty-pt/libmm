#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include <ttypt/mm.h>
#include <ttypt/stele.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#ifndef strtok_r
#define strtok_r strtok_s
#endif
#endif

/* Forward declaration from mm_ctx.c */
int mm_recall_persistent(mm_ctx_t *ctx, const mm_recall_req_t *req,
                         char **out_buf, size_t *out_len);

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
 * 1. Session Transcript Ingestion, Lineage DAG & Multi-Session Recall
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

int mm_session_ingest_event(mm_ctx_t *ctx, mm_tier_t tier, const mm_session_event_t *ev) {
	if (!ctx || !ev || !ev->text) return -1;
	mm_tier_t eff_tier = tier;
	if (eff_tier == MM_TIER_AUTO || eff_tier == MM_TIER_BOTH) {
		eff_tier = mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL;
	}
	const char *tdir = mm_get_tier_dir(ctx, eff_tier);
	if (!tdir) return -1;

	char sid[128];
	sanitize_session_id(ev->session_id, sid, sizeof(sid));

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/sessions/%s.jsonl", tdir, sid);

	FILE *fp = fopen(path, "a");
	if (!fp) return -1;

	time_t now = time(NULL);
	struct tm tm_utc;
	stl_gmtime_r(&now, &tm_utc);
	char ts[32];
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

	fprintf(fp, "{\"session\":");
	write_json_escaped(fp, sid);
	fprintf(fp, ",\"turn\":%d,\"role\":", ev->turn_idx > 0 ? ev->turn_idx : 1);
	write_json_escaped(fp, (ev->role && *ev->role) ? ev->role : "user");
	if (ev->entry_id && *ev->entry_id) {
		fprintf(fp, ",\"entryId\":");
		write_json_escaped(fp, ev->entry_id);
	}
	if (ev->parent_id && *ev->parent_id) {
		fprintf(fp, ",\"parentId\":");
		write_json_escaped(fp, ev->parent_id);
	}
	if (ev->tool_name && *ev->tool_name) {
		fprintf(fp, ",\"toolName\":");
		write_json_escaped(fp, ev->tool_name);
	}
	if (ev->tool_index > 0) {
		fprintf(fp, ",\"toolIndex\":%d", ev->tool_index);
	}
	if (ev->has_exit_code) {
		fprintf(fp, ",\"exitCode\":%d", ev->exit_code);
	}
	if (ev->files_touched && *ev->files_touched) {
		fprintf(fp, ",\"filesTouched\":");
		write_json_escaped(fp, ev->files_touched);
	}
	if (ev->error_snippet && *ev->error_snippet) {
		fprintf(fp, ",\"errorSnippet\":");
		write_json_escaped(fp, ev->error_snippet);
	}
	fprintf(fp, ",\"timestamp\":\"%s\",\"text\":", ts);
	write_json_escaped(fp, ev->text);
	fprintf(fp, "}\n");
	fclose(fp);
	return 0;
}

int mm_session_ingest_turn(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                           int turn_idx, const char *role, const char *text) {
	mm_session_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.session_id = session_id;
	ev.turn_idx   = turn_idx;
	ev.role       = role;
	ev.text       = text;
	return mm_session_ingest_event(ctx, tier, &ev);
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
	if (!isdigit((unsigned char)*p) && *p != '-') return def_val;
	return atoi(p);
}

static bool text_matches_query_ci(const char *text, const char *query) {
	if (!query || !*query) return true;
	if (!text || !*text) return false;
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

typedef struct {
	int  turn;
	int  tool_index;
	int  exit_code;
	char sid[128];
	char role[32];
	char entry_id[64];
	char parent_id[64];
	char tool_name[64];
	char files_touched[256];
	char error_snippet[256];
	char text[4096];
	bool on_active_branch;
} mm_sess_row_t;

/* Compute active branch ancestry from the latest entry_id leaf backward via parent_id */
static void mark_active_lineage(mm_sess_row_t *rows, size_t n_rows, bool active_only) {
	if (n_rows == 0) return;
	bool any_entry_id = false;
	ssize_t leaf_idx = -1;
	for (size_t i = 0; i < n_rows; i++) {
		rows[i].on_active_branch = true;
		if (rows[i].entry_id[0]) {
			any_entry_id = true;
			leaf_idx = (ssize_t)i;
		}
	}
	if (!active_only || !any_entry_id || leaf_idx < 0) return;

	char active_ids[128][64];
	size_t n_active = 0;
	char cur_id[64];
	snprintf(cur_id, sizeof(cur_id), "%s", rows[leaf_idx].entry_id);

	while (cur_id[0] && n_active < 128) {
		snprintf(active_ids[n_active++], 64, "%s", cur_id);
		char next_parent[64] = {0};
		for (ssize_t i = (ssize_t)n_rows - 1; i >= 0; i--) {
			if (!strcmp(rows[i].entry_id, cur_id)) {
				snprintf(next_parent, sizeof(next_parent), "%s", rows[i].parent_id);
				break;
			}
		}
		if (!next_parent[0] || !strcmp(next_parent, cur_id)) break;
		snprintf(cur_id, sizeof(cur_id), "%s", next_parent);
	}

	/* Mark turns that belong to the active entry_id chain */
	for (size_t i = 0; i < n_rows; i++) {
		if (!rows[i].entry_id[0]) continue;
		bool found = false;
		for (size_t a = 0; a < n_active; a++) {
			if (!strcmp(rows[i].entry_id, active_ids[a])) {
				found = true;
				break;
			}
		}
		rows[i].on_active_branch = found;
	}
}

static const char *session_provenance_badge(const char *sid, const char *session_filter) {
	if (!sid || !*sid) return "[Current Session]";
	if (strstr(sid, "slot-") || strstr(sid, "parallel")) return "[Parallel Session]";
	if (session_filter && !strcmp(session_filter, "all") && strcmp(sid, "default") != 0 && strcmp(sid, "current") != 0) {
		return "[Past Session]";
	}
	return "[Current Session]";
}

static void scan_session_dir(const char *sess_dir, const char *session_filter,
                             const char *query, int zoom_turn, int tool_index,
                             const char *lineage, int line_start, int line_end,
                             int limit, int *io_matches,
                             char **buf, size_t *pos, size_t *cap) {
	DIR *d = opendir(sess_dir);
	if (!d) return;

	bool active_only = !lineage || !*lineage || strcmp(lineage, "all") != 0;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && *io_matches < limit) {
		const char *ext = strrchr(ent->d_name, '.');
		if (!ext || strcmp(ext, ".jsonl") != 0) continue;

		if (session_filter && *session_filter && strcmp(session_filter, "all") != 0 &&
		    strcmp(session_filter, "current") != 0) {
			if (!strcmp(session_filter, "parallel")) {
				if (!strstr(ent->d_name, "slot-") && !strstr(ent->d_name, "parallel"))
					continue;
			} else if (strncmp(ent->d_name, session_filter, strlen(session_filter)) != 0) {
				continue;
			}
		}

		char fpath[PATH_MAX];
		snprintf(fpath, sizeof(fpath), "%s/%s", sess_dir, ent->d_name);
		FILE *fp = fopen(fpath, "r");
		if (!fp) continue;

		size_t rcap = 64, rcount = 0;
		mm_sess_row_t *rows = (mm_sess_row_t *)calloc(rcap, sizeof(mm_sess_row_t));
		char line[8192];
		while (fgets(line, sizeof(line), fp)) {
			if (rcount >= rcap) {
				rcap *= 2;
				rows = (mm_sess_row_t *)realloc(rows, rcap * sizeof(mm_sess_row_t));
			}
			mm_sess_row_t *r = &rows[rcount];
			memset(r, 0, sizeof(*r));
			r->turn       = extract_jsonl_field_int(line, "turn", 0);
			r->tool_index = extract_jsonl_field_int(line, "toolIndex", 0);
			r->exit_code  = extract_jsonl_field_int(line, "exitCode", 0);
			extract_jsonl_field_str(line, "session", r->sid, sizeof(r->sid));
			extract_jsonl_field_str(line, "role", r->role, sizeof(r->role));
			extract_jsonl_field_str(line, "entryId", r->entry_id, sizeof(r->entry_id));
			extract_jsonl_field_str(line, "parentId", r->parent_id, sizeof(r->parent_id));
			extract_jsonl_field_str(line, "toolName", r->tool_name, sizeof(r->tool_name));
			extract_jsonl_field_str(line, "filesTouched", r->files_touched, sizeof(r->files_touched));
			extract_jsonl_field_str(line, "errorSnippet", r->error_snippet, sizeof(r->error_snippet));
			if (extract_jsonl_field_str(line, "text", r->text, sizeof(r->text))) {
				rcount++;
			}
		}
		fclose(fp);

		mark_active_lineage(rows, rcount, active_only);

		for (size_t i = 0; i < rcount && *io_matches < limit; i++) {
			const mm_sess_row_t *r = &rows[i];
			if (!r->on_active_branch) continue;
			if (zoom_turn > 0 && r->turn != zoom_turn) continue;
			if (tool_index > 0 && r->tool_index != tool_index) continue;
			if (zoom_turn <= 0 && !text_matches_query_ci(r->text, query) &&
			    !text_matches_query_ci(r->tool_name, query) &&
			    !text_matches_query_ci(r->files_touched, query)) {
				continue;
			}

			(*io_matches)++;
			if (*pos + 2048 >= *cap) {
				*cap *= 2;
				*buf = (char *)realloc(*buf, *cap);
			}

			const char *badge = session_provenance_badge(r->sid, session_filter);

			if (zoom_turn > 0) {
				int s_l = line_start > 0 ? line_start : 1;
				int e_l = line_end > 0 ? line_end : 9999;
				*pos += (size_t)snprintf(*buf + *pos, *cap - *pos,
					"=== %s Session %s · Turn %d (%s%s%s, lines %d-%d) ===\n",
					badge,
					r->sid[0] ? r->sid : ent->d_name,
					r->turn,
					r->role[0] ? r->role : "user",
					r->tool_name[0] ? " · tool:" : "",
					r->tool_name,
					s_l, e_l);
				int cur_l = 1;
				const char *lp = r->text;
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
				char snippet[240];
				size_t slen = strlen(r->text);
				if (slen > 200) slen = 200;
				memcpy(snippet, r->text, slen);
				snippet[slen] = '\0';
				for (size_t k = 0; k < slen; k++) {
					if (snippet[k] == '\n' || snippet[k] == '\r') snippet[k] = ' ';
				}
				*pos += (size_t)snprintf(*buf + *pos, *cap - *pos,
					"%s [Session %s · Turn %d · %s%s%s]\n"
					"  ├─ Summary: %s%s\n"
					"  └─ Zoom: memory_recall({ scope: \"session\", session: \"%s\", zoomTurn: %d, lineRange: [1, 50] })\n",
					badge,
					r->sid[0] ? r->sid : "default", r->turn,
					r->role[0] ? r->role : "user",
					r->tool_name[0] ? " · " : "", r->tool_name,
					snippet, strlen(r->text) > 200 ? "..." : "",
					r->sid[0] ? r->sid : "default", r->turn);
			}
		}
		free(rows);
	}
	closedir(d);
}

int mm_session_recall_ex(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                         const char *query, int zoom_turn, int tool_index,
                         const char *lineage, int line_start, int line_end,
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
		scan_session_dir(sdir, session_id, query, zoom_turn, tool_index, lineage,
		                 line_start, line_end, limit, &matches, &buf, &pos, &cap);
	}
	if (check_glob && matches < limit) {
		char sdir[PATH_MAX];
		snprintf(sdir, sizeof(sdir), "%s/sessions", mm_get_tier_dir(ctx, MM_TIER_GLOBAL));
		scan_session_dir(sdir, session_id, query, zoom_turn, tool_index, lineage,
		                 line_start, line_end, limit, &matches, &buf, &pos, &cap);
	}

	if (matches == 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "No matching session turns found.\n");
	}
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_session_recall(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                      const char *query, int zoom_turn, int line_start, int line_end,
                      int limit, char **out_buf, size_t *out_len) {
	return mm_session_recall_ex(ctx, tier, session_id, query, zoom_turn, 0, "active",
	                            line_start, line_end, limit, out_buf, out_len);
}

/* Mipmap-Driven Session Compaction (matches pi-mm src/session/compaction.ts) */
int mm_session_compact(mm_ctx_t *ctx, mm_tier_t tier, const char *session_id,
                       const char *custom_instructions,
                       char **out_summary, size_t *out_len) {
	if (!ctx || !out_summary) return -1;
	mm_tier_t eff_tier = (tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
	                     (mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL);
	const char *tdir = mm_get_tier_dir(ctx, eff_tier);
	if (!tdir) return -1;

	char sid[128];
	sanitize_session_id(session_id, sid, sizeof(sid));
	char fpath[PATH_MAX];
	snprintf(fpath, sizeof(fpath), "%s/sessions/%s.jsonl", tdir, sid);

	FILE *fp = fopen(fpath, "r");
	if (!fp) return -1;

	char first_goal[512] = {0};
	char last_turn_text[512] = {0};
	char touched_files[1024] = {0};
	char diag_signals[1024] = {0};
	int min_turn = 0, max_turn = 0, total_turns = 0;

	char line[8192];
	while (fgets(line, sizeof(line), fp)) {
		int turn = extract_jsonl_field_int(line, "turn", 0);
		char role[32] = {0}, text[2048] = {0}, files[256] = {0}, err[256] = {0};
		extract_jsonl_field_str(line, "role", role, sizeof(role));
		extract_jsonl_field_str(line, "text", text, sizeof(text));
		extract_jsonl_field_str(line, "filesTouched", files, sizeof(files));
		extract_jsonl_field_str(line, "errorSnippet", err, sizeof(err));

		if (turn > 0) {
			if (min_turn == 0 || turn < min_turn) min_turn = turn;
			if (turn > max_turn) max_turn = turn;
			total_turns++;
		}
		if (!first_goal[0] && !strcmp(role, "user") && text[0]) {
			snprintf(first_goal, sizeof(first_goal), "%s", text);
		}
		if (text[0]) {
			snprintf(last_turn_text, sizeof(last_turn_text), "Turn %d (%s): %s", turn, role, text);
		}
		if (files[0] && !strstr(touched_files, files)) {
			if (touched_files[0]) strncat(touched_files, ", ", sizeof(touched_files) - strlen(touched_files) - 1);
			strncat(touched_files, files, sizeof(touched_files) - strlen(touched_files) - 1);
		}
		if (err[0]) {
			char dline[320];
			snprintf(dline, sizeof(dline), "  • Turn %d error: %s\n", turn, err);
			strncat(diag_signals, dline, sizeof(diag_signals) - strlen(diag_signals) - 1);
		}
	}
	fclose(fp);

	char *reinf = NULL;
	mm_synthesize_reinforcement(ctx, "compaction", NULL, &reinf, NULL);

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== Mipmap Session Compaction Summary ===\n"
		"[Session Goal]\n  %s\n\n"
		"[L2 Epoch Summary]\n  Compacted %d event(s) across Turns %d..%d in session `%s`.\n\n"
		"[Active Project Invariants & Directives]\n%s\n"
		"[Files And Touched Assets]\n  %s\n\n"
		"[Key Diagnostic Signals]\n%s\n"
		"[Recent Conversational Thread]\n  %s\n\n"
		"[Ephemeral Session Memory Anchor]\n"
		"  Drill down into verbatim turns via `memory_recall({ scope: \"session\", session: \"%s\", zoomTurn: <N>, lineRange: [1, 80] })`.\n",
		first_goal[0] ? first_goal : "(general engineering session)",
		total_turns, min_turn, max_turn, sid,
		reinf ? reinf : "  (none)\n",
		touched_files[0] ? touched_files : "(none recorded)",
		diag_signals[0] ? diag_signals : "  (no errors recorded)\n",
		last_turn_text[0] ? last_turn_text : "(none)",
		sid);

	if (custom_instructions && *custom_instructions) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "\n[Custom Compaction Focus]\n  %s\n", custom_instructions);
	}
	free(reinf);

	/* Record L2 compaction epoch turn in session log */
	mm_session_event_t comp_ev;
	memset(&comp_ev, 0, sizeof(comp_ev));
	comp_ev.session_id = sid;
	comp_ev.turn_idx   = max_turn + 1;
	comp_ev.role       = "compaction";
	comp_ev.text       = buf;
	mm_session_ingest_event(ctx, eff_tier, &comp_ev);

	*out_summary = buf;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 2. Unified Multi-Axis Recall Dispatcher (Code + Session + Persistent)
 * ============================================================================ */

static void append_linked_lore_to_code(mm_ctx_t *ctx, const char *query_sym,
                                       const char *file_path, const char *session_id,
                                       char **io_buf, size_t *io_len) {
	if (!ctx || !io_buf || !*io_buf) return;
	size_t pos = *io_len ? *io_len : strlen(*io_buf);
	size_t cap = pos + 8192;
	*io_buf = (char *)realloc(*io_buf, cap);

	/* 1. Linked Persistent Lore */
	if (query_sym && *query_sym) {
		mm_recall_req_t lreq;
		memset(&lreq, 0, sizeof(lreq));
		lreq.scope = MM_SCOPE_PERSISTENT;
		lreq.tier  = MM_TIER_BOTH;
		lreq.query = query_sym;
		lreq.limit = 3;
		char *lore_buf = NULL;
		if (mm_recall_persistent(ctx, &lreq, &lore_buf, NULL) == 0 && lore_buf) {
			if (strstr(lore_buf, "[Ref ")) {
				pos += (size_t)snprintf(*io_buf + pos, cap - pos,
					"\n=== Linked Engineering Lore & Invariants ===\n%s", lore_buf);
			}
			free(lore_buf);
		}
	}

	/* 2. AST-Bound Lore Drift Check */
	const char *drift_target = (query_sym && *query_sym) ? query_sym : file_path;
	if (drift_target && *drift_target) {
		char *drift_buf = NULL;
		if (mm_lore_verify_drift(ctx, drift_target, &drift_buf, NULL) == 0 && drift_buf) {
			if (drift_buf[0]) {
				pos += (size_t)snprintf(*io_buf + pos, cap - pos,
					"=== AST-Bound Invariant Freshness ===\n%s", drift_buf);
			}
			free(drift_buf);
		}
	}

	/* 3. Swarm Stigmergy Leases */
	if ((query_sym && *query_sym) || (file_path && *file_path)) {
		char *swarm_buf = NULL;
		if (mm_swarm_query_leases(ctx, session_id, query_sym, file_path, &swarm_buf, NULL) == 0 && swarm_buf) {
			if (swarm_buf[0]) {
				pos += (size_t)snprintf(*io_buf + pos, cap - pos, "%s", swarm_buf);
			}
			free(swarm_buf);
		}
	}

	/* 4. Working-Memory Context Staleness Radar */
	if (file_path && *file_path) {
		char *ctx_drift = NULL;
		if (mm_context_check_drift(ctx, session_id, file_path, &ctx_drift, NULL) == 0 && ctx_drift) {
			if (ctx_drift[0]) {
				pos += (size_t)snprintf(*io_buf + pos, cap - pos,
					"=== Working-Memory Context Staleness Alert ===\n%s", ctx_drift);
			}
			free(ctx_drift);
		}
	}

	if (io_len) *io_len = pos;
}

int mm_recall(mm_ctx_t *ctx, const mm_recall_req_t *req, char **out_buf, size_t *out_len) {
	if (!ctx || !req || !out_buf) return -1;

	/* 0. Pre-Tool Counterfactual Guardrails */
	if (req->guardrail && *req->guardrail) {
		return mm_evaluate_guardrail(ctx, req->guardrail, req->file, req->symbol, out_buf, out_len);
	}

	/* 1. Combined zoomRef + zoomTurn: jump from a persistent memory to its source session turn */
	if (req->zoom_ref > 0 && req->zoom_turn > 0) {
		mm_tier_t ztier = (req->zoom_tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
		                  (mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL);
		mm_envelope_t env;
		if (mm_get_envelope(ctx, ztier, req->zoom_ref, &env) == 0) {
			const char *sid = env.ep_session_id[0] ? env.ep_session_id : req->session_id;
			int rc = mm_session_recall_ex(ctx, ztier, sid, NULL, req->zoom_turn, req->tool_index,
			                              req->lineage, req->line_start, req->line_end,
			                              req->limit, out_buf, out_len);
			mm_envelope_free(&env);
			return rc;
		}
	}

	/* 2. Direct Persistent Ref Zoom */
	if (req->zoom_ref > 0) {
		return mm_recall_persistent(ctx, req, out_buf, out_len);
	}

	/* 3. Direct Session Turn Zoom or Session Scope */
	if (req->zoom_turn > 0 || req->scope == MM_SCOPE_SESSION) {
		return mm_session_recall_ex(ctx, req->tier, req->session_id, req->query,
		                            req->zoom_turn, req->tool_index, req->lineage,
		                            req->line_start, req->line_end,
		                            req->limit, out_buf, out_len);
	}

	/* 4. Codebase Scope Modes */
	if (req->scope == MM_SCOPE_CODE || req->symbol || (req->symbols && req->n_symbols > 0) ||
	    req->trace || req->diff || req->diagnose) {
		mm_code_index_t *idx = ensure_code_index(ctx);
		if (!idx) return -1;
		int rc = 0;
		size_t clen = 0;

		if (req->diff) {
			rc = mm_code_diff_impact(idx, req->depth, out_buf, &clen);
		} else if (req->diagnose && *req->diagnose) {
			rc = mm_code_diagnose(idx, req->diagnose, req->depth, out_buf, &clen);
			if (rc == 0) {
				append_linked_lore_to_code(ctx, req->diagnose, req->file, req->session_id, out_buf, &clen);
			}
		} else if (req->trace && *req->trace) {
			rc = mm_code_trace(idx, req->trace, req->direction, req->depth, out_buf, &clen);
		} else if (req->overview) {
			rc = mm_code_overview(idx, out_buf, &clen);
		} else if (req->symbols && req->n_symbols > 0) {
			rc = mm_code_read_symbols_ex(idx, req->symbols, req->n_symbols,
			                             req->parent_symbol, req->file, req->neighbors,
			                             req->line_start, req->line_end, out_buf, &clen);
			if (rc == 0) {
				append_linked_lore_to_code(ctx, req->symbols[0], req->file, req->session_id, out_buf, &clen);
			}
		} else if (req->symbol && *req->symbol) {
			const char *syms[1] = { req->symbol };
			rc = mm_code_read_symbols_ex(idx, syms, 1,
			                             req->parent_symbol, req->file, req->neighbors,
			                             req->line_start, req->line_end, out_buf, &clen);
			if (rc == 0) {
				append_linked_lore_to_code(ctx, req->symbol, req->file, req->session_id, out_buf, &clen);
			}
		} else {
			rc = mm_code_search(idx, req->query, req->file, req->kind, req->limit, out_buf, &clen);
			if (rc == 0 && req->query && *req->query) {
				append_linked_lore_to_code(ctx, req->query, req->file, req->session_id, out_buf, &clen);
			}
		}
		if (out_len) *out_len = clen;
		return rc;
	}

	/* 5. Persistent Scope Mode */
	if (req->scope == MM_SCOPE_PERSISTENT) {
		return mm_recall_persistent(ctx, req, out_buf, out_len);
	}

	/* 6. Unified Scope (MM_SCOPE_ALL): Combine Session + Code + Persistent */
	if (req->overview) {
		return mm_build_memory_overview(ctx, req->tier, req->limit > 0 ? req->limit : 10, out_buf, out_len);
	}

	char *sess_buf = NULL, *code_buf = NULL, *pers_buf = NULL;
	size_t sess_len = 0, code_len = 0, pers_len = 0;

	mm_session_recall_ex(ctx, req->tier, req->session_id, req->query, 0, 0, req->lineage, 0, 0,
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
 * 3. Fast Intent Classifier, Priming Hook & L0->L1->L2->L3 Consolidation
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
		"remember that", "always use", "always prefer", "always run",
		"never use", "from now on", "standing rule",
		"make sure you always", "keep in mind forever", NULL
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

static bool summaries_conflict(const char *a, const char *b) {
	if (!a || !b) return false;
	bool a_neg = prompt_has_ci(a, "never ") || prompt_has_ci(a, "do not ") || prompt_has_ci(a, "disable ");
	bool b_neg = prompt_has_ci(b, "never ") || prompt_has_ci(b, "do not ") || prompt_has_ci(b, "disable ");
	bool a_pos = prompt_has_ci(a, "always ") || prompt_has_ci(a, "enable ") || prompt_has_ci(a, "must use ");
	bool b_pos = prompt_has_ci(b, "always ") || prompt_has_ci(b, "enable ") || prompt_has_ci(b, "must use ");
	return (a_neg && b_pos) || (a_pos && b_neg);
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
	char target_run_id[128] = {0};
	char target_sess_id[128] = {0};
	char target_project[128] = {0};
	char target_subsystem[128] = {0};
	char combined_summary[2048] = {0};
	size_t cpos = 0;

	/* Order newest-first, grouping by run_id / session_id */
	for (ssize_t i = (ssize_t)n_refs - 1; i >= 0 && (int)n_l0 < max_batch && n_l0 < 32; i--) {
		mm_envelope_t env;
		if (mm_get_envelope(ctx, eff_tier, refs[i], &env) == 0) {
			if (env.level == MM_LEVEL_L0 && env.lifecycle != MM_STATUS_SUPERSEDED &&
			    env.superseded_by == 0 && env.summary && env.summary[0]) {
				if (n_l0 == 0) {
					snprintf(target_run_id, sizeof(target_run_id), "%s", env.ep_run_id);
					snprintf(target_sess_id, sizeof(target_sess_id), "%s", env.ep_session_id);
					snprintf(target_project, sizeof(target_project), "%s", env.ctx_project);
					snprintf(target_subsystem, sizeof(target_subsystem), "%s", env.ctx_subsystem);
				} else if (target_run_id[0] && env.ep_run_id[0] && strcmp(target_run_id, env.ep_run_id) != 0) {
					mm_envelope_free(&env);
					continue;
				}
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

	if (n_l0 == 0) {
		free(refs);
		return 0;
	}

	/* Check if an active L1 episode already exists for the same run_id to evolve as successor */
	uint32_t prior_l1_ref = 0;
	uint32_t merged_ev[MM_MAX_EVIDENCE];
	size_t n_merged_ev = 0;

	if (target_run_id[0]) {
		for (size_t i = 0; i < n_refs; i++) {
			mm_envelope_t env;
			if (mm_get_envelope(ctx, eff_tier, refs[i], &env) == 0) {
				if (env.level == MM_LEVEL_L1 && env.lifecycle != MM_STATUS_SUPERSEDED &&
				    env.superseded_by == 0 && !strcmp(env.ep_run_id, target_run_id)) {
					prior_l1_ref = refs[i];
					for (size_t e = 0; e < env.n_evidence && n_merged_ev < MM_MAX_EVIDENCE; e++) {
						merged_ev[n_merged_ev++] = env.evidence[e];
					}
				}
				mm_envelope_free(&env);
			}
		}
	}
	free(refs);

	for (size_t i = 0; i < n_l0 && n_merged_ev < MM_MAX_EVIDENCE; i++) {
		merged_ev[n_merged_ev++] = l0_refs[i];
	}

	mm_record_req_t req;
	memset(&req, 0, sizeof(req));
	req.tier           = eff_tier;
	req.level          = MM_LEVEL_L1;
	req.problem        = combined_summary;
	req.type           = "episode";
	req.status         = MM_STATUS_TESTED;
	req.evidence_refs  = merged_ev;
	req.n_evidence     = n_merged_ev;
	req.supersedes_ref = prior_l1_ref;
	req.run_id         = target_run_id[0] ? target_run_id : NULL;
	req.session_id     = target_sess_id[0] ? target_sess_id : NULL;
	req.project        = target_project[0] ? target_project : NULL;
	req.subsystem      = target_subsystem[0] ? target_subsystem : NULL;

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

/* L1 -> L2 Scope Consolidation (min 2 non-conflicting L1s) & L2 -> L3 Principle Synthesis (min 3 L2s) */
int mm_consolidate_scopes_and_principles(mm_ctx_t *ctx, mm_tier_t tier,
                                         int *out_l2_created, int *out_l3_created) {
	if (out_l2_created) *out_l2_created = 0;
	if (out_l3_created) *out_l3_created = 0;
	if (!ctx) return -1;

	mm_tier_t eff_tier = (tier == MM_TIER_GLOBAL) ? MM_TIER_GLOBAL :
	                     (mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL);

	uint32_t *refs = NULL;
	size_t n_refs = 0;
	if (mm_list_refs(ctx, eff_tier, &refs, &n_refs) != 0 || !refs || n_refs == 0) {
		free(refs);
		return 0;
	}

	/* 1. Group active L1 episodes by subsystem (or project) and synthesize L2 if >= 2 non-conflicting */
	char tried_subs[16][128];
	size_t n_tried = 0;

	for (size_t seed = 0; seed < n_refs && n_tried < 16; seed++) {
		mm_envelope_t senv;
		if (mm_get_envelope(ctx, eff_tier, refs[seed], &senv) != 0) continue;
		if (senv.level != MM_LEVEL_L1 || senv.lifecycle == MM_STATUS_SUPERSEDED ||
		    senv.lifecycle == MM_STATUS_CONTRADICTED || senv.superseded_by != 0 ||
		    !senv.summary || !senv.summary[0]) {
			mm_envelope_free(&senv);
			continue;
		}
		bool already_tried = false;
		for (size_t t = 0; t < n_tried; t++) {
			if (!strcmp(tried_subs[t], senv.ctx_subsystem)) { already_tried = true; break; }
		}
		if (already_tried) {
			mm_envelope_free(&senv);
			continue;
		}
		char group_proj[128] = {0}, group_sub[128] = {0};
		snprintf(group_proj, sizeof(group_proj), "%s", senv.ctx_project);
		snprintf(group_sub, sizeof(group_sub), "%s", senv.ctx_subsystem);
		snprintf(tried_subs[n_tried++], 128, "%s", group_sub);
		mm_envelope_free(&senv);

		uint32_t l1_group[16];
		char *l1_sums[16] = {0};
		size_t n_l1 = 0;
		bool conflict = false;

		for (size_t i = seed; i < n_refs && n_l1 < 16; i++) {
			mm_envelope_t env;
			if (mm_get_envelope(ctx, eff_tier, refs[i], &env) == 0) {
				if (env.level == MM_LEVEL_L1 &&
				    env.lifecycle != MM_STATUS_SUPERSEDED &&
				    env.lifecycle != MM_STATUS_CONTRADICTED &&
				    env.superseded_by == 0 && env.summary && env.summary[0] &&
				    !strcmp(env.ctx_subsystem, group_sub)) {
					for (size_t k = 0; k < n_l1; k++) {
						if (summaries_conflict(l1_sums[k], env.summary)) conflict = true;
					}
					l1_group[n_l1] = refs[i];
					l1_sums[n_l1]  = strdup(env.summary);
					n_l1++;
				}
				mm_envelope_free(&env);
			}
		}

		if (n_l1 >= 2 && !conflict) {
			char l2_sum[2048];
			size_t pos = (size_t)snprintf(l2_sum, sizeof(l2_sum), "Consolidated L2 subsystem invariant (%s): ",
			                              group_sub[0] ? group_sub : (group_proj[0] ? group_proj : "general"));
			for (size_t i = 0; i < n_l1; i++) {
				pos += (size_t)snprintf(l2_sum + pos, sizeof(l2_sum) - pos, "%s%s", (i > 0 ? " | " : ""), l1_sums[i]);
			}
			mm_record_req_t rreq;
			memset(&rreq, 0, sizeof(rreq));
			rreq.tier          = eff_tier;
			rreq.level         = MM_LEVEL_L2;
			rreq.type          = "decision";
			rreq.status        = MM_STATUS_SUPPORTED;
			rreq.problem       = l2_sum;
			rreq.project       = group_proj[0] ? group_proj : NULL;
			rreq.subsystem     = group_sub[0] ? group_sub : NULL;
			rreq.evidence_refs = l1_group;
			rreq.n_evidence    = n_l1;
			uint32_t l2_ref = 0;
			if (mm_record(ctx, &rreq, NULL, &l2_ref) == 0 && l2_ref > 0) {
				if (out_l2_created) (*out_l2_created)++;
				for (size_t i = 0; i < n_l1; i++) {
					mm_envelope_t e1;
					if (mm_get_envelope(ctx, eff_tier, l1_group[i], &e1) == 0) {
						e1.superseded_by = l2_ref;
						mm_put_envelope(ctx, eff_tier, l1_group[i], &e1);
						mm_envelope_free(&e1);
					}
				}
			}
		}
		for (size_t i = 0; i < n_l1; i++) free(l1_sums[i]);
	}

	/* Refresh refs list and check if >= 3 active L2 records can synthesize an L3 architectural principle */
	free(refs);
	refs = NULL;
	n_refs = 0;
	if (mm_list_refs(ctx, eff_tier, &refs, &n_refs) == 0 && refs) {
		uint32_t l2_group[16];
		char *l2_sums[16] = {0};
		size_t n_l2 = 0;
		for (size_t i = 0; i < n_refs && n_l2 < 16; i++) {
			mm_envelope_t env;
			if (mm_get_envelope(ctx, eff_tier, refs[i], &env) == 0) {
				if (env.level == MM_LEVEL_L2 &&
				    (env.lifecycle == MM_STATUS_SUPPORTED || env.lifecycle == MM_STATUS_TESTED) &&
				    env.superseded_by == 0 && env.summary && env.summary[0]) {
					l2_group[n_l2] = refs[i];
					l2_sums[n_l2]  = strdup(env.summary);
					n_l2++;
				}
				mm_envelope_free(&env);
			}
		}
		if (n_l2 >= 3) {
			char l3_sum[2048];
			size_t pos = (size_t)snprintf(l3_sum, sizeof(l3_sum), "Consolidated L3 architectural principle: ");
			for (size_t i = 0; i < n_l2; i++) {
				pos += (size_t)snprintf(l3_sum + pos, sizeof(l3_sum) - pos, "%s%s", (i > 0 ? " | " : ""), l2_sums[i]);
			}
			mm_record_req_t rreq;
			memset(&rreq, 0, sizeof(rreq));
			rreq.tier          = eff_tier;
			rreq.level         = MM_LEVEL_L3;
			rreq.type          = "rule";
			rreq.status        = MM_STATUS_SUPPORTED;
			rreq.anchor        = true;
			rreq.problem       = l3_sum;
			rreq.evidence_refs = l2_group;
			rreq.n_evidence    = n_l2;
			uint32_t l3_ref = 0;
			if (mm_record(ctx, &rreq, NULL, &l3_ref) == 0 && l3_ref > 0) {
				if (out_l3_created) (*out_l3_created)++;
				for (size_t i = 0; i < n_l2; i++) {
					mm_envelope_t e2;
					if (mm_get_envelope(ctx, eff_tier, l2_group[i], &e2) == 0) {
						e2.superseded_by = l3_ref;
						mm_put_envelope(ctx, eff_tier, l2_group[i], &e2);
						mm_envelope_free(&e2);
					}
				}
			}
		}
		for (size_t i = 0; i < n_l2; i++) free(l2_sums[i]);
		free(refs);
	}

	return 0;
}
