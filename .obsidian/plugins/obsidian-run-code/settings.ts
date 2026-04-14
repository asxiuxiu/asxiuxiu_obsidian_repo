export interface RunCodeSettings {
	enabledLanguages: string[];
	showRunButton: boolean;
}

export const DEFAULT_SETTINGS: RunCodeSettings = {
	enabledLanguages: [
		"c", "cpp", "python", "python3", "javascript", "js", "typescript", "ts",
		"java", "go", "rust", "ruby", "php", "kotlin", "swift", "scala",
		"csharp", "cs", "lua", "perl", "haskell", "erlang", "elixir",
		"d", "nim", "crystal", "julia", "ocaml", "fsharp", "fs",
		"bash", "sh", "shell", "coffeescript", "groovy", "vb", "zig"
	],
	showRunButton: true,
};
