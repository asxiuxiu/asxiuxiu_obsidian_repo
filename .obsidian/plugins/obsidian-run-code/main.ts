import { App, Plugin, PluginSettingTab, Setting, Editor, MarkdownView } from "obsidian";
import { DEFAULT_SETTINGS, RunCodeSettings } from "./settings";
import { runButtonExtension } from "./runButtonExtension";
import { runCpp, getDefaultWorkspacePath } from "./cppRunner";

const CPP_SNIPPET = "```cpp\n#include <iostream>\n\nint main() {\n    \n    return 0;\n}\n```\n";

export default class RunCodePlugin extends Plugin {
	settings: RunCodeSettings;

	async onload() {
		await this.loadSettings();

		// Auto-fill default workspace path if empty
		if (!this.settings.workspacePath) {
			const basePath = (this.app.vault.adapter as any).basePath as string;
			this.settings.workspacePath = getDefaultWorkspacePath(basePath);
		}

		this.registerEditorExtension(runButtonExtension(this.settings));

		this.registerMarkdownPostProcessor((element, context) => {
			if (!this.settings.showRunButton) return;

			const codeBlocks = element.querySelectorAll("pre code");
			codeBlocks.forEach((codeEl) => {
				const pre = codeEl.parentElement;
				if (!pre) return;

				const classes = Array.from(codeEl.classList);
				const langClass = classes.find((c) => c.startsWith("language-"));
				const lang = langClass ? langClass.replace("language-", "").toLowerCase() : "";

				if (lang !== "cpp") return;

				const wrapper = document.createElement("div");
				wrapper.className = "obsidian-run-code-block";
				pre.parentNode?.insertBefore(wrapper, pre);
				wrapper.appendChild(pre);

				const btn = document.createElement("button");
				btn.textContent = "▶";
				btn.title = "Run C++";
				btn.className = "obsidian-run-code-btn";
				btn.addEventListener("click", async () => {
					let outputEl = wrapper.querySelector(".obsidian-run-code-output") as HTMLElement | null;
					if (!outputEl) {
						outputEl = document.createElement("div");
						outputEl.className = "obsidian-run-code-output";
						wrapper.appendChild(outputEl);
					}
					outputEl.textContent = "Running...";
					outputEl.style.display = "block";

					const code = codeEl.textContent || "";
					const result = await runCpp(code, this.settings.workspacePath);
					outputEl.textContent = result.text;
					outputEl.classList.toggle("is-error", result.isError);
				});

				wrapper.appendChild(btn);
			});
		});

		this.addCommand({
			id: "insert-cpp-snippet",
			name: "Insert C++ snippet",
			editorCallback: (editor: Editor, view: MarkdownView) => {
				const cursor = editor.getCursor();
				editor.replaceRange(CPP_SNIPPET, cursor);
				const newCursor = { line: cursor.line + 4, ch: 4 };
				editor.setCursor(newCursor);
			},
		});

		this.addSettingTab(new RunCodeSettingTab(this.app, this));
	}

	onunload() {}

	async loadSettings() {
		this.settings = Object.assign({}, DEFAULT_SETTINGS, await this.loadData());
	}

	async saveSettings() {
		await this.saveData(this.settings);
	}
}

class RunCodeSettingTab extends PluginSettingTab {
	plugin: RunCodePlugin;

	constructor(app: App, plugin: RunCodePlugin) {
		super(app, plugin);
		this.plugin = plugin;
	}

	display(): void {
		const { containerEl } = this;
		containerEl.empty();
		containerEl.createEl("h2", { text: "Run Code Settings" });

		containerEl.createEl("p", {
			text: "C++ code runs locally with g++. Make sure g++ is installed and accessible.",
			cls: "setting-item-description",
		});

		new Setting(containerEl)
			.setName("Show run button")
			.setDesc("Display a run button on C++ code blocks.")
			.addToggle((toggle) =>
				toggle.setValue(this.plugin.settings.showRunButton).onChange(async (value) => {
					this.plugin.settings.showRunButton = value;
					await this.plugin.saveSettings();
				})
			);

		new Setting(containerEl)
			.setName("Workspace path")
			.setDesc("Directory for temporary compile files. Default: vault/workspace/run-code/")
			.addText((text) => {
				const basePath = (this.app.vault.adapter as any).basePath as string;
				const defaultPath = getDefaultWorkspacePath(basePath);
				text.setPlaceholder("Auto")
					.setValue(
						this.plugin.settings.workspacePath === defaultPath ? "" : this.plugin.settings.workspacePath
					)
					.onChange(async (value) => {
						this.plugin.settings.workspacePath = value.trim() || defaultPath;
						await this.plugin.saveSettings();
					});
			});
	}
}
