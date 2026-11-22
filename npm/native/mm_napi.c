#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <ttypt/mm.h>

/* ============================================================================
 * Helper Utilities for N-API Value Extraction
 * ============================================================================ */

static bool get_str_arg(napi_env env, napi_value val, char *buf, size_t cap) {
	if (!val || cap == 0) return false;
	buf[0] = '\0';
	napi_valuetype vt;
	if (napi_typeof(env, val, &vt) != napi_ok || vt != napi_string) return false;
	size_t len = 0;
	napi_get_value_string_utf8(env, val, buf, cap, &len);
	return true;
}

static char *get_str_heap(napi_env env, napi_value val) {
	if (!val) return NULL;
	napi_valuetype vt;
	if (napi_typeof(env, val, &vt) != napi_ok || vt != napi_string) return NULL;
	size_t len = 0;
	napi_get_value_string_utf8(env, val, NULL, 0, &len);
	char *buf = (char *)malloc(len + 1);
	if (!buf) return NULL;
	napi_get_value_string_utf8(env, val, buf, len + 1, &len);
	return buf;
}

static bool get_prop_str(napi_env env, napi_value obj, const char *key, char *buf, size_t cap) {
	buf[0] = '\0';
	napi_valuetype vt;
	if (napi_typeof(env, obj, &vt) != napi_ok || vt != napi_object) return false;
	bool has = false;
	if (napi_has_named_property(env, obj, key, &has) != napi_ok || !has) return false;
	napi_value val;
	if (napi_get_named_property(env, obj, key, &val) != napi_ok) return false;
	return get_str_arg(env, val, buf, cap);
}

static char *get_prop_str_heap(napi_env env, napi_value obj, const char *key) {
	napi_valuetype vt;
	if (napi_typeof(env, obj, &vt) != napi_ok || vt != napi_object) return NULL;
	bool has = false;
	if (napi_has_named_property(env, obj, key, &has) != napi_ok || !has) return NULL;
	napi_value val;
	if (napi_get_named_property(env, obj, key, &val) != napi_ok) return NULL;
	return get_str_heap(env, val);
}

static int get_prop_int(napi_env env, napi_value obj, const char *key, int def_val) {
	napi_valuetype vt;
	if (napi_typeof(env, obj, &vt) != napi_ok || vt != napi_object) return def_val;
	bool has = false;
	if (napi_has_named_property(env, obj, key, &has) != napi_ok || !has) return def_val;
	napi_value val;
	if (napi_get_named_property(env, obj, key, &val) != napi_ok) return def_val;
	if (napi_typeof(env, val, &vt) != napi_ok || vt != napi_number) return def_val;
	int32_t out = def_val;
	napi_get_value_int32(env, val, &out);
	return (int)out;
}

static double get_prop_double(napi_env env, napi_value obj, const char *key, double def_val) {
	napi_valuetype vt;
	if (napi_typeof(env, obj, &vt) != napi_ok || vt != napi_object) return def_val;
	bool has = false;
	if (napi_has_named_property(env, obj, key, &has) != napi_ok || !has) return def_val;
	napi_value val;
	if (napi_get_named_property(env, obj, key, &val) != napi_ok) return def_val;
	if (napi_typeof(env, val, &vt) != napi_ok) return def_val;
	if (vt == napi_number) {
		double out = def_val;
		napi_get_value_double(env, val, &out);
		return out;
	}
	if (vt == napi_string) {
		char s[64] = {0};
		get_str_arg(env, val, s, sizeof(s));
		if (s[0]) return strtod(s, NULL);
	}
	return def_val;
}

static bool get_prop_bool(napi_env env, napi_value obj, const char *key, bool def_val) {
	napi_valuetype vt;
	if (napi_typeof(env, obj, &vt) != napi_ok || vt != napi_object) return def_val;
	bool has = false;
	if (napi_has_named_property(env, obj, key, &has) != napi_ok || !has) return def_val;
	napi_value val;
	if (napi_get_named_property(env, obj, key, &val) != napi_ok) return def_val;
	if (napi_typeof(env, val, &vt) != napi_ok || vt != napi_boolean) return def_val;
	bool out = def_val;
	napi_get_value_bool(env, val, &out);
	return out;
}

static mm_tier_t parse_tier_str(const char *s, mm_tier_t def_tier) {
	if (!s || !*s) return def_tier;
	if (strcmp(s, "global") == 0 || strcmp(s, "G") == 0) return MM_TIER_GLOBAL;
	if (strcmp(s, "project") == 0 || strcmp(s, "P") == 0) return MM_TIER_PROJECT;
	if (strcmp(s, "both") == 0) return MM_TIER_BOTH;
	if (strcmp(s, "auto") == 0) return MM_TIER_AUTO;
	return def_tier;
}

static const char *tier_to_str(mm_tier_t tier) {
	switch (tier) {
	case MM_TIER_GLOBAL:  return "global";
	case MM_TIER_PROJECT: return "project";
	case MM_TIER_BOTH:    return "both";
	default:              return "auto";
	}
}

/* ============================================================================
 * 1. Stateless Algorithmic & Heuristic Bindings
 * ============================================================================ */

static napi_value NapiAdaptiveRrfK(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	int32_t count = 0;
	if (argc >= 1) napi_get_value_int32(env, args[0], &count);
	int k = mm_adaptive_rrf_k(count > 0 ? (size_t)count : 0);
	napi_value res;
	napi_create_int32(env, k, &res);
	return res;
}

static napi_value NapiIsTechnicalInquiry(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *q = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	bool ok = mm_is_technical_inquiry(q);
	free(q);
	napi_value res;
	napi_get_boolean(env, ok, &res);
	return res;
}

static napi_value NapiIsCleanSummaryText(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *s = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	bool ok = mm_is_clean_summary_text(s);
	free(s);
	napi_value res;
	napi_get_boolean(env, ok, &res);
	return res;
}

static napi_value NapiIsCodeOrDocArtifact(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *rec = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	char *env_json = (argc >= 2) ? get_str_heap(env, args[1]) : NULL;
	mm_envelope_t me;
	mm_envelope_init(&me);
	if (env_json && env_json[0]) {
		mm_envelope_from_json(env_json, strlen(env_json), &me);
	} else if (rec && rec[0]) {
		mm_payload_decode(rec, strlen(rec), &me);
	}
	bool ok = mm_is_code_or_doc_artifact(rec, &me);
	mm_envelope_free(&me);
	free(rec);
	free(env_json);
	napi_value res;
	napi_get_boolean(env, ok, &res);
	return res;
}

static napi_value NapiIsQuestionMemory(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *env_json = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	mm_envelope_t me;
	mm_envelope_init(&me);
	if (env_json && env_json[0]) {
		mm_envelope_from_json(env_json, strlen(env_json), &me);
	}
	bool ok = mm_is_question_memory(&me);
	mm_envelope_free(&me);
	free(env_json);
	napi_value res;
	napi_get_boolean(env, ok, &res);
	return res;
}

static napi_value NapiIsAssistantEcho(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *env_json = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	mm_envelope_t me;
	mm_envelope_init(&me);
	if (env_json && env_json[0]) {
		mm_envelope_from_json(env_json, strlen(env_json), &me);
	}
	bool ok = mm_is_assistant_echo(&me);
	mm_envelope_free(&me);
	free(env_json);
	napi_value res;
	napi_get_boolean(env, ok, &res);
	return res;
}

