#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

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

static bool is_keyword(const char *w) {
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
		NULL
	};
	for (int i = 0; kw[i]; i++) {
		if (!strcmp(w, kw[i])) return true;
	}
	return false;
}

static void add_raw_callee(mm_code_symbol_t *sym, const char *callee) {
	if (!sym || !callee || !*callee || is_keyword(callee)) return;
	if (!strcmp(callee, sym->name)) return; /* skip direct self-recursion noise */
	for (size_t i = 0; i < sym->n_raw_callees; i++) {
		if (!strcmp(sym->raw_callees[i], callee)) return;
	}
	if (sym->n_raw_callees < MM_CODE_MAX_CALLEES) {
		snprintf(sym->raw_callees[sym->n_raw_callees++], 64, "%s", callee);
	}
}

static void scan_body_callees(mm_code_symbol_t *sym, const char *body_start, const char *body_end) {
	const char *p = body_start;
	while (p < body_end) {
		/* Skip string literals and line comments */
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
		if (is_ident_start(*p)) {
			const char *id_start = p;
			while (p < body_end && is_ident_char(*p)) p++;
			size_t id_len = (size_t)(p - id_start);
			const char *ws = p;
			while (ws < body_end && (*ws == ' ' || *ws == '\t')) ws++;
			if (ws < body_end && *ws == '(' && id_len > 1 && id_len < 64) {
				char ident[64];
				memcpy(ident, id_start, id_len);
				ident[id_len] = '\0';
				add_raw_callee(sym, ident);
			}
			continue;
		}
		p++;
	}
}

