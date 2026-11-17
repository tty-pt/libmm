#include <ttypt/mm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage:\n"
		"  %s recall [options] [query]\n"
		"      --scope <all|code|session|persistent>\n"
		"      --tier <auto|global|project|both>\n"
		"      --symbol <name>         Read exact symbol definition & callers/callees\n"
		"      --parent-symbol <name>  Disambiguate class method parent\n"
		"      --file <substr>         Filter code search by file path\n"
		"      --kind <Kind>           Filter code search by symbol kind\n"
		"      --trace <name>          Trace call graph for symbol\n"
		"      --direction <in|out|both>\n"
		"      --depth <N>             Call-graph or diff depth (default 2)\n"
		"      --diff                  Analyze git diff blast radius\n"
		"      --overview              Show codebase or lore overview\n"
		"      --diagnose <text>       Localize fault from error/stack trace\n"
		"      --guardrail <text>      Evaluate counterfactual pre-flight guardrail\n"
		"      --zoom-ref <N>          Zoom into persistent memory ref N\n"
		"      --zoom-turn <N>         Zoom into session turn N\n"
		"      --tool-index <N>        Select specific tool call index inside turn\n"
		"      --lineage <active|all>  Session lineage branch filter\n"
		"      --session <id>          Filter session ID\n"
		"      --limit <N>             Max results (default 10)\n"
		"      --include-l0            Include raw L0 records\n"
		"      --json                  Output JSON\n\n"
		"  %s record [options] <problem/rule summary>\n"
		"      --standing              Record as global standing operator rule (Tier 1)\n"
		"      --anchor                Mark as flashbulb salience anchor (1.25x)\n"
		"      --tier <auto|global|project>\n"
		"      --type <note|episode|decision|rule>\n"
		"      --status <supported|tested|observed|hypothesized>\n"
		"      --level <L0|L1|L2|L3>\n"
		"      --project <name>        Override project tag\n"
		"      --subsystem <name>      Set subsystem tag\n\n"
		"  %s review --ref <N> --action <inspect|update_lifecycle|confirm|contradict|supersede|helpful|harmful|obsolete> [options]\n"
		"  %s forget --ref <N> [--tier <global|project>]\n"
		"  %s prime [prompt]\n"
		"  %s session ingest --session <id> --turn <N> --role <user|assistant> <text>\n"
		"  %s session compact [--session <id>]\n"
		"  %s consolidate [--tier <global|project>] [--batch <N>]\n"
		"  %s status\n"
		"  %s mcp\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static mm_tier_t parse_tier_arg(const char *s) {
	if (!s || !*s) return MM_TIER_AUTO;
	if (!strcmp(s, "global") || !strcmp(s, "G") || !strcmp(s, "1")) return MM_TIER_GLOBAL;
	if (!strcmp(s, "project") || !strcmp(s, "P") || !strcmp(s, "2")) return MM_TIER_PROJECT;
	if (!strcmp(s, "both")) return MM_TIER_BOTH;
	return MM_TIER_AUTO;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[1];
	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
		usage(argv[0]);
		return 0;
	}

	mm_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.prefer_daemon = true;

	mm_ctx_t *ctx = mm_open(&opts);
	if (!ctx) {
		fprintf(stderr, "mm: failed to initialize context\n");
		return 1;
	}

	int exit_code = 0;

	if (!strcmp(cmd, "mcp") || !strcmp(cmd, "--mcp")) {
		exit_code = mm_mcp_serve_stdio(ctx);
	} else if (!strcmp(cmd, "status")) {
		printf("Global Tier (Tier 1):  %s\n", mm_get_tier_dir(ctx, MM_TIER_GLOBAL));
		printf("Project Tier (Tier 2): %s\n", mm_has_project_tier(ctx) ? mm_get_tier_dir(ctx, MM_TIER_PROJECT) : "(none)");
		printf("Project Root:          %s\n", mm_get_project_root(ctx));
		printf("Project Name:          %s\n", mm_get_project_name(ctx));
	} else if (!strcmp(cmd, "prime")) {
		const char *prompt = (argc >= 3) ? argv[2] : "";
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--prompt") && i + 1 < argc) {
				prompt = argv[i + 1];
				break;
			}
		}
		char *out = NULL;
		if (mm_prime(ctx, prompt, &out, NULL) == 0 && out) {
			fputs(out, stdout);
			free(out);
		} else {
			exit_code = 1;
		}
	} else if (!strcmp(cmd, "recall")) {
		mm_recall_req_t req;
		memset(&req, 0, sizeof(req));
		req.scope      = MM_SCOPE_ALL;
		req.tier       = MM_TIER_BOTH;
		req.limit      = 10;
		req.depth      = 2;
		req.tool_index = 0;

		for (int i = 2; i < argc; i++) {
			const char *a = argv[i];
			if (!strcmp(a, "--scope") && i + 1 < argc) {
				const char *v = argv[++i];
				if (!strcmp(v, "code")) req.scope = MM_SCOPE_CODE;
				else if (!strcmp(v, "session")) req.scope = MM_SCOPE_SESSION;
				else if (!strcmp(v, "persistent")) req.scope = MM_SCOPE_PERSISTENT;
				else req.scope = MM_SCOPE_ALL;
			} else if (!strcmp(a, "--tier") && i + 1 < argc) {
				req.tier = parse_tier_arg(argv[++i]);
			} else if (!strcmp(a, "--symbol") && i + 1 < argc) {
				req.symbol = argv[++i];
				req.scope  = MM_SCOPE_CODE;
			} else if (!strcmp(a, "--parent-symbol") && i + 1 < argc) {
				req.parent_symbol = argv[++i];
			} else if (!strcmp(a, "--file") && i + 1 < argc) {
				req.file = argv[++i];
			} else if (!strcmp(a, "--kind") && i + 1 < argc) {
				req.kind = argv[++i];
			} else if (!strcmp(a, "--neighbors") && i + 1 < argc) {
				req.neighbors = argv[++i];
			} else if (!strcmp(a, "--trace") && i + 1 < argc) {
				req.trace = argv[++i];
				req.scope = MM_SCOPE_CODE;
			} else if (!strcmp(a, "--direction") && i + 1 < argc) {
				req.direction = argv[++i];
			} else if (!strcmp(a, "--depth") && i + 1 < argc) {
				req.depth = atoi(argv[++i]);
			} else if (!strcmp(a, "--diff")) {
				req.diff  = true;
				req.scope = MM_SCOPE_CODE;
			} else if (!strcmp(a, "--overview")) {
				req.overview = true;
			} else if (!strcmp(a, "--diagnose") && i + 1 < argc) {
				req.diagnose = argv[++i];
				req.scope    = MM_SCOPE_CODE;
			} else if (!strcmp(a, "--guardrail") && i + 1 < argc) {
				req.guardrail = argv[++i];
			} else if (!strcmp(a, "--zoom-ref") && i + 1 < argc) {
				req.zoom_ref = (uint32_t)strtoul(argv[++i], NULL, 10);
			} else if (!strcmp(a, "--zoom-turn") && i + 1 < argc) {
				req.zoom_turn = atoi(argv[++i]);
				req.scope     = MM_SCOPE_SESSION;
			} else if (!strcmp(a, "--tool-index") && i + 1 < argc) {
				req.tool_index = atoi(argv[++i]);
			} else if (!strcmp(a, "--lineage") && i + 1 < argc) {
				req.lineage = argv[++i];
			} else if (!strcmp(a, "--session") && i + 1 < argc) {
				req.session_id = argv[++i];
			} else if (!strcmp(a, "--limit") && i + 1 < argc) {
				req.limit = atoi(argv[++i]);
			} else if (!strcmp(a, "--include-l0")) {
				req.include_l0 = true;
			} else if (!strcmp(a, "--include-evidence")) {
				req.include_evidence = true;
			} else if (!strcmp(a, "--dedupe")) {
				req.dedupe = true;
			} else if (!strcmp(a, "--json")) {
				req.json_output = true;
			} else if (a[0] != '-') {
				req.query = a;
			}
		}

		char *out = NULL;
		if (mm_recall(ctx, &req, &out, NULL) == 0 && out) {
			fputs(out, stdout);
			if (out[0] && out[strlen(out) - 1] != '\n') fputc('\n', stdout);
			free(out);
		} else {
			exit_code = 1;
		}
	} else if (!strcmp(cmd, "record")) {
		mm_record_req_t req;
		memset(&req, 0, sizeof(req));
		req.tier   = MM_TIER_AUTO;
		req.level  = MM_LEVEL_NONE;
		req.status = MM_STATUS_SUPPORTED;

		for (int i = 2; i < argc; i++) {
			const char *a = argv[i];
			if (!strcmp(a, "--standing")) {
				req.standing = true;
				req.anchor   = true;
			} else if (!strcmp(a, "--anchor")) {
				req.anchor = true;
			} else if (!strcmp(a, "--tier") && i + 1 < argc) {
				req.tier = parse_tier_arg(argv[++i]);
			} else if (!strcmp(a, "--type") && i + 1 < argc) {
				req.type = argv[++i];
			} else if (!strcmp(a, "--status") && i + 1 < argc) {
				req.status = mm_status_from_str(argv[++i]);
			} else if (!strcmp(a, "--level") && i + 1 < argc) {
				req.level = mm_level_from_str(argv[++i]);
			} else if (!strcmp(a, "--project") && i + 1 < argc) {
				req.project = argv[++i];
			} else if (!strcmp(a, "--subsystem") && i + 1 < argc) {
				req.subsystem = argv[++i];
			} else if (!strcmp(a, "--supersedes") && i + 1 < argc) {
				req.supersedes_ref = (uint32_t)strtoul(argv[++i], NULL, 10);
			} else if (!strcmp(a, "--problem") && i + 1 < argc) {
				req.problem = argv[++i];
			} else if (a[0] != '-') {
				req.problem = a;
			}
		}

		if (!req.problem || !req.problem[0]) {
			fprintf(stderr, "mm record: missing problem/rule summary\n");
			exit_code = 1;
		} else {
			mm_tier_t out_tier = MM_TIER_PROJECT;
			uint32_t out_ref = 0;
			if (mm_record(ctx, &req, &out_tier, &out_ref) == 0) {
				printf("Recorded [Ref %s:%u]\n",
				       out_tier == MM_TIER_GLOBAL ? "G" : "P", out_ref);
			} else {
				fprintf(stderr, "mm record: failed to persist record\n");
				exit_code = 1;
			}
		}
	} else if (!strcmp(cmd, "review")) {
		mm_review_req_t req;
		memset(&req, 0, sizeof(req));
		req.tier = MM_TIER_AUTO;
		for (int i = 2; i < argc; i++) {
			const char *a = argv[i];
			if (!strcmp(a, "--ref") && i + 1 < argc) {
				req.ref = (uint32_t)strtoul(argv[++i], NULL, 10);
			} else if (!strcmp(a, "--action") && i + 1 < argc) {
				req.action = argv[++i];
			} else if (!strcmp(a, "--tier") && i + 1 < argc) {
				req.tier = parse_tier_arg(argv[++i]);
			} else if (!strcmp(a, "--superseded-by") && i + 1 < argc) {
				req.superseded_by_ref = (uint32_t)strtoul(argv[++i], NULL, 10);
			} else if (!strcmp(a, "--target-lifecycle") && i + 1 < argc) {
				req.target_lifecycle = mm_status_from_str(argv[++i]);
			} else if (!strcmp(a, "--reason") && i + 1 < argc) {
				req.reason = argv[++i];
			} else if (!strcmp(a, "--evidence-ref") && i + 1 < argc) {
				req.evidence_ref = (uint32_t)strtoul(argv[++i], NULL, 10);
			} else if (!strcmp(a, "--direction") && i + 1 < argc) {
				req.direction = argv[++i];
			} else if (!strcmp(a, "--depth") && i + 1 < argc) {
				req.depth = atoi(argv[++i]);
			}
		}
		char *out = NULL;
		if (mm_review_ex(ctx, &req, &out, NULL) == 0) {
			if (out && out[0]) {
				fputs(out, stdout);
				if (out[strlen(out) - 1] != '\n') fputc('\n', stdout);
			} else {
				printf("Reviewed ref %u (%s)\n", req.ref, req.action ? req.action : "");
			}
			free(out);
		} else {
			if (out) {
				fprintf(stderr, "%s\n", out);
				free(out);
			} else {
				fprintf(stderr, "mm review: failed\n");
			}
			exit_code = 1;
		}
	} else if (!strcmp(cmd, "forget")) {
		mm_tier_t tier = MM_TIER_AUTO;
		uint32_t ref = 0;
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--ref") && i + 1 < argc) ref = (uint32_t)strtoul(argv[++i], NULL, 10);
			else if (!strcmp(argv[i], "--tier") && i + 1 < argc) tier = parse_tier_arg(argv[++i]);
			else if (argv[i][0] != '-') ref = (uint32_t)strtoul(argv[i], NULL, 10);
		}
		if (tier == MM_TIER_AUTO) tier = mm_has_project_tier(ctx) ? MM_TIER_PROJECT : MM_TIER_GLOBAL;
		if (mm_forget(ctx, tier, ref) == 0) {
			printf("Forgot [Ref %s:%u]\n", tier == MM_TIER_GLOBAL ? "G" : "P", ref);
		} else {
			exit_code = 1;
		}
	} else if (!strcmp(cmd, "session")) {
		const char *sub = (argc >= 3) ? argv[2] : "";
		if (!strcmp(sub, "ingest")) {
			mm_tier_t tier = MM_TIER_AUTO;
			const char *sid = "default";
			const char *role = "user";
			const char *text = NULL;
			int turn = 1;
			for (int i = 3; i < argc; i++) {
				if (!strcmp(argv[i], "--tier") && i + 1 < argc) tier = parse_tier_arg(argv[++i]);
				else if (!strcmp(argv[i], "--session") && i + 1 < argc) sid = argv[++i];
				else if (!strcmp(argv[i], "--role") && i + 1 < argc) role = argv[++i];
				else if (!strcmp(argv[i], "--turn") && i + 1 < argc) turn = atoi(argv[++i]);
				else if (argv[i][0] != '-') text = argv[i];
			}
			if (!text || mm_session_ingest_turn(ctx, tier, sid, turn, role, text) != 0) {
				exit_code = 1;
			} else {
				printf("Ingested session %s turn %d\n", sid, turn);
			}
		} else if (!strcmp(sub, "compact")) {
			mm_tier_t tier = MM_TIER_AUTO;
			const char *sid = "default";
			for (int i = 3; i < argc; i++) {
				if (!strcmp(argv[i], "--tier") && i + 1 < argc) tier = parse_tier_arg(argv[++i]);
				else if (!strcmp(argv[i], "--session") && i + 1 < argc) sid = argv[++i];
			}
			char *summary = NULL;
			if (mm_session_compact(ctx, tier, sid, NULL, &summary, NULL) == 0) {
				if (summary) {
					printf("%s\n", summary);
					free(summary);
				}
			} else {
				exit_code = 1;
			}
		} else {
			usage(argv[0]);
			exit_code = 1;
		}
	} else if (!strcmp(cmd, "consolidate")) {
		mm_tier_t tier = MM_TIER_AUTO;
		int batch = 20;
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--tier") && i + 1 < argc) tier = parse_tier_arg(argv[++i]);
			else if (!strcmp(argv[i], "--batch") && i + 1 < argc) batch = atoi(argv[++i]);
		}
		int drained = mm_consolidate_drain(ctx, tier, batch);
		int l2_created = 0, l3_created = 0;
		mm_consolidate_scopes_and_principles(ctx, tier, &l2_created, &l3_created);
		printf("Consolidated %d L0 memory record(s) (L2: %d, L3: %d)\n",
		       drained >= 0 ? drained : 0, l2_created, l3_created);
	} else {
		usage(argv[0]);
		exit_code = 1;
	}

	mm_close(ctx);
	return exit_code;
}
