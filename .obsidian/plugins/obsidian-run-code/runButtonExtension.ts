import { Decoration, DecorationSet, EditorView, ViewPlugin, ViewUpdate, WidgetType } from "@codemirror/view";
import { RangeSetBuilder } from "@codemirror/state";
import { syntaxTree } from "@codemirror/language";
import { RunCodeSettings } from "./settings";
import { runCpp } from "./cppRunner";

class RunButtonWidget extends WidgetType {
	constructor(private code: string, private settings: RunCodeSettings) {
		super();
	}

	toDOM(): HTMLElement {
		const btn = document.createElement("button");
		btn.textContent = "▶";
		btn.title = "Run C++";
		btn.className = "obsidian-run-code-btn";
		btn.addEventListener("click", async () => {
			await this.runCode();
		});
		return btn;
	}

	async runCode() {
		const container = document.activeElement?.closest(".cm-preview-code-block") as HTMLElement | null;
		if (!container) return;

		let outputEl = container.querySelector(".obsidian-run-code-output") as HTMLElement | null;
		if (!outputEl) {
			outputEl = document.createElement("div");
			outputEl.className = "obsidian-run-code-output";
			container.appendChild(outputEl);
		}
		outputEl.textContent = "Running...";
		outputEl.style.display = "block";

		const result = await runCpp(this.code, this.settings.workspacePath);
		outputEl.textContent = result.text;
		outputEl.classList.toggle("is-error", result.isError);
	}
}

export function runButtonExtension(settings: RunCodeSettings) {
	return ViewPlugin.fromClass(
		class {
			decorations: DecorationSet = Decoration.none;
			constructor(view: EditorView) {
				this.decorations = this.buildDecorations(view);
			}

			update(update: ViewUpdate) {
				if (update.docChanged || update.viewportChanged) {
					this.decorations = this.buildDecorations(update.view);
				}
			}

			buildDecorations(view: EditorView): DecorationSet {
				if (!settings.showRunButton) return Decoration.none;

				const builder = new RangeSetBuilder<Decoration>();
				const tree = syntaxTree(view.state);

				for (const { from, to } of view.visibleRanges) {
					tree.iterate({
						from,
						to,
						enter: (node) => {
							if (node.type.name === "HyperMD-codeblock-begin") {
								const line = view.state.doc.lineAt(node.to);
								const match = line.text.match(/^```\s*(\S*)/);
								const lang = match ? match[1].trim().toLowerCase() : "";
								if (lang === "cpp") {
									let codeEnd = node.to;
									let depth = 1;
									tree.iterate({
										from: node.to,
										to: view.state.doc.length,
										enter: (n) => {
											if (n.type.name === "HyperMD-codeblock-begin") depth++;
											if (n.type.name === "HyperMD-codeblock-end") {
												depth--;
												if (depth === 0) {
													codeEnd = n.from;
													return false;
												}
											}
											return undefined;
										},
									});

									const codeText = view.state.doc.sliceString(node.to, codeEnd).trim();
									const deco = Decoration.widget({
										widget: new RunButtonWidget(codeText, settings),
										side: 1,
									});
									builder.add(node.from, node.from, deco);
								}
							}
						},
					});
				}

				return builder.finish();
			}
		},
		{
			decorations: (v) => v.decorations,
		}
	);
}
