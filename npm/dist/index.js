import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

let steleLoadNativeAddon = null;
let steleResolveAxisPrebuildDirs = null;

if (typeof process !== "undefined" && process.versions?.node) {
  const steleCandidates = [
    "@tty-pt/stele",
    new URL("../../../libstele/npm/dist/index.js", import.meta.url).href,
    new URL("../../../site/external/libstele/npm/dist/index.js", import.meta.url).href,
  ];
  for (const cand of steleCandidates) {
    try {
      const stele = await import(cand);
      if (typeof stele?.loadNativeAddon === "function") {
        steleLoadNativeAddon = stele.loadNativeAddon;
      }
      if (typeof stele?.resolveAxisPrebuildDirs === "function") {
        steleResolveAxisPrebuildDirs = stele.resolveAxisPrebuildDirs;
      }
      if (steleLoadNativeAddon) break;
    } catch {
      // continue
    }
  }
}

function ensureDefaultAxisPath() {
  if (typeof process === "undefined" || !process.env) return;
  const dirs = [];
  if (process.env.CORM_AXIS_PATH) {
    for (const part of process.env.CORM_AXIS_PATH.split(":")) {
      if (part) dirs.push(part);
    }
  }
  if (steleResolveAxisPrebuildDirs) {
    try {
      const prebuildDirs = steleResolveAxisPrebuildDirs(import.meta.url);
      for (const d of prebuildDirs) {
        if (d && !dirs.includes(d)) dirs.push(d);
      }
    } catch {
      // ignore
    }
  }
  const siblingAxes = ["liblemma", "libsepal", "libjoint", "libislet"];
  for (const sibling of siblingAxes) {
    const candidate = path.resolve(__dirname, "..", "..", "..", sibling, "lib");
    if (fs.existsSync(candidate) && !dirs.includes(candidate)) {
      dirs.push(candidate);
    }
  }
  if (dirs.length > 0) {
    process.env.CORM_AXIS_PATH = dirs.join(":");
    if (!process.env.CORM_DEFAULT_AXIS_PATH) {
      process.env.CORM_DEFAULT_AXIS_PATH = dirs.join(":");
    }
  }
}

ensureDefaultAxisPath();

function loadMmNativeBinding() {
  if (steleLoadNativeAddon) {
    try {
      const addon = steleLoadNativeAddon(import.meta.url, "mm");
      if (addon && Object.keys(addon).length > 0) {
        return addon;
      }
    } catch {
      // fall through to local candidates
    }
  }
  const req = createRequire(import.meta.url);
  const candidates = [
    path.join(__dirname, "mm_node.node"),
    path.join(__dirname, "mm.node"),
    path.join(__dirname, "..", "build", "Release", "mm_node.node"),
    path.join(__dirname, "..", "prebuilds", `${process.platform}-${process.arch}`, "mm.node"),
  ];
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      try {
        return req(candidate);
      } catch {
        try {
          const mod = { exports: {} };
          process.dlopen(mod, candidate);
          return mod.exports;
        } catch {
          // continue
        }
      }
    }
  }
  return null;
}

export const nativeBinding = loadMmNativeBinding();

export function isNativeAvailable() {
  return nativeBinding !== null;
}

export function adaptiveRrfK(candidateCount) {
  return nativeBinding.adaptiveRrfK(candidateCount);
}

export function isTechnicalInquiry(query) {
  return nativeBinding.isTechnicalInquiry(query || "");
}

export function isCleanSummaryText(summary) {
  return nativeBinding.isCleanSummaryText(summary || "");
}

export function isCodeOrDocArtifact(record, envelope) {
  const envJson = typeof envelope === "string"
    ? envelope
    : envelope
    ? JSON.stringify(envelope)
    : "";
  return nativeBinding.isCodeOrDocArtifact(record || "", envJson);
}

export function isQuestionMemory(envelope) {
  const envJson = typeof envelope === "string"
    ? envelope
    : envelope
    ? JSON.stringify(envelope)
    : "";
  return nativeBinding.isQuestionMemory(envJson);
}

export function isAssistantEcho(envelope) {
  const envJson = typeof envelope === "string"
    ? envelope
    : envelope
    ? JSON.stringify(envelope)
    : "";
  return nativeBinding.isAssistantEcho(envJson);
}

export function fuseRankingsRRF(lexical, semantic, kOverride) {
  return nativeBinding.fuseRankingsRRF(
    Array.isArray(lexical) ? lexical : [],
    Array.isArray(semantic) ? semantic : [],
    typeof kOverride === "number" ? kOverride : 0,
  );
}