/* ============================================================================
 * 2. Multi-Language Source File Scanner (C/H, TS/JS, Python, Shell)
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

	/* Split into line pointers */
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

	for (int i = 0; i < line_count; i++) {
		char line[512];
		size_t clen = line_lens[i] < sizeof(line) - 1 ? line_lens[i] : sizeof(line) - 1;
		memcpy(line, line_starts[i], clen);
		line[clen] = '\0';

		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		if (!*s || (s[0] == '/' && s[1] == '/') || (s[0] == '/' && s[1] == '*') || s[0] == '*')
			continue;

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
				}
			}
			continue;
		}

		/* 3. TypeScript / JS declarations */
		if (is_ts) {
			const char *cur = s;
			if (strncmp(cur, "export ", 7) == 0) {
				cur += 7;
				while (*cur == ' ') cur++;
			}
			if (strncmp(cur, "default ", 8) == 0) {
				cur += 8;
				while (*cur == ' ') cur++;
			}
			if (strncmp(cur, "async ", 6) == 0) {
				cur += 6;
				while (*cur == ' ') cur++;
			}

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

						/* Find matching closing brace for block */
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
						if (seen_brace) {
							scan_body_callees(sym, line_starts[i],
							                  line_starts[end_l] + line_lens[end_l]);
						}
					}
				}
				continue;
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
					sym->kind = kind;
					snprintf(sym->file_path, sizeof(sym->file_path), "%s", file_path);
					snprintf(sym->qualified_name, sizeof(sym->qualified_name), "%s:%s", file_path, sym->name);
					sym->start_line = i + 1;
					sym->end_line = (i + 15 < line_count) ? (i + 15) : line_count;
					snprintf(sym->signature, sizeof(sym->signature), "%s", s);
					scan_body_callees(sym, line_starts[i],
					                  line_starts[sym->end_line - 1] + line_lens[sym->end_line - 1]);
				}
			}
			continue;
		}

		/* 5. C/H Struct, Enum, Typedef, and Function definitions */
		if (is_c && line[0] != ' ' && line[0] != '\t' && line[0] != '#') {
			if (strncmp(s, "typedef struct", 14) == 0 || strncmp(s, "struct ", 7) == 0) {
				/* Check if single-line or multi-line struct */
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
						/* Look for "} name_t;" on end_l */
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
						}
					}
					i = end_l;
					continue;
				}
			}

			/* Top-level C function definition: look for identifier followed by '(' and '{' on same or next line */
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
					if (!is_keyword(fname)) {
						/* Verify opening brace '{' within 4 lines and no ';' before '{' */
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
								scan_body_callees(sym, line_starts[brace_line],
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
		if (S_ISLNK(st.st_mode)) continue;
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
			for (size_t j = 0; j < idx->n_symbols; j++) {
				if (i == j) continue;
				mm_code_symbol_t *callee = &idx->symbols[j];
				if (strcmp(callee->name, target_name) == 0) {
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
					break;
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
	fprintf(fp, "# MM_CODE_MANIFEST_V1\t%zu\t%zu\n", idx->n_files, idx->n_symbols);
	for (size_t i = 0; i < idx->n_symbols; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		fprintf(fp, "SYM\t%u\t%u\t%s\t%s\t%s\t%d\t%d\t%s\t",
		        s->ref, s->file_id, s->name, mm_sym_kind_to_str(s->kind),
		        s->file_path, s->start_line, s->end_line, s->signature);
		for (size_t c = 0; c < s->n_raw_callees; c++) {
			fprintf(fp, "%s%s", (c > 0 ? "," : ""), s->raw_callees[c]);
		}
		fprintf(fp, "\n");
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

		char *fields[10] = {0};
		int nf = 0;
		char *p = line + 4;
		while (nf < 9) {
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
		snprintf(s->qualified_name, sizeof(s->qualified_name), "%s:%s", s->file_path, s->name);
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
	}
	fclose(fp);
	mm_code_resolve_call_graph(idx);
	return 0;
}

/* ============================================================================
 * 5. Code Search, Concrete Symbol Read, Call-Graph Trace, Diff & Overview
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

int mm_code_read_symbol(const mm_code_index_t *idx, const char *symbol_name,
                        const char *file_filter, const char *neighbors,
                        int line_start, int line_end,
                        char **out_buf, size_t *out_len) {
	if (!idx || !symbol_name || !out_buf) return -1;

	const mm_code_symbol_t *matches[16];
	size_t n_matches = 0;
	for (size_t i = 0; i < idx->n_symbols; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		if (file_filter && *file_filter && !str_contains_ci(s->file_path, file_filter))
			continue;
		if (!strcmp(s->name, symbol_name) || !strcmp(s->qualified_name, symbol_name)) {
			if (n_matches < 16) matches[n_matches++] = s;
		}
	}

	if (n_matches == 0) {
		char *msg = (char *)malloc(256);
		int n = snprintf(msg, 256, "No symbol matching '%s' found.\n", symbol_name);
		*out_buf = msg;
		if (out_len) *out_len = (size_t)n;
		return 0;
	}
	if (n_matches > 1) {
		/* Fail-closed on ambiguity: list disambiguating candidates */
		size_t cap = 2048;
		char *msg = (char *)malloc(cap);
		size_t pos = (size_t)snprintf(msg, cap,
			"Ambiguous symbol '%s' (%zu matches). Specify file filter:\n",
			symbol_name, n_matches);
		for (size_t i = 0; i < n_matches; i++) {
			pos += (size_t)snprintf(msg + pos, cap - pos,
				"  • [SymRef %u] (%s) %s:%d-%d · %s\n",
				matches[i]->ref, mm_sym_kind_to_str(matches[i]->kind),
				matches[i]->file_path, matches[i]->start_line, matches[i]->end_line,
				matches[i]->qualified_name);
		}
		*out_buf = msg;
		if (out_len) *out_len = pos;
		return 0;
	}

	const mm_code_symbol_t *sym = matches[0];
	int s_line = (line_start > 0) ? line_start : sym->start_line;
	int e_line = (line_end > 0)   ? line_end   : sym->end_line;

	size_t cap = 16384;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== [SymRef %u] %s %s:%d-%d (%s) ===\n"
		"Signature: %s\n",
		sym->ref, mm_sym_kind_to_str(sym->kind),
		sym->file_path, sym->start_line, sym->end_line,
		sym->qualified_name, sym->signature);

	bool show_callers = !neighbors || !*neighbors || !strcmp(neighbors, "both") || !strcmp(neighbors, "callers");
	bool show_callees = !neighbors || !*neighbors || !strcmp(neighbors, "both") || !strcmp(neighbors, "callees");

	if (show_callers && sym->n_callers > 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "Inbound Callers (%zu):\n", sym->n_callers);
		for (size_t i = 0; i < sym->n_callers; i++) {
			const mm_code_symbol_t *c = find_sym_by_ref(idx, sym->callers[i]);
			if (c) {
				pos += (size_t)snprintf(buf + pos, cap - pos, "  <- %s (%s:%d)\n",
				                        c->name, c->file_path, c->start_line);
			}
		}
	}
	if (show_callees && sym->n_callees > 0) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "Outbound Callees (%zu):\n", sym->n_callees);
		for (size_t i = 0; i < sym->n_callees; i++) {
			const mm_code_symbol_t *c = find_sym_by_ref(idx, sym->callees[i]);
			if (c) {
				pos += (size_t)snprintf(buf + pos, cap - pos, "  -> %s (%s:%d)\n",
				                        c->name, c->file_path, c->start_line);
			}
		}
	}

	/* Read source lines from disk */
	char abs_path[PATH_MAX];
	snprintf(abs_path, sizeof(abs_path), "%s/%s", idx->root_dir, sym->file_path);
	FILE *fp = fopen(abs_path, "r");
	if (fp) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "--- Source (%s:%d-%d) ---\n",
		                        sym->file_path, s_line, e_line);
		char line[1024];
		int cur = 0;
		while (fgets(line, sizeof(line), fp)) {
			cur++;
			if (cur < s_line) continue;
			if (cur > e_line) break;
			size_t need = strlen(line) + 32;
			if (pos + need >= cap) {
				cap *= 2;
				buf = (char *)realloc(buf, cap);
			}
			pos += (size_t)snprintf(buf + pos, cap - pos, "%4d: %s", cur, line);
		}
		fclose(fp);
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
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
		    str_contains_ci(s->file_path, query)) {
			matched[n_matched++] = s;
		}
	}

	/* Auto-preview when a single symbol matches unambiguously */
	if (n_matched == 1) {
		return mm_code_read_symbol(idx, matched[0]->name, matched[0]->file_path, "both", 0, 0, out_buf, out_len);
	}

	size_t show = n_matched < (size_t)limit ? n_matched : (size_t)limit;
	size_t cap = 1024 + show * 512;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== Codebase Symbols (%zu matches for \"%s\") ===\n",
		n_matched, query ? query : "");
	for (size_t i = 0; i < show; i++) {
		const mm_code_symbol_t *s = matched[i];
		pos += (size_t)snprintf(buf + pos, cap - pos,
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
		if (*pos + 512 >= *cap) {
			*cap *= 2;
			*buf = (char *)realloc(*buf, *cap);
		}
		for (int d = 0; d < depth; d++) {
			*pos += (size_t)snprintf(*buf + *pos, *cap - *pos, "  ");
		}
		*pos += (size_t)snprintf(*buf + *pos, *cap - *pos,
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
			break;
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
	size_t pos = (size_t)snprintf(buf, cap,
		"=== Call Graph Trace: %s (%s:%d-%d, depth=%d) ===\n",
		root->name, root->file_path, root->start_line, root->end_line, max_depth);

	if (do_in) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "Inbound Callers:\n");
		uint32_t visited[256] = { root->ref };
		size_t n_vis = 1;
		format_trace_recursive(idx, root, true, 1, max_depth, visited, &n_vis, &buf, &pos, &cap);
	}
	if (do_out) {
		pos += (size_t)snprintf(buf + pos, cap - pos, "Outbound Callees:\n");
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
	for (size_t i = 0; i < idx->n_symbols; i++) {
		total_edges += idx->symbols[i].n_callees;
	}

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== Codebase Architecture Overview (L3/L2 Mipmap) ===\n"
		"Root: %s\n"
		"Files: %zu | Symbols: %zu | Call Edges: %zu\n"
		"High-Degree Call Hotspots:\n",
		idx->root_dir, idx->n_files, idx->n_symbols, total_edges);

	/* Find top 10 highest-degree symbols */
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
		pos += (size_t)snprintf(buf + pos, cap - pos,
			"  • [SymRef %u] %s (%s:%d-%d) — degree:%zu (in:%zu out:%zu)\n",
			s->ref, s->name, s->file_path, s->start_line, s->end_line,
			best_deg, s->n_callers, s->n_callees);
	}

	*out_buf = buf;
	if (out_len) *out_len = pos;
	return 0;
}

