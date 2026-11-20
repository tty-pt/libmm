export type MmLevel = "L0" | "L1" | "L2" | "L3";
export type MmEpistemicStatus =
  | "observed"
  | "hypothesized"
  | "tested"
  | "supported"
  | "contradicted"
  | "superseded";
export type MmTier = "auto" | "global" | "project" | "both";
export type MmScope = "auto" | "persistent" | "session" | "code" | "all";
export type MmReviewAction =
  | "inspect"
  | "forget"
  | "confirm"
  | "contradict"
  | "supersede"
  | "reclassify"
  | "promote";

export interface MmEpisode {
  problem?: string;
  request?: string;
  outcome?: string;
  rootCause?: string;
  prevention?: string;
  files?: string[];
  symbols?: string[];
  branches?: string[];
  commits?: string[];
  sessionId?: string;
}

export interface MmEvidence {
  kind: string;
  summary: string;
  weight?: number;
  createdAt?: string;
}

export interface MmHistoryEvent {
  event: string;
  fromStatus?: string;
  toStatus?: string;
  fromLevel?: string;
  toLevel?: string;
  note?: string;
  timestamp?: string;
}

export interface MmEnvelope {
  v?: number;
  id?: string;
  summary: string;
  level?: MmLevel;
  type?: string;
  timestamp?: string;
  tags?: string[];
  constraints?: string[];
  anchor?: boolean;
  confidence?: number;
  importance?: number;
  recallCount?: number;
  confirmations?: number;
  contradictions?: number;
  lastRecalled?: string;
  lastVerified?: string;
  lifecycle?: MmEpistemicStatus;
  supersedes?: number[];
  supersededBy?: number[];
  episode?: MmEpisode;
  evidence?: MmEvidence[];
  history?: MmHistoryEvent[];
}

export interface MmRankedRef {
  ref: number;
  score: number;
  origin?: "lemma" | "sepal" | "both";
}

export interface MmCandidate {
  ref: number;
  tier?: string;
  baseScore?: number;
  finalScore?: number;
  origin?: string;
  record?: string;
  envelope?: MmEnvelope;
  envelopeJson?: string;
}

export interface MmFilterCriteria {
  query?: string;
  level?: string;
  typeFilter?: string;
  tagsFilter?: string;
  statusFilter?: string;
  filesFilter?: string;
  symbolsFilter?: string;
  includeSuperseded?: boolean;
  includeContradicted?: boolean;
  anchorsOnly?: boolean;
  minConfidence?: number;
  minImportance?: number;
  nowEpochSec?: number;
}

export interface MmPromptProfile {
  intent: string;
  recencyBias: boolean;
  cleanedTokens: string;
  rawTerms: string;
}

export interface MmCodeSymbol {
  ref: number;
  fileId: number;
  name: string;
  qualifiedName: string;
  parentSymbol: string;
  kind: string;
  filePath: string;
  startLine: number;
  endLine: number;
  signature: string;
  callees: string[];
}

export interface MmContextOptions {
  cwd?: string;
  globalDir?: string;
  projectDir?: string;
  cormBin?: string;
  axisPath?: string;
  aurenUrl?: string;
  embedModel?: string;
  semanticSearch?: boolean;
  preferDaemon?: boolean;
}

export interface MmRecordRequest {
  problem?: string;
  summary?: string;
  level?: MmLevel;
  tier?: MmTier;
  type?: string;
  tags?: string;
  constraints?: string;
  request?: string;
  outcome?: string;
  rootCause?: string;
  prevention?: string;
  files?: string;
  symbols?: string;
  branches?: string;
  commits?: string;
  sessionId?: string;
  anchor?: boolean;
  standing?: boolean;
  confidence?: number;
  importance?: number;
  lifecycle?: MmEpistemicStatus;
  supersedesRef?: number;
  deduplicate?: boolean;
}

export interface MmRecallRequest {
  query?: string;
  scope?: MmScope;
  tier?: MmTier;
  level?: string;
  typeFilter?: string;
  tagsFilter?: string;
  statusFilter?: string;
  filesFilter?: string;
  symbolsFilter?: string;
  parentSymbol?: string;
  neighbors?: string;
  traceSymbol?: string;
  traceDirection?: string;
  traceDepth?: number;
  sessionFilter?: string;
  roleFilter?: string;
  toolFilter?: string;
  sinceIso?: string;
  untilIso?: string;
  overview?: boolean;
  diffImpact?: boolean;
  xyMismatch?: boolean;
  crossBoundary?: boolean;
  firstTouch?: string;
  guardrailIntent?: string;
  diagnoseTrace?: string;
  includeSuperseded?: boolean;
  includeContradicted?: boolean;
  anchorsOnly?: boolean;
  minConfidence?: number;
  minImportance?: number;
  maxResults?: number;
  lineStart?: number;
  lineEnd?: number;
}