export function computeRankingScore(cand, crit) {
  const candPayload = {
    ...cand,
    envelopeJson: cand?.envelope ? JSON.stringify(cand.envelope) : cand?.envelopeJson || "",
  };
  return nativeBinding.computeRankingScore(candPayload, crit || {});
}

export function filterAndEnrich(candidates, crit) {
  const prepared = (Array.isArray(candidates) ? candidates : []).map((c) => ({
    ...c,
    envelopeJson: c?.envelope ? JSON.stringify(c.envelope) : c?.envelopeJson || "",
  }));
  const raw = nativeBinding.filterAndEnrich(prepared, crit || {});
  return raw.map((r) => {
    let envelope = undefined;
    if (r.envelopeJson) {
      try {
        envelope = JSON.parse(r.envelopeJson);
      } catch {
        // ignore
      }
    }
    const { envelopeJson: _ej, ...rest } = r;
    return {
      ...rest,
      ...(envelope ? { envelope } : {}),
    };
  });
}

export function payloadEncode(envelopeOrJson) {
  const str = typeof envelopeOrJson === "string" ? envelopeOrJson : JSON.stringify(envelopeOrJson);
  return nativeBinding.payloadEncode(str);
}

export function payloadDecode(bytesOrStr) {
  const jsonStr = nativeBinding.payloadDecode(bytesOrStr);
  return JSON.parse(jsonStr);
}

export function windowForLevel(level, nowEpochSec) {
  return nativeBinding.windowForLevel(String(level || "L2"), Number(nowEpochSec || Math.floor(Date.now() / 1000)));
}

export function classifyPrompt(prompt) {
  return nativeBinding.classifyPrompt(String(prompt || ""));
}

export function scanSource(filePath, content, fileId = 1) {
  return nativeBinding.scanSource(String(filePath), String(content), Number(fileId));
}

export class MmContext {
  constructor(opts = {}) {
    ensureDefaultAxisPath();
    this._handle = nativeBinding.openContext(opts);
  }

  info() {
    return nativeBinding.getContextInfo(this._handle);
  }

  record(req) {
    return nativeBinding.record(this._handle, req);
  }

  recall(req) {
    return nativeBinding.recall(this._handle, req);
  }

  review(req) {
    return nativeBinding.review(this._handle, req);
  }

  overview(tier = "both", maxPerBucket = 10) {
    return nativeBinding.overview(this._handle, tier, maxPerBucket);
  }

  evaluateGuardrail(intentText, filePath = "", symbolName = "") {
    return nativeBinding.evaluateGuardrail(this._handle, intentText, filePath, symbolName);
  }

  observeVerification(command, exitCode, outputSnippet = "") {
    return nativeBinding.observeVerification(this._handle, command, exitCode, outputSnippet);
  }

  recordFileMutation(filePath) {
    return nativeBinding.recordFileMutation(this._handle, filePath);
  }

  prime(prompt) {
    return nativeBinding.prime(this._handle, prompt);
  }

  consolidate(tier = "project") {
    return nativeBinding.consolidate(this._handle, tier);
  }

  close() {
    if (this._handle) {
      nativeBinding.closeContext(this._handle);
      this._handle = null;
    }
  }
}

export class MmCodeIndex {
  constructor(rootDir = ".") {
    this._handle = nativeBinding.openCodeIndex(rootDir);
    this.fileCount = this._handle.fileCount;
    this.symbolCount = this._handle.symbolCount;
  }

  search(query, fileFilter = "", kindFilter = "", limit = 10) {
    return nativeBinding.codeSearch(this._handle, query, fileFilter, kindFilter, limit);
  }

  readSymbols(symbols, opts = {}) {
    return nativeBinding.codeReadSymbols(
      this._handle,
      symbols,
      opts.parentSymbol || "",
      opts.file || "",
      opts.neighbors || "both",
      opts.lineStart || 0,
      opts.lineEnd || 0,
    );
  }

  trace(symbolName, direction = "both", depth = 3) {
    return nativeBinding.codeTrace(this._handle, symbolName, direction, depth);
  }

  overview() {
    return nativeBinding.codeOverview(this._handle);
  }

  diffImpact(depth = 2) {
    return nativeBinding.codeDiffImpact(this._handle, depth);
  }

  diagnose(traceText, depth = 2) {
    return nativeBinding.codeDiagnose(this._handle, traceText, depth);
  }

  detectXyMismatches() {
    return nativeBinding.codeDetectXyMismatches(this._handle);
  }

  resolveCrossBoundary(symbolName = "", filePath = "") {
    return nativeBinding.codeResolveCrossBoundary(this._handle, symbolName, filePath);
  }

  firstTouchDossier(fileOrSymbol) {
    return nativeBinding.codeFirstTouchDossier(this._handle, fileOrSymbol);
  }
}
