let steleLoadWasmModule = null;
let steleLoadNativeAddon = null;
let steleGetSubstrate = null;
let steleResolveAxisPrebuildDirs = null;

{
  const steleCandidates = ["@tty-pt/stele"];
  if (typeof process !== "undefined" && process.versions?.node) {
    steleCandidates.push(
      new URL("../../../libstele/npm/dist/index.js", import.meta.url).href,
      new URL("../../../site/external/libstele/npm/dist/index.js", import.meta.url).href,
    );
  }
  for (const cand of steleCandidates) {
    try {
      const stele = await import(cand);
      if (typeof stele?.loadWasmModule === "function") {
        steleLoadWasmModule = stele.loadWasmModule;
      }
      if (typeof stele?.loadNativeAddon === "function") {
        steleLoadNativeAddon = stele.loadNativeAddon;
      }
      if (typeof stele?.getSubstrate === "function") {
        steleGetSubstrate = stele.getSubstrate;
      }
      if (typeof stele?.resolveAxisPrebuildDirs === "function") {
        steleResolveAxisPrebuildDirs = stele.resolveAxisPrebuildDirs;
      }
      if (steleLoadNativeAddon || steleLoadWasmModule) break;
    } catch {
      // continue
    }
  }
}

let wasmInstance = null;