export interface MmReviewRequest {
  ref: number;
  tier?: MmTier;
  action: MmReviewAction;
  note?: string;
  replacementSummary?: string;
  newLevel?: MmLevel;
  newType?: string;
  newTags?: string;
  newStatus?: MmEpistemicStatus;
  newAnchor?: number;
  newConfidence?: number;
  newImportance?: number;
}

export declare const nativeBinding: Record<string, any> | null;
export declare function isNativeAvailable(): boolean;
export declare function getMmWasm(): WebAssembly.Instance;
export declare function wasmRecord(
  problem: string,
  outcome?: string,
  subsystem?: string,
  standing?: boolean
): number;
export declare function wasmRecallJson(
  query?: string,
  subsystem?: string,
  limit?: number
): string;
export declare function wasmReview(
  ref: number,
  action: MmReviewAction | string,
  reason?: string
): number;
export declare function adaptiveRrfK(candidateCount: number): number;
export declare function isTechnicalInquiry(query: string): boolean;
export declare function isCleanSummaryText(summary: string): boolean;
export declare function isCodeOrDocArtifact(record: string, envelope?: MmEnvelope | string): boolean;
export declare function isQuestionMemory(envelope?: MmEnvelope | string): boolean;
export declare function isAssistantEcho(envelope?: MmEnvelope | string): boolean;
export declare function fuseRankingsRRF(
  lexical: Array<{ ref: number; score?: number }>,
  semantic: Array<{ ref: number; score?: number }>,
  kOverride?: number
): MmRankedRef[];
export declare function computeRankingScore(cand: MmCandidate, crit?: MmFilterCriteria): number;
export declare function filterAndEnrich(candidates: MmCandidate[], crit?: MmFilterCriteria): MmCandidate[];
export declare function payloadEncode(envelopeOrJson: MmEnvelope | string): { bytes: Uint8Array; json: string };
export declare function payloadDecode(bytesOrStr: Uint8Array | string): MmEnvelope;
export declare function windowForLevel(
  level: MmLevel | string,
  nowEpochSec?: number
): { bounded: boolean; since: string; until: string };
export declare function classifyPrompt(prompt: string): MmPromptProfile;
export declare function scanSource(filePath: string, content: string, fileId?: number): MmCodeSymbol[];

export declare class MmContext {
  constructor(opts?: MmContextOptions);
  info(): {
    projectRoot: string;
    projectName: string;
    hasProjectTier: boolean;
    globalDir: string;
    projectDir: string | null;
  };
  record(req: MmRecordRequest): { ref: number; tier: "global" | "project"; message: string };
  recall(req: MmRecallRequest): string;
  review(req: MmReviewRequest): string;
  overview(tier?: MmTier, maxPerBucket?: number): string;
  evaluateGuardrail(intentText: string, filePath?: string, symbolName?: string): string;
  observeVerification(command: string, exitCode: number, outputSnippet?: string): string;
  recordFileMutation(filePath: string): void;
  prime(prompt: string): string;
  consolidate(tier?: MmTier): {
    duplicatesMerged: number;
    contradictionsFlagged: number;
    decayedCount: number;
    promotedCount: number;
    report: string;
  };
  close(): void;
}

export declare class MmCodeIndex {
  readonly fileCount: number;
  readonly symbolCount: number;
  constructor(rootDir?: string);
  search(query?: string, fileFilter?: string, kindFilter?: string, limit?: number): string;
  readSymbols(
    symbols: string[] | string,
    opts?: {
      parentSymbol?: string;
      file?: string;
      neighbors?: string;
      lineStart?: number;
      lineEnd?: number;
    }
  ): string;
  trace(symbolName: string, direction?: string, depth?: number): string;
  overview(): string;
  diffImpact(depth?: number): string;
  diagnose(traceText: string, depth?: number): string;
  detectXyMismatches(): string;
  resolveCrossBoundary(symbolName?: string, filePath?: string): string;
  firstTouchDossier(fileOrSymbol: string): string;
}