static mm_result_line_t *extract_result_lines(napi_env env, napi_value arr_val, size_t *out_len) {
	*out_len = 0;
	if (!arr_val) return NULL;
	bool is_arr = false;
	if (napi_is_array(env, arr_val, &is_arr) != napi_ok || !is_arr) return NULL;
	uint32_t len = 0;
	napi_get_array_length(env, arr_val, &len);
	if (len == 0) return NULL;
	mm_result_line_t *lines = (mm_result_line_t *)calloc(len, sizeof(mm_result_line_t));
	if (!lines) return NULL;
	for (uint32_t i = 0; i < len; i++) {
		napi_value elem;
		napi_get_element(env, arr_val, i, &elem);
		char tier_s[32] = {0};
		get_prop_str(env, elem, "tier", tier_s, sizeof(tier_s));
		lines[i].tier = parse_tier_str(tier_s, MM_TIER_PROJECT);
		lines[i].ref = (uint32_t)get_prop_int(env, elem, "ref", 0);
		lines[i].score = get_prop_double(env, elem, "score", 0.0);
		lines[i].record = get_prop_str_heap(env, elem, "record");
		if (!lines[i].record) lines[i].record = strdup("");
	}
	*out_len = len;
	return lines;
}

static void free_result_lines(mm_result_line_t *lines, size_t len) {
	if (!lines) return;
	for (size_t i = 0; i < len; i++) free(lines[i].record);
	free(lines);
}

static napi_value candidate_to_js(napi_env env, const mm_candidate_t *c, bool include_envelope_json) {
	napi_value obj;
	napi_create_object(env, &obj);

	napi_value v_ref, v_tier, v_rec, v_orig, v_rrf;
	napi_create_uint32(env, c->ref, &v_ref);
	napi_create_string_utf8(env, tier_to_str(c->tier), NAPI_AUTO_LENGTH, &v_tier);
	napi_create_string_utf8(env, c->record ? c->record : "", NAPI_AUTO_LENGTH, &v_rec);
	napi_create_string_utf8(env, mm_origin_to_str(c->origin), NAPI_AUTO_LENGTH, &v_orig);
	napi_create_double(env, c->rrf_score, &v_rrf);

	napi_set_named_property(env, obj, "ref", v_ref);
	napi_set_named_property(env, obj, "tier", v_tier);
	napi_set_named_property(env, obj, "record", v_rec);
	napi_set_named_property(env, obj, "origin", v_orig);
	napi_set_named_property(env, obj, "rrfScore", v_rrf);

	if (c->lexical_rank > 0) {
		napi_value v_lr;
		napi_create_int32(env, c->lexical_rank, &v_lr);
		napi_set_named_property(env, obj, "lexicalRank", v_lr);
		if (c->lexical_score > 0.0) {
			char sbuf[32];
			snprintf(sbuf, sizeof(sbuf), "%.6f", c->lexical_score);
			napi_value v_ls;
			napi_create_string_utf8(env, sbuf, NAPI_AUTO_LENGTH, &v_ls);
			napi_set_named_property(env, obj, "lexicalScore", v_ls);
		}
	}
	if (c->semantic_rank > 0) {
		napi_value v_sr;
		napi_create_int32(env, c->semantic_rank, &v_sr);
		napi_set_named_property(env, obj, "semanticRank", v_sr);
		if (c->semantic_score > 0.0) {
			char sbuf[32];
			snprintf(sbuf, sizeof(sbuf), "%.6f", c->semantic_score);
			napi_value v_ss;
			napi_create_string_utf8(env, sbuf, NAPI_AUTO_LENGTH, &v_ss);
			napi_set_named_property(env, obj, "semanticScore", v_ss);
		}
	}
	if (c->ranking_score > 0.0) {
		napi_value v_rs;
		napi_create_double(env, c->ranking_score, &v_rs);
		napi_set_named_property(env, obj, "rankingScore", v_rs);
	}
	if (include_envelope_json && c->has_envelope) {
		char *ejson = NULL;
		if (mm_envelope_to_json(&c->envelope, &ejson, NULL) == 0 && ejson) {
			napi_value v_ej;
			napi_create_string_utf8(env, ejson, NAPI_AUTO_LENGTH, &v_ej);
			napi_set_named_property(env, obj, "envelopeJson", v_ej);
			free(ejson);
		}
		napi_value v_lvl, v_lc, v_sum;
		napi_create_string_utf8(env, mm_level_to_str(c->envelope.level), NAPI_AUTO_LENGTH, &v_lvl);
		napi_create_string_utf8(env, mm_status_to_str(c->envelope.lifecycle), NAPI_AUTO_LENGTH, &v_lc);
		napi_create_string_utf8(env, c->envelope.summary ? c->envelope.summary : "", NAPI_AUTO_LENGTH, &v_sum);
		napi_set_named_property(env, obj, "level", v_lvl);
		napi_set_named_property(env, obj, "lifecycle", v_lc);
		napi_set_named_property(env, obj, "summary", v_sum);
	}
	return obj;
}

static napi_value NapiFuseRankingsRRF(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);

	size_t n_lex = 0, n_sem = 0;
	mm_result_line_t *lex = (argc >= 1) ? extract_result_lines(env, args[0], &n_lex) : NULL;
	mm_result_line_t *sem = (argc >= 2) ? extract_result_lines(env, args[1], &n_sem) : NULL;
	int32_t k_override = 0;
	if (argc >= 3) {
		napi_valuetype vt;
		if (napi_typeof(env, args[2], &vt) == napi_ok && vt == napi_number) {
			napi_get_value_int32(env, args[2], &k_override);
		}
	}

	mm_candidate_t *fused = NULL;
	size_t n_fused = 0;
	mm_fuse_rankings_rrf(lex, n_lex, sem, n_sem, k_override, &fused, &n_fused);
	free_result_lines(lex, n_lex);
	free_result_lines(sem, n_sem);

	napi_value arr;
	napi_create_array_with_length(env, n_fused, &arr);
	for (size_t i = 0; i < n_fused; i++) {
		napi_set_element(env, arr, (uint32_t)i, candidate_to_js(env, &fused[i], false));
	}
	mm_candidates_free(fused, n_fused);
	return arr;
}

static void js_to_candidate(napi_env env, napi_value obj, mm_candidate_t *out) {
	memset(out, 0, sizeof(*out));
	char tier_s[32] = {0}, orig_s[32] = {0};
	get_prop_str(env, obj, "tier", tier_s, sizeof(tier_s));
	get_prop_str(env, obj, "origin", orig_s, sizeof(orig_s));
	out->tier = parse_tier_str(tier_s, MM_TIER_PROJECT);
	out->ref = (uint32_t)get_prop_int(env, obj, "ref", 0);
	out->record = get_prop_str_heap(env, obj, "record");
	if (!out->record) out->record = strdup("");
	if (!strcmp(orig_s, "sepal")) out->origin = MM_ORIGIN_SEPAL;
	else if (!strcmp(orig_s, "both")) out->origin = MM_ORIGIN_BOTH;
	else out->origin = MM_ORIGIN_LEMMA;
	out->rrf_score = get_prop_double(env, obj, "rrfScore", 0.0);
	if (out->rrf_score <= 0.0) {
		out->rrf_score = get_prop_double(env, obj, "score", 0.0);
	}
	out->lexical_rank = get_prop_int(env, obj, "lexicalRank", 0);
	out->semantic_rank = get_prop_int(env, obj, "semanticRank", 0);
	out->lexical_score = get_prop_double(env, obj, "lexicalScore", 0.0);
	out->semantic_score = get_prop_double(env, obj, "semanticScore", 0.0);

	char *env_json = get_prop_str_heap(env, obj, "envelopeJson");
	if (env_json && env_json[0]) {
		if (mm_envelope_from_json(env_json, strlen(env_json), &out->envelope) == 0) {
			out->has_envelope = true;
		}
	}
	free(env_json);
}

