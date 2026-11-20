#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#define lstat stat
#define popen _popen
#define pclose _pclose
#endif

static void mm_buf_appendf(char **buf, size_t *pos, size_t *cap, const char *fmt, ...) {
	if (!buf || !*buf || !pos || !cap) return;
	for (;;) {
		size_t avail = (*cap > *pos) ? (*cap - *pos) : 0;
		va_list ap;
		va_start(ap, fmt);
		int n = vsnprintf(*buf + *pos, avail, fmt, ap);
		va_end(ap);
		if (n < 0) return;
		if ((size_t)n < avail) {
			*pos += (size_t)n;
			return;
		}
		size_t new_cap = *cap ? *cap * 2 : 4096;
		while (new_cap <= *pos + (size_t)n + 1) new_cap *= 2;
		char *nb = (char *)realloc(*buf, new_cap);
		if (!nb) {
			if (avail > 0) *pos = *cap - 1;
			return;
		}
		*buf = nb;
		*cap = new_cap;
	}
}

/* ============================================================================
 * 1. Code Index Lifecycle & Helpers
 * ============================================================================ */

void mm_code_index_init(mm_code_index_t *idx, const char *root_dir) {
	if (!idx) return;
	memset(idx, 0, sizeof(*idx));
	if (root_dir && *root_dir) {
		snprintf(idx->root_dir, sizeof(idx->root_dir), "%s", root_dir);
	} else {
		strcpy(idx->root_dir, ".");
	}
}

void mm_code_index_free(mm_code_index_t *idx) {
	if (!idx) return;
	free(idx->files);
	idx->files = NULL;
	idx->n_files = idx->cap_files = 0;
	free(idx->symbols);
	idx->symbols = NULL;
	idx->n_symbols = idx->cap_symbols = 0;
}

static mm_code_file_t *alloc_code_file(mm_code_index_t *idx) {
	if (idx->n_files >= idx->cap_files) {
		size_t ncap = idx->cap_files == 0 ? 64 : idx->cap_files * 2;
		mm_code_file_t *nf = (mm_code_file_t *)realloc(idx->files, ncap * sizeof(mm_code_file_t));
		if (!nf) return NULL;
		idx->files = nf;
		idx->cap_files = ncap;
	}
	mm_code_file_t *f = &idx->files[idx->n_files++];
	memset(f, 0, sizeof(*f));
	f->file_id = (uint32_t)idx->n_files;
	return f;
}

static mm_code_symbol_t *alloc_code_symbol(mm_code_index_t *idx) {
	if (idx->n_symbols >= idx->cap_symbols) {
		size_t ncap = idx->cap_symbols == 0 ? 256 : idx->cap_symbols * 2;
		mm_code_symbol_t *ns = (mm_code_symbol_t *)realloc(idx->symbols, ncap * sizeof(mm_code_symbol_t));
		if (!ns) return NULL;
		idx->symbols = ns;
		idx->cap_symbols = ncap;
	}
	mm_code_symbol_t *s = &idx->symbols[idx->n_symbols++];
	memset(s, 0, sizeof(*s));
	s->ref = (uint32_t)idx->n_symbols;
	return s;
}

static bool is_ident_start(char c) {
	return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c) {
	return isalnum((unsigned char)c) || c == '_';
}

static bool is_keyword_or_stdlib(const char *w) {
	static const char *const kw[] = {
		"if", "else", "for", "while", "do", "switch", "case", "default",
		"break", "continue", "return", "goto", "sizeof", "typeof", "alignof",
		"struct", "union", "enum", "typedef", "static", "extern", "inline",
		"const", "volatile", "unsigned", "signed", "void", "int", "char",
		"short", "long", "float", "double", "bool", "size_t", "ssize_t",
		"uint8_t", "uint16_t", "uint32_t", "uint64_t", "int32_t", "int64_t",
		"function", "class", "interface", "type", "export", "import", "from",
		"async", "await", "new", "catch", "try", "finally", "throw", "super",
		"def", "elif", "except", "lambda", "with", "yield", "assert",
		/* C / POSIX / JS stdlib noise denylist */
		"malloc", "calloc", "realloc", "free", "memcpy", "memmove", "memset",
		"memcmp", "memchr", "strlen", "strcpy", "strncpy", "strcat", "strncat",
		"strcmp", "strncmp", "strcasecmp", "strncasecmp", "strchr", "strrchr",
		"strstr", "strdup", "strtok_r", "strtol", "strtoul", "strtod", "atoi",
		"printf", "fprintf", "snprintf", "vsnprintf", "sscanf", "fscanf",
		"fopen", "fclose", "fread", "fwrite", "fgets", "fputs", "fputc", "fflush",
		"open", "close", "read", "write", "stat", "lstat", "mkdir", "unlink",
		"opendir", "readdir", "closedir", "qsort", "time", "gmtime_r", "localtime_r",
		"strftime", "mktime", "push", "pop", "shift", "slice", "splice", "map",
		"filter", "reduce", "forEach", "includes", "indexOf", "trim", "split",
		"join", "replace", "toString", "parseInt", "parseFloat", "len", "range",
		NULL
	};
	for (int i = 0; kw[i]; i++) {
		if (!strcmp(w, kw[i])) return true;
	}
	return false;
}

static void add_raw_callee(mm_code_symbol_t *sym, const char *callee) {
	if (!sym || !callee || !*callee || is_keyword_or_stdlib(callee)) return;
	if (!strcmp(callee, sym->name)) return;
	for (size_t i = 0; i < sym->n_raw_callees; i++) {
		if (!strcmp(sym->raw_callees[i], callee)) return;
	}
	if (sym->n_raw_callees < MM_CODE_MAX_CALLEES) {
		snprintf(sym->raw_callees[sym->n_raw_callees++], 64, "%s", callee);
	}
}

static bool is_snake_or_camel_fn_ident(const char *s, size_t len) {
	if (len < 3 || len >= 64) return false;
	bool has_lower = false;
	bool has_sep_or_upper = false;
	for (size_t i = 0; i < len; i++) {
		if (islower((unsigned char)s[i])) has_lower = true;
		if (s[i] == '_' || (i > 0 && isupper((unsigned char)s[i]))) has_sep_or_upper = true;
	}
	return has_lower && has_sep_or_upper;
}

/* Scan body for direct calls, higher-order callback arguments, C struct designated
 * initializers (.detail = song_detail_handler), and xy_load("./mods/<M>/<M>") edges */
static void scan_body_callees(mm_code_symbol_t *sym, mm_code_file_t *file_rec,
                              const char *body_start, const char *body_end) {
	const char *p = body_start;
	while (p < body_end) {
		if (*p == '"' || *p == '\'') {
			char q = *p++;
			while (p < body_end && *p != q) {
				if (*p == '\\' && p + 1 < body_end) p++;
				p++;
			}
			if (p < body_end) p++;
			continue;
		}
		if (p + 1 < body_end && p[0] == '/' && p[1] == '/') {
			while (p < body_end && *p != '\n') p++;
			continue;
		}
		if (p + 1 < body_end && p[0] == '/' && p[1] == '*') {
			p += 2;
			while (p + 1 < body_end && !(p[0] == '*' && p[1] == '/')) p++;
			if (p + 1 < body_end) p += 2;
			continue;
		}

		/* C struct designated initializer callback: .field = identifier_handler */
		if (*p == '.' && p + 1 < body_end && is_ident_start(p[1])) {
			const char *fp = p + 1;
			while (fp < body_end && is_ident_char(*fp)) fp++;
			while (fp < body_end && (*fp == ' ' || *fp == '\t')) fp++;
			if (fp < body_end && *fp == '=' && (fp + 1 >= body_end || fp[1] != '=')) {
				fp++;
				while (fp < body_end && (*fp == ' ' || *fp == '\t' || *fp == '&')) fp++;
				if (fp < body_end && is_ident_start(*fp)) {
					const char *cb_start = fp;
					while (fp < body_end && is_ident_char(*fp)) fp++;
					size_t cb_len = (size_t)(fp - cb_start);
					if (is_snake_or_camel_fn_ident(cb_start, cb_len)) {
						char cb[64];
						memcpy(cb, cb_start, cb_len);
						cb[cb_len] = '\0';
						add_raw_callee(sym, cb);
					}
					p = fp;
					continue;
				}
			}
		}

		if (is_ident_start(*p)) {
			const char *id_start = p;
			while (p < body_end && is_ident_char(*p)) p++;
			size_t id_len = (size_t)(p - id_start);

			/* Skip struct member function pointer calls like ops->load(...) */
			if (id_start >= body_start + 2 && id_start[-2] == '-' && id_start[-1] == '>') {
				continue;
			}

			const char *ws = p;
			while (ws < body_end && (*ws == ' ' || *ws == '\t')) ws++;
			if (ws < body_end && *ws == '(' && id_len > 1 && id_len < 64) {
				char ident[64];
				memcpy(ident, id_start, id_len);
				ident[id_len] = '\0';
				add_raw_callee(sym, ident);

				/* Check for xy_load("...") module dependency or higher-order callback args */
				const char *arg_p = ws + 1;
				int pdepth = 1;
				while (arg_p < body_end && pdepth > 0) {
					if (*arg_p == '(') pdepth++;
					else if (*arg_p == ')') {
						pdepth--;
						if (pdepth == 0) break;
					} else if (*arg_p == '"' && !strcmp(ident, "xy_load")) {
						const char *s1 = arg_p + 1;
						const char *s2 = s1;
						while (s2 < body_end && *s2 != '"') s2++;
						if (s2 > s1 && file_rec && file_rec->n_imports < 16) {
							size_t sl = (size_t)(s2 - s1);
							if (sl >= 128) sl = 127;
							memcpy(file_rec->imports[file_rec->n_imports], s1, sl);
							file_rec->imports[file_rec->n_imports][sl] = '\0';
							file_rec->n_imports++;
						}
						add_raw_callee(sym, "xy_install");
						arg_p = s2;
					} else if (*arg_p == ',' && pdepth == 1) {
						const char *ca = arg_p + 1;
						while (ca < body_end && (*ca == ' ' || *ca == '\t' || *ca == '\n' || *ca == '\r' || *ca == '&'))
							ca++;
						if (ca < body_end && is_ident_start(*ca)) {
							const char *cb_end = ca;
							while (cb_end < body_end && is_ident_char(*cb_end)) cb_end++;
							const char *after_cb = cb_end;
							while (after_cb < body_end && (*after_cb == ' ' || *after_cb == '\t')) after_cb++;
							if (after_cb < body_end && (*after_cb == ',' || *after_cb == ')')) {
								size_t cb_len = (size_t)(cb_end - ca);
								if (is_snake_or_camel_fn_ident(ca, cb_len)) {
									char cb[64];
									memcpy(cb, ca, cb_len);
									cb[cb_len] = '\0';
									add_raw_callee(sym, cb);
								}
							}
						}
					}
					arg_p++;
				}
			}
			continue;
		}
		p++;
	}
}

static bool is_http_method_str(const char *s, size_t len, char *upper_out) {
	static const char *const methods[] = {
		"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS", NULL
	};
	for (int i = 0; methods[i]; i++) {
		if (strlen(methods[i]) == len && strncasecmp(s, methods[i], len) == 0) {
			strcpy(upper_out, methods[i]);
			return true;
		}
	}
	return false;
}

/* Library-agnostic HTTP Route detector:
 * Pattern 1: app.get("/path", handler)
 * Pattern 2: register_route("POST", "/path", handler)
 * Pattern 3: route("GET:/path", handler) or route("GET /path", handler)
 * Pattern 4: register_handler("/path", handler) where fn name contains "route"/"handler"
 */