function ensureDefaultAxisPath() {
  if (typeof process === "undefined" || !process.env || !process.versions?.node) return;
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
  try {
    const getModule = globalThis.process?.getBuiltinModule;
    const fs = getModule ? getModule("fs") : null;
    const path = getModule ? getModule("path") : null;
    const url = getModule ? getModule("url") : null;
    if (fs && path && url) {
      const baseDir = path.dirname(url.fileURLToPath(import.meta.url));
      const siblingAxes = ["liblemma", "libsepal", "libjoint", "libislet"];
      for (const sibling of siblingAxes) {
        const candidate = path.resolve(baseDir, "..", "..", "..", sibling, "lib");
        if (fs.existsSync(candidate) && !dirs.includes(candidate)) {
          dirs.push(candidate);
        }
      }
    }
  } catch {
    // ignore
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
  if (typeof process === "undefined" || !process.versions?.node) {
    return null;
  }
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
  try {
    const getModule = globalThis.process?.getBuiltinModule;
    const fs = getModule ? getModule("fs") : null;
    const path = getModule ? getModule("path") : null;
    const url = getModule ? getModule("url") : null;
    const mod = getModule ? getModule("module") : null;
    if (!fs || !path || !url || !mod) return null;
    const baseDir = path.dirname(url.fileURLToPath(import.meta.url));
    const req = mod.createRequire(import.meta.url);
    const candidates = [
      path.join(baseDir, "mm_node.node"),
      path.join(baseDir, "mm.node"),
      path.join(baseDir, "..", "build", "Release", "mm_node.node"),
      path.join(baseDir, "..", "prebuilds", `${process.platform}-${process.arch}`, "mm.node"),
    ];
    for (const candidate of candidates) {
      if (fs.existsSync(candidate)) {
        try {
          return req(candidate);
        } catch {
          try {
            const m = { exports: {} };
            process.dlopen(m, candidate);
            return m.exports;
          } catch {
            // continue
          }
        }
      }
    }
  } catch {
    // ignore
  }
  return null;
}

export const nativeBinding = loadMmNativeBinding();

export function isNativeAvailable() {
  return nativeBinding !== null;
}

/**
 * Plug-and-play WebAssembly loader for @tty-pt/mm via Stele substrate
 */
export function getMmWasm() {
  if (wasmInstance) return wasmInstance;
  if (!steleLoadWasmModule) {
    throw new Error("@tty-pt/stele is required to load mm.wasm");
  }
  const substrate = steleGetSubstrate ? steleGetSubstrate() : null;
  wasmInstance = steleLoadWasmModule(new URL("./mm.wasm", import.meta.url), {
    wasi_snapshot_preview1: {
      fd_prestat_get: () => 8,
      fd_prestat_dir_name: () => 8,
      fd_fdstat_get: () => 8,
      fd_fdstat_set_flags: () => 8,
      fd_close: () => 0,
      fd_read: () => 8,
      fd_readdir: () => 8,
      fd_seek: () => 8,
      fd_write: () => 0,
      path_filestat_get: () => 44,
      path_open: () => 44,
      environ_sizes_get: (countPtr, bufSizePtr) => {
        if (substrate) {
          const dv = new DataView(substrate.memory.buffer);
          dv.setUint32(countPtr, 0, true);
          dv.setUint32(bufSizePtr, 0, true);
        }
        return 0;
      },
      environ_get: () => 0,
      clock_time_get: (_id, _prec, outPtr) => {
        if (substrate) {
          const dv = new DataView(substrate.memory.buffer);
          dv.setBigUint64(outPtr, BigInt(Date.now()) * 1000000n, true);
        }
        return 0;
      },
      proc_exit: () => 0,
    },
  });
  return wasmInstance;
}

export function wasmRecord(problem, outcome = "", subsystem = "wasm", standing = false) {
  const wasm = getMmWasm();
  const substrate = steleGetSubstrate();
  return substrate.withStrings(
    [String(problem || ""), String(outcome || ""), String(subsystem || "wasm")],
    (probPtr, outPtr, subPtr) =>
      wasm.exports.mm_wasm_record(probPtr, outPtr, subPtr, standing ? 1 : 0),
  );
}

export function wasmRecallJson(query = "", subsystem = "", limit = 5) {
  const wasm = getMmWasm();
  const substrate = steleGetSubstrate();
  const outCap = 65536;
  const outPtr = substrate.malloc(outCap);
  try {
    return substrate.withStrings(
      [String(query || ""), String(subsystem || "")],
      (qPtr, subPtr) => {
        const written = wasm.exports.mm_wasm_recall_json(
          qPtr,
          subPtr,
          Number(limit || 5),
          outPtr,
          outCap,
        );
        return written > 0 ? substrate.readString(outPtr, written) : "[]";
      },
    );
  } finally {
    substrate.free(outPtr);
  }
}

export function wasmReview(ref, action, reason = "") {
  const wasm = getMmWasm();
  const substrate = steleGetSubstrate();
  return substrate.withStrings(
    [String(action || "inspect"), String(reason || "")],
    (actPtr, reasonPtr) =>
      wasm.exports.mm_wasm_review(Number(ref || 0), actPtr, reasonPtr),
  );
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
    this._opts = opts || {};
    if (nativeBinding) {
      this._handle = nativeBinding.openContext(this._opts);
    } else {
      getMmWasm();
      this._handle = null;
    }
  }

  info() {
    if (!this._handle) {
      return {
        projectRoot: this._opts.cwd || "",
        projectName: "wasm",
        hasProjectTier: false,
        globalDir: "/tmp/mm-wasm-global",
        projectDir: null,
      };
    }
    return nativeBinding.getContextInfo(this._handle);
  }

  record(req = {}) {
    if (!this._handle) {
      const ref = wasmRecord(
        req.problem || req.summary || "",
        req.outcome || "",
        req.subsystem || "wasm",
        Boolean(req.standing),
      );
      return {
        ref,
        tier: "global",
        message: ref > 0 ? `Recorded Ref G:${ref}` : "wasm record failed",
      };
    }
    return nativeBinding.record(this._handle, req);
  }

  recall(req = {}) {
    if (!this._handle) {
      return wasmRecallJson(req.query || "", req.subsystem || "", req.maxResults || 5);
    }
    return nativeBinding.recall(this._handle, req);
  }

  review(req = {}) {
    if (!this._handle) {
      const rc = wasmReview(req.ref || 0, req.action || "inspect", req.note || "");
      return JSON.stringify({ ok: rc === 0, ref: req.ref || 0, action: req.action || "inspect" });
    }
    return nativeBinding.review(this._handle, req);
  }

  overview(tier = "both", maxPerBucket = 10) {
    if (!this._handle) return wasmRecallJson("", "", maxPerBucket);
    return nativeBinding.overview(this._handle, tier, maxPerBucket);
  }

  evaluateGuardrail(intentText, filePath = "", symbolName = "") {
    if (!this._handle) return wasmRecallJson(intentText || "", "", 5);
    return nativeBinding.evaluateGuardrail(this._handle, intentText, filePath, symbolName);
  }

  observeVerification(command, exitCode, outputSnippet = "") {
    if (!this._handle) return 0;
    return nativeBinding.observeVerification(this._handle, command, exitCode, outputSnippet);
  }

  recordFileMutation(filePath) {
    if (!this._handle) return;
    return nativeBinding.recordFileMutation(this._handle, filePath);
  }

  prime(prompt) {
    if (!this._handle) return wasmRecallJson(prompt || "", "", 5);
    return nativeBinding.prime(this._handle, prompt);
  }

  consolidate(tier = "project") {
    if (!this._handle) {
      return { duplicatesMerged: 0, contradictionsFlagged: 0, decayedCount: 0, promotedCount: 0, report: "" };
    }
    return nativeBinding.consolidate(this._handle, tier);
  }

  close() {
    if (this._handle && nativeBinding) {
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