static napi_value NapiComputeRankingScore(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) {
		napi_value z;
		napi_create_double(env, 0.0, &z);
		return z;
	}
	mm_candidate_t cand;
	js_to_candidate(env, args[0], &cand);
	if (!cand.has_envelope && cand.record && cand.record[0]) {
		if (mm_payload_decode(cand.record, strlen(cand.record), &cand.envelope) == 0) {
			cand.has_envelope = true;
		}
	}

	mm_filter_criteria_t crit;
	memset(&crit, 0, sizeof(crit));
	char mipmap_s[16] = {0}, lc_s[32] = {0}, proj_s[128] = {0}, sub_s[128] = {0}, q_s[256] = {0};
	if (argc >= 2) {
		get_prop_str(env, args[1], "mipmap", mipmap_s, sizeof(mipmap_s));
		get_prop_str(env, args[1], "lifecycle", lc_s, sizeof(lc_s));
		get_prop_str(env, args[1], "project", proj_s, sizeof(proj_s));
		get_prop_str(env, args[1], "subsystem", sub_s, sizeof(sub_s));
		get_prop_str(env, args[1], "query", q_s, sizeof(q_s));
		crit.mipmap = mipmap_s[0] ? mm_level_from_str(mipmap_s) : MM_LEVEL_NONE;
		crit.lifecycle = lc_s[0] ? lc_s : NULL;
		crit.project = proj_s[0] ? proj_s : NULL;
		crit.subsystem = sub_s[0] ? sub_s : NULL;
		crit.query = q_s[0] ? q_s : NULL;
		crit.limit = get_prop_int(env, args[1], "limit", 10);
		crit.dedupe = get_prop_bool(env, args[1], "dedupe", false);
		crit.include_l0 = get_prop_bool(env, args[1], "includeL0", false);
	}

	double score = mm_compute_ranking_score(&cand, &crit);
	free(cand.record);
	if (cand.has_envelope) mm_envelope_free(&cand.envelope);

	napi_value res;
	napi_create_double(env, score, &res);
	return res;
}

static napi_value NapiFilterAndEnrich(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) {
		napi_value empty;
		napi_create_array(env, &empty);
		return empty;
	}
	bool is_arr = false;
	if (napi_is_array(env, args[0], &is_arr) != napi_ok || !is_arr) {
		napi_value empty;
		napi_create_array(env, &empty);
		return empty;
	}
	uint32_t len = 0;
	napi_get_array_length(env, args[0], &len);
	if (len == 0) {
		napi_value empty;
		napi_create_array(env, &empty);
		return empty;
	}

	mm_candidate_t *in_cands = (mm_candidate_t *)calloc(len, sizeof(mm_candidate_t));
	for (uint32_t i = 0; i < len; i++) {
		napi_value elem;
		napi_get_element(env, args[0], i, &elem);
		js_to_candidate(env, elem, &in_cands[i]);
	}

	mm_filter_criteria_t crit;
	memset(&crit, 0, sizeof(crit));
	char mipmap_s[16] = {0}, lc_s[32] = {0}, proj_s[128] = {0}, sub_s[128] = {0}, q_s[256] = {0};
	crit.limit = 10;
	if (argc >= 2) {
		get_prop_str(env, args[1], "mipmap", mipmap_s, sizeof(mipmap_s));
		get_prop_str(env, args[1], "lifecycle", lc_s, sizeof(lc_s));
		get_prop_str(env, args[1], "project", proj_s, sizeof(proj_s));
		get_prop_str(env, args[1], "subsystem", sub_s, sizeof(sub_s));
		get_prop_str(env, args[1], "query", q_s, sizeof(q_s));
		crit.mipmap = mipmap_s[0] ? mm_level_from_str(mipmap_s) : MM_LEVEL_NONE;
		crit.lifecycle = lc_s[0] ? lc_s : NULL;
		crit.project = proj_s[0] ? proj_s : NULL;
		crit.subsystem = sub_s[0] ? sub_s : NULL;
		crit.query = q_s[0] ? q_s : NULL;
		crit.limit = get_prop_int(env, args[1], "limit", 10);
		crit.dedupe = get_prop_bool(env, args[1], "dedupe", false);
		crit.include_l0 = get_prop_bool(env, args[1], "includeL0", false);
	}

	mm_candidate_t *filtered = NULL;
	size_t n_filtered = 0;
	mm_filter_and_enrich(in_cands, len, &crit, &filtered, &n_filtered);
	mm_candidates_free(in_cands, len);

	napi_value out_arr;
	napi_create_array_with_length(env, n_filtered, &out_arr);
	for (size_t i = 0; i < n_filtered; i++) {
		napi_set_element(env, out_arr, (uint32_t)i, candidate_to_js(env, &filtered[i], true));
	}
	mm_candidates_free(filtered, n_filtered);
	return out_arr;
}

static napi_value NapiPayloadEncode(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *json_in = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	if (!json_in) {
		napi_throw_type_error(env, NULL, "Expected JSON string argument");
		return NULL;
	}
	mm_envelope_t me;
	if (mm_envelope_from_json(json_in, strlen(json_in), &me) != 0) {
		free(json_in);
		napi_throw_error(env, NULL, "Failed to parse envelope JSON");
		return NULL;
	}
	free(json_in);

	uint8_t *bytes = NULL;
	size_t n_bytes = 0;
	char *json_out = NULL;
	size_t n_json = 0;
	int rc = mm_payload_encode(&me, &bytes, &n_bytes, &json_out, &n_json);
	mm_envelope_free(&me);
	if (rc != 0 || !bytes) {
		free(bytes);
		free(json_out);
		napi_throw_error(env, NULL, "Failed to encode HTOK payload");
		return NULL;
	}

	napi_value obj, ab, u8, jstr;
	napi_create_object(env, &obj);
	void *ab_data = NULL;
	napi_create_arraybuffer(env, n_bytes, &ab_data, &ab);
	if (ab_data && n_bytes > 0) memcpy(ab_data, bytes, n_bytes);
	napi_create_typedarray(env, napi_uint8_array, n_bytes, ab, 0, &u8);
	napi_create_string_utf8(env, json_out ? json_out : "{}", NAPI_AUTO_LENGTH, &jstr);

	napi_set_named_property(env, obj, "bytes", u8);
	napi_set_named_property(env, obj, "json", jstr);
	free(bytes);
	free(json_out);
	return obj;
}

