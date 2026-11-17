#!/bin/sh -e
# test_cli_mcp.sh — End-to-end test for bin/mm CLI and bin/mm mcp JSON-RPC server

td=$(mktemp -d)
trap 'rm -rf "$td"' EXIT

export MM_GLOBAL_DIR="$td/global"
export MM_PROJECT_DIR="$td/project"
export LD_LIBRARY_PATH="./lib:../libcorm/lib:../libstele/lib:../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib:$LD_LIBRARY_PATH"
export CORM_AXIS_PATH="../libjoint/lib:../liblemma/lib:../libsepal/lib:../libislet/lib"

mm=./bin/mm

echo "=== test_cli_mcp.sh: CLI record, recall, review, prime & MCP stdio ==="

out=$("$mm" record --standing "Always run targeted tier-0 tests before integration")
echo "$out" | grep -q '\[Ref G:1\]'
echo "ok - CLI record --standing routes to Global Tier [Ref G:1]"

out=$("$mm" record "Implemented libmm two-tier federated store tests")
echo "$out" | grep -q '\[Ref P:1\]'
echo "ok - CLI record default routes to Project Tier [Ref P:1]"

out=$("$mm" recall --scope persistent "tests")
echo "$out" | grep -q '\[Ref G:1\]'
echo "$out" | grep -q '\[Ref P:1\]'
echo "ok - CLI recall merges Global + Project tiers"

out=$("$mm" recall --symbol mm_adaptive_rrf_k)
echo "$out" | grep -q 'mm_adaptive_rrf_k'
echo "ok - CLI recall --symbol auto-previews C symbol"

out=$("$mm" prime "Remember that we always use memory_recall")
echo "$out" | grep -q 'intent="directive"'
echo "$out" | grep -q '\[Ref G:1\]'
echo "ok - CLI prime detects directive intent and injects Global rules"

# Test MCP JSON-RPC 2.0 server over stdio
mcp_out=$(printf '%s\n%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"memory_recall","arguments":{"scope":"persistent","query":"tests"}}}' \
  | "$mm" mcp)

echo "$mcp_out" | grep -q '"protocolVersion":"2024-11-05"'
echo "$mcp_out" | grep -q '"name":"memory_recall"'
echo "$mcp_out" | grep -q '\[Ref G:1\]'
echo "ok - MCP stdio server handles initialize, tools/list, and tools/call"

echo "test_cli_mcp.sh: all green"
