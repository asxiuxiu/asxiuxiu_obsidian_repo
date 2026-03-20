#!/bin/bash
# UserPromptSubmit hook: inject active Obsidian note + selection into context

# Get active file info
FILE_INFO=$(obsidian file active 2>/dev/null)
if [ -z "$FILE_INFO" ]; then
  exit 0
fi

FILE_PATH=$(echo "$FILE_INFO" | awk -F'\t' '/^path/{print $2}')
FILE_NAME=$(echo "$FILE_INFO" | awk -F'\t' '/^name/{print $2}')

# Get active file content
FILE_CONTENT=$(obsidian read active 2>/dev/null)

# Get selection (eval returns "=> value" or "=> ")
SELECTION_RAW=$(obsidian eval code="app.workspace.activeEditor?.editor?.getSelection() || ''" 2>/dev/null)
# Strip the leading "=> " prefix
SELECTION="${SELECTION_RAW#=> }"

# Build context and output JSON via Python (safe string handling)
export OBS_FILE_PATH="$FILE_PATH"
export OBS_FILE_NAME="$FILE_NAME"
export OBS_FILE_CONTENT="$FILE_CONTENT"
export OBS_SELECTION="$SELECTION"

python3 - <<'PYEOF'
import json, os

file_path = os.environ.get("OBS_FILE_PATH", "")
file_name = os.environ.get("OBS_FILE_NAME", "")
file_content = os.environ.get("OBS_FILE_CONTENT", "")
selection = os.environ.get("OBS_SELECTION", "").strip().strip("'\"")

parts = [
    f"## Obsidian 激活笔记上下文",
    f"**笔记**: {file_name}  ",
    f"**路径**: {file_path}",
    f"\n### 笔记内容\n```markdown\n{file_content}\n```",
]

if selection:
    parts.append(f"\n### 当前选中文本\n```\n{selection}\n```")

ctx = "\n".join(parts)

print(json.dumps({
    "hookSpecificOutput": {
        "hookEventName": "UserPromptSubmit",
        "additionalContext": ctx
    }
}))
PYEOF
