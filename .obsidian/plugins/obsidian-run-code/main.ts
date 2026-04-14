import { App, Plugin, PluginSettingTab, Setting } from "obsidian";
import { DEFAULT_SETTINGS, RunCodeSettings } from "./settings";
import { runButtonExtension } from "./runButtonExtension";
import { canRunLocally, runLocal } from "./localRunner";
import { WandboxClient, WANDBOX_LANGUAGE_MAP } from "./wandboxClient";

export default class RunCodePlugin extends Plugin {
	settings: RunCodeSettings;

	async onload() {
		await this.loadSettings();

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

				if (!lang || !this.settings.enabledLanguages.includes(lang)) return;

				const wrapper = document.createElement("div");
				wrapper.className = "obsidian-run-code-block";
				pre.parentNode?.insertBefore(wrapper, pre);
				wrapper.appendChild(pre);

				const btn = document.createElement("button");
				btn.textContent = "▶";
				btn.title = "Run code";
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

					if (canRunLocally(lang)) {
						const result = runLocal(code);
						outputEl.textContent = result.text;
						outputEl.classList.toggle("is-error", result.isError);
						return;
					}

					const client = new WandboxClient();
					try {
						const result = await client.execute(code, lang);
						const formatted = client.formatOutput(result);
						outputEl.textContent = formatted.text;
						outputEl.classList.toggle("is-error", formatted.isError);
					} catch (e) {
						outputEl.textContent = "Error: " + (e as Error).message;
						outputEl.classList.add("is-error");
					}
				});

				wrapper.appendChild(btn);
			});
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
			text: "JavaScript/TypeScript runs locally. All other languages run remotely via Wandbox.",
			cls: "setting-item-description",
		});

		new Setting(containerEl)
			.setName("Show run button")
			.setDesc("Display a run button on supported code blocks.")
			.addToggle((toggle) =>
				toggle.setValue(this.plugin.settings.showRunButton).onChange(async (value) => {
					this.plugin.settings.showRunButton = value;
					await this.plugin.saveSettings();
				})
			);

		new Setting(containerEl)
			.setName("Enabled languages")
			.setDesc("Comma-separated list of languages that should show the run button.")
			.addTextArea((text) => {
				text.setValue(this.plugin.settings.enabledLanguages.join(", ")).onChange(async (value) => {
					this.plugin.settings.enabledLanguages = value
						.split(",")
						.map((s) => s.trim().toLowerCase())
						.filter((s) => s.length > 0);
					await this.plugin.saveSettings();
				});
			});

		const supportedLangs = Object.keys(WANDBOX_LANGUAGE_MAP).sort().join(", ");
		containerEl.createEl("p", {
			text: `Wandbox supported languages: ${supportedLangs}`,
			cls: "setting-item-description",
		});
	}
}
