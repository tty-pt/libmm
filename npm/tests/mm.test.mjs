import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {
  isNativeAvailable,
  getMmWasm,
  wasmRecord,
  wasmRecallJson,
  wasmReview,
  adaptiveRrfK,
  isTechnicalInquiry,
  isCleanSummaryText,
  isCodeOrDocArtifact,
  isQuestionMemory,
  isAssistantEcho,
  fuseRankingsRRF,
  filterAndEnrich,
  payloadEncode,
  payloadDecode,
  windowForLevel,
  classifyPrompt,
  scanSource,
  MmContext,
  MmCodeIndex,
} from "../dist/index.js";

test("@tty-pt/mm native binding availability & stateless fusion/heuristics", () => {
  assert.equal(isNativeAvailable(), true, "mm_node.node should load cleanly");
  assert.equal(adaptiveRrfK(0), 10);
  assert.equal(adaptiveRrfK(8), 16);
  assert.equal(isTechnicalInquiry("mm_fusion.c"), true);
  assert.equal(isTechnicalInquiry("Why is the sky blue?"), false);
  assert.equal(isCleanSummaryText("Valid summary"), true);
  assert.equal(isQuestionMemory({ summary: "Do you remember the port?" }), true);
  assert.equal(isAssistantEcho({ summary: "You mentioned: port 8081" }), true);
  assert.equal(isCodeOrDocArtifact("#include <stdio.h>", { summary: "#include <stdio.h>" }), true);

  const fused = fuseRankingsRRF(
    [
      { ref: 1, score: "0.85", record: JSON.stringify({ level: "L2", lifecycle: "supported", type: "decision", summary: "Use libmm native N-API" }) },
      { ref: 2, score: "0.60", record: JSON.stringify({ level: "L2", lifecycle: "superseded", type: "note", summary: "Old note" }) },
    ],
    [
      { ref: 1, score: "0.91", record: JSON.stringify({ level: "L2", lifecycle: "supported", type: "decision", summary: "Use libmm native N-API" }) },
    ],
  );
  assert.equal(fused.length, 2);
  assert.equal(fused[0].ref, 1);
  assert.equal(fused[0].origin, "both");

  const ranked = filterAndEnrich(fused, { limit: 5, dedupe: true });
  assert.equal(ranked.length, 1);
  assert.equal(ranked[0].ref, 1);
  assert.equal(ranked[0].summary, "Use libmm native N-API");
});

test("@tty-pt/mm HTOK v4 payload encode/decode, windowForLevel, classifyPrompt & scanSource", () => {
  const encoded = payloadEncode({
    level: "L2",
    lifecycle: "supported",
    type: "decision",
    summary: "Binary HTOK v4 roundtrip via N-API",
    standing: true,
    isAnchor: true,
    tags: ["mm", "napi"],
  });
  assert.ok(encoded.bytes instanceof Uint8Array);
  assert.ok(encoded.bytes.byteLength > 10);

  const decoded = payloadDecode(encoded.bytes);
  assert.equal(decoded.level, "L2");
  assert.equal(decoded.summary, "Binary HTOK v4 roundtrip via N-API");
  assert.equal(decoded.standing, true);

  const win = windowForLevel("L1", 1775000000);
  assert.equal(win.bounded, true);
  assert.ok(win.since.length >= 8);

  assert.equal(classifyPrompt("Always prefer memory_recall over grep"), "directive");

  const syms = scanSource("sample.c", "int add_nums(int a, int b) {\n  return a + b;\n}\n");
  assert.equal(syms.length, 1);
  assert.equal(syms[0].name, "add_nums");
  assert.equal(syms[0].kind, "Function");
});

test("@tty-pt/mm MmContext two-tier record, recall, review & MmCodeIndex", () => {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mm_napi_test_"));
  const globalDir = path.join(tmpDir, "global");
  const projectDir = path.join(tmpDir, "project");
  const srcDir = path.join(tmpDir, "src");
  fs.mkdirSync(globalDir, { recursive: true });
  fs.mkdirSync(projectDir, { recursive: true });
  fs.mkdirSync(srcDir, { recursive: true });
  fs.writeFileSync(
    path.join(srcDir, "engine.c"),
    "int compute_flux(int x) {\n  return x * 42;\n}\nint run_engine(int v) {\n  return compute_flux(v);\n}\n",
  );

  const ctx = new MmContext({
    globalDir,
    projectDir,
    cwd: srcDir,
    semanticSearch: false,
    preferDaemon: false,
  });

  const info = ctx.info();
  assert.equal(info.hasProjectTier, true);
  assert.equal(info.projectDir, projectDir);

  ctx.recordFileMutation("src/engine.c");
  const verifFail = ctx.observeVerification("make test", 1, "engine.c:2: error");
  assert.equal(verifFail, 0);

  const cormCandidates = [
    process.env.MM_CORM_BIN,
    path.resolve(process.cwd(), "../../libcorm/bin/corm"),
    "/usr/local/bin/corm",
    "/opt/homebrew/bin/corm",
    "/usr/bin/corm",
  ].filter(Boolean);
  const hasCormCli = cormCandidates.some((p) => fs.existsSync(p));

  if (hasCormCli) {
    const r1 = ctx.record({
      problem: "Always run targeted Tier-0 tests first",
      type: "rule",
      standing: true,
      anchor: true,
    });
    assert.equal(r1.tier, "global");
    assert.equal(r1.ref, 1);

    const r2 = ctx.record({
      problem: "compute_flux scales input by 42",
      type: "decision",
    });
    assert.equal(r2.tier, "project");
    assert.equal(r2.ref, 1);

    const recallOut = ctx.recall({ scope: "persistent", query: "compute_flux" });
    assert.ok(recallOut.includes("compute_flux scales input by 42"));

    const ovOut = ctx.overview("both", 5);
    assert.ok(ovOut.includes("Always run targeted Tier-0 tests first"));
  }

  ctx.close();

  const codeIdx = new MmCodeIndex(srcDir);
  assert.equal(codeIdx.fileCount, 1);
  assert.ok(codeIdx.symbolCount >= 2);

  const symRead = codeIdx.readSymbols(["run_engine"]);
  assert.ok(symRead.includes("compute_flux"));

  const traceOut = codeIdx.trace("run_engine", "outbound", 2);
  assert.ok(traceOut.includes("compute_flux"));

  fs.rmSync(tmpDir, { recursive: true, force: true });
});

test("@tty-pt/mm WebAssembly reactor (mm.wasm) record, recallJson & review", () => {
  const wasm = getMmWasm();
  assert.ok(wasm && wasm.exports, "mm.wasm should instantiate on Stele substrate");
  assert.equal(typeof wasm.exports.mm_wasm_record, "function");
  assert.equal(typeof wasm.exports.mm_wasm_recall_json, "function");
  assert.equal(typeof wasm.exports.mm_wasm_review, "function");

  const ref = wasmRecord(
    "WASM reactor stores federated memory without native addon",
    "Verified in-memory record/recall",
    "wasm-test",
    true,
  );
  assert.ok(ref >= 1);

  const json = wasmRecallJson("federated memory", "wasm-test", 5);
  assert.ok(json.includes("WASM reactor stores federated memory"));

  const rc = wasmReview(ref, "confirm", "Verified via node --test");
  assert.equal(rc, 0);
});