static napi_value NapiPayloadDecode(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) {
		napi_throw_type_error(env, NULL, "Expected Uint8Array or string");
		return NULL;
	}

	mm_envelope_t me;
	int rc = -1;
	bool is_ta = false;
	napi_is_typedarray(env, args[0], &is_ta);
	if (is_ta) {
		napi_typedarray_type ta_type;
		size_t length = 0;
		void *data = NULL;
		napi_value ab;
		size_t byte_offset = 0;
		napi_get_typedarray_info(env, args[0], &ta_type, &length, &data, &ab, &byte_offset);
		rc = mm_payload_decode(data, length, &me);
	} else {
		char *str = get_str_heap(env, args[0]);
		if (str) {
			rc = mm_payload_decode(str, strlen(str), &me);
			free(str);
		}
	}

	if (rc != 0) {
		napi_throw_error(env, NULL, "Failed to decode payload");
		return NULL;
	}

	char *json_out = NULL;
	mm_envelope_to_json(&me, &json_out, NULL);
	mm_envelope_free(&me);

	napi_value res;
	napi_create_string_utf8(env, json_out ? json_out : "{}", NAPI_AUTO_LENGTH, &res);
	free(json_out);
	return res;
}

static napi_value NapiWindowForLevel(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char lvl_s[16] = "L2";
	if (argc >= 1) get_str_arg(env, args[0], lvl_s, sizeof(lvl_s));
	int64_t now_sec = 0;
	if (argc >= 2) napi_get_value_int64(env, args[1], &now_sec);

	char since[16] = {0}, until[16] = {0};
	bool bounded = mm_window_for_level(mm_level_from_str(lvl_s), now_sec, since, until);

	napi_value obj, v_b, v_s, v_u;
	napi_create_object(env, &obj);
	napi_get_boolean(env, bounded, &v_b);
	napi_create_string_utf8(env, since, NAPI_AUTO_LENGTH, &v_s);
	napi_create_string_utf8(env, until, NAPI_AUTO_LENGTH, &v_u);
	napi_set_named_property(env, obj, "bounded", v_b);
	napi_set_named_property(env, obj, "since", v_s);
	napi_set_named_property(env, obj, "until", v_u);
	return obj;
}

static napi_value NapiClassifyPrompt(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *p = (argc >= 1) ? get_str_heap(env, args[0]) : NULL;
	mm_intent_t intent = mm_classify_prompt(p ? p : "");
	free(p);
	const char *s = "standalone";
	switch (intent) {
	case MM_INTENT_RECALL:    s = "recall"; break;
	case MM_INTENT_DIRECTIVE: s = "directive"; break;
	case MM_INTENT_OVERVIEW:  s = "overview"; break;
	case MM_INTENT_WONDERING: s = "wondering"; break;
	default:                  s = "standalone"; break;
	}
	napi_value res;
	napi_create_string_utf8(env, s, NAPI_AUTO_LENGTH, &res);
	return res;
}

static napi_value NapiScanSource(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) {
		napi_value empty;
		napi_create_array(env, &empty);
		return empty;
	}
	char *fpath = get_str_heap(env, args[0]);
	char *content = get_str_heap(env, args[1]);
	int32_t file_id = 1;
	if (argc >= 3) napi_get_value_int32(env, args[2], &file_id);

	mm_code_index_t idx;
	mm_code_index_init(&idx, ".");
	if (fpath && content) {
		mm_code_scan_source(fpath, content, strlen(content), (uint32_t)file_id, &idx);
	}
	free(fpath);
	free(content);

	napi_value arr;
	napi_create_array_with_length(env, idx.n_symbols, &arr);
	for (size_t i = 0; i < idx.n_symbols; i++) {
		const mm_code_symbol_t *s = &idx.symbols[i];
		napi_value obj, v_name, v_qname, v_psym, v_kind, v_file, v_sl, v_el, v_sig, v_doc, v_callees;
		napi_create_object(env, &obj);
		napi_create_string_utf8(env, s->name, NAPI_AUTO_LENGTH, &v_name);
		napi_create_string_utf8(env, s->qualified_name, NAPI_AUTO_LENGTH, &v_qname);
		napi_create_string_utf8(env, s->parent_symbol, NAPI_AUTO_LENGTH, &v_psym);
		napi_create_string_utf8(env, mm_sym_kind_to_str(s->kind), NAPI_AUTO_LENGTH, &v_kind);
		napi_create_string_utf8(env, s->file_path, NAPI_AUTO_LENGTH, &v_file);
		napi_create_int32(env, s->start_line, &v_sl);
		napi_create_int32(env, s->end_line, &v_el);
		napi_create_string_utf8(env, s->signature, NAPI_AUTO_LENGTH, &v_sig);
		napi_create_string_utf8(env, s->doc_comment, NAPI_AUTO_LENGTH, &v_doc);
		napi_create_array_with_length(env, s->n_raw_callees, &v_callees);
		for (size_t k = 0; k < s->n_raw_callees; k++) {
			napi_value vc;
			napi_create_string_utf8(env, s->raw_callees[k], NAPI_AUTO_LENGTH, &vc);
			napi_set_element(env, v_callees, (uint32_t)k, vc);
		}
		napi_set_named_property(env, obj, "name", v_name);
		napi_set_named_property(env, obj, "qualifiedName", v_qname);
		napi_set_named_property(env, obj, "parentSymbol", v_psym);
		napi_set_named_property(env, obj, "kind", v_kind);
		napi_set_named_property(env, obj, "filePath", v_file);
		napi_set_named_property(env, obj, "startLine", v_sl);
		napi_set_named_property(env, obj, "endLine", v_el);
		napi_set_named_property(env, obj, "signature", v_sig);
		napi_set_named_property(env, obj, "docComment", v_doc);
		napi_set_named_property(env, obj, "rawCallees", v_callees);
		napi_set_element(env, arr, (uint32_t)i, obj);
	}
	mm_code_index_free(&idx);
	return arr;
}

/* ============================================================================
 * 2. Stateful Context (`mm_ctx_t`) & Codebase Index (`mm_code_index_t`) Wrappers
 * ============================================================================ */

typedef struct {
	mm_ctx_t *ctx;
	bool      is_open;
} MmCtxWrapper;

static void MmCtxFinalizer(napi_env env, void *data, void *hint) {
	(void)env;
	(void)hint;
	MmCtxWrapper *w = (MmCtxWrapper *)data;
	if (w) {
		if (w->is_open && w->ctx) {
			mm_close(w->ctx);
			w->ctx = NULL;
			w->is_open = false;
		}
		free(w);
	}
}

static mm_ctx_t *unwrap_ctx(napi_env env, napi_value val) {
	MmCtxWrapper *w = NULL;
	if (napi_unwrap(env, val, (void **)&w) != napi_ok || !w || !w->is_open || !w->ctx) {
		napi_throw_error(env, NULL, "Invalid or closed MmContext");
		return NULL;
	}
	return w->ctx;
}

