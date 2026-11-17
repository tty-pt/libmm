#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ============================================================================
 * 1. Heuristic Classifiers & Noise Filters (matches pi-mm ranking.ts)
 * ============================================================================ */

int mm_adaptive_rrf_k(size_t candidate_count) {
	if (candidate_count <= 1) return 10;
	size_t scaled = candidate_count * 2;
	if (scaled < 10) return 10;
	if (scaled > 60) return 60;
	return (int)scaled;
}

bool mm_is_clean_summary_text(const char *summary) {
	if (!summary) return false;
	while (*summary && isspace((unsigned char)*summary)) summary++;
	if (!*summary) return false;

	size_t total = 0;
	size_t printable = 0;
	for (const unsigned char *p = (const unsigned char *)summary; *p; p++) {
		total++;
		if (*p >= 0x20 || *p == '\n' || *p == '\r' || *p == '\t') {
			printable++;
		}
	}
	if (total == 0) return false;
	return ((double)printable / (double)total) >= 0.90;
}

static bool starts_with_ci(const char *s, const char *prefix) {
	size_t plen = strlen(prefix);
	return strncasecmp(s, prefix, plen) == 0;
}

static bool contains_word_ci(const char *s, const char *word) {
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

bool mm_is_technical_inquiry(const char *query) {
	if (!query) return false;
	while (*query && isspace((unsigned char)*query)) query++;
	if (!*query) return false;

	for (const char *p = query; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == '.' || *p == '_' || *p == '#' || *p == ':')
			return true;
	}

	static const char *const tech_words[] = {
		"function", "class", "const", "let", "var", "import", "export",
		"include", "err", "error", "crash", "bug", "fix", "build",
		"make", "socket", "corm", "db", NULL
	};
	for (int i = 0; tech_words[i]; i++) {
		if (contains_word_ci(query, tech_words[i])) return true;
	}

	/* Check camelCase or UPPER_CASE */
	for (const char *p = query; *p && p[1]; p++) {
		if (islower((unsigned char)p[0]) && isupper((unsigned char)p[1])) return true;
		if (isupper((unsigned char)p[0]) && p[1] == '_') return true;
	}
	return false;
}

bool mm_is_code_or_doc_artifact(const char *record, const mm_envelope_t *env) {
	if (!env) return false;
	if (!strcmp(env->type, "episode") || (env->ep_problem && env->ep_problem[0])) {
		return false;
	}
	const char *s = (env->summary && env->summary[0]) ? env->summary : (record ? record : "");
	while (*s && isspace((unsigned char)*s)) s++;
	if (!*s) return false;

	static const char *const code_prefixes[] = {
		"import ", "export ", "const ", "function ", "class ", "var ", "let ",
		"#include ", "#include<", "#define ", NULL
	};
	for (int i = 0; code_prefixes[i]; i++) {
		if (strncmp(s, code_prefixes[i], strlen(code_prefixes[i])) == 0) return true;
	}

	if (strstr(s, "isletFspec") || strstr(s, "mem-islet.db")) return true;

	/* Check grep/ripgrep hit line pattern: path/to/file.ext:123: */
	const char *colon = strchr(s, ':');
	if (colon && colon > s && (colon - s) < 128 && isdigit((unsigned char)colon[1])) {
		bool has_dot = false;
		bool valid_path = true;
		for (const char *p = s; p < colon; p++) {
			if (*p == '.') has_dot = true;
			if (isspace((unsigned char)*p)) { valid_path = false; break; }
		}
		if (has_dot && valid_path) return true;
	}

	if (strncmp(s, "# ", 2) == 0) {
		if (strstr(s, "README") || strstr(s, "readme") ||
		    strstr(s, "Documentation") || strstr(s, "LICENSE") ||
		    strstr(s, "pi-mm:")) {
			return true;
		}
	}
	return false;
}

bool mm_is_question_memory(const mm_envelope_t *env) {
	if (!env) return false;
	const char *s = env->summary ? env->summary : "";
	while (*s && isspace((unsigned char)*s)) s++;
	size_t slen = strlen(s);
	while (slen > 0 && isspace((unsigned char)s[slen - 1])) slen--;
	if (slen > 0 && s[slen - 1] == '?') return true;

	const char *check = s;
	if (starts_with_ci(check, "investigation:")) {
		check += 14;
		while (*check && isspace((unsigned char)*check)) check++;
	}
	static const char *const q_prefixes[] = {
		"remember anything", "anything about", "do you remember", "can you remember",
		"what did i say", "did i mention", "what do you remember", "what do you recall",
		"what did i tell", NULL
	};
	for (int i = 0; q_prefixes[i]; i++) {
		if (starts_with_ci(check, q_prefixes[i])) return true;
	}
	return false;
}

