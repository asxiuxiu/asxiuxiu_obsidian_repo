---
name: deep-learning-note-writer
description: Use when the user asks to create or refine Obsidian notes in Notes/深度学习入门/ based on the book "Deep Learning from Scratch" or beginner deep-learning topics such as perceptrons, neural networks, backpropagation, optimizers, and CNNs.
---

# Deep Learning Note Writer

## Overview

Write beginner-friendly Obsidian notes for *Deep Learning from Scratch*（《深度学习入门：基于Python的理论与实现》）. Keep the depth aligned with the book: **run the code, explain the core idea in your own words, and stop there**. Do not expand into advanced topics or over-engineer the structure.

**Core principle:** One note should feel like one chapter's worth of understanding — not one subsection, not one formula.

## When to Use

Use this skill when:

- The user asks to write a note for a chapter or concept from *Deep Learning from Scratch*.
- The user asks to refine an existing note under `Notes/深度学习入门/`.
- The topic is a beginner deep-learning concept covered by the book: perceptron, activation function, loss function, gradient descent, backpropagation, optimizer, CNN, etc.

Do **not** use for:

- Advanced topics outside the book (Transformers, diffusion models, detailed RL).
- Non-deep-learning notes.

## Required Background

**REQUIRED:** Read `obsidian-markdown` before writing any `.md` file.

**REQUIRED:** Read `Notes/深度学习入门/Roadmap.md` to confirm which note to write and avoid splitting topics too finely.

## Writing Workflow

1. **Find the target note** in `Notes/深度学习入门/Roadmap.md`.
2. **Read the relevant chapter** of the book (or the excerpt the user provided).
3. **Run or understand the code** in the book.
4. **Close the book and write** in your own words.
5. **Keep it simple**: one note per roadmap entry, no deeper than the book.

## Note Structure

### 1. Frontmatter

```yaml
---
title: <Note Title>
date: YYYY-MM-DD
tags:
  - deep-learning
aliases:
  - <optional alias>
---
```

### 2. Return Navigation

```markdown
> [[Notes/深度学习入门/Roadmap|← 返回 深度学习入门路线图]]
```

### 3. Opening

Start with a short question or scenario. Do not begin with a dry definition.

**Bad:** "感知机是一种线性分类器。"
**Good:** "如果让每个证据按重要性投票，超过门槛就通过——这就是感知机的基本想法。"

### 4. Body

- Explain the idea in plain language first.
- Introduce the formal term right after explaining it.
- Include the key formula with LaTeX.
- Include the key Python code from the book.
- Add a short "小结" summarizing 2–3 takeaways.

### 5. Links

- Link to related notes in `Notes/深度学习入门/` when they already exist or are in the roadmap.
- Use full vault paths: `[[Notes/深度学习入门/感知机]]`.
- Do not create links to notes that do not exist yet.

## Style Rules

| Rule | Why |
|------|-----|
| **Match the book's depth** | This is an intro book. Do not add ResNet details in the perceptron note. |
| **One note per roadmap entry** | The roadmap already groups subsections sensibly. Do not split further. |
| **No long book quotes** | Rewrite in your own words. |
| **Code must run** | Use the book's code, make sure imports are included. |
| **Explain new terms** | Assume the reader is new to neural networks. |
| **Escape pipes in wikilinks inside tables** | `[[Notes/深度学习入门/感知机\|感知机]]` |

## Code Rules

```python
import numpy as np

def perceptron(x, w, b):
    return int(np.sum(w * x) + b > 0)
```

- Keep code close to the book's implementation.
- Include necessary imports.
- If the code is from a specific section, mention it in a comment.

## Self-Check

Before finishing, confirm:

- [ ] Frontmatter has `title`, `date`, `tags`.
- [ ] Return link to `Notes/深度学习入门/Roadmap` is at the top.
- [ ] The note matches the depth of the corresponding book chapter.
- [ ] New terms are explained in plain language.
- [ ] Key code from the book is included and runnable.
- [ ] Wikilinks use full vault paths.
- [ ] Pipes in wikilinks inside tables are escaped.

## Common Mistakes

| Mistake | Fix |
|---------|-----|
| Splitting one chapter into 5+ notes | Follow the roadmap's grouping. |
| Adding advanced material beyond the book | Stay at intro level. |
| Copying the book verbatim | Rewrite after closing the book. |
| Skipping the return navigation | Always add it. |
| Linking to non-existent notes | Only link notes that exist or are in the roadmap. |