static void scan_http_route_on_line(const char *file_path, uint32_t file_id,
                                    int line_no, const char *s, mm_code_index_t *idx) {
	const char *lp = strchr(s, '(');
	if (!lp || lp == s) return;

	/* Extract function/method name before '(' */
	const char *fn_end = lp;
	while (fn_end > s && (fn_end[-1] == ' ' || fn_end[-1] == '\t')) fn_end--;
	const char *fn_start = fn_end;
	while (fn_start > s && is_ident_char(fn_start[-1])) fn_start--;
	size_t fn_len = (size_t)(fn_end - fn_start);
	if (fn_len == 0 || fn_len >= 64) return;

	char fn_name[64];
	memcpy(fn_name, fn_start, fn_len);
	fn_name[fn_len] = '\0';
	if (is_keyword_or_stdlib(fn_name)) return;

	/* Extract up to 2 string literals and trailing handler identifier inside (...) */
	const char *p = lp + 1;
	char str1[128] = {0};
	char str2[128] = {0};
	char handler[64] = {0};

	while (*p == ' ' || *p == '\t') p++;
	if (*p == '"' || *p == '\'') {
		char q = *p++;
		size_t j = 0;
		while (*p && *p != q && j + 1 < sizeof(str1)) str1[j++] = *p++;
		str1[j] = '\0';
		if (*p == q) p++;
	}
	if (!str1[0]) return;

	while (*p && *p != ',' && *p != ')') p++;
	if (*p == ',') {
		p++;
		while (*p == ' ' || *p == '\t' || *p == '&') p++;
		if (*p == '"' || *p == '\'') {
			char q = *p++;
			size_t j = 0;
			while (*p && *p != q && j + 1 < sizeof(str2)) str2[j++] = *p++;
			str2[j] = '\0';
			if (*p == q) p++;
			while (*p && *p != ',' && *p != ')') p++;
			if (*p == ',') {
				p++;
				while (*p == ' ' || *p == '\t' || *p == '&') p++;
			}
		}
		if (is_ident_start(*p)) {
			size_t j = 0;
			while (is_ident_char(*p) && j + 1 < sizeof(handler)) handler[j++] = *p++;
			handler[j] = '\0';
		}
	}

	char method[16] = {0};
	char route_path[128] = {0};

	if (is_http_method_str(fn_name, fn_len, method) && str1[0] == '/') {
		/* Pattern 1: app.get("/path", handler) */
		snprintf(route_path, sizeof(route_path), "%s", str1);
	} else if (is_http_method_str(str1, strlen(str1), method) && str2[0] == '/') {
		/* Pattern 2: register_route("POST", "/path", handler) */
		snprintf(route_path, sizeof(route_path), "%s", str2);
	} else {
		/* Pattern 3: "GET:/path" or "POST /path" */
		const char *sep = strpbrk(str1, ": ");
		if (sep && sep > str1 && sep[1] == '/' &&
		    is_http_method_str(str1, (size_t)(sep - str1), method)) {
			snprintf(route_path, sizeof(route_path), "%s", sep + 1);
		} else if (str1[0] == '/' &&
		           (strstr(fn_name, "route") || strstr(fn_name, "Route") ||
		            strstr(fn_name, "handler") || strstr(fn_name, "Handler"))) {
			/* Pattern 4: register_handler("/path", handler) */
			strcpy(method, "ANY");
			snprintf(route_path, sizeof(route_path), "%s", str1);
		}
	}

	if (!method[0] || !route_path[0]) return;

	mm_code_symbol_t *sym = alloc_code_symbol(idx);
	if (!sym) return;
	sym->file_id = file_id;
	sym->kind    = MM_SYM_ROUTE;
	snprintf(sym->name, sizeof(sym->name), "%s %s", method, route_path);
	snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
	snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
	sym->start_line = line_no;
	sym->end_line   = line_no;
	snprintf(sym->signature, sizeof(sym->signature), "%s", s);
	if (handler[0] && !is_keyword_or_stdlib(handler)) {
		add_raw_callee(sym, handler);
	}
}

static void extract_leading_doc_comment(const char **line_starts, const size_t *line_lens,
                                        int sym_line_idx, char *out, size_t cap) {
	out[0] = '\0';
	if (sym_line_idx <= 0) return;
	int k = sym_line_idx - 1;
	while (k >= 0) {
		const char *ls = line_starts[k];
		size_t ll = line_lens[k];
		while (ll > 0 && (*ls == ' ' || *ls == '\t')) { ls++; ll--; }
		if (ll == 0) break;
		if ((ll >= 2 && ls[0] == '/' && ls[1] == '/') ||
		    (ll >= 2 && ls[0] == '/' && ls[1] == '*') ||
		    ls[0] == '*' || ls[0] == '#') {
			while (ll > 0 && (*ls == '/' || *ls == '*' || *ls == '#' || *ls == ' ' || *ls == '\t')) {
				ls++; ll--;
			}
			if (ll > 0) {
				size_t copy_n = ll < cap - 1 ? ll : cap - 1;
				memcpy(out, ls, copy_n);
				out[copy_n] = '\0';
				return;
			}
			k--;
		} else {
			break;
		}
	}
}

/* ============================================================================
 * 2. Multi-Language Source File Scanner (C/H, TS/JS, Python)
 * ============================================================================ */