bool mm_is_assistant_echo(const mm_envelope_t *env) {
	if (!env || !env->summary) return false;
	const char *s = env->summary;
	while (*s && isspace((unsigned char)*s)) s++;
	if (strncmp(s, "> *\"", 4) == 0 || strncmp(s, "> \"", 3) == 0) return true;

	static const char *const echo_prefixes[] = {
		"you mentioned:", "you mentioned -", "you said:", "you said -",
		"i remember that you:", "as you said:", NULL
	};
	for (int i = 0; echo_prefixes[i]; i++) {
		if (starts_with_ci(s, echo_prefixes[i])) return true;
	}
	return false;
}

/* ============================================================================
 * 2. Stage 1: Scale-Invariant Reciprocal Rank Fusion (RRF)
 * ============================================================================ */

static int cmp_rrf_desc(const void *a, const void *b) {
	const mm_candidate_t *ca = (const mm_candidate_t *)a;
	const mm_candidate_t *cb = (const mm_candidate_t *)b;
	if (cb->rrf_score > ca->rrf_score) return 1;
	if (cb->rrf_score < ca->rrf_score) return -1;
	if (ca->tier != cb->tier) return (int)ca->tier - (int)cb->tier;
	if (ca->ref < cb->ref) return -1;
	if (ca->ref > cb->ref) return 1;
	return 0;
}

int mm_fuse_rankings_rrf(const mm_result_line_t *lexical, size_t n_lexical,
                         const mm_result_line_t *semantic, size_t n_semantic,
                         int k_override,
                         mm_candidate_t **out_candidates, size_t *out_count) {
	if (!out_candidates || !out_count) return -1;
	*out_candidates = NULL;
	*out_count = 0;

	size_t max_cap = n_lexical + n_semantic;
	if (max_cap == 0) return 0;

	int eff_k = (k_override > 0) ? k_override : mm_adaptive_rrf_k(max_cap);
	mm_candidate_t *pool = (mm_candidate_t *)calloc(max_cap, sizeof(mm_candidate_t));
	if (!pool) return -1;
	size_t count = 0;

	for (size_t i = 0; i < n_lexical; i++) {
		int rank = (int)(i + 1);
		double contrib = 1.0 / (double)(eff_k + rank);
		mm_tier_t tier = lexical[i].tier ? lexical[i].tier : MM_TIER_PROJECT;

		/* Deduplicate within lexical if same (tier, ref) appears twice */
		ssize_t found = -1;
		for (size_t k = 0; k < count; k++) {
			if (pool[k].tier == tier && pool[k].ref == lexical[i].ref) {
				found = (ssize_t)k;
				break;
			}
		}
		if (found >= 0) continue;

		mm_candidate_t *c = &pool[count++];
		c->tier = tier;
		c->ref = lexical[i].ref;
		c->record = lexical[i].record ? strdup(lexical[i].record) : strdup("");
		c->origin = MM_ORIGIN_LEMMA;
		c->rrf_score = contrib;
		c->lexical_rank = rank;
		c->lexical_score = lexical[i].score;
	}

	for (size_t j = 0; j < n_semantic; j++) {
		int rank = (int)(j + 1);
		double contrib = 1.0 / (double)(eff_k + rank);
		mm_tier_t tier = semantic[j].tier ? semantic[j].tier : MM_TIER_PROJECT;

		ssize_t found = -1;
		for (size_t k = 0; k < count; k++) {
			if (pool[k].tier == tier && pool[k].ref == semantic[j].ref) {
				found = (ssize_t)k;
				break;
			}
		}
		if (found >= 0) {
			mm_candidate_t *c = &pool[found];
			if (c->origin == MM_ORIGIN_LEMMA) {
				c->origin = MM_ORIGIN_BOTH;
				c->rrf_score += contrib;
				c->semantic_rank = rank;
				c->semantic_score = semantic[j].score;
				if ((!c->record || !c->record[0]) && semantic[j].record) {
					free(c->record);
					c->record = strdup(semantic[j].record);
				}
			}
		} else {
			mm_candidate_t *c = &pool[count++];
			c->tier = tier;
			c->ref = semantic[j].ref;
			c->record = semantic[j].record ? strdup(semantic[j].record) : strdup("");
			c->origin = MM_ORIGIN_SEPAL;
			c->rrf_score = contrib;
			c->semantic_rank = rank;
			c->semantic_score = semantic[j].score;
		}
	}

	qsort(pool, count, sizeof(mm_candidate_t), cmp_rrf_desc);
	*out_candidates = pool;
	*out_count = count;
	return 0;
}

/* ============================================================================
 * 3. Stage 2: Symbolic & Epistemic Re-Ranking
 * ============================================================================ */

