# knowledge-card

Generate and send a knowledge card from the Obsidian vault.

## Tools

- fs: read files and list directories
- message: send the card to Feishu

## Usage

```bash
# Generate a morning card
claude -p "生成晨间知识卡片" --skill knowledge-card

# Generate an afternoon card
claude -p "生成午后知识卡片" --skill knowledge-card
```

## Workflow

1. List markdown files in the vault (exclude `graphics/` directory)
2. Randomly select one file
3. Read and extract key knowledge points
4. Format as a knowledge card with proper greeting
5. Send via Feishu message

## Card Format

```
📚 {时段}知识卡片 — {笔记标题}

{问候语}

**核心要点：**
1. {要点1}
2. {要点2}
...

**相关笔记：** [[xxx]]
```

## Greetings by Time

- Morning: "早——新的一天开始 ☕"
- Afternoon: "下午好——午休后唤醒大脑 💪"
- Evening: "下班前——轻松回顾一下，帮助记忆沉淀 🌆"

## Output

- channel: feishu
- target: ou_59f07954975b3704ca8f070d0cb8cd2a
- Single message containing both greeting and card content
