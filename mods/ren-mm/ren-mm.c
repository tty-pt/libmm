#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include "ttypt/libren.h"
#include "ttypt/mm.h"
#include <ttypt/xy.h>

static mm_ctx_t *g_mm_ctx = NULL;

static int do_recall(const char *args, char **out_result) {
    if (!g_mm_ctx) {
        *out_result = strdup("Error: mm context not initialized");
        return -1;
    }
    if (!args) args = "";

    /* Check for "projects" listing */
    if (strcmp(args, "projects") == 0) {
        mm_project_info_t *list = NULL;
        size_t count = 0;
        if (mm_project_list(&list, &count) != 0 || count == 0) {
            free(list);
            *out_result = strdup("No projects registered yet.");
            return 0;
        }
        size_t cap = count * 640 + 64;
        char *buf = malloc(cap);
        size_t off = 0;
        for (size_t i = 0; i < count; i++) {
            int n = snprintf(buf + off, cap - off, "%s: %s\n", list[i].name, list[i].root_path);
            if (n > 0) off += (size_t)n;
        }
        free(list);
        *out_result = buf;
        return 0;
    }

    char *json_proj = NULL;
    char *json_query = NULL;
    struct json_object *obj = json_tokener_parse(args);
    if (obj && json_object_is_type(obj, json_type_object)) {
        struct json_object *p = json_object_object_get(obj, "project");
        if (p && json_object_is_type(p, json_type_string)) {
            json_proj = strdup(json_object_get_string(p));
        }
        struct json_object *q = json_object_object_get(obj, "query");
        if (!q) q = json_object_object_get(obj, "args");
        if (q && json_object_is_type(q, json_type_string)) {
            json_query = strdup(json_object_get_string(q));
        }
        json_object_put(obj);
    } else if (obj) {
        json_object_put(obj);
    }

    /* Optional "project:<name> " prefix */
    char proj_buf[128] = {0};
    const char *proj = json_proj;
    const char *rest = json_query ? json_query : args;
    if (!proj && strncmp(rest, "project:", 8) == 0) {
        rest += 8;
        const char *sp = strchr(rest, ' ');
        if (sp) {
            size_t len = (size_t)(sp - rest);
            if (len >= sizeof(proj_buf)) len = sizeof(proj_buf) - 1;
            memcpy(proj_buf, rest, len);
            proj_buf[len] = '\0';
            proj = proj_buf;
            rest = sp + 1;
            while (*rest == ' ') rest++;
        }
    }

    char *buf = NULL;
    size_t len = 0;
    int rc = -1;

    if (strncmp(rest, "code:", 5) == 0) {
        rc = mm_project_code_search(proj, rest + 5, NULL, NULL, 8, &buf, &len);
        *out_result = (rc == 0 && buf) ? buf : strdup("No code matches found.");
        return 0;
    }
    if (strncmp(rest, "symbol:", 7) == 0) {
        rc = mm_project_code_symbol(proj, rest + 7, NULL, &buf, &len);
        *out_result = (rc == 0 && buf) ? buf : strdup("Symbol not found.");
        return 0;
    }
    if (strncmp(rest, "trace:", 6) == 0) {
        rc = mm_project_code_trace(proj, rest + 6, "both", 2, &buf, &len);
        *out_result = (rc == 0 && buf) ? buf : strdup("No call graph edges found.");
        return 0;
    }
    if (strcmp(rest, "diff") == 0) {
        mm_project_info_t pinfo;
        const char *target = proj ? proj : ".";
        if (mm_project_resolve(target, &pinfo) == 0) {
            mm_code_index_t idx;
            if (mm_project_open_index(&pinfo, &idx) == 0) {
                rc = mm_code_diff_impact(&idx, 2, &buf, &len);
                mm_code_index_free(&idx);
            }
        }
        *out_result = (rc == 0 && buf) ? buf : strdup("No git diff impact found.");
        return 0;
    }
    if (strncmp(rest, "guardrail:", 10) == 0) {
        rc = mm_evaluate_guardrail(g_mm_ctx, rest + 10, NULL, NULL, &buf, &len);
        *out_result = (rc == 0 && buf) ? buf : strdup("No guardrail warnings.");
        return 0;
    }
    if (strncmp(rest, "session:", 8) == 0) {
        rc = mm_session_recall(g_mm_ctx, MM_TIER_BOTH, NULL, rest + 8, 0, 0, 0, 5, &buf, &len);
        *out_result = (rc == 0 && buf) ? buf : strdup("No session history found.");
        return 0;
    }

    /* Check for explicit federated search "project:all <query>" */
    if (proj && strcmp(proj, "all") == 0) {
        rc = mm_project_recall_all(rest, 5, &buf, &len);
        *out_result = (rc == 0 && buf) ? buf : strdup("No memories found across projects.");
        return 0;
    }

    mm_recall_req_t req;
    memset(&req, 0, sizeof(req));
    req.scope = MM_SCOPE_PERSISTENT;
    req.query = rest;
    req.tier = MM_TIER_BOTH;
    req.limit = 5;

    /* If explicit project was requested and it's not the current one, resolve it */
    mm_ctx_t *query_ctx = g_mm_ctx;
    mm_ctx_t *custom_ctx = NULL;
    if (proj && *proj) {
        mm_project_info_t pinfo;
        if (mm_project_resolve(proj, &pinfo) == 0) {
            mm_options_t popts;
            memset(&popts, 0, sizeof(popts));
            popts.cwd = pinfo.root_path;
            popts.semantic_search = false;
            custom_ctx = mm_open(&popts);
            if (custom_ctx) query_ctx = custom_ctx;
        }
    }

    rc = mm_recall(query_ctx, &req, &buf, &len);
    if (custom_ctx) mm_close(custom_ctx);

    /* Ambient Fallback: If no explicit project was requested and local search yields 0 matches,
     * automatically federate across all registered projects in projects.tsv */
    if ((!proj || !*proj) && (rc != 0 || !buf || strstr(buf, "(0 matches)") != NULL)) {
        free(buf);
        buf = NULL;
        char *fed_buf = NULL;
        size_t fed_len = 0;
        if (mm_project_recall_all(rest, 5, &fed_buf, &fed_len) == 0 && fed_buf) {
            if (strstr(fed_buf, "(0 matches)") == NULL) {
                *out_result = fed_buf;
                return 0;
            }
            free(fed_buf);
        }
        *out_result = strdup("No memories found.");
        return 0;
    }

    if (rc != 0 || !buf) {
        free(json_proj);
        free(json_query);
        *out_result = strdup("No memories found.");
        return 0;
    }
    free(json_proj);
    free(json_query);
    *out_result = buf;
    return 0;
}