static double lifecycle_weight(mm_epistemic_status_t st) {
	switch (st) {
	case MM_STATUS_SUPPORTED:    return 1.00;
	case MM_STATUS_TESTED:       return 0.95;
	case MM_STATUS_OBSERVED:     return 0.90;
	case MM_STATUS_HYPOTHESIZED: return 0.70;
	case MM_STATUS_CONTRADICTED: return 0.10;
	case MM_STATUS_SUPERSEDED:   return 0.05;
	default:                     return 0.80;
	}
}

static double mipmap_weight(mm_level_t lvl) {
	switch (lvl) {
	case MM_LEVEL_L3: return 1.15;
	case MM_LEVEL_L2: return 1.10;
	case MM_LEVEL_L1: return 1.05;
	case MM_LEVEL_L0: return 0.90;
	default:          return 1.00;
	}
}

double mm_compute_ranking_score(const mm_candidate_t *cand, const mm_filter_criteria_t *crit) {
	if (!cand) return 0.0;
	double score = (cand->rrf_score > 0.0)
		? cand->rrf_score
		: (cand->lexical_score > 0.0 ? cand->lexical_score : (cand->semantic_score > 0.0 ? cand->semantic_score : 1.0));
	if (isnan(score) || score <= 0.0) score = 1.0;

	if (!cand->has_envelope) return score;
	const mm_envelope_t *env = &cand->envelope;

	/* 1. Epistemic Lifecycle Multiplier */
	score *= lifecycle_weight(env->lifecycle);

	/* 2. Hierarchical Mipmap Level Selection */
	score *= mipmap_weight(env->level);

	/* 3. Provenance bonus for evidence-backed conclusions */
	if (env->n_evidence > 0) {
		score *= 1.05;
	}

	/* 4. Utility feedback factor */
	if (env->used_count > 0 || env->irrelevant_count > 0 || env->obsolete_count > 0) {
		int net = env->used_count - env->irrelevant_count - (env->obsolete_count * 2);
		double delta = (double)net * 0.01;
		if (delta < -0.05) delta = -0.05;
		if (delta > 0.05)  delta = 0.05;
		score *= (1.0 + delta);
	}

	/* 5. Scope compatibility bonus */
	if (crit) {
		if (crit->project && crit->project[0] && !strcmp(env->ctx_project, crit->project)) {
			score *= 1.05;
		}
		if (crit->subsystem && crit->subsystem[0] && !strcmp(env->ctx_subsystem, crit->subsystem)) {
			score *= 1.05;
		}
	}

	/* 6. Flashbulb anchor memory salience boost */
	if (env->is_anchor || env->standing) {
		score *= 1.25;
	}

	/* 7. Downrank code or documentation syntax artifacts unless L0 is explicitly requested */
	if ((!crit || crit->mipmap != MM_LEVEL_L0) && mm_is_code_or_doc_artifact(cand->record, env)) {
		score *= 0.20;
	}

	return score;
}

static int cmp_ranking_desc(const void *a, const void *b) {
	const mm_candidate_t *ca = (const mm_candidate_t *)a;
	const mm_candidate_t *cb = (const mm_candidate_t *)b;
	if (cb->ranking_score > ca->ranking_score) return 1;
	if (cb->ranking_score < ca->ranking_score) return -1;
	if (ca->tier != cb->tier) return (int)ca->tier - (int)cb->tier;
	if (ca->ref < cb->ref) return -1;
	if (ca->ref > cb->ref) return 1;
	return 0;
}

static void normalize_summary(const char *s, char *out, size_t cap) {
	if (!s || cap == 0) {
		if (cap > 0) out[0] = '\0';
		return;
	}
	size_t j = 0;
	bool prev_space = true;
	for (const unsigned char *p = (const unsigned char *)s; *p && j + 1 < cap; p++) {
		if (isalnum(*p) || *p == '_') {
			out[j++] = (char)tolower(*p);
			prev_space = false;
		} else if (isspace(*p)) {
			if (!prev_space) {
				out[j++] = ' ';
				prev_space = true;
			}
		}
	}
	while (j > 0 && out[j - 1] == ' ') j--;
	out[j] = '\0';
}

static void clone_envelope(mm_envelope_t *dst, const mm_envelope_t *src) {
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
}