static napi_value NapiOpenContext(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);

	mm_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.semantic_search = true;
	opts.prefer_daemon = true;

	char global_dir[512] = {0}, project_dir[512] = {0}, cwd[512] = {0};
	char corm_bin[512] = {0}, axis_path[1024] = {0}, auren_url[256] = {0}, embed_model[128] = {0};

	if (argc >= 1) {
		get_prop_str(env, args[0], "globalDir", global_dir, sizeof(global_dir));
		get_prop_str(env, args[0], "projectDir", project_dir, sizeof(project_dir));
		get_prop_str(env, args[0], "cwd", cwd, sizeof(cwd));
		get_prop_str(env, args[0], "cormBin", corm_bin, sizeof(corm_bin));
		get_prop_str(env, args[0], "axisPath", axis_path, sizeof(axis_path));
		get_prop_str(env, args[0], "aurenUrl", auren_url, sizeof(auren_url));
		get_prop_str(env, args[0], "embedModel", embed_model, sizeof(embed_model));
		opts.semantic_search = get_prop_bool(env, args[0], "semanticSearch", true);
		opts.prefer_daemon = get_prop_bool(env, args[0], "preferDaemon", true);
	}
	if (global_dir[0])  opts.global_dir  = global_dir;
	if (project_dir[0]) opts.project_dir = project_dir;
	if (cwd[0])         opts.cwd         = cwd;
	if (corm_bin[0])    opts.corm_bin    = corm_bin;
	if (axis_path[0])   opts.axis_path   = axis_path;
	if (auren_url[0])   opts.auren_url   = auren_url;
	if (embed_model[0]) opts.embed_model = embed_model;

	mm_ctx_t *ctx = mm_open(&opts);
	if (!ctx) {
		napi_throw_error(env, NULL, "Failed to open mm_ctx_t");
		return NULL;
	}

	MmCtxWrapper *w = (MmCtxWrapper *)calloc(1, sizeof(MmCtxWrapper));
	w->ctx = ctx;
	w->is_open = true;

	napi_value obj;
	napi_create_object(env, &obj);
	napi_wrap(env, obj, w, MmCtxFinalizer, NULL, NULL);
	return obj;
}

static napi_value NapiCloseContext(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc >= 1) {
		MmCtxWrapper *w = NULL;
		if (napi_unwrap(env, args[0], (void **)&w) == napi_ok && w && w->is_open && w->ctx) {
			mm_close(w->ctx);
			w->ctx = NULL;
			w->is_open = false;
		}
	}
	napi_value undef;
	napi_get_undefined(env, &undef);
	return undef;
}

static napi_value NapiGetContextInfo(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	napi_value obj, v_gd, v_pd, v_pr, v_pn, v_hp;
	napi_create_object(env, &obj);
	const char *gd = mm_get_tier_dir(ctx, MM_TIER_GLOBAL);
	const char *pd = mm_get_tier_dir(ctx, MM_TIER_PROJECT);
	const char *pr = mm_get_project_root(ctx);
	const char *pn = mm_get_project_name(ctx);
	napi_create_string_utf8(env, gd ? gd : "", NAPI_AUTO_LENGTH, &v_gd);
	napi_create_string_utf8(env, pd ? pd : "", NAPI_AUTO_LENGTH, &v_pd);
	napi_create_string_utf8(env, pr ? pr : "", NAPI_AUTO_LENGTH, &v_pr);
	napi_create_string_utf8(env, pn ? pn : "", NAPI_AUTO_LENGTH, &v_pn);
	napi_get_boolean(env, mm_has_project_tier(ctx), &v_hp);

	napi_set_named_property(env, obj, "globalDir", v_gd);
	napi_set_named_property(env, obj, "projectDir", v_pd);
	napi_set_named_property(env, obj, "projectRoot", v_pr);
	napi_set_named_property(env, obj, "projectName", v_pn);
	napi_set_named_property(env, obj, "hasProjectTier", v_hp);
	return obj;
}

static napi_value NapiRecord(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	napi_value ro = args[1];
	mm_record_req_t req;
	memset(&req, 0, sizeof(req));

	char tier_s[32] = {0}, lvl_s[16] = {0}, type_s[32] = {0}, status_s[32] = {0};
	char proj_s[128] = {0}, sub_s[128] = {0}, mod_s[128] = {0}, path_s[256] = {0};
	char sid_s[128] = {0}, rid_s[128] = {0}, lid_s[128] = {0};

	get_prop_str(env, ro, "tier", tier_s, sizeof(tier_s));
	get_prop_str(env, ro, "level", lvl_s, sizeof(lvl_s));
	get_prop_str(env, ro, "type", type_s, sizeof(type_s));
	get_prop_str(env, ro, "status", status_s, sizeof(status_s));
	get_prop_str(env, ro, "project", proj_s, sizeof(proj_s));
	get_prop_str(env, ro, "subsystem", sub_s, sizeof(sub_s));
	get_prop_str(env, ro, "module", mod_s, sizeof(mod_s));
	get_prop_str(env, ro, "path", path_s, sizeof(path_s));
	get_prop_str(env, ro, "sessionId", sid_s, sizeof(sid_s));
	get_prop_str(env, ro, "runId", rid_s, sizeof(rid_s));
	get_prop_str(env, ro, "leafId", lid_s, sizeof(lid_s));

	char *prob = get_prop_str_heap(env, ro, "problem");
	if (!prob || !prob[0]) {
		free(prob);
		prob = get_prop_str_heap(env, ro, "summary");
	}
	char *req_s = get_prop_str_heap(env, ro, "request");
	char *out_s = get_prop_str_heap(env, ro, "outcome");

	req.tier = parse_tier_str(tier_s, MM_TIER_AUTO);
	req.level = lvl_s[0] ? mm_level_from_str(lvl_s) : MM_LEVEL_NONE;
	req.problem = prob ? prob : "";
	req.request = req_s;
	req.outcome = out_s;
	req.type = type_s[0] ? type_s : "note";
	req.status = status_s[0] ? mm_status_from_str(status_s) : MM_STATUS_SUPPORTED;
	req.standing = get_prop_bool(env, ro, "standing", false);
	req.anchor = get_prop_bool(env, ro, "anchor", false);
	if (proj_s[0]) req.project = proj_s;
	if (sub_s[0])  req.subsystem = sub_s;
	if (mod_s[0])  req.module = mod_s;
	if (path_s[0]) req.path = path_s;
	if (sid_s[0])  req.session_id = sid_s;
	if (rid_s[0])  req.run_id = rid_s;
	if (lid_s[0])  req.leaf_id = lid_s;
	req.turn_id = get_prop_int(env, ro, "turnId", 0);
	req.supersedes_ref = (uint32_t)get_prop_int(env, ro, "supersedesRef", 0);

	mm_tier_t out_tier = MM_TIER_PROJECT;
	uint32_t out_ref = 0;
	int rc = mm_record(ctx, &req, &out_tier, &out_ref);
	free(prob);
	free(req_s);
	free(out_s);

	if (rc != 0) {
		napi_throw_error(env, NULL, "mm_record failed");
		return NULL;
	}

	napi_value res, v_tier, v_ref;
	napi_create_object(env, &res);
	napi_create_string_utf8(env, tier_to_str(out_tier), NAPI_AUTO_LENGTH, &v_tier);
	napi_create_uint32(env, out_ref, &v_ref);
	napi_set_named_property(env, res, "tier", v_tier);
	napi_set_named_property(env, res, "ref", v_ref);
	return res;
}