int mm_code_scan_source(const char *file_path, const char *content, size_t len,
                        uint32_t file_id, mm_code_index_t *idx) {
	if (!file_path || !content || !idx) return -1;
	if (len == 0) len = strlen(content);

	mm_code_file_t *f = alloc_code_file(idx);
	if (!f) return -1;
	if (file_id > 0) f->file_id = file_id;
	snprintf(f->file_path, sizeof(f->file_path), "%s", file_path);
	f->byte_size = len;

	int line_cap = 512;
	int line_count = 0;
	const char **line_starts = (const char **)malloc((size_t)line_cap * sizeof(char *));
	size_t *line_lens = (size_t *)malloc((size_t)line_cap * sizeof(size_t));
	if (!line_starts || !line_lens) {
		free(line_starts);
		free(line_lens);
		return -1;
	}

	const char *p = content;
	const char *end = content + len;
	while (p < end) {
		const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
		size_t llen = nl ? (size_t)(nl - p) : (size_t)(end - p);
		if (llen > 0 && p[llen - 1] == '\r') llen--;
		if (line_count >= line_cap) {
			line_cap *= 2;
			line_starts = (const char **)realloc(line_starts, (size_t)line_cap * sizeof(char *));
			line_lens   = (size_t *)realloc(line_lens, (size_t)line_cap * sizeof(size_t));
		}
		line_starts[line_count] = p;
		line_lens[line_count]   = llen;
		line_count++;
		p = nl ? (nl + 1) : end;
	}
	f->line_count = line_count;

	const char *ext = strrchr(file_path, '.');
	bool is_c  = ext && (!strcmp(ext, ".c") || !strcmp(ext, ".h") || !strcmp(ext, ".cpp") || !strcmp(ext, ".hpp"));
	bool is_ts = ext && (!strcmp(ext, ".ts") || !strcmp(ext, ".tsx") || !strcmp(ext, ".js") || !strcmp(ext, ".mjs"));
	bool is_py = ext && !strcmp(ext, ".py");

	char active_parent_class[128] = {0};
	int  parent_class_end_line = -1;

	for (int i = 0; i < line_count; i++) {
		if (parent_class_end_line > 0 && (i + 1) > parent_class_end_line) {
			active_parent_class[0] = '\0';
			parent_class_end_line = -1;
		}

		char line[512];
		size_t clen = line_lens[i] < sizeof(line) - 1 ? line_lens[i] : sizeof(line) - 1;
		memcpy(line, line_starts[i], clen);
		line[clen] = '\0';

		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		if (!*s || (s[0] == '/' && s[1] == '/') || (s[0] == '/' && s[1] == '*') || s[0] == '*')
			continue;

		/* Check HTTP route registration on any line */
		scan_http_route_on_line(file_path, f->file_id, i + 1, s, idx);

		/* 1. Extract imports / #includes */
		if (strncmp(s, "#include", 8) == 0 && f->n_imports < 16) {
			const char *q1 = strpbrk(s + 8, "\"<");
			if (q1) {
				char close_ch = (*q1 == '<') ? '>' : '"';
				const char *q2 = strchr(q1 + 1, close_ch);
				if (q2 && q2 > q1 + 1) {
					size_t ilen = (size_t)(q2 - (q1 + 1));
					if (ilen >= 128) ilen = 127;
					memcpy(f->imports[f->n_imports], q1 + 1, ilen);
					f->imports[f->n_imports][ilen] = '\0';
					f->n_imports++;
				}
			}
			continue;
		}
		if (is_ts && (strncmp(s, "import ", 7) == 0 || strncmp(s, "export ", 7) == 0) && strstr(s, " from ")) {
			const char *fr = strstr(s, " from ");
			const char *q1 = strpbrk(fr + 6, "\"'");
			if (q1 && f->n_imports < 16) {
				const char *q2 = strchr(q1 + 1, *q1);
				if (q2 && q2 > q1 + 1) {
					size_t ilen = (size_t)(q2 - (q1 + 1));
					if (ilen >= 128) ilen = 127;
					memcpy(f->imports[f->n_imports], q1 + 1, ilen);
					f->imports[f->n_imports][ilen] = '\0';
					f->n_imports++;
				}
			}
		}

		/* 2. Macro definitions in C */
		if (is_c && strncmp(s, "#define ", 8) == 0) {
			const char *mp = s + 8;
			while (*mp == ' ' || *mp == '\t') mp++;
			const char *id_start = mp;
			while (is_ident_char(*mp)) mp++;
			size_t id_len = (size_t)(mp - id_start);
			if (id_len > 1 && id_len < 120 && !strstr(id_start, "_H")) {
				mm_code_symbol_t *sym = alloc_code_symbol(idx);
				if (sym) {
					sym->file_id = f->file_id;
					memcpy(sym->name, id_start, id_len);
					sym->name[id_len] = '\0';
					sym->kind = MM_SYM_MACRO;
					snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
					snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
					sym->start_line = i + 1;
					sym->end_line = i + 1;
					snprintf(sym->signature, sizeof(sym->signature), "%s", s);
					extract_leading_doc_comment(line_starts, line_lens, i, sym->doc_comment, sizeof(sym->doc_comment));
				}
			}
			continue;
		}

		/* 2b. libxylem XY_DECL / XY_IMPL and STL_WASM_MODULE macros in C */
		if (is_c && (strncmp(s, "XY_DECL(", 8) == 0 || strncmp(s, "XY_IMPL(", 8) == 0)) {
			bool is_decl = (s[3] == 'D');
			const char *a1 = s + 8;
			const char *comma1 = strchr(a1, ',');
			if (comma1) {
				const char *a2 = comma1 + 1;
				while (*a2 == ' ' || *a2 == '\t') a2++;
				const char *id_end = a2;
				while (is_ident_char(*id_end)) id_end++;
				size_t id_len = (size_t)(id_end - a2);
				if (id_len > 0 && id_len < 120) {
					int end_l = i;
					if (!is_decl) {
						int brace_depth = 0;
						bool seen_brace = false;
						for (int k = i; k < line_count; k++) {
							for (size_t c = 0; c < line_lens[k]; c++) {
								char ch = line_starts[k][c];
								if (ch == '{') { brace_depth++; seen_brace = true; }
								else if (ch == '}') { brace_depth--; }
							}
							end_l = k;
							if (seen_brace && brace_depth <= 0) break;
							if (!seen_brace && k > i + 4) break;
						}
					}
					mm_code_symbol_t *sym = alloc_code_symbol(idx);
					if (sym) {
						sym->file_id = f->file_id;
						memcpy(sym->name, a2, id_len);
						sym->name[id_len] = '\0';
						sym->kind = is_decl ? MM_SYM_INTERFACE : MM_SYM_FUNCTION;
						snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
						snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
						sym->start_line = i + 1;
						sym->end_line   = end_l + 1;
						snprintf(sym->signature, sizeof(sym->signature), "%s", s);
						extract_leading_doc_comment(line_starts, line_lens, i, sym->doc_comment, sizeof(sym->doc_comment));
						if (!is_decl && end_l >= i) {
							scan_body_callees(sym, f, line_starts[i], line_starts[end_l] + line_lens[end_l]);
							i = end_l;
						}
					}
				}
			}
			continue;
		}

		if (is_c && strncmp(s, "STL_WASM_MODULE(", 16) == 0) {
			const char *a1 = s + 16;
			while (*a1 == ' ' || *a1 == '\t' || *a1 == '"') a1++;
			const char *id_end = a1;
			while (is_ident_char(*id_end)) id_end++;
			size_t id_len = (size_t)(id_end - a1);
			if (id_len > 0 && id_len < 100) {
				mm_code_symbol_t *sym = alloc_code_symbol(idx);
				if (sym) {
					sym->file_id = f->file_id;
					snprintf(sym->name, sizeof(sym->name), "STL_WASM_MODULE_%.*s", (int)id_len, a1);
					sym->kind = MM_SYM_MACRO;
					snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
					snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
					sym->start_line = i + 1;
					sym->end_line   = i + 1;
					snprintf(sym->signature, sizeof(sym->signature), "%s", s);
					scan_body_callees(sym, f, line_starts[i], line_starts[i] + line_lens[i]);
				}
			}
			continue;
		}

		/* 3. TypeScript / JS declarations & Class Methods */
		if (is_ts) {
			const char *cur = s;
			if (strncmp(cur, "export ", 7) == 0) { cur += 7; while (*cur == ' ') cur++; }
			if (strncmp(cur, "default ", 8) == 0) { cur += 8; while (*cur == ' ') cur++; }
			if (strncmp(cur, "async ", 6) == 0) { cur += 6; while (*cur == ' ') cur++; }

			mm_sym_kind_t kind = MM_SYM_FUNCTION;
			const char *after_kw = NULL;
			if (strncmp(cur, "function ", 9) == 0) { kind = MM_SYM_FUNCTION; after_kw = cur + 9; }
			else if (strncmp(cur, "class ", 6) == 0) { kind = MM_SYM_CLASS; after_kw = cur + 6; }
			else if (strncmp(cur, "interface ", 10) == 0) { kind = MM_SYM_INTERFACE; after_kw = cur + 10; }
			else if (strncmp(cur, "type ", 5) == 0) { kind = MM_SYM_TYPE; after_kw = cur + 5; }
			else if (strncmp(cur, "enum ", 5) == 0) { kind = MM_SYM_ENUM; after_kw = cur + 5; }

			if (after_kw) {
				while (*after_kw == ' ') after_kw++;
				const char *id_start = after_kw;
				while (is_ident_char(*after_kw)) after_kw++;
				size_t id_len = (size_t)(after_kw - id_start);
				if (id_len > 0 && id_len < 120) {
					mm_code_symbol_t *sym = alloc_code_symbol(idx);
					if (sym) {
						sym->file_id = f->file_id;
						memcpy(sym->name, id_start, id_len);
						sym->name[id_len] = '\0';
						sym->kind = kind;
						snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
						snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
						sym->start_line = i + 1;
						sym->end_line = i + 1;
						snprintf(sym->signature, sizeof(sym->signature), "%s", s);
						extract_leading_doc_comment(line_starts, line_lens, i, sym->doc_comment, sizeof(sym->doc_comment));

						int brace_depth = 0;
						bool seen_brace = false;
						int end_l = i;
						for (int k = i; k < line_count; k++) {
							for (size_t c = 0; c < line_lens[k]; c++) {
								char ch = line_starts[k][c];
								if (ch == '{') { brace_depth++; seen_brace = true; }
								else if (ch == '}') { brace_depth--; }
							}
							end_l = k;
							if (seen_brace && brace_depth <= 0) break;
							if (!seen_brace && k > i + 3) break;
						}
						sym->end_line = end_l + 1;
						if (kind == MM_SYM_CLASS) {
							snprintf(active_parent_class, sizeof(active_parent_class), "%s", sym->name);
							parent_class_end_line = sym->end_line;
						} else if (seen_brace) {
							scan_body_callees(sym, f, line_starts[i],
							                  line_starts[end_l] + line_lens[end_l]);
						}
					}
				}
				continue;
			}

			/* Check TS/JS class method inside active_parent_class */
			if (active_parent_class[0] && (line[0] == ' ' || line[0] == '\t')) {
				const char *mc = cur;
				if (strncmp(mc, "public ", 7) == 0) mc += 7;
				else if (strncmp(mc, "private ", 8) == 0) mc += 8;
				else if (strncmp(mc, "protected ", 10) == 0) mc += 10;
				while (*mc == ' ') mc++;
				if (strncmp(mc, "static ", 7) == 0) { mc += 7; while (*mc == ' ') mc++; }
				if (strncmp(mc, "async ", 6) == 0) { mc += 6; while (*mc == ' ') mc++; }

				const char *m_start = mc;
				while (is_ident_char(*mc)) mc++;
				size_t m_len = (size_t)(mc - m_start);
				while (*mc == ' ') mc++;
				if (m_len > 1 && m_len < 100 && *mc == '(' && strchr(mc, '{')) {
					char mname[128];
					memcpy(mname, m_start, m_len);
					mname[m_len] = '\0';
					if (!is_keyword_or_stdlib(mname) && strcmp(mname, "constructor") != 0) {
						int brace_depth = 0;
						bool seen_brace = false;
						int end_l = i;
						for (int k = i; k < line_count; k++) {
							for (size_t c = 0; c < line_lens[k]; c++) {
								char ch = line_starts[k][c];
								if (ch == '{') { brace_depth++; seen_brace = true; }
								else if (ch == '}') { brace_depth--; }
							}
							end_l = k;
							if (seen_brace && brace_depth <= 0) break;
						}
						mm_code_symbol_t *sym = alloc_code_symbol(idx);
						if (sym) {
							sym->file_id = f->file_id;
							snprintf(sym->name, sizeof(sym->name), "%s", mname);
							snprintf(sym->parent_symbol, sizeof(sym->parent_symbol), "%s", active_parent_class);
							sym->kind = MM_SYM_METHOD;
							snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
							snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s.%s",
							         file_path, active_parent_class, sym->name);
							sym->start_line = i + 1;
							sym->end_line   = end_l + 1;
							snprintf(sym->signature, sizeof(sym->signature), "%s", s);
							scan_body_callees(sym, f, line_starts[i], line_starts[end_l] + line_lens[end_l]);
						}
						i = end_l;
						continue;
					}
				}
			}
		}

		/* 4. Python def / class */
		if (is_py && (strncmp(s, "def ", 4) == 0 || strncmp(s, "class ", 6) == 0)) {
			mm_sym_kind_t kind = (s[0] == 'c') ? MM_SYM_CLASS : MM_SYM_FUNCTION;
			const char *id_start = s + (kind == MM_SYM_CLASS ? 6 : 4);
			while (*id_start == ' ') id_start++;
			const char *id_end = id_start;
			while (is_ident_char(*id_end)) id_end++;
			size_t id_len = (size_t)(id_end - id_start);
			if (id_len > 0 && id_len < 120) {
				mm_code_symbol_t *sym = alloc_code_symbol(idx);
				if (sym) {
					sym->file_id = f->file_id;
					memcpy(sym->name, id_start, id_len);
					sym->name[id_len] = '\0';
					if (kind == MM_SYM_CLASS) {
						snprintf(active_parent_class, sizeof(active_parent_class), "%s", sym->name);
						parent_class_end_line = i + 50;
					} else if (line[0] == ' ' || line[0] == '\t') {
						if (active_parent_class[0]) {
							snprintf(sym->parent_symbol, sizeof(sym->parent_symbol), "%s", active_parent_class);
							sym->kind = MM_SYM_METHOD;
						}
					} else {
						active_parent_class[0] = '\0';
					}
					if (sym->kind == 0) sym->kind = kind;
					snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
					if (sym->parent_symbol[0]) {
						snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s.%s",
						         file_path, sym->parent_symbol, sym->name);
					} else {
						snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
					}
					sym->start_line = i + 1;
					sym->end_line = (i + 15 < line_count) ? (i + 15) : line_count;
					snprintf(sym->signature, sizeof(sym->signature), "%s", s);
					scan_body_callees(sym, f, line_starts[i],
					                  line_starts[sym->end_line - 1] + line_lens[sym->end_line - 1]);
				}
			}
			continue;
		}

		/* 5. C/H Struct, Enum, Typedef, and Function definitions */
		if (is_c && line[0] != ' ' && line[0] != '\t' && line[0] != '#') {
			if (strncmp(s, "typedef struct", 14) == 0 || strncmp(s, "struct ", 7) == 0) {
				int brace_depth = 0;
				bool seen_brace = false;
				int end_l = i;
				for (int k = i; k < line_count; k++) {
					for (size_t c = 0; c < line_lens[k]; c++) {
						char ch = line_starts[k][c];
						if (ch == '{') { brace_depth++; seen_brace = true; }
						else if (ch == '}') { brace_depth--; }
					}
					end_l = k;
					if (seen_brace && brace_depth <= 0) break;
					if (!seen_brace && strchr(line, ';')) break;
					if (!seen_brace && k > i + 2) break;
				}
				if (seen_brace) {
					char sname[128] = {0};
					if (strncmp(s, "struct ", 7) == 0) {
						const char *id = s + 7;
						while (*id == ' ') id++;
						size_t n = 0;
						while (is_ident_char(id[n]) && n < sizeof(sname) - 1) {
							sname[n] = id[n];
							n++;
						}
						sname[n] = '\0';
					}
					if (!sname[0] && end_l < line_count) {
						char eline[256];
						size_t elen = line_lens[end_l] < sizeof(eline) - 1 ? line_lens[end_l] : sizeof(eline) - 1;
						memcpy(eline, line_starts[end_l], elen);
						eline[elen] = '\0';
						const char *rb = strrchr(eline, '}');
						if (rb) {
							rb++;
							while (*rb == ' ' || *rb == '\t') rb++;
							size_t n = 0;
							while (is_ident_char(rb[n]) && n < sizeof(sname) - 1) {
								sname[n] = rb[n];
								n++;
							}
							sname[n] = '\0';
						}
					}
					if (sname[0]) {
						mm_code_symbol_t *sym = alloc_code_symbol(idx);
						if (sym) {
							sym->file_id = f->file_id;
							snprintf(sym->name, sizeof(sym->name), "%s", sname);
							sym->kind = MM_SYM_STRUCT;
							snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
							snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
							sym->start_line = i + 1;
							sym->end_line = end_l + 1;
							snprintf(sym->signature, sizeof(sym->signature), "struct %s", sname);
							extract_leading_doc_comment(line_starts, line_lens, i, sym->doc_comment, sizeof(sym->doc_comment));
						}
					}
					i = end_l;
					continue;
				}
			}

			/* Top-level C function definition */
			const char *lparen = strchr(s, '(');
			if (lparen && lparen > s && !strchr(s, ';')) {
				const char *id_end = lparen;
				while (id_end > s && (id_end[-1] == ' ' || id_end[-1] == '\t')) id_end--;
				const char *id_start = id_end;
				while (id_start > s && is_ident_char(id_start[-1])) id_start--;
				size_t id_len = (size_t)(id_end - id_start);

				if (id_len > 1 && id_len < 120) {
					char fname[128];
					memcpy(fname, id_start, id_len);
					fname[id_len] = '\0';
					if (!is_keyword_or_stdlib(fname)) {
						bool has_open_brace = false;
						bool has_semi = false;
						int brace_line = i;
						for (int k = i; k < line_count && k <= i + 4; k++) {
							for (size_t c = 0; c < line_lens[k]; c++) {
								char ch = line_starts[k][c];
								if (ch == ';') { has_semi = true; break; }
								if (ch == '{') { has_open_brace = true; brace_line = k; break; }
							}
							if (has_semi || has_open_brace) break;
						}
						if (has_open_brace && !has_semi) {
							int brace_depth = 0;
							int end_l = brace_line;
							for (int k = brace_line; k < line_count; k++) {
								for (size_t c = 0; c < line_lens[k]; c++) {
									char ch = line_starts[k][c];
									if (ch == '{') brace_depth++;
									else if (ch == '}') brace_depth--;
								}
								end_l = k;
								if (brace_depth <= 0) break;
							}
							mm_code_symbol_t *sym = alloc_code_symbol(idx);
							if (sym) {
								sym->file_id = f->file_id;
								snprintf(sym->name, sizeof(sym->name), "%s", fname);
								sym->kind = MM_SYM_FUNCTION;
								snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
								snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
								sym->start_line = i + 1;
								sym->end_line = end_l + 1;
								snprintf(sym->signature, sizeof(sym->signature), "%s", s);
								extract_leading_doc_comment(line_starts, line_lens, i, sym->doc_comment, sizeof(sym->doc_comment));
								scan_body_callees(sym, f, line_starts[brace_line],
								                  line_starts[end_l] + line_lens[end_l]);
							}
							i = end_l;
							continue;
						}
					}
				}
			}
		}
	}

	free(line_starts);
	free(line_lens);
	return 0;
}

