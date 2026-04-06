#!/usr/bin/env bash
# Pre-push hook: blocks git push if CHANGELOG.md hasn't been updated
# Called by Claude Code PreToolUse hook on Bash commands matching "git push"

set -euo pipefail

# Read the tool input from stdin
cmd=$(jq -r '.tool_input.command' 2>/dev/null || echo "")

# Only check git push commands
case "$cmd" in
  *"git push"*)
    # Find the merge base to compare against
    remote=$(echo "$cmd" | grep -oP '(?<=git push\s)\S+' || echo "origin")
    branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "main")

    # Try to find merge base with remote, fall back to first commit
    base=$(git merge-base "$remote/$branch" HEAD 2>/dev/null || \
           git rev-list --max-parents=0 HEAD 2>/dev/null | head -1)

    if [ -z "$base" ]; then
      # No base found — allow push (new repo, no remote tracking)
      echo '{}'
      exit 0
    fi

    if ! git diff --name-only "$base"..HEAD 2>/dev/null | grep -q '^CHANGELOG.md$'; then
      echo '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"CHANGELOG.md has not been updated. Please update the changelog before pushing."}}'
      exit 0
    fi

    echo '{}'
    ;;
  *)
    echo '{}'
    ;;
esac