int mm_filter_and_enrich(mm_candidate_t *candidates, size_t n_candidates,
                         const mm_filter_criteria_t *crit,
                         mm_candidate_t **out_filtered, size_t *out_count) {
	if (!out_filtered || !out_count) return -1;
	*out_filtered = NULL;
	*out_count = 0;
	if (!candidates || n_candidates == 0) return 0;

	mm_filter_criteria_t def_crit;
	memset(&def_crit, 0, sizeof(def_crit));
	def_crit.mipmap = MM_LEVEL_NONE;
	def_crit.limit = 10;
	if (!crit) crit = &def_crit;

	bool technical = mm_is_technical_inquiry(crit->query);
	mm_candidate_t *filtered = (mm_candidate_t *)calloc(n_candidates, sizeof(mm_candidate_t));
	if (!filtered) return -1;
	size_t count = 0;

	for (size_t i = 0; i < n_candidates; i++) {
		mm_candidate_t *cand = &candidates[i];
		if (!cand->has_envelope) {
			if (mm_payload_decode(cand->record ? cand->record : "",
			                      cand->record ? strlen(cand->record) : 0,
			                      &cand->envelope) == 0) {
				cand->has_envelope = true;
			}
		}
		if (!cand->has_envelope) continue;
		const mm_envelope_t *env = &cand->envelope;

		if (crit->mipmap != MM_LEVEL_NONE && env->level != crit->mipmap) continue;

		/* Strict pruning of code/doc artifacts on conversational queries */
		if (crit->mipmap != MM_LEVEL_L0 && mm_is_code_or_doc_artifact(cand->record, env)) {
			if (!technical) continue;
		}

		/* Lifecycle filtering: exclude superseded by default */
		if (crit->lifecycle && crit->lifecycle[0] && strcmp(crit->lifecycle, "all") != 0) {
			if (env->lifecycle != mm_status_from_str(crit->lifecycle)) continue;
		} else {
			if (env->lifecycle == MM_STATUS_SUPERSEDED) continue;
		}

		/* Project/subsystem filter (Tier 1 standing directives bypass repo filter!) */
		if (!env->standing && cand->tier != MM_TIER_GLOBAL) {
			if (crit->project && crit->project[0] && env->ctx_project[0] &&
			    strcmp(env->ctx_project, crit->project) != 0) {
				continue;
			}
			if (crit->subsystem && crit->subsystem[0] && env->ctx_subsystem[0] &&
			    strcmp(env->ctx_subsystem, crit->subsystem) != 0) {
				continue;
			}
		}

		/* Normal recall prefers L1+ over raw L0 unless requested */
		if (crit->mipmap == MM_LEVEL_NONE && env->level == MM_LEVEL_L0 && !crit->include_l0) {
			continue;
		}

		/* Exclude assistant echoes and dirty summaries */
		if (mm_is_assistant_echo(env)) continue;
		if (!mm_is_clean_summary_text(env->summary)) continue;

		/* Exclude question-formed investigations from normal retrieval */
		if (!strcmp(env->type, "investigation") && mm_is_question_memory(env)) continue;

		mm_candidate_t *dst = &filtered[count++];
		dst->tier = cand->tier;
		dst->ref = cand->ref;
		dst->record = cand->record ? strdup(cand->record) : strdup("");
		dst->origin = cand->origin;
		dst->rrf_score = cand->rrf_score;
		dst->lexical_rank = cand->lexical_rank;
		dst->semantic_rank = cand->semantic_rank;
		dst->lexical_score = cand->lexical_score;
		dst->semantic_score = cand->semantic_score;
		clone_envelope(&dst->envelope, env);
		dst->has_envelope = true;
		dst->ranking_score = mm_compute_ranking_score(dst, crit);
	}

	qsort(filtered, count, sizeof(mm_candidate_t), cmp_ranking_desc);

	size_t max_out = (crit->limit > 0) ? (size_t)crit->limit : count;
	if (crit->dedupe && count > 1) {
		size_t write_idx = 0;
		for (size_t i = 0; i < count; i++) {
			char norm_i[512];
			normalize_summary(filtered[i].envelope.summary, norm_i, sizeof(norm_i));
			bool dup = false;
			for (size_t j = 0; j < write_idx; j++) {
				char norm_j[512];
				normalize_summary(filtered[j].envelope.summary, norm_j, sizeof(norm_j));
				if (norm_i[0] && strcmp(norm_i, norm_j) == 0) {
					dup = true;
					break;
				}
			}
			if (dup) {
				free(filtered[i].record);
				if (filtered[i].has_envelope) mm_envelope_free(&filtered[i].envelope);
			} else {
				if (write_idx != i) filtered[write_idx] = filtered[i];
				write_idx++;
			}
		}
		count = write_idx;
	}

	if (count > max_out) {
		for (size_t i = max_out; i < count; i++) {
			free(filtered[i].record);
			if (filtered[i].has_envelope) mm_envelope_free(&filtered[i].envelope);
		}
		count = max_out;
	}

	*out_filtered = filtered;
	*out_count = count;
	return 0;
}

void mm_candidates_free(mm_candidate_t *candidates, size_t count) {
	if (!candidates) return;
	for (size_t i = 0; i < count; i++) {
		free(candidates[i].record);
		if (candidates[i].has_envelope) {
			mm_envelope_free(&candidates[i].envelope);
		}
	}
	free(candidates);
}