/* ============================================================================
 * 3. Recursive Directory Indexer & Call-Graph Resolver
 * ============================================================================ */

static bool should_skip_dir(const char *name) {
	if (!name || name[0] == '.') return true;
	return !strcmp(name, "node_modules") || !strcmp(name, "bin") ||
	       !strcmp(name, "lib") || !strcmp(name, "build") ||
	       !strcmp(name, "dist") || !strcmp(name, "var") ||
	       !strcmp(name, "prebuilds");
}

static bool is_supported_source_file(const char *name) {
	const char *ext = strrchr(name, '.');
	if (!ext) return false;
	return !strcmp(ext, ".c") || !strcmp(ext, ".h") ||
	       !strcmp(ext, ".ts") || !strcmp(ext, ".tsx") ||
	       !strcmp(ext, ".js") || !strcmp(ext, ".py");
}

static void walk_dir_recursive(mm_code_index_t *idx, const char *base_root, const char *rel_dir) {
	char full_dir[PATH_MAX];
	if (rel_dir[0]) snprintf(full_dir, sizeof(full_dir), "%s/%s", base_root, rel_dir);
	else snprintf(full_dir, sizeof(full_dir), "%s", base_root);

	DIR *d = opendir(full_dir);
	if (!d) return;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

		char rel_path[PATH_MAX];
		if (rel_dir[0]) snprintf(rel_path, sizeof(rel_path), "%s/%s", rel_dir, ent->d_name);
		else snprintf(rel_path, sizeof(rel_path), "%s", ent->d_name);

		char abs_path[PATH_MAX];
		snprintf(abs_path, sizeof(abs_path), "%s/%s", base_root, rel_path);

		struct stat st;
		if (lstat(abs_path, &st) != 0) continue;
#ifdef S_ISLNK
		if (S_ISLNK(st.st_mode)) continue;
#endif
		if (S_ISDIR(st.st_mode)) {
			if (!should_skip_dir(ent->d_name)) {
				walk_dir_recursive(idx, base_root, rel_path);
			}
			continue;
		}
		if (S_ISREG(st.st_mode) && is_supported_source_file(ent->d_name) && st.st_size > 0 && st.st_size < (2 * 1024 * 1024)) {
			FILE *fp = fopen(abs_path, "rb");
			if (!fp) continue;
			char *buf = (char *)malloc((size_t)st.st_size + 1);
			if (buf) {
				size_t rd = fread(buf, 1, (size_t)st.st_size, fp);
				buf[rd] = '\0';
				mm_code_scan_source(rel_path, buf, rd, 0, idx);
				if (idx->n_files > 0) {
					idx->files[idx->n_files - 1].mtime_ms = (int64_t)st.st_mtime * 1000;
				}
				free(buf);
			}
			fclose(fp);
		}
	}
	closedir(d);
}

void mm_code_resolve_call_graph(mm_code_index_t *idx) {
	if (!idx) return;
	for (size_t i = 0; i < idx->n_symbols; i++) {
		idx->symbols[i].n_callees = 0;
		idx->symbols[i].n_callers = 0;
	}

	for (size_t i = 0; i < idx->n_symbols; i++) {
		mm_code_symbol_t *caller = &idx->symbols[i];
		for (size_t r = 0; r < caller->n_raw_callees; r++) {
			const char *target_name = caller->raw_callees[r];
			/* Prefer Function/Method implementations over Interface (XY_DECL) when resolving */
			ssize_t best_j = -1;
			for (size_t j = 0; j < idx->n_symbols; j++) {
				if (i == j) continue;
				mm_code_symbol_t *cand = &idx->symbols[j];
				if (strcmp(cand->name, target_name) == 0) {
					if (best_j < 0) best_j = (ssize_t)j;
					else if (idx->symbols[best_j].kind == MM_SYM_INTERFACE && cand->kind == MM_SYM_FUNCTION) {
						best_j = (ssize_t)j;
					} else if (!strcmp(cand->file_path, caller->file_path)) {
						best_j = (ssize_t)j;
						break;
					}
				}
			}
			if (best_j >= 0) {
				mm_code_symbol_t *callee = &idx->symbols[best_j];
				if (caller->n_callees < MM_CODE_MAX_EDGES) {
					bool dup = false;
					for (size_t k = 0; k < caller->n_callees; k++) {
						if (caller->callees[k] == callee->ref) { dup = true; break; }
					}
					if (!dup) caller->callees[caller->n_callees++] = callee->ref;
				}
				if (callee->n_callers < MM_CODE_MAX_EDGES) {
					bool dup = false;
					for (size_t k = 0; k < callee->n_callers; k++) {
						if (callee->callers[k] == caller->ref) { dup = true; break; }
					}
					if (!dup) callee->callers[callee->n_callers++] = caller->ref;
				}
			}
		}
	}
}

int mm_code_index_dir(mm_code_index_t *idx, const char *root_dir) {
	if (!idx) return -1;
	const char *root = (root_dir && *root_dir) ? root_dir : idx->root_dir;
	mm_code_index_free(idx);
	mm_code_index_init(idx, root);
	walk_dir_recursive(idx, idx->root_dir, "");
	mm_code_resolve_call_graph(idx);
	return 0;
}

/* ============================================================================
 * 4. Manifest Persistence (.mm/code_manifest.tsv)
 * ============================================================================ */

int mm_code_save_manifest(const mm_code_index_t *idx, const char *manifest_path) {
	if (!idx || !manifest_path) return -1;
	FILE *fp = fopen(manifest_path, "w");
	if (!fp) return -1;
	fprintf(fp, "# MM_CODE_MANIFEST_V2\t%zu\t%zu\n", idx->n_files, idx->n_symbols);
	for (size_t i = 0; i < idx->n_symbols; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		fprintf(fp, "SYM\t%u\t%u\t%s\t%s\t%s\t%d\t%d\t%s\t",
		        s->ref, s->file_id, s->name, mm_sym_kind_to_str(s->kind),
		        s->file_path, s->start_line, s->end_line, s->signature);
		for (size_t c = 0; c < s->n_raw_callees; c++) {
			fprintf(fp, "%s%s", (c > 0 ? "," : ""), s->raw_callees[c]);
		}
		fprintf(fp, "\t%s\n", s->parent_symbol);
	}
	fclose(fp);
	return 0;
}

int mm_code_load_manifest(mm_code_index_t *idx, const char *manifest_path) {
	if (!idx || !manifest_path) return -1;
	FILE *fp = fopen(manifest_path, "r");
	if (!fp) return -1;

	char line[2048];
	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
		if (strncmp(line, "SYM\t", 4) != 0) continue;

		char *fields[12] = {0};
		int nf = 0;
		char *p = line + 4;
		while (nf < 11) {
			fields[nf++] = p;
			char *tab = strchr(p, '\t');
			if (!tab) break;
			*tab = '\0';
			p = tab + 1;
		}
		if (nf < 8) continue;

		mm_code_symbol_t *s = alloc_code_symbol(idx);
		if (!s) break;
		s->ref        = (uint32_t)strtoul(fields[0], NULL, 10);
		s->file_id    = (uint32_t)strtoul(fields[1], NULL, 10);
		snprintf(s->name, sizeof(s->name), "%s", fields[2]);
		s->kind       = mm_sym_kind_from_str(fields[3]);
		snprintf(s->file_path, sizeof(s->file_path), "%s", fields[4]);
		s->start_line = atoi(fields[5]);
		s->end_line   = atoi(fields[6]);
		snprintf(s->signature, sizeof(s->signature), "%s", fields[7]);

		if (nf >= 9 && fields[8] && fields[8][0]) {
			char *saveptr = NULL;
			char *tok = strtok_r(fields[8], ",", &saveptr);
			while (tok) {
				add_raw_callee(s, tok);
				tok = strtok_r(NULL, ",", &saveptr);
			}
		}
		if (nf >= 10 && fields[9] && fields[9][0]) {
			snprintf(s->parent_symbol, sizeof(s->parent_symbol), "%s", fields[9]);
			snprintf(s->qualified_name, sizeof(s->qualified_name), "%s:%s.%s",
			         s->file_path, s->parent_symbol, s->name);
		} else {
			snprintf(s->qualified_name, sizeof(s->qualified_name), "%s:%s", s->file_path, s->name);
		}
	}
	fclose(fp);
	mm_code_resolve_call_graph(idx);
	return 0;
}

/* ============================================================================
 * 5. Cross-Boundary Pairs, XY Contract Flight Check & First-Touch Module Dossier
 * ============================================================================ */

static const mm_code_symbol_t *find_sym_by_ref(const mm_code_index_t *idx, uint32_t ref) {
	if (!idx || ref == 0) return NULL;
	if (ref <= idx->n_symbols && idx->symbols[ref - 1].ref == ref) {
		return &idx->symbols[ref - 1];
	}
	for (size_t i = 0; i < idx->n_symbols; i++) {
		if (idx->symbols[i].ref == ref) return &idx->symbols[i];
	}
	return NULL;
}

static bool str_contains_ci(const char *haystack, const char *needle) {
	if (!needle || !*needle) return true;
	if (!haystack || !*haystack) return false;
	size_t nlen = strlen(needle);
	for (const char *p = haystack; *p; p++) {
		if (strncasecmp(p, needle, nlen) == 0) return true;
	}
	return false;
}

