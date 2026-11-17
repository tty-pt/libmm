#!/bin/sh -e
# test_cli_mcp.sh — End-to-end test for bin/mm CLI and bin/mm mcp JSON-RPC server

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT

export MM_GLOBAL_DIR="$td/global"
export MM_PROJECT_DIR="$td/project"
LIB_PATHS="./lib:../libcorm/lib:../libstele/lib:../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib"
export LD_LIBRARY_PATH="$LIB_PATHS:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="$LIB_PATHS:$DYLD_LIBRARY_PATH"
export CORM_AXIS_PATH="../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib:/opt/homebrew/lib:/usr/local/lib:/usr/lib"

mm=./bin/mm

echo "=== test_cli_mcp.sh: CLI record, recall, review, guardrail, overview, prime & MCP stdio ==="

out=$("$mm" record --standing "Never use raw polling loops when waiting for tasks")
echo "$out" | grep -q '\[Ref G:1\]'
echo "ok - CLI record --standing routes to Global Tier [Ref G:1]"

out=$("$mm" record "Implemented libmm two-tier federated store tests")
echo "$out" | grep -q '\[Ref P:1\]'
echo "ok - CLI record default routes to Project Tier [Ref P:1]"

out=$("$mm" recall --scope persistent "tests")
echo "$out" | grep -q '\[Ref P:1\]'
echo "ok - CLI recall retrieves persistent records"

out=$("$mm" recall --scope persistent --overview)
echo "$out" | grep -q 'Persona Tenets'
echo "$out" | grep -q 'Chronological Epoch Timeline'
echo "ok - CLI recall --scope persistent --overview builds hierarchical lore overview"

out=$("$mm" recall --guardrail "Using raw polling loops to check task status")
echo "$out" | grep -q 'MEMORY GUARDRAIL WARNING'
echo "$out" | grep -q 'standing_restriction'
echo "ok - CLI recall --guardrail evaluates counterfactual pre-flight warnings"

out=$("$mm" review --ref 1 --tier project --action inspect)
echo "$out" | grep -q 'Evidence Tree'
echo "$out" | grep -q '\[Ref #1\]'
echo "ok - CLI review --action inspect outputs detailed evidence tree inspection"

out=$("$mm" recall --symbol mm_adaptive_rrf_k)
echo "$out" | grep -q 'mm_adaptive_rrf_k'
echo "ok - CLI recall --symbol auto-previews C symbol"

out=$("$mm" prime "Remember that we never use raw polling loops")
echo "$out" | grep -q 'intent="directive"'
echo "$out" | grep -q '\[Ref G:1\]'
echo "ok - CLI prime detects directive intent and injects Global rules"

# Test MCP JSON-RPC 2.0 server over stdio
mcp_out=$(printf '%s\n%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"memory_recall","arguments":{"scope":"persistent","query":"tests"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"memory_review","arguments":{"ref":1,"tier":"project","action":"inspect"}}}' \
  | "$mm" mcp)

echo "$mcp_out" | grep -q '"protocolVersion":"2024-11-05"'
echo "$mcp_out" | grep -q '"name":"memory_recall"'
echo "$mcp_out" | grep -q '\[Ref P:1\]'
echo "$mcp_out" | grep -q 'Evidence Tree'
echo "ok - MCP stdio server handles initialize, tools/list, memory_recall, and memory_review inspect"

echo "test_cli_mcp.sh: all green"
