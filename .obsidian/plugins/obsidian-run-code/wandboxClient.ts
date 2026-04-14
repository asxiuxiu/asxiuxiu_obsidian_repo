import { requestUrl } from "obsidian";

export interface WandboxCompileResult {
	status: string;
	signal: string;
	compiler_output: string;
	compiler_error: string;
	program_output: string;
	program_error: string;
	permlink: string;
	url: string;
}

export const WANDBOX_LANGUAGE_MAP: Record<string, { compiler: string; options?: string }> = {
	// C / C++
	c: { compiler: "gcc-13.2.0", options: "-std=c17 -Wall -O2" },
	cpp: { compiler: "gcc-13.2.0", options: "-std=c++20 -Wall -O2" },
	"c++": { compiler: "gcc-13.2.0", options: "-std=c++20 -Wall -O2" },

	// Systems
	rust: { compiler: "rust-1.73.0" },
	go: { compiler: "go-1.21.3" },
	zig: { compiler: "zig-0.11.0" },
	d: { compiler: "dmd-2.105.0" },
	nim: { compiler: "nim-2.0.0" },
	crystal: { compiler: "crystal-1.10.1" },

	// JVM
	java: { compiler: "openjdk-jdk-21" },
	kotlin: { compiler: "kotlin-1.9.10" },
	scala: { compiler: "scala-3.3.1" },
	groovy: { compiler: "groovy-4.0.15" },

	// .NET
	csharp: { compiler: "dotnet-7.0.401" },
	cs: { compiler: "dotnet-7.0.401" },
	fsharp: { compiler: "fsharp-7.0.401" },
	fs: { compiler: "fsharp-7.0.401" },
	vb: { compiler: "vbc-7.0.401" },

	// Scripting
	python: { compiler: "cpython-3.11.4" },
	python3: { compiler: "cpython-3.11.4" },
	ruby: { compiler: "ruby-3.2.2" },
	php: { compiler: "php-8.2.10" },
	lua: { compiler: "lua-5.4.6" },
	perl: { compiler: "perl-5.38.0" },
	bash: { compiler: "bash" },
	sh: { compiler: "bash" },
	shell: { compiler: "bash" },

	// Functional
	haskell: { compiler: "ghc-9.4.6" },
	erlang: { compiler: "erlang-26.1" },
	elixir: { compiler: "elixir-1.15.6" },
	ocaml: { compiler: "ocaml-4.14.2" },

	// Other
	swift: { compiler: "swift-5.9" },
	julia: { compiler: "julia-1.9.3" },
	coffeescript: { compiler: "coffeescript-2.7.0" },

	// JS/TS are handled locally, but we keep them here for completeness if we ever want remote
	// typescript: { compiler: "typescript-5.2.2" },
	// javascript: { compiler: "nodejs-20.8.0" },
};

export class WandboxClient {
	resolveCompiler(lang: string): { compiler: string; options?: string } | null {
		const lower = lang.toLowerCase().trim();
		return WANDBOX_LANGUAGE_MAP[lower] ?? null;
	}

	async execute(code: string, lang: string): Promise<WandboxCompileResult> {
		const mapped = this.resolveCompiler(lang);
		if (!mapped) {
			throw new Error(`Language "${lang}" is not supported by Wandbox backend.`);
		}

		const body: Record<string, unknown> = {
			code,
			compiler: mapped.compiler,
			save: false,
		};

		if (mapped.options) {
			body.options = mapped.options;
		}

		const response = await requestUrl({
			url: "https://wandbox.org/api/compile.json",
			method: "POST",
			headers: {
				"Content-Type": "application/json",
			},
			body: JSON.stringify(body),
		});

		if (response.status !== 200) {
			throw new Error(`Wandbox API returned ${response.status}: ${response.text}`);
		}

		return response.json as WandboxCompileResult;
	}

	formatOutput(result: WandboxCompileResult): { text: string; isError: boolean } {
		let output = "";
		let isError = false;

		if (result.compiler_output) {
			output += "[Compiler Output]\n" + result.compiler_output + "\n\n";
		}
		if (result.compiler_error) {
			output += "[Compiler Error]\n" + result.compiler_error + "\n\n";
			isError = true;
		}
		if (result.program_output) {
			output += result.program_output;
			if (!result.program_output.endsWith("\n")) {
				output += "\n";
			}
		}
		if (result.program_error) {
			output += "[Runtime Error]\n" + result.program_error + "\n";
			isError = true;
		}
		if (result.status && result.status !== "0" && result.status !== "") {
			isError = true;
		}

		if (!output.trim()) {
			output = "(no output)";
		}

		return { text: output.trimEnd(), isError };
	}
}