int mm_code_diff_impact(const mm_code_index_t *idx, int depth, char **out_buf, size_t *out_len) {
	if (!idx || !out_buf) return -1;
	if (depth <= 0) depth = 2;

	char cmd[PATH_MAX + 64];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff --unified=0 HEAD 2>/dev/null", idx->root_dir);
	FILE *fp = popen(cmd, "r");
	if (!fp) return -1;

	uint32_t affected_refs[128];
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

				/* Intersect hunk interval [start_l, end_l] against symbol spans [start_line, end_line] */
				for (size_t i = 0; i < idx->n_symbols; i++) {
					const mm_code_symbol_t *s = &idx->symbols[i];
					if (strcmp(s->file_path, cur_file) == 0 &&
					    s->start_line <= end_l && s->end_line >= start_l) {
						bool dup = false;
						for (size_t k = 0; k < n_affected; k++) {
							if (affected_refs[k] == s->ref) { dup = true; break; }
						}
						if (!dup && n_affected < 128) affected_refs[n_affected++] = s->ref;
					}
				}
			}
		}
	}
	pclose(fp);

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap,
		"=== Worktree Diff Impact & Blast Radius (%zu modified symbol%s) ===\n",
		n_affected, n_affected == 1 ? "" : "s");

	for (size_t i = 0; i < n_affected; i++) {
		const mm_code_symbol_t *s = find_sym_by_ref(idx, affected_refs[i]);
		if (!s) continue;
		pos += (size_t)snprintf(buf + pos, cap - pos,
			"• Modified [SymRef %u] %s (%s:%d-%d)\n",
			s->ref, s->name, s->file_path, s->start_line, s->end_line);
		uint32_t visited[256] = { s->ref };
		size_t n_vis = 1;
		format_trace_recursive(idx, s, true, 1, depth, visited, &n_vis, &buf, &pos, &cap);
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

int mm_code_diagnose(const mm_code_index_t *idx, const char *trace_text,
                     int depth, char **out_buf, size_t *out_len) {
	if (!idx || !trace_text || !out_buf) return -1;
	if (depth <= 0) depth = 2;

	size_t cap = 8192;
	char *buf = (char *)malloc(cap);
	size_t pos = (size_t)snprintf(buf, cap, "=== Fault Localization Diagnosis ===\n");

	uint32_t matched_refs[64];
	size_t n_matched = 0;

	/* Match any known symbol name or file:line appearing in trace_text */
	for (size_t i = 0; i < idx->n_symbols && n_matched < 64; i++) {
		const mm_code_symbol_t *s = &idx->symbols[i];
		if (strlen(s->name) >= 3 && code_contains_word_ci(trace_text, s->name)) {
			matched_refs[n_matched++] = s->ref;
		}
	}

	for (size_t i = 0; i < n_matched; i++) {
		const mm_code_symbol_t *s = find_sym_by_ref(idx, matched_refs[i]);
		if (!s) continue;
		pos += (size_t)snprintf(buf + pos, cap - pos,
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