static napi_value NapiRecall(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	napi_value ro = args[1];
	mm_recall_req_t req;
	memset(&req, 0, sizeof(req));

	char scope_s[32] = {0}, tier_s[32] = {0}, ztier_s[32] = {0};
	char sym_s[128] = {0}, psym_s[128] = {0}, file_s[256] = {0}, kind_s[64] = {0};
	char neigh_s[32] = {0}, trace_s[128] = {0}, dir_s[32] = {0}, sid_s[128] = {0}, lin_s[32] = {0};

	get_prop_str(env, ro, "scope", scope_s, sizeof(scope_s));
	get_prop_str(env, ro, "tier", tier_s, sizeof(tier_s));
	get_prop_str(env, ro, "zoomTier", ztier_s, sizeof(ztier_s));
	get_prop_str(env, ro, "symbol", sym_s, sizeof(sym_s));
	get_prop_str(env, ro, "parentSymbol", psym_s, sizeof(psym_s));
	get_prop_str(env, ro, "file", file_s, sizeof(file_s));
	get_prop_str(env, ro, "kind", kind_s, sizeof(kind_s));
	get_prop_str(env, ro, "neighbors", neigh_s, sizeof(neigh_s));
	get_prop_str(env, ro, "trace", trace_s, sizeof(trace_s));
	get_prop_str(env, ro, "direction", dir_s, sizeof(dir_s));
	get_prop_str(env, ro, "sessionId", sid_s, sizeof(sid_s));
	get_prop_str(env, ro, "lineage", lin_s, sizeof(lin_s));

	char *query_s = get_prop_str_heap(env, ro, "query");
	char *diag_s = get_prop_str_heap(env, ro, "diagnose");
	char *guard_s = get_prop_str_heap(env, ro, "guardrail");

	if (!strcmp(scope_s, "code")) req.scope = MM_SCOPE_CODE;
	else if (!strcmp(scope_s, "session")) req.scope = MM_SCOPE_SESSION;
	else if (!strcmp(scope_s, "persistent")) req.scope = MM_SCOPE_PERSISTENT;
	else req.scope = MM_SCOPE_ALL;

	req.tier = parse_tier_str(tier_s, MM_TIER_BOTH);
	req.zoom_tier = parse_tier_str(ztier_s, MM_TIER_AUTO);
	if (query_s && query_s[0]) req.query = query_s;
	if (sym_s[0])   req.symbol = sym_s;
	if (psym_s[0])  req.parent_symbol = psym_s;
	if (file_s[0])  req.file = file_s;
	if (kind_s[0])  req.kind = kind_s;
	if (neigh_s[0]) req.neighbors = neigh_s;
	if (trace_s[0]) req.trace = trace_s;
	if (dir_s[0])   req.direction = dir_s;
	if (diag_s && diag_s[0])   req.diagnose = diag_s;
	if (guard_s && guard_s[0]) req.guardrail = guard_s;
	if (sid_s[0])   req.session_id = sid_s;
	if (lin_s[0])   req.lineage = lin_s;

	req.depth = get_prop_int(env, ro, "depth", 2);
	req.diff = get_prop_bool(env, ro, "diff", false);
	req.overview = get_prop_bool(env, ro, "overview", false);
	req.zoom_ref = (uint32_t)get_prop_int(env, ro, "zoomRef", 0);
	req.zoom_turn = get_prop_int(env, ro, "zoomTurn", 0);
	req.tool_index = get_prop_int(env, ro, "toolIndex", 0);
	req.line_start = get_prop_int(env, ro, "lineStart", 0);
	req.line_end = get_prop_int(env, ro, "lineEnd", 0);
	req.limit = get_prop_int(env, ro, "limit", 10);
	req.include_l0 = get_prop_bool(env, ro, "includeL0", false);
	req.include_evidence = get_prop_bool(env, ro, "includeEvidence", false);
	req.dedupe = get_prop_bool(env, ro, "dedupe", true);
	req.json_output = get_prop_bool(env, ro, "jsonOutput", false);

	char *out = NULL;
	int rc = mm_recall(ctx, &req, &out, NULL);
	free(query_s);
	free(diag_s);
	free(guard_s);

	if (rc != 0 || !out) {
		free(out);
		napi_throw_error(env, NULL, "mm_recall failed");
		return NULL;
	}
	napi_value res;
	napi_create_string_utf8(env, out, NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiReview(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	napi_value ro = args[1];
	mm_review_req_t req;
	memset(&req, 0, sizeof(req));

	char tier_s[32] = {0}, act_s[32] = {0}, lc_s[32] = {0}, dir_s[32] = {0};
	get_prop_str(env, ro, "tier", tier_s, sizeof(tier_s));
	get_prop_str(env, ro, "action", act_s, sizeof(act_s));
	get_prop_str(env, ro, "lifecycle", lc_s, sizeof(lc_s));
	get_prop_str(env, ro, "direction", dir_s, sizeof(dir_s));
	char *reason_s = get_prop_str_heap(env, ro, "reason");

	req.tier = parse_tier_str(tier_s, MM_TIER_AUTO);
	req.ref = (uint32_t)get_prop_int(env, ro, "ref", 0);
	req.action = act_s;
	if (reason_s && reason_s[0]) req.reason = reason_s;
	req.superseded_by_ref = (uint32_t)get_prop_int(env, ro, "supersededBy", 0);
	if (lc_s[0]) req.target_lifecycle = mm_status_from_str(lc_s);
	req.evidence_ref = (uint32_t)get_prop_int(env, ro, "evidenceRef", 0);
	if (dir_s[0]) req.direction = dir_s;
	req.depth = get_prop_int(env, ro, "depth", 4);

	char *out = NULL;
	int rc = mm_review_ex(ctx, &req, &out, NULL);
	free(reason_s);

	if (rc != 0) {
		napi_throw_error(env, NULL, out ? out : "mm_review_ex failed");
		free(out);
		return NULL;
	}
	napi_value res;
	napi_create_string_utf8(env, out ? out : "OK", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiOverview(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	char tier_s[32] = "both";
	if (argc >= 2) get_str_arg(env, args[1], tier_s, sizeof(tier_s));
	int32_t max_per = 10;
	if (argc >= 3) napi_get_value_int32(env, args[2], &max_per);

	char *out = NULL;
	if (mm_build_memory_overview(ctx, parse_tier_str(tier_s, MM_TIER_BOTH), max_per, &out, NULL) != 0 || !out) {
		free(out);
		napi_throw_error(env, NULL, "mm_build_memory_overview failed");
		return NULL;
	}
	napi_value res;
	napi_create_string_utf8(env, out, NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiEvaluateGuardrail(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	char *intent = get_str_heap(env, args[1]);
	char *fpath = (argc >= 3) ? get_str_heap(env, args[2]) : NULL;
	char *sym = (argc >= 4) ? get_str_heap(env, args[3]) : NULL;

	char *out = NULL;
	mm_evaluate_guardrail(ctx, intent ? intent : "", fpath, sym, &out, NULL);
	free(intent);
	free(fpath);
	free(sym);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiObserveVerification(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 3) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	char *cmd = get_str_heap(env, args[1]);
	int32_t exit_code = 0;
	napi_get_value_int32(env, args[2], &exit_code);
	char *snip = (argc >= 4) ? get_str_heap(env, args[3]) : NULL;

	uint32_t promoted = 0;
	mm_observe_verification_command(ctx, cmd ? cmd : "", exit_code, snip ? snip : "", &promoted);
	free(cmd);
	free(snip);

	napi_value res;
	napi_create_uint32(env, promoted, &res);
	return res;
}

static napi_value NapiRecordFileMutation(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	char *fp = get_str_heap(env, args[1]);
	int rc = mm_record_file_mutation(ctx, fp ? fp : "");
	free(fp);

	napi_value res;
	napi_get_boolean(env, rc == 0, &res);
	return res;
}

static napi_value NapiPrime(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	char *prompt = get_str_heap(env, args[1]);
	char *out = NULL;
	mm_prime(ctx, prompt ? prompt : "", &out, NULL);
	free(prompt);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiConsolidate(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_ctx_t *ctx = unwrap_ctx(env, args[0]);
	if (!ctx) return NULL;

	char tier_s[32] = "project";
	if (argc >= 2) get_str_arg(env, args[1], tier_s, sizeof(tier_s));
	int l2 = 0, l3 = 0;
	mm_consolidate_scopes_and_principles(ctx, parse_tier_str(tier_s, MM_TIER_PROJECT), &l2, &l3);

	napi_value obj, v_l2, v_l3;
	napi_create_object(env, &obj);
	napi_create_int32(env, l2, &v_l2);
	napi_create_int32(env, l3, &v_l3);
	napi_set_named_property(env, obj, "l2Created", v_l2);
	napi_set_named_property(env, obj, "l3Created", v_l3);
	return obj;
}

/* ============================================================================
 * 3. Standalone Code Index (`mm_code_index_t`) Wrapper
 * ============================================================================ */

typedef struct {
	mm_code_index_t idx;
	bool            is_open;
} MmCodeIndexWrapper;

static void MmCodeIndexFinalizer(napi_env env, void *data, void *hint) {
	(void)env;
	(void)hint;
	MmCodeIndexWrapper *w = (MmCodeIndexWrapper *)data;
	if (w) {
		if (w->is_open) {
			mm_code_index_free(&w->idx);
			w->is_open = false;
		}
		free(w);
	}
}

static mm_code_index_t *unwrap_code_idx(napi_env env, napi_value val) {
	MmCodeIndexWrapper *w = NULL;
	if (napi_unwrap(env, val, (void **)&w) != napi_ok || !w || !w->is_open) {
		napi_throw_error(env, NULL, "Invalid or closed MmCodeIndex");
		return NULL;
	}
	return &w->idx;
}

static napi_value NapiOpenCodeIndex(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char root_dir[512] = ".";
	if (argc >= 1) get_str_arg(env, args[0], root_dir, sizeof(root_dir));

	MmCodeIndexWrapper *w = (MmCodeIndexWrapper *)calloc(1, sizeof(MmCodeIndexWrapper));
	mm_code_index_init(&w->idx, root_dir);
	mm_code_index_dir(&w->idx, root_dir);
	mm_code_resolve_call_graph(&w->idx);
	w->is_open = true;

	napi_value obj, v_nf, v_ns;
	napi_create_object(env, &obj);
	napi_create_uint32(env, (uint32_t)w->idx.n_files, &v_nf);
	napi_create_uint32(env, (uint32_t)w->idx.n_symbols, &v_ns);
	napi_set_named_property(env, obj, "fileCount", v_nf);
	napi_set_named_property(env, obj, "symbolCount", v_ns);
	napi_wrap(env, obj, w, MmCodeIndexFinalizer, NULL, NULL);
	return obj;
}

static napi_value NapiCodeSearch(napi_env env, napi_callback_info info) {
	size_t argc = 5;
	napi_value args[5];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *q = get_str_heap(env, args[1]);
	char *ff = (argc >= 3) ? get_str_heap(env, args[2]) : NULL;
	char *kf = (argc >= 4) ? get_str_heap(env, args[3]) : NULL;
	int32_t limit = 10;
	if (argc >= 5) napi_get_value_int32(env, args[4], &limit);

	char *out = NULL;
	mm_code_search(idx, q ? q : "", (ff && ff[0]) ? ff : NULL, (kf && kf[0]) ? kf : NULL, limit, &out, NULL);
	free(q);
	free(ff);
	free(kf);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeReadSymbols(napi_env env, napi_callback_info info) {
	size_t argc = 7;
	napi_value args[7];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	const char *syms[32];
	char *heap_syms[32];
	size_t n_syms = 0;

	bool is_arr = false;
	napi_is_array(env, args[1], &is_arr);
	if (is_arr) {
		uint32_t alen = 0;
		napi_get_array_length(env, args[1], &alen);
		for (uint32_t i = 0; i < alen && n_syms < 32; i++) {
			napi_value el;
			napi_get_element(env, args[1], i, &el);
			char *s = get_str_heap(env, el);
			if (s && s[0]) {
				heap_syms[n_syms] = s;
				syms[n_syms] = s;
				n_syms++;
			} else {
				free(s);
			}
		}
	} else {
		char *s = get_str_heap(env, args[1]);
		if (s && s[0]) {
			heap_syms[0] = s;
			syms[0] = s;
			n_syms = 1;
		} else {
			free(s);
		}
	}

	char *psym = (argc >= 3) ? get_str_heap(env, args[2]) : NULL;
	char *ff = (argc >= 4) ? get_str_heap(env, args[3]) : NULL;
	char *neigh = (argc >= 5) ? get_str_heap(env, args[4]) : NULL;
	int32_t ls = 0, le = 0;
	if (argc >= 6) napi_get_value_int32(env, args[5], &ls);
	if (argc >= 7) napi_get_value_int32(env, args[6], &le);

	char *out = NULL;
	mm_code_read_symbols_ex(idx, syms, n_syms,
	                        (psym && psym[0]) ? psym : NULL,
	                        (ff && ff[0]) ? ff : NULL,
	                        (neigh && neigh[0]) ? neigh : "both",
	                        ls, le, &out, NULL);

	for (size_t i = 0; i < n_syms; i++) free(heap_syms[i]);
	free(psym);
	free(ff);
	free(neigh);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeTrace(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *sym = get_str_heap(env, args[1]);
	char *dir = (argc >= 3) ? get_str_heap(env, args[2]) : NULL;
	int32_t depth = 3;
	if (argc >= 4) napi_get_value_int32(env, args[3], &depth);

	char *out = NULL;
	mm_code_trace(idx, sym ? sym : "", (dir && dir[0]) ? dir : "both", depth, &out, NULL);
	free(sym);
	free(dir);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeOverview(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *out = NULL;
	mm_code_overview(idx, &out, NULL);
	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeDiffImpact(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	int32_t depth = 2;
	if (argc >= 2) napi_get_value_int32(env, args[1], &depth);
	char *out = NULL;
	mm_code_diff_impact(idx, depth, &out, NULL);
	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeDiagnose(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *trace_text = get_str_heap(env, args[1]);
	int32_t depth = 2;
	if (argc >= 3) napi_get_value_int32(env, args[2], &depth);
	char *out = NULL;
	mm_code_diagnose(idx, trace_text ? trace_text : "", depth, &out, NULL);
	free(trace_text);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeDetectXyMismatches(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *out = NULL;
	mm_code_detect_xy_mismatches(idx, &out, NULL);
	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeResolveCrossBoundary(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 1) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *sym = (argc >= 2) ? get_str_heap(env, args[1]) : NULL;
	char *fp = (argc >= 3) ? get_str_heap(env, args[2]) : NULL;
	char *out = NULL;
	mm_code_resolve_cross_boundary_pairs(idx,
	                                     (sym && sym[0]) ? sym : NULL,
	                                     (fp && fp[0]) ? fp : NULL,
	                                     &out, NULL);
	free(sym);
	free(fp);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

static napi_value NapiCodeFirstTouchDossier(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	if (argc < 2) return NULL;
	mm_code_index_t *idx = unwrap_code_idx(env, args[0]);
	if (!idx) return NULL;

	char *fos = get_str_heap(env, args[1]);
	char *out = NULL;
	mm_code_first_touch_dossier(idx, fos ? fos : "", &out, NULL);
	free(fos);

	napi_value res;
	napi_create_string_utf8(env, out ? out : "", NAPI_AUTO_LENGTH, &res);
	free(out);
	return res;
}

/* ============================================================================
 * Module Registration
 * ============================================================================ */

#define EXPORT_FN(name, fn) \
	do { \
		napi_value v; \
		napi_create_function(env, name, NAPI_AUTO_LENGTH, fn, NULL, &v); \
		napi_set_named_property(env, exports, name, v); \
	} while (0)

static void napi_copy_str(napi_env env, napi_value v, char *out, size_t cap) {
	napi_valuetype vt;
	if (napi_typeof(env, v, &vt) == napi_ok && vt == napi_string) {
		size_t n = 0;
		napi_get_value_string_utf8(env, v, out, cap, &n);
	}
}

static napi_value NapiProjectCodeSearch(napi_env env, napi_callback_info info) {
	size_t argc = 5;
	napi_value args[5];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char proj[128] = {0}, query[512] = {0}, file_f[256] = {0}, kind_f[64] = {0};
	int32_t limit = 8;
	if (argc >= 1) napi_copy_str(env, args[0], proj, sizeof(proj));
	if (argc >= 2) napi_copy_str(env, args[1], query, sizeof(query));
	if (argc >= 3) napi_copy_str(env, args[2], file_f, sizeof(file_f));
	if (argc >= 4) napi_copy_str(env, args[3], kind_f, sizeof(kind_f));
	if (argc >= 5) {
		napi_valuetype vt;
		if (napi_typeof(env, args[4], &vt) == napi_ok && vt == napi_number) {
			napi_get_value_int32(env, args[4], &limit);
		}
	}
	char *buf = NULL;
	size_t len = 0;
	int rc = mm_project_code_search(proj[0] ? proj : NULL, query,
	                                file_f[0] ? file_f : NULL,
	                                kind_f[0] ? kind_f : NULL,
	                                limit, &buf, &len);
	napi_value res;
	if (rc == 0 && buf) {
		napi_create_string_utf8(env, buf, len, &res);
		free(buf);
	} else {
		napi_create_string_utf8(env, "", 0, &res);
	}
	return res;
}

static napi_value NapiProjectCodeSymbol(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char proj[128] = {0}, sym[256] = {0}, file_f[256] = {0};
	if (argc >= 1) napi_copy_str(env, args[0], proj, sizeof(proj));
	if (argc >= 2) napi_copy_str(env, args[1], sym, sizeof(sym));
	if (argc >= 3) napi_copy_str(env, args[2], file_f, sizeof(file_f));
	char *buf = NULL;
	size_t len = 0;
	int rc = mm_project_code_symbol(proj[0] ? proj : NULL, sym,
	                                file_f[0] ? file_f : NULL, &buf, &len);
	napi_value res;
	if (rc == 0 && buf) {
		napi_create_string_utf8(env, buf, len, &res);
		free(buf);
	} else {
		napi_create_string_utf8(env, "", 0, &res);
	}
	return res;
}

static napi_value NapiProjectCodeTrace(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char proj[128] = {0}, sym[256] = {0}, dir[32] = "both";
	int32_t depth = 2;
	if (argc >= 1) napi_copy_str(env, args[0], proj, sizeof(proj));
	if (argc >= 2) napi_copy_str(env, args[1], sym, sizeof(sym));
	if (argc >= 3) napi_copy_str(env, args[2], dir, sizeof(dir));
	if (argc >= 4) {
		napi_valuetype vt;
		if (napi_typeof(env, args[3], &vt) == napi_ok && vt == napi_number) {
			napi_get_value_int32(env, args[3], &depth);
		}
	}
	char *buf = NULL;
	size_t len = 0;
	int rc = mm_project_code_trace(proj[0] ? proj : NULL, sym,
	                               dir[0] ? dir : "both", depth, &buf, &len);
	napi_value res;
	if (rc == 0 && buf) {
		napi_create_string_utf8(env, buf, len, &res);
		free(buf);
	} else {
		napi_create_string_utf8(env, "", 0, &res);
	}
	return res;
}

static napi_value Init(napi_env env, napi_value exports) {
	/* Stateless algorithmic functions */
	EXPORT_FN("adaptiveRrfK", NapiAdaptiveRrfK);
	EXPORT_FN("isTechnicalInquiry", NapiIsTechnicalInquiry);
	EXPORT_FN("isCleanSummaryText", NapiIsCleanSummaryText);
	EXPORT_FN("isCodeOrDocArtifact", NapiIsCodeOrDocArtifact);
	EXPORT_FN("isQuestionMemory", NapiIsQuestionMemory);
	EXPORT_FN("isAssistantEcho", NapiIsAssistantEcho);
	EXPORT_FN("fuseRankingsRRF", NapiFuseRankingsRRF);
	EXPORT_FN("computeRankingScore", NapiComputeRankingScore);
	EXPORT_FN("filterAndEnrich", NapiFilterAndEnrich);
	EXPORT_FN("payloadEncode", NapiPayloadEncode);
	EXPORT_FN("payloadDecode", NapiPayloadDecode);
	EXPORT_FN("windowForLevel", NapiWindowForLevel);
	EXPORT_FN("classifyPrompt", NapiClassifyPrompt);
	EXPORT_FN("scanSource", NapiScanSource);

	/* Stateful mm_ctx_t functions */
	EXPORT_FN("openContext", NapiOpenContext);
	EXPORT_FN("closeContext", NapiCloseContext);
	EXPORT_FN("getContextInfo", NapiGetContextInfo);
	EXPORT_FN("record", NapiRecord);
	EXPORT_FN("recall", NapiRecall);
	EXPORT_FN("review", NapiReview);
	EXPORT_FN("overview", NapiOverview);
	EXPORT_FN("evaluateGuardrail", NapiEvaluateGuardrail);
	EXPORT_FN("observeVerification", NapiObserveVerification);
	EXPORT_FN("recordFileMutation", NapiRecordFileMutation);
	EXPORT_FN("prime", NapiPrime);
	EXPORT_FN("consolidate", NapiConsolidate);

	/* Standalone mm_code_index_t functions */
	EXPORT_FN("openCodeIndex", NapiOpenCodeIndex);
	EXPORT_FN("codeSearch", NapiCodeSearch);
	EXPORT_FN("codeReadSymbols", NapiCodeReadSymbols);
	EXPORT_FN("codeTrace", NapiCodeTrace);
	EXPORT_FN("codeOverview", NapiCodeOverview);
	EXPORT_FN("codeDiffImpact", NapiCodeDiffImpact);
	EXPORT_FN("codeDiagnose", NapiCodeDiagnose);
	EXPORT_FN("codeDetectXyMismatches", NapiCodeDetectXyMismatches);
	EXPORT_FN("codeResolveCrossBoundary", NapiCodeResolveCrossBoundary);
	EXPORT_FN("codeFirstTouchDossier", NapiCodeFirstTouchDossier);

	/* Multi-project lazy discovery & code query */
	EXPORT_FN("projectCodeSearch", NapiProjectCodeSearch);
	EXPORT_FN("projectCodeSymbol", NapiProjectCodeSymbol);
	EXPORT_FN("projectCodeTrace", NapiProjectCodeTrace);

	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
