import { App, Modal } from "obsidian";
import { EditorView, keymap, ViewPlugin, ViewUpdate, Decoration, DecorationSet } from "@codemirror/view";
import { EditorState } from "@codemirror/state";
import { StreamLanguage, syntaxTree } from "@codemirror/language";
import { cpp } from "@codemirror/legacy-modes/mode/clike";
import { runCpp } from "./cppRunner";




// Manual highlight via Decoration.mark — bypasses HighlightStyle/Tag instance issues.
const cppHighlightPlugin = ViewPlugin.fromClass(
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
			const tree = syntaxTree(view.state);
			if (!tree || tree.length === 0) return Decoration.none;
			const decorations: any[] = [];
			const cursor = tree.cursor();
			do {
				const name = cursor.type.name;
				let className = "";
				if (name === "keyword") className = "cmt-keyword";
				else if (name === "string") className = "cmt-string";
				else if (name === "comment") className = "cmt-comment";
				else if (name === "number") className = "cmt-number";
				else if (name === "typeName") className = "cmt-typeName";
				else if (name === "variableName") className = "cmt-variableName";
				else if (name === "operator") className = "cmt-operator";
				else if (name === "punctuation") className = "cmt-punctuation";
				else if (name === "propertyName") className = "cmt-propertyName";
				else if (name === "meta") className = "cmt-meta";
				else if (name === "atom") className = "cmt-atom";
				else if (name === "labelName") className = "cmt-labelName";
				else if (name === "namespace") className = "cmt-namespace";
				else if (name === "className") className = "cmt-className";
				else if (name === "macroName") className = "cmt-macroName";

				if (className) {
					decorations.push(Decoration.mark({ class: className }).range(cursor.from, cursor.to));
				}
			} while (cursor.next());
			return Decoration.set(decorations);
		}
	},
	{ decorations: (v) => v.decorations }
);

export class CloneRunModal extends Modal {
	private originalCode: string;
	private workspacePath: string;
	private editorView: EditorView;
	private outputEl: HTMLElement;

	constructor(app: App, code: string, workspacePath: string) {
		super(app);
		this.originalCode = code;
		this.workspacePath = workspacePath;
	}

	onOpen() {
		const { contentEl } = this;
		contentEl.empty();
		contentEl.addClass("run-code-clone-modal");

		contentEl.createEl("h3", { text: "Draft Run" });
		contentEl.createEl("p", {
			text: "Modify the code below and run. The original snippet won't be affected.",
			cls: "setting-item-description",
		});

		const editorContainer = contentEl.createDiv({ cls: "run-code-clone-cm6" });

		const state = EditorState.create({
			doc: this.originalCode,
			extensions: [
				StreamLanguage.define(cpp),
				cppHighlightPlugin,
				keymap.of([
					{
						key: "Tab",
						run: (view) => {
							view.dispatch(
								view.state.changeByRange((range) => ({
									changes: { from: range.from, insert: "\t" },
									range: { anchor: range.from + 1, head: range.from + 1 },
								}))
							);
							return true;
						},
					},
				]),
				EditorView.theme({
					"&": {
						fontSize: "var(--code-size)",
						fontFamily: "var(--font-monospace)",
						border: "1px solid var(--background-modifier-border)",
						borderRadius: "4px",
						backgroundColor: "var(--code-background)",
					},
					".cm-content": {
						padding: "8px",
						caretColor: "var(--text-normal)",
					},
					".cm-gutters": {
						display: "none",
					},
					".cm-activeLine": {
						backgroundColor: "transparent",
					},
					".cm-selectionBackground": {
						backgroundColor: "var(--text-selection) !important",
					},
					".cm-focused > .cm-scroller > .cm-selectionLayer .cm-selectionBackground": {
						backgroundColor: "var(--text-selection) !important",
					},
					".cmt-keyword": { color: "var(--code-keyword, #c678dd)" },
					".cmt-string": { color: "var(--code-string, #98c379)" },
					".cmt-comment": { color: "var(--code-comment, #5c6370)", fontStyle: "italic" },
					".cmt-number": { color: "var(--code-value, #d19a66)" },
					".cmt-typeName": { color: "var(--code-property, #e5c07b)" },
					".cmt-variableName": { color: "var(--text-normal)" },
					".cmt-operator": { color: "var(--code-operator, #56b6c2)" },
					".cmt-punctuation": { color: "var(--code-punctuation, #abb2bf)" },
					".cmt-propertyName": { color: "var(--code-property, #e06c75)" },
					".cmt-meta": { color: "var(--code-comment, #5c6370)" },
					".cmt-atom": { color: "var(--code-value, #d19a66)" },
					".cmt-function": { color: "var(--code-function, #61afef)" },
					".cmt-className": { color: "var(--code-property, #e5c07b)" },
					".cmt-namespace": { color: "var(--code-keyword, #c678dd)" },
					".cmt-macroName": { color: "var(--code-function, #61afef)" },
				}),
			],
		});

		this.editorView = new EditorView({
			state,
			parent: editorContainer,
		});

		const btnRow = contentEl.createDiv({ cls: "run-code-clone-actions" });
		const runBtn = btnRow.createEl("button", { text: "▶ Run", cls: "mod-cta" });
		const resetBtn = btnRow.createEl("button", { text: "↺ Reset" });

		runBtn.addEventListener("click", async () => {
			this.outputEl.textContent = "Running...";
			this.outputEl.classList.remove("is-error");
			const code = this.editorView.state.doc.toString();
			const result = await runCpp(code, this.workspacePath);
			this.outputEl.textContent = result.text;
			this.outputEl.classList.toggle("is-error", result.isError);
		});

		resetBtn.addEventListener("click", () => {
			this.editorView.dispatch({
				changes: {
					from: 0,
					to: this.editorView.state.doc.length,
					insert: this.originalCode,
				},
			});
			this.outputEl.textContent = "";
			this.outputEl.classList.remove("is-error");
		});

		this.outputEl = contentEl.createEl("pre", { cls: "run-code-clone-output" });
	}

	onClose() {
		this.editorView?.destroy();
		this.contentEl.empty();
	}
}
