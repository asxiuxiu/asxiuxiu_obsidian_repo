export interface LocalRunResult {
	text: string;
	isError: boolean;
}

export function canRunLocally(lang: string): boolean {
	const lower = lang.toLowerCase().trim();
	return ["javascript", "js", "typescript", "ts"].includes(lower);
}

export function runLocal(code: string): LocalRunResult {
	const logs: string[] = [];
	const errors: string[] = [];

	const originalLog = console.log;
	const originalError = console.error;

	console.log = (...args: unknown[]) => {
		logs.push(args.map((a) => String(a)).join(" "));
	};
	console.error = (...args: unknown[]) => {
		errors.push(args.map((a) => String(a)).join(" "));
	};

	let isError = false;
	try {
		const fn = new Function(code);
		const result = fn();
		if (result !== undefined) {
			logs.push(String(result));
		}
	} catch (e) {
		errors.push((e as Error).message || String(e));
		isError = true;
	} finally {
		console.log = originalLog;
		console.error = originalError;
	}

	let output = logs.join("\n");
	if (errors.length) {
		output += (output ? "\n" : "") + errors.join("\n");
		isError = true;
	}

	if (!output.trim()) {
		output = "(no output)";
	}

	return { text: output.trimEnd(), isError };
}