int mm_code_resolve_cross_boundary_pairs(const mm_code_index_t *idx,
                                         const char *symbol_name,
                                         const char *file_path,
                                         char **out_buf, size_t *out_len) {
	if (!idx || !out_buf) return -1;
	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	buf[0] = '\0';
	size_t pairs = 0;

	/* 1. XY_DECL <-> XY_IMPL pairs for symbol_name */
	if (symbol_name && *symbol_name) {
		const mm_code_symbol_t *decl_sym = NULL;
		const mm_code_symbol_t *impl_sym = NULL;
		for (size_t i = 0; i < idx->n_symbols; i++) {
			const mm_code_symbol_t *s = &idx->symbols[i];
			if (!strcmp(s->name, symbol_name)) {
				if (strncmp(s->signature, "XY_DECL(", 8) == 0) decl_sym = s;
				else if (strncmp(s->signature, "XY_IMPL(", 8) == 0) impl_sym = s;
			}
		}
		if (decl_sym && impl_sym) {
			if (pairs == 0) {
				mm_buf_appendf(&buf, &pos, &cap,
					"=== Cross-Boundary Protocol & Isomorphic Pairs ===\n");
			}
			pairs++;
			mm_buf_appendf(&buf, &pos, &cap,
				"  • [XY Protocol Pair] %s: Decl (%s:%d) <-> Impl (%s:%d)\n",
				symbol_name, decl_sym->file_path, decl_sym->start_line,
				impl_sym->file_path, impl_sym->start_line);
		}
	}

	/* 2. Server C (mods/<M>/<M>.c) <-> Isomorphic UX WASM (mods/<M>/ux/<file>.c) */
	const char *check_path = file_path;
	if (!check_path && symbol_name) {
		for (size_t i = 0; i < idx->n_symbols; i++) {
			if (!strcmp(idx->symbols[i].name, symbol_name)) {
				check_path = idx->symbols[i].file_path;
				break;
			}
		}
	}
	if (check_path && strncmp(check_path, "mods/", 5) == 0) {
		char mod_name[64] = {0};
		const char *mstart = check_path + 5;
		const char *slash = strchr(mstart, '/');
		if (slash && (size_t)(slash - mstart) < sizeof(mod_name)) {
			memcpy(mod_name, mstart, (size_t)(slash - mstart));
			mod_name[slash - mstart] = '\0';
			bool is_ux = strstr(check_path, "/ux/") != NULL;
			char prefix[128];
			if (is_ux) snprintf(prefix, sizeof(prefix), "mods/%s/%s.c", mod_name, mod_name);
			else snprintf(prefix, sizeof(prefix), "mods/%s/ux/", mod_name);

			for (size_t i = 0; i < idx->n_symbols; i++) {
				if (strncmp(idx->symbols[i].file_path, prefix, strlen(prefix)) == 0) {
					if (pairs == 0) {
						mm_buf_appendf(&buf, &pos, &cap,
							"=== Cross-Boundary Protocol & Isomorphic Pairs ===\n");
					}
					pairs++;
					mm_buf_appendf(&buf, &pos, &cap,
						"  • [Isomorphic Server/UX Pair] %s <-> %s (%s:%d)\n",
						check_path, idx->symbols[i].name,
						idx->symbols[i].file_path, idx->symbols[i].start_line);
					break;
				}
			}
		}
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

static void extract_xy_macro_params(const char *sig, char *out, size_t cap) {
	out[0] = '\0';
	const char *lp = strchr(sig, '(');
	if (!lp) return;
	const char *c1 = strchr(lp + 1, ',');
	if (!c1) return;
	const char *c2 = strchr(c1 + 1, ',');
	if (!c2) return;
	const char *p = c2 + 1;
	while (*p == ' ' || *p == '\t') p++;
	size_t j = 0;
	while (*p && *p != '{' && j + 1 < cap) {
		if (!isspace((unsigned char)*p)) out[j++] = *p;
		p++;
	}
	if (j > 0 && out[j - 1] == ';') j--;
	if (j > 0 && out[j - 1] == ')') j--;
	out[j] = '\0';
}

int mm_code_detect_xy_mismatches(const mm_code_index_t *idx,
                                 char **out_buf, size_t *out_len) {
	if (!idx || !out_buf) return -1;
	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	buf[0] = '\0';
	size_t mismatches = 0;

	for (size_t i = 0; i < idx->n_symbols; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		if (strncmp(s->signature, "XY_DECL(", 8) == 0) {
			const mm_code_symbol_t *impl = NULL;
			for (size_t j = 0; j < idx->n_symbols; j++) {
				if (!strcmp(idx->symbols[j].name, s->name) &&
				    strncmp(idx->symbols[j].signature, "XY_IMPL(", 8) == 0) {
					impl = &idx->symbols[j];
					break;
				}
			}
			if (!impl) {
				if (mismatches == 0) mm_buf_appendf(&buf, &pos, &cap, "=== XY Contract Flight Check ===\n");
				mismatches++;
				mm_buf_appendf(&buf, &pos, &cap,
					"  • [missing_impl] XY_DECL %s (%s:%d) has no matching XY_IMPL\n",
					s->name, s->file_path, s->start_line);
			} else {
				char p_decl[256], p_impl[256];
				extract_xy_macro_params(s->signature, p_decl, sizeof(p_decl));
				extract_xy_macro_params(impl->signature, p_impl, sizeof(p_impl));
				if (strcmp(p_decl, p_impl) != 0) {
					if (mismatches == 0) mm_buf_appendf(&buf, &pos, &cap, "=== XY Contract Flight Check ===\n");
					mismatches++;
					mm_buf_appendf(&buf, &pos, &cap,
						"  • [signature_mismatch] %s: XY_DECL(%s) at %s:%d != XY_IMPL(%s) at %s:%d\n",
						s->name, p_decl, s->file_path, s->start_line,
						p_impl, impl->file_path, impl->start_line);
				}
			}
		} else if (strncmp(s->signature, "XY_IMPL(", 8) == 0) {
			bool has_decl = false;
			for (size_t j = 0; j < idx->n_symbols; j++) {
				if (!strcmp(idx->symbols[j].name, s->name) &&
				    strncmp(idx->symbols[j].signature, "XY_DECL(", 8) == 0) {
					has_decl = true;
					break;
				}
			}
			if (!has_decl) {
				if (mismatches == 0) mm_buf_appendf(&buf, &pos, &cap, "=== XY Contract Flight Check ===\n");
				mismatches++;
				mm_buf_appendf(&buf, &pos, &cap,
					"  • [missing_decl] XY_IMPL %s (%s:%d) has no matching XY_DECL header\n",
					s->name, s->file_path, s->start_line);
			}
		}
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

static void infer_tier0_gate(const char *file_path, char *cmd_out, size_t cap) {
	if (!file_path || !*file_path) {
		snprintf(cmd_out, cap, "make test-fast");
		return;
	}
	if (strncmp(file_path, "mods/", 5) == 0) {
		char mod[64] = {0};
		const char *p = file_path + 5;
		const char *sl = strchr(p, '/');
		if (sl && (size_t)(sl - p) < sizeof(mod)) {
			memcpy(mod, p, (size_t)(sl - p));
			mod[sl - p] = '\0';
			if (strstr(file_path, "/ux/")) {
				snprintf(cmd_out, cap, "make -C mods/%s && make site-wasm && sh scripts/check-module-boundaries.sh", mod);
			} else {
				snprintf(cmd_out, cap, "make -C mods/%s && make test-mod MOD=%s", mod, mod);
			}
			return;
		}
	}
	if (strncmp(file_path, "external/", 9) == 0) {
		char lib[64] = {0};
		const char *p = file_path + 9;
		const char *sl = strchr(p, '/');
		if (sl && (size_t)(sl - p) < sizeof(lib)) {
			memcpy(lib, p, (size_t)(sl - p));
			lib[sl - p] = '\0';
			snprintf(cmd_out, cap, "make -C external/%s test", lib);
			return;
		}
	}
	if (strncmp(file_path, "htdocs/", 7) == 0) {
		snprintf(cmd_out, cap, "sh scripts/check-no-site-specific-js.sh");
		return;
	}
	snprintf(cmd_out, cap, "make test-fast");
}

int mm_code_first_touch_dossier(const mm_code_index_t *idx,
                                const char *file_or_symbol,
                                char **out_buf, size_t *out_len) {
	if (!idx || !file_or_symbol || !out_buf) return -1;

	const char *target_file = file_or_symbol;
	for (size_t i = 0; i < idx->n_symbols; i++) {
		if (!strcmp(idx->symbols[i].name, file_or_symbol) ||
		    !strcmp(idx->symbols[i].file_path, file_or_symbol)) {
			target_file = idx->symbols[i].file_path;
			break;
		}
	}

	char gate_cmd[256];
	infer_tier0_gate(target_file, gate_cmd, sizeof(gate_cmd));

	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	mm_buf_appendf(&buf, &pos, &cap,
		"=== First-Touch Module Dossier (%s) ===\n"
		"  • Tier-0 Verification Gate: `%s`\n",
		target_file, gate_cmd);

	if (strstr(target_file, "/ux/")) {
		mm_buf_appendf(&buf, &pos, &cap,
			"  • Isomorphic UX Boundary: Pure C + bud_jsx.h + stele_wasm.h only (forbidden: XY_/corm_/source_/axil_).\n");
	} else if (strncmp(target_file, "mods/", 5) == 0) {
		mm_buf_appendf(&buf, &pos, &cap,
			"  • Server XY Module Boundary: Two-header rule (<M>-types.h vs <M>.h); static by default; declare deps via xy_load().\n");
	}

	/* List up to 5 key entrypoints in the same file */
	mm_buf_appendf(&buf, &pos, &cap, "  • Key Module Symbols:");
	int shown = 0;
	for (size_t i = 0; i < idx->n_symbols && shown < 5; i++) {
		if (!strcmp(idx->symbols[i].file_path, target_file)) {
			mm_buf_appendf(&buf, &pos, &cap, " %s(%s)",
			               idx->symbols[i].name, mm_sym_kind_to_str(idx->symbols[i].kind));
			shown++;
		}
	}
	mm_buf_appendf(&buf, &pos, &cap, "\n");

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 6. Code Search, Concrete/Batch Symbol Read, Call-Graph Trace, Diff & Diagnose
 * ============================================================================ */

int mm_code_read_symbols_ex(const mm_code_index_t *idx,
                            const char *const *symbols, size_t n_symbols,
                            const char *parent_symbol,
                            const char *file_filter, const char *neighbors,
                            int line_start, int line_end,
                            char **out_buf, size_t *out_len) {
	if (!idx || !symbols || n_symbols == 0 || !out_buf) return -1;

	size_t cap = 16384 * n_symbols;
	char *buf = (char *)malloc(cap);
	if (!buf) return -1;
	size_t pos = 0;
	buf[0] = '\0';

	for (size_t s_idx = 0; s_idx < n_symbols; s_idx++) {
		const char *symbol_name = symbols[s_idx];
		if (!symbol_name || !*symbol_name) continue;

		/* Allow Class.method or file:symbol syntax */
		char parsed_parent[128] = {0};
		const char *lookup_name = symbol_name;
		const char *dot = strchr(symbol_name, '.');
		if (dot && !strchr(symbol_name, '/') && !strchr(symbol_name, ':')) {
			size_t plen = (size_t)(dot - symbol_name);
			if (plen < sizeof(parsed_parent)) {
				memcpy(parsed_parent, symbol_name, plen);
				parsed_parent[plen] = '\0';
				lookup_name = dot + 1;
			}
		}
		const char *eff_parent = (parent_symbol && *parent_symbol) ? parent_symbol :
		                         (parsed_parent[0] ? parsed_parent : NULL);

		const mm_code_symbol_t *matches[16];
		size_t n_matches = 0;
		for (size_t i = 0; i < idx->n_symbols; i++) {
			const mm_code_symbol_t *s = &idx->symbols[i];
			if (file_filter && *file_filter && !str_contains_ci(s->file_path, file_filter))
				continue;
			if (eff_parent && *eff_parent && strcasecmp(s->parent_symbol, eff_parent) != 0)
				continue;
			if (!strcmp(s->name, lookup_name) || !strcmp(s->qualified_name, symbol_name)) {
				if (n_matches < 16) matches[n_matches++] = s;
			}
		}

		/* If both XY_DECL and XY_IMPL match, prefer the XY_IMPL implementation */
		if (n_matches == 2) {
			if (matches[0]->kind == MM_SYM_INTERFACE && matches[1]->kind == MM_SYM_FUNCTION) {
				matches[0] = matches[1];
				n_matches = 1;
			} else if (matches[1]->kind == MM_SYM_INTERFACE && matches[0]->kind == MM_SYM_FUNCTION) {
				n_matches = 1;
			}
		}

		if (n_matches == 0) {
			mm_buf_appendf(&buf, &pos, &cap, "No symbol matching '%s' found.\n", symbol_name);
			continue;
		}
		if (n_matches > 1) {
			mm_buf_appendf(&buf, &pos, &cap,
				"Ambiguous symbol '%s' (%zu matches). Specify file or parentSymbol filter:\n",
				symbol_name, n_matches);
			for (size_t i = 0; i < n_matches; i++) {
				mm_buf_appendf(&buf, &pos, &cap,
					"  • [SymRef %u] (%s%s%s) %s:%d-%d · %s\n",
					matches[i]->ref,
					mm_sym_kind_to_str(matches[i]->kind),
					matches[i]->parent_symbol[0] ? " in " : "",
					matches[i]->parent_symbol,
					matches[i]->file_path, matches[i]->start_line, matches[i]->end_line,
					matches[i]->qualified_name);
			}
			continue;
		}

		const mm_code_symbol_t *sym = matches[0];
		int s_line = (line_start > 0) ? line_start : sym->start_line;
		int e_line = (line_end > 0)   ? line_end   : sym->end_line;

		mm_buf_appendf(&buf, &pos, &cap,
			"=== [SymRef %u] %s %s:%d-%d (%s) ===\n"
			"Signature: %s\n",
			sym->ref, mm_sym_kind_to_str(sym->kind),
			sym->file_path, sym->start_line, sym->end_line,
			sym->qualified_name, sym->signature);
		if (sym->parent_symbol[0]) {
			mm_buf_appendf(&buf, &pos, &cap, "Parent: %s\n", sym->parent_symbol);
		}
		if (sym->doc_comment[0]) {
			mm_buf_appendf(&buf, &pos, &cap, "Doc: %s\n", sym->doc_comment);
		}

		bool show_callers = !neighbors || !*neighbors || !strcmp(neighbors, "both") || !strcmp(neighbors, "callers");
		bool show_callees = !neighbors || !*neighbors || !strcmp(neighbors, "both") || !strcmp(neighbors, "callees");

		if (show_callers && sym->n_callers > 0) {
			mm_buf_appendf(&buf, &pos, &cap, "Inbound Callers (%zu):\n", sym->n_callers);
			for (size_t i = 0; i < sym->n_callers; i++) {
				const mm_code_symbol_t *c = find_sym_by_ref(idx, sym->callers[i]);
				if (c) {
					mm_buf_appendf(&buf, &pos, &cap, "  <- %s (%s:%d)\n",
					               c->name, c->file_path, c->start_line);
				}
			}
		}
		if (show_callees && sym->n_callees > 0) {
			mm_buf_appendf(&buf, &pos, &cap, "Outbound Callees (%zu):\n", sym->n_callees);
			for (size_t i = 0; i < sym->n_callees; i++) {
				const mm_code_symbol_t *c = find_sym_by_ref(idx, sym->callees[i]);
				if (c) {
					mm_buf_appendf(&buf, &pos, &cap, "  -> %s (%s:%d)\n",
					               c->name, c->file_path, c->start_line);
				}
			}
		}

		/* Read source lines from disk */
		char abs_path[PATH_MAX];
		snprintf(abs_path, sizeof(abs_path), "%s/%s", idx->root_dir, sym->file_path);
		FILE *fp = fopen(abs_path, "r");
		if (fp) {
			mm_buf_appendf(&buf, &pos, &cap, "--- Source (%s:%d-%d) ---\n",
			               sym->file_path, s_line, e_line);
			char line[1024];
			int cur = 0;
			while (fgets(line, sizeof(line), fp)) {
				cur++;
				if (cur < s_line) continue;
				if (cur > e_line) break;
				mm_buf_appendf(&buf, &pos, &cap, "%4d: %s", cur, line);
			}
			fclose(fp);
		}

		/* Append Cross-Boundary Pairs & First-Touch Module Dossier */
		char *pairs_buf = NULL;
		if (mm_code_resolve_cross_boundary_pairs(idx, sym->name, sym->file_path, &pairs_buf, NULL) == 0 && pairs_buf) {
			if (pairs_buf[0]) mm_buf_appendf(&buf, &pos, &cap, "%s", pairs_buf);
			free(pairs_buf);
		}
		char *dossier_buf = NULL;
		if (mm_code_first_touch_dossier(idx, sym->file_path, &dossier_buf, NULL) == 0 && dossier_buf) {
			mm_buf_appendf(&buf, &pos, &cap, "%s", dossier_buf);
			free(dossier_buf);
		}
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_code_read_symbol(const mm_code_index_t *idx, const char *symbol_name,
                        const char *file_filter, const char *neighbors,
                        int line_start, int line_end,
                        char **out_buf, size_t *out_len) {
	if (!symbol_name) return -1;
	const char *syms[1] = { symbol_name };
	return mm_code_read_symbols_ex(idx, syms, 1, NULL, file_filter, neighbors,
	                               line_start, line_end, out_buf, out_len);
}

int mm_code_search(const mm_code_index_t *idx, const char *query,
                   const char *file_filter, const char *kind_filter,
                   int limit, char **out_buf, size_t *out_len) {
	if (!idx || !out_buf) return -1;
	if (limit <= 0) limit = 10;

	const mm_code_symbol_t *matched[128];
	size_t n_matched = 0;

	for (size_t i = 0; i < idx->n_symbols && n_matched < 128; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		if (file_filter && *file_filter && !str_contains_ci(s->file_path, file_filter))
			continue;
		if (kind_filter && *kind_filter && strcasecmp(mm_sym_kind_to_str(s->kind), kind_filter) != 0)
			continue;
		if (!query || !*query ||
		    str_contains_ci(s->name, query) ||
		    str_contains_ci(s->signature, query) ||
		    str_contains_ci(s->file_path, query) ||
		    str_contains_ci(s->doc_comment, query)) {
			matched[n_matched++] = s;
		}
	}

	if (n_matched == 1) {
		return mm_code_read_symbol(idx, matched[0]->name, matched[0]->file_path, "both", 0, 0, out_buf, out_len);
	}

	size_t show = n_matched < (size_t)limit ? n_matched : (size_t)limit;
	size_t cap = 1024 + show * 512;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	mm_buf_appendf(&buf, &pos, &cap,
		"=== Codebase Symbols (%zu matches for \"%s\") ===\n",
		n_matched, query ? query : "");
	for (size_t i = 0; i < show; i++) {
		const mm_code_symbol_t *s = matched[i];
		mm_buf_appendf(&buf, &pos, &cap,
			"• [SymRef %u] (%s) %s:%d-%d · %s [callers:%zu callees:%zu]\n"
			"    Sig: %s\n",
			s->ref, mm_sym_kind_to_str(s->kind),
			s->file_path, s->start_line, s->end_line,
			s->qualified_name, s->n_callers, s->n_callees,
			s->signature);
	}
	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

static void format_trace_recursive(const mm_code_index_t *idx, const mm_code_symbol_t *sym,
                                   bool inbound, int depth, int max_depth,
                                   uint32_t *visited, size_t *n_visited,
                                   char **buf, size_t *pos, size_t *cap) {
	if (!sym || depth > max_depth) return;
	const uint32_t *edges = inbound ? sym->callers : sym->callees;
	size_t n_edges = inbound ? sym->n_callers : sym->n_callees;

	for (size_t i = 0; i < n_edges; i++) {
		const mm_code_symbol_t *child = find_sym_by_ref(idx, edges[i]);
		if (!child) continue;
		for (int d = 0; d < depth; d++) {
			mm_buf_appendf(buf, pos, cap, "  ");
		}
		mm_buf_appendf(buf, pos, cap,
			"%s [SymRef %u] %s (%s:%d-%d)\n",
			inbound ? "<-" : "->",
			child->ref, child->name, child->file_path, child->start_line, child->end_line);

		bool seen = false;
		for (size_t v = 0; v < *n_visited; v++) {
			if (visited[v] == child->ref) { seen = true; break; }
		}
		if (!seen && *n_visited < 256) {
			visited[(*n_visited)++] = child->ref;
			format_trace_recursive(idx, child, inbound, depth + 1, max_depth,
			                       visited, n_visited, buf, pos, cap);
		}
	}
}

int mm_code_trace(const mm_code_index_t *idx, const char *symbol_name,
                  const char *direction, int max_depth,
                  char **out_buf, size_t *out_len) {
	if (!idx || !symbol_name || !out_buf) return -1;
	if (max_depth <= 0) max_depth = 3;

	const mm_code_symbol_t *root = NULL;
	for (size_t i = 0; i < idx->n_symbols; i++) {
		if (!strcmp(idx->symbols[i].name, symbol_name) ||
		    !strcmp(idx->symbols[i].qualified_name, symbol_name)) {
			root = &idx->symbols[i];
			if (root->kind == MM_SYM_FUNCTION) break;
		}
	}
	if (!root) {
		char *msg = (char *)malloc(256);
		int n = snprintf(msg, 256, "No symbol '%s' found for call-graph trace.\n", symbol_name);
		*out_buf = msg;
		if (out_len) *out_len = (size_t)n;
		return 0;
	}

	bool do_in  = !direction || !*direction || !strcmp(direction, "both") || !strcmp(direction, "inbound");
	bool do_out = !direction || !*direction || !strcmp(direction, "both") || !strcmp(direction, "outbound");

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	mm_buf_appendf(&buf, &pos, &cap,
		"=== Call Graph Trace: %s (%s:%d-%d, depth=%d) ===\n",
		root->name, root->file_path, root->start_line, root->end_line, max_depth);

	if (do_in) {
		mm_buf_appendf(&buf, &pos, &cap, "Inbound Callers:\n");
		uint32_t visited[256] = { root->ref };
		size_t n_vis = 1;
		format_trace_recursive(idx, root, true, 1, max_depth, visited, &n_vis, &buf, &pos, &cap);
	}
	if (do_out) {
		mm_buf_appendf(&buf, &pos, &cap, "Outbound Callees:\n");
		uint32_t visited[256] = { root->ref };
		size_t n_vis = 1;
		format_trace_recursive(idx, root, false, 1, max_depth, visited, &n_vis, &buf, &pos, &cap);
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_code_overview(const mm_code_index_t *idx, char **out_buf, size_t *out_len) {
	if (!idx || !out_buf) return -1;
	size_t total_edges = 0;
	size_t n_routes = 0;
	for (size_t i = 0; i < idx->n_symbols; i++) {
		total_edges += idx->symbols[i].n_callees;
		if (idx->symbols[i].kind == MM_SYM_ROUTE) n_routes++;
	}

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	mm_buf_appendf(&buf, &pos, &cap,
		"=== Codebase Architecture Overview (L3/L2 Mipmap) ===\n"
		"Root: %s\n"
		"Files: %zu | Symbols: %zu | Call Edges: %zu | HTTP Routes: %zu\n"
		"High-Degree Call Hotspots:\n",
		idx->root_dir, idx->n_files, idx->n_symbols, total_edges, n_routes);

	bool picked[4096] = {false};
	size_t max_pick = idx->n_symbols < 10 ? idx->n_symbols : 10;
	for (size_t p = 0; p < max_pick; p++) {
		ssize_t best = -1;
		size_t best_deg = 0;
		for (size_t i = 0; i < idx->n_symbols && i < 4096; i++) {
			if (picked[i]) continue;
			size_t deg = idx->symbols[i].n_callers + idx->symbols[i].n_callees;
			if (best < 0 || deg > best_deg) {
				best = (ssize_t)i;
				best_deg = deg;
			}
		}
		if (best < 0) break;
		picked[best] = true;
		const mm_code_symbol_t *s = &idx->symbols[best];
		mm_buf_appendf(&buf, &pos, &cap,
			"  • [SymRef %u] %s (%s:%d-%d) — degree:%zu (in:%zu out:%zu)\n",
			s->ref, s->name, s->file_path, s->start_line, s->end_line,
			best_deg, s->n_callers, s->n_callees);
	}

	if (n_routes > 0) {
		mm_buf_appendf(&buf, &pos, &cap, "Registered HTTP Routes:\n");
		for (size_t i = 0; i < idx->n_symbols; i++) {
			if (idx->symbols[i].kind == MM_SYM_ROUTE) {
				mm_buf_appendf(&buf, &pos, &cap,
					"  • %s (%s:%d)\n",
					idx->symbols[i].name, idx->symbols[i].file_path, idx->symbols[i].start_line);
			}
		}
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

static size_t collect_git_diff_affected_symbols(const mm_code_index_t *idx,
                                                uint32_t *affected_refs, size_t max_refs) {
	char cmd[PATH_MAX + 64];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff --unified=0 HEAD 2>/dev/null", idx->root_dir);
	FILE *fp = popen(cmd, "r");
	if (!fp) return 0;

	size_t n_affected = 0;
	char cur_file[256] = {0};
	char line[1024];

	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "+++ b/", 6) == 0) {
			size_t len = strlen(line + 6);
			while (len > 0 && (line[6 + len - 1] == '\n' || line[6 + len - 1] == '\r')) len--;
			if (len >= sizeof(cur_file)) len = sizeof(cur_file) - 1;
			memcpy(cur_file, line + 6, len);
			cur_file[len] = '\0';
		} else if (strncmp(line, "@@ ", 3) == 0 && cur_file[0]) {
			const char *plus = strchr(line, '+');
			if (plus) {
				int start_l = atoi(plus + 1);
				int count_l = 1;
				const char *comma = strchr(plus, ',');
				const char *at2   = strstr(plus, " @@");
				if (comma && (!at2 || comma < at2)) {
					count_l = atoi(comma + 1);
					if (count_l <= 0) count_l = 1;
				}
				int end_l = start_l + count_l - 1;

				for (size_t i = 0; i < idx->n_symbols; i++) {
					const mm_code_symbol_t *s = &idx->symbols[i];
					if (strcmp(s->file_path, cur_file) == 0 &&
					    s->start_line <= end_l && s->end_line >= start_l) {
						bool dup = false;
						for (size_t k = 0; k < n_affected; k++) {
							if (affected_refs[k] == s->ref) { dup = true; break; }
						}
						if (!dup && n_affected < max_refs) affected_refs[n_affected++] = s->ref;
					}
				}
			}
		}
	}
	pclose(fp);
	return n_affected;
}

int mm_code_diff_impact(const mm_code_index_t *idx, int depth, char **out_buf, size_t *out_len) {
	if (!idx || !out_buf) return -1;
	if (depth <= 0) depth = 2;

	uint32_t affected_refs[128];
	size_t n_affected = collect_git_diff_affected_symbols(idx, affected_refs, 128);

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	mm_buf_appendf(&buf, &pos, &cap,
		"=== Worktree Diff Impact & Blast Radius (%zu modified symbol%s) ===\n",
		n_affected, n_affected == 1 ? "" : "s");

	for (size_t i = 0; i < n_affected; i++) {
		const mm_code_symbol_t *s = find_sym_by_ref(idx, affected_refs[i]);
		if (!s) continue;
		char gate_cmd[256];
		infer_tier0_gate(s->file_path, gate_cmd, sizeof(gate_cmd));
		mm_buf_appendf(&buf, &pos, &cap,
			"• Modified [SymRef %u] %s (%s:%d-%d) [Gate: `%s`]\n",
			s->ref, s->name, s->file_path, s->start_line, s->end_line, gate_cmd);
		uint32_t visited[256] = { s->ref };
		size_t n_vis = 1;
		format_trace_recursive(idx, s, true, 1, depth, visited, &n_vis, &buf, &pos, &cap);
	}

	char *xy_buf = NULL;
	if (mm_code_detect_xy_mismatches(idx, &xy_buf, NULL) == 0 && xy_buf) {
		if (xy_buf[0]) mm_buf_appendf(&buf, &pos, &cap, "%s", xy_buf);
		free(xy_buf);
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

static bool code_contains_word_ci(const char *s, const char *word) {
	size_t wlen = strlen(word);
	const char *p = s;
	while (*p) {
		if (strncasecmp(p, word, wlen) == 0) {
			bool left_ok = (p == s) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_');
			bool right_ok = !isalnum((unsigned char)p[wlen]) && p[wlen] != '_';
			if (left_ok && right_ok) return true;
		}
		p++;
	}
	return false;
}

/* Parse stack frames (file.c:123, at fn (file.ts:10:5), File "x.py", line 10, in fn)
 * and intersect with git diff symbols to compute Causal Fault Intersections */
int mm_code_diagnose(const mm_code_index_t *idx, const char *trace_text,
                     int depth, char **out_buf, size_t *out_len) {
	if (!idx || !trace_text || !out_buf) return -1;
	if (depth <= 0) depth = 2;

	uint32_t stack_refs[64];
	size_t n_stack = 0;

	/* 1. Resolve file:line spans in trace_text against symbol line intervals */
	for (size_t i = 0; i < idx->n_symbols && n_stack < 64; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		const char *base_f = strrchr(s->file_path, '/');
		base_f = base_f ? (base_f + 1) : s->file_path;

		const char *fp = strstr(trace_text, base_f);
		if (fp) {
			const char *after = fp + strlen(base_f);
			if (*after == ':') {
				int lno = atoi(after + 1);
				if (lno >= s->start_line && lno <= s->end_line) {
					bool dup = false;
					for (size_t k = 0; k < n_stack; k++) {
						if (stack_refs[k] == s->ref) { dup = true; break; }
					}
					if (!dup) stack_refs[n_stack++] = s->ref;
				}
			} else if (strstr(after, "line ")) {
				const char *ln_p = strstr(after, "line ") + 5;
				int lno = atoi(ln_p);
				if (lno >= s->start_line && lno <= s->end_line) {
					bool dup = false;
					for (size_t k = 0; k < n_stack; k++) {
						if (stack_refs[k] == s->ref) { dup = true; break; }
					}
					if (!dup) stack_refs[n_stack++] = s->ref;
				}
			}
		}

		if (strlen(s->name) >= 3 && code_contains_word_ci(trace_text, s->name)) {
			bool dup = false;
			for (size_t k = 0; k < n_stack; k++) {
				if (stack_refs[k] == s->ref) { dup = true; break; }
			}
			if (!dup && n_stack < 64) stack_refs[n_stack++] = s->ref;
		}
	}

	uint32_t diff_refs[128];
	size_t n_diff = collect_git_diff_affected_symbols(idx, diff_refs, 128);

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	mm_buf_appendf(&buf, &pos, &cap,
		"=== Fault Localization & Differential Failure Topology ===\n");

	/* Check Causal Fault Intersections between stack_refs and diff_refs */
	for (size_t i = 0; i < n_stack; i++) {
		const mm_code_symbol_t *s = find_sym_by_ref(idx, stack_refs[i]);
		if (!s) continue;

		bool on_diff = false;
		for (size_t d = 0; d < n_diff; d++) {
			if (diff_refs[d] == s->ref) { on_diff = true; break; }
		}
		if (on_diff) {
			mm_buf_appendf(&buf, &pos, &cap,
				"🔥 [FAULT ROOT · hop 0] [modified_on_stack] %s (%s:%d-%d) is both on the failure stack and modified in git diff!\n",
				s->name, s->file_path, s->start_line, s->end_line);
		} else {
			/* Check if any direct callee of s is in diff_refs */
			for (size_t c = 0; c < s->n_callees; c++) {
				for (size_t d = 0; d < n_diff; d++) {
					if (s->callees[c] == diff_refs[d]) {
						const mm_code_symbol_t *cal = find_sym_by_ref(idx, s->callees[c]);
						if (cal) {
							mm_buf_appendf(&buf, &pos, &cap,
								"⚠️ [MODIFIED CALLEE · hop 1] [modified_callee_of_stack] Stack frame %s calls modified symbol %s (%s:%d-%d)\n",
								s->name, cal->name, cal->file_path, cal->start_line, cal->end_line);
						}
					}
				}
			}
		}

		mm_buf_appendf(&buf, &pos, &cap,
			"• Suspect [SymRef %u] %s (%s:%d-%d)\n    Sig: %s\n",
			s->ref, s->name, s->file_path, s->start_line, s->end_line, s->signature);
		uint32_t visited[256] = { s->ref };
		size_t n_vis = 1;
		format_trace_recursive(idx, s, true, 1, depth, visited, &n_vis, &buf, &pos, &cap);
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

/* ============================================================================
 * 7. Code-to-Lore Causal Binding, Context Staleness Radar & Swarm Stigmergy
 * ============================================================================ */

struct mm_ctx_code_access {
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

static mm_code_index_t *get_ctx_code_index(mm_ctx_t *ctx) {
	if (!ctx) return NULL;
	struct mm_ctx_code_access *p = (struct mm_ctx_code_access *)ctx;
	const char *root = (p->project_root[0]) ? p->project_root : p->cwd;
	if (!p->code_index_loaded) {
		mm_code_index_dir(&p->code_index, root);
		p->code_index_loaded = true;
	}
	return &p->code_index;
}

static mm_code_index_t *refresh_ctx_code_index(mm_ctx_t *ctx) {
	if (!ctx) return NULL;
	struct mm_ctx_code_access *p = (struct mm_ctx_code_access *)ctx;
	const char *root = (p->project_root[0]) ? p->project_root : p->cwd;
	mm_code_index_free(&p->code_index);
	mm_code_index_init(&p->code_index, root);
	mm_code_index_dir(&p->code_index, root);
	p->code_index_loaded = true;
	return &p->code_index;
}

static void compute_symbol_fingerprint(const mm_code_symbol_t *s, char *out, size_t cap) {
	unsigned long h = 5381;
	for (const unsigned char *p = (const unsigned char *)s->signature; *p; p++) {
		if (!isspace(*p)) h = ((h << 5) + h) + *p;
	}
	for (size_t i = 0; i < s->n_raw_callees; i++) {
		for (const unsigned char *p = (const unsigned char *)s->raw_callees[i]; *p; p++) {
			h = ((h << 5) + h) + *p;
		}
	}
	snprintf(out, cap, "%08lx", h & 0xffffffffUL);
}

int mm_lore_bind_symbol(mm_ctx_t *ctx, uint32_t mem_ref, const char *summary,
                        const char *symbol_name, const char *file_path) {
	if (!ctx || mem_ref == 0) return -1;
	const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	if (!tdir) return 0;

	mm_code_index_t *idx = get_ctx_code_index(ctx);
	if (!idx || idx->n_symbols == 0) return 0;

	char lpath[PATH_MAX];
	snprintf(lpath, sizeof(lpath), "%s/lore_bindings.tsv", tdir);

	for (size_t i = 0; i < idx->n_symbols; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		bool match = false;
		if (symbol_name && *symbol_name && !strcmp(s->name, symbol_name)) match = true;
		else if (summary && strlen(s->name) >= 4 && code_contains_word_ci(summary, s->name)) match = true;
		if (!match) continue;
		if (file_path && *file_path && !str_contains_ci(s->file_path, file_path)) continue;

		char fp_hash[32];
		compute_symbol_fingerprint(s, fp_hash, sizeof(fp_hash));
		FILE *fp = fopen(lpath, "a");
		if (fp) {
			fprintf(fp, "%u\t%s\t%s\t%s\t%d\t%d\n",
			        mem_ref, s->name, s->file_path, fp_hash, s->start_line, s->end_line);
			fclose(fp);
		}
	}
	return 0;
}

int mm_lore_verify_drift(mm_ctx_t *ctx, const char *symbol_or_file,
                         char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	if (!tdir) return -1;

	char lpath[PATH_MAX];
	snprintf(lpath, sizeof(lpath), "%s/lore_bindings.tsv", tdir);
	FILE *fp = fopen(lpath, "r");
	if (!fp) {
		*out_buf = strdup("");
		if (out_len) *out_len = 0;
		return 0;
	}

	mm_code_index_t *idx = refresh_ctx_code_index(ctx);
	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	buf[0] = '\0';

	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		uint32_t ref = 0;
		char sname[128] = {0}, fpath[256] = {0}, old_hash[32] = {0};
		int old_start = 0, old_end = 0;
		if (sscanf(line, "%u\t%127[^\t]\t%255[^\t]\t%31[^\t]\t%d\t%d",
		           &ref, sname, fpath, old_hash, &old_start, &old_end) < 4) {
			continue;
		}
		if (symbol_or_file && *symbol_or_file &&
		    !str_contains_ci(sname, symbol_or_file) &&
		    !str_contains_ci(fpath, symbol_or_file)) {
			continue;
		}

		const mm_code_symbol_t *cur_sym = NULL;
		for (size_t i = 0; i < idx->n_symbols; i++) {
			if (!strcmp(idx->symbols[i].name, sname) && !strcmp(idx->symbols[i].file_path, fpath)) {
				cur_sym = &idx->symbols[i];
				break;
			}
		}

		if (!cur_sym) {
			mm_buf_appendf(&buf, &pos, &cap,
				"⚠️ [STALE_SUSPECT] [Ref P:%u] Bound symbol `%s` (%s) was deleted or renamed!\n",
				ref, sname, fpath);
		} else {
			char cur_hash[32];
			compute_symbol_fingerprint(cur_sym, cur_hash, sizeof(cur_hash));
			if (strcmp(cur_hash, old_hash) != 0) {
				mm_buf_appendf(&buf, &pos, &cap,
					"⚠️ [STALE_SUSPECT] [Ref P:%u] Bound symbol `%s` (%s:%d-%d) signature/callees mutated since invariant was recorded!\n",
					ref, sname, fpath, cur_sym->start_line, cur_sym->end_line);
			} else {
				mm_buf_appendf(&buf, &pos, &cap,
					"✓ [FRESH] [Ref P:%u] Bound symbol `%s` (%s:%d-%d) matches AST fingerprint %s.\n",
					ref, sname, fpath, cur_sym->start_line, cur_sym->end_line, cur_hash);
			}
		}
	}
	fclose(fp);

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_context_record_snapshot(mm_ctx_t *ctx, const char *session_id,
                               const char *file_path, int turn_idx) {
	if (!ctx || !file_path || !*file_path) return -1;
	const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	if (!tdir) return -1;

	mm_code_index_t *idx = get_ctx_code_index(ctx);
	char snap_path[PATH_MAX];
	snprintf(snap_path, sizeof(snap_path), "%s/context_snapshots.tsv", tdir);
	FILE *fp = fopen(snap_path, "w");
	if (!fp) return -1;

	for (size_t i = 0; i < idx->n_symbols; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		if (!strcmp(s->file_path, file_path)) {
			char h[32];
			compute_symbol_fingerprint(s, h, sizeof(h));
			fprintf(fp, "%s\t%d\t%s\t%s\t%s\t%d\t%d\n",
			        (session_id && *session_id) ? session_id : "default",
			        turn_idx, s->file_path, s->name, h, s->start_line, s->end_line);
		}
	}
	fclose(fp);
	return 0;
}

int mm_context_check_drift(mm_ctx_t *ctx, const char *session_id,
                           const char *file_path,
                           char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	if (!tdir) return -1;

	char snap_path[PATH_MAX];
	snprintf(snap_path, sizeof(snap_path), "%s/context_snapshots.tsv", tdir);
	FILE *fp = fopen(snap_path, "r");
	if (!fp) {
		*out_buf = strdup("");
		if (out_len) *out_len = 0;
		return 0;
	}

	mm_code_index_t *idx = refresh_ctx_code_index(ctx);
	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	buf[0] = '\0';

	char seen_syms[128][128];
	size_t n_seen = 0;
	char line[1024];

	while (fgets(line, sizeof(line), fp)) {
		char sid[128] = {0}, fpath[256] = {0}, sname[128] = {0}, old_h[32] = {0};
		int turn = 0, old_s = 0, old_e = 0;
		if (sscanf(line, "%127[^\t]\t%d\t%255[^\t]\t%127[^\t]\t%31[^\t]\t%d\t%d",
		           sid, &turn, fpath, sname, old_h, &old_s, &old_e) < 7) {
			continue;
		}
		if (session_id && *session_id && strcmp(sid, session_id) != 0) continue;
		if (file_path && *file_path && strcmp(fpath, file_path) != 0) continue;

		if (n_seen < 128) snprintf(seen_syms[n_seen++], 128, "%s", sname);

		const mm_code_symbol_t *cur = NULL;
		for (size_t i = 0; i < idx->n_symbols; i++) {
			if (!strcmp(idx->symbols[i].file_path, fpath) && !strcmp(idx->symbols[i].name, sname)) {
				cur = &idx->symbols[i];
				break;
			}
		}

		if (!cur) {
			if (pos == 0) mm_buf_appendf(&buf, &pos, &cap, "=== Proactive Context Drift Warning ===\n");
			mm_buf_appendf(&buf, &pos, &cap,
				"• [removed] `%s` in %s (was lines %d-%d at Turn %d)\n",
				sname, fpath, old_s, old_e, turn);
		} else {
			char cur_h[32];
			compute_symbol_fingerprint(cur, cur_h, sizeof(cur_h));
			if (strcmp(cur_h, old_h) != 0) {
				if (pos == 0) mm_buf_appendf(&buf, &pos, &cap, "=== Proactive Context Drift Warning ===\n");
				mm_buf_appendf(&buf, &pos, &cap,
					"• [modified] `%s` in %s (lines %d-%d -> %d-%d)\n",
					sname, fpath, old_s, old_e, cur->start_line, cur->end_line);
			} else if (cur->start_line != old_s || cur->end_line != old_e) {
				if (pos == 0) mm_buf_appendf(&buf, &pos, &cap, "=== Proactive Context Drift Warning ===\n");
				mm_buf_appendf(&buf, &pos, &cap,
					"• [lines_shifted] `%s` in %s shifted lines %d-%d -> %d-%d\n",
					sname, fpath, old_s, old_e, cur->start_line, cur->end_line);
			}
		}
	}
	fclose(fp);

	/* Check for newly added symbols in file_path */
	if (file_path && *file_path && n_seen > 0) {
		for (size_t i = 0; i < idx->n_symbols; i++) {
			const mm_code_symbol_t *s = &idx->symbols[i];
			if (!strcmp(s->file_path, file_path)) {
				bool was_seen = false;
				for (size_t k = 0; k < n_seen; k++) {
					if (!strcmp(seen_syms[k], s->name)) { was_seen = true; break; }
				}
				if (!was_seen) {
					if (pos == 0) mm_buf_appendf(&buf, &pos, &cap, "=== Proactive Context Drift Warning ===\n");
					mm_buf_appendf(&buf, &pos, &cap,
						"• [added] `%s` in %s (lines %d-%d)\n",
						s->name, s->file_path, s->start_line, s->end_line);
				}
			}
		}
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_swarm_record_lease(mm_ctx_t *ctx, const char *session_id,
                          const char *symbol_name, const char *file_path,
                          const char *mode) {
	if (!ctx || !session_id || !*session_id) return -1;
	const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	if (!tdir) return -1;

	char lpath[PATH_MAX];
	snprintf(lpath, sizeof(lpath), "%s/swarm_leases.tsv", tdir);
	FILE *fp = fopen(lpath, "a");
	if (!fp) return -1;

	long now_sec = (long)time(NULL);
	fprintf(fp, "%s\t%s\t%s\t%s\t%ld\n",
	        session_id,
	        (symbol_name && *symbol_name) ? symbol_name : "-",
	        (file_path && *file_path) ? file_path : "-",
	        (mode && *mode) ? mode : "inspecting",
	        now_sec);
	fclose(fp);
	return 0;
}

int mm_swarm_query_leases(mm_ctx_t *ctx, const char *current_session_id,
                          const char *symbol_name, const char *file_path,
                          char **out_buf, size_t *out_len) {
	if (!ctx || !out_buf) return -1;
	const char *tdir = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	if (!tdir) return -1;

	char lpath[PATH_MAX];
	snprintf(lpath, sizeof(lpath), "%s/swarm_leases.tsv", tdir);
	FILE *fp = fopen(lpath, "r");
	if (!fp) {
		*out_buf = strdup("");
		if (out_len) *out_len = 0;
		return 0;
	}

	mm_code_index_t *idx = get_ctx_code_index(ctx);
	long now_sec = (long)time(NULL);
	size_t cap = 4096;
	char *buf = (char *)malloc(cap);
	size_t pos = 0;
	buf[0] = '\0';

	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		char sid[128] = {0}, lsym[128] = {0}, lfile[256] = {0}, lmode[32] = {0};
		long ts = 0;
		if (sscanf(line, "%127[^\t]\t%127[^\t]\t%255[^\t]\t%31[^\t]\t%ld",
		           sid, lsym, lfile, lmode, &ts) < 5) {
			continue;
		}
		if (current_session_id && *current_session_id && !strcmp(sid, current_session_id))
			continue;
		if (now_sec - ts > 900) continue; /* 15-minute TTL */

		const char *overlap = NULL;
		if (symbol_name && *symbol_name && !strcmp(lsym, symbol_name)) {
			overlap = "exact_symbol";
		} else if (file_path && *file_path && !strcmp(lfile, file_path)) {
			overlap = "same_file";
		} else if (symbol_name && *symbol_name && idx) {
			/* Check 1-hop call-graph neighbor */
			for (size_t i = 0; i < idx->n_symbols; i++) {
				if (!strcmp(idx->symbols[i].name, symbol_name)) {
					for (size_t c = 0; c < idx->symbols[i].n_raw_callees; c++) {
						if (!strcmp(idx->symbols[i].raw_callees[c], lsym)) {
							overlap = "call_graph_neighbor";
							break;
						}
					}
				}
			}
		}

		if (overlap) {
			if (pos == 0) {
				mm_buf_appendf(&buf, &pos, &cap,
					"=== Swarm Stigmergy (Active Parallel Agent Leases) ===\n");
			}
			mm_buf_appendf(&buf, &pos, &cap,
				"  • [%s] Session `%s` is %s `%s` (%s)\n",
				overlap, sid, lmode, lsym, lfile);
		}
	}
	fclose(fp);

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}
