#!/bin/bash
# knowledge-card.sh - Generate a knowledge card from Obsidian vault

set -e

VAULT_PATH="/root/.openclaw/workspace/asxiuxiu_obsidian_repo"
TIME_OF_DAY="${1:-morning}"

# Find all markdown files excluding graphics directory
mapfile -t files < <(find "$VAULT_PATH" -name "*.md" -type f ! -path "*/graphics/*" ! -path "*/.claude/*" ! -path "*/workspace/*")

if [ ${#files[@]} -eq 0 ]; then
    echo "No markdown files found in vault"
    exit 1
fi

# Randomly select one file
random_index=$((RANDOM % ${#files[@]}))
selected_file="${files[$random_index]}"

# Get relative path for display
rel_path="${selected_file#$VAULT_PATH/}"

# Extract title (first H1 or filename)
title=$(grep -m1 "^# " "$selected_file" 2>/dev/null | sed 's/^# //' || basename "$selected_file" .md)

echo "Selected: $rel_path"
echo "Title: $title"
echo "---"
head -100 "$selected_file"