static int do_record(const char *args, char **out_result) {
    if (!g_mm_ctx) {
        *out_result = strdup("Error: mm context not initialized");
        return -1;
    }
    /* Check if input is a zero-JSON line-delimited distillation block */
    if (strncmp(args, "PROBLEM:", 8) == 0 || strstr(args, "\nPROBLEM:") != NULL) {
        mm_tier_t tier = MM_TIER_PROJECT;
        uint32_t ref = 0;
        int rc = mm_distill_commit(g_mm_ctx, args, &tier, &ref);
        if (rc == 0 && ref > 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Distilled & recorded as [%s Ref %u]",
                     tier == MM_TIER_GLOBAL ? "Global" : "Project", ref);
            *out_result = strdup(msg);
            return 0;
        }
    }

    /* args: problem::findings or JSON {problem, findings} */
    char *json_problem = NULL;
    char *json_findings = NULL;
    struct json_object *obj = json_tokener_parse(args);
    if (obj && json_object_is_type(obj, json_type_object)) {
        struct json_object *p = json_object_object_get(obj, "problem");
        if (p && json_object_is_type(p, json_type_string)) {
            json_problem = strdup(json_object_get_string(p));
        }
        struct json_object *f = json_object_object_get(obj, "findings");
        if (!f) f = json_object_object_get(obj, "finding");
        if (f && json_object_is_type(f, json_type_string)) {
            json_findings = strdup(json_object_get_string(f));
        }
        json_object_put(obj);
    } else if (obj) {
        json_object_put(obj);
    }

    char *copy = json_problem ? NULL : strdup(args);
    const char *problem = json_problem ? json_problem : copy;
    const char *findings = json_findings;
    if (!json_problem && copy) {
        char *sep = strstr(copy, "::");
        if (sep) {
            *sep = '\0';
            findings = sep + 2;
        }
    }

    mm_record_req_t req;
    memset(&req, 0, sizeof(req));
    req.problem = problem;
    req.type = "episode";
    req.level = MM_LEVEL_L1;
    const char *f_arr[1];
    if (findings) {
        f_arr[0] = findings;
        req.findings = f_arr;
        req.n_findings = 1;
    }

    mm_tier_t tier;
    uint32_t ref;
    int rc = mm_record(g_mm_ctx, &req, &tier, &ref);
    free(copy);
    free(json_problem);
    free(json_findings);
    if (rc != 0) {
        *out_result = strdup("Error: record failed");
        return -1;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "Recorded as ref %u", ref);
    *out_result = strdup(msg);
    return 0;
}

static const char RECALL_SCHEMA[] =
    "{\n"
    "  \"type\": \"object\",\n"
    "  \"properties\": {\n"
    "    \"query\": {\"type\": \"string\", \"description\": \"Search query or mode (e.g. query, code:query, symbol:name, trace:name, diff, guardrail:intent, session:query, projects)\"},\n"
    "    \"project\": {\"type\": \"string\", \"description\": \"Optional project name filter or 'all'\"}\n"
    "  },\n"
    "  \"required\": [\"query\"]\n"
    "}";

static const char RECORD_SCHEMA[] =
    "{\n"
    "  \"type\": \"object\",\n"
    "  \"properties\": {\n"
    "    \"problem\": {\"type\": \"string\", \"description\": \"Problem description, standing rule, or task\"},\n"
    "    \"findings\": {\"type\": \"string\", \"description\": \"Findings, observations, or solution details\"}\n"
    "  },\n"
    "  \"required\": [\"problem\"]\n"
    "}";

XY_MODULE_API void xy_install(void) {
    mm_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.cwd = ".";
    opts.semantic_search = false;
    g_mm_ctx = mm_open(&opts);

    ren_register_tool_with_schema("recall", "recall [project:<name>] <query|code:query|symbol:name|trace:name|diff|guardrail:intent|session:query|projects> (or JSON {query, project})", RECALL_SCHEMA, do_recall);
    ren_register_tool_with_schema("record", "record <problem>::<findings> | <line-delimited distillation block> (or JSON {problem, findings})", RECORD_SCHEMA, do_record);
    ren_register_prompt_hint(
        "Memory & code tools are available: use 'recall <query>' for memories, "
        "'recall [project:<name>] code:<query>' to search code, "
        "'recall [project:<name>] symbol:<name>' to read a symbol, "
        "'recall [project:<name>] trace:<name>' for call graphs, "
        "'recall [project:<name>] diff' for git diff blast radius, "
        "'recall guardrail:<intent>' for pre-flight checks, "
        "'recall session:<query>' for past conversations, "
        "'recall projects' to list projects, and 'record <problem>::<findings>' "
        "(or 'record PROBLEM: ...\\nAPPROACH: ...\\nFINDING: ...') to distill and save facts.");
}
