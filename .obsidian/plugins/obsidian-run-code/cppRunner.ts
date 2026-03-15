import { execFile, ExecFileOptions } from "child_process";
import { promisify } from "util";
import * as fs from "fs";
import * as path from "path";

const execFileAsync = promisify(execFile);

export interface CppRunResult {
	text: string;
	isError: boolean;
}

export interface CompileFlags {
	flags: string[];
	isOverride: boolean;
}

const DEFAULT_FLAGS = ["-std=c++20", "-Wall", "-O2"];

function extractFlags(code: string): CompileFlags {
	const lines = code.split(/\r?\n/);
	for (const line of lines.slice(0, 20)) {
		const trimmed = line.trim();
		const match = trimmed.match(/^\/\/\s*flags:\s*(.*)$/);
		if (match) {
			const rest = match[1].trim();
			if (rest.startsWith("override ")) {
				return {
					flags: rest.slice("override ".length).trim().split(/\s+/).filter(Boolean),
					isOverride: true,
				};
			}
			return {
				flags: rest.split(/\s+/).filter(Boolean),
				isOverride: false,
			};
		}
	}
	return { flags: [], isOverride: false };
}

function buildArgs(srcFile: string, exeFile: string, flags: CompileFlags): string[] {
	if (flags.isOverride) {
		return [srcFile, "-o", exeFile, ...flags.flags];
	}
	return [srcFile, "-o", exeFile, ...DEFAULT_FLAGS, ...flags.flags];
}

let cachedGppPath: string | null = null;

function findGpp(): string {
	if (cachedGppPath) return cachedGppPath;

	const candidates = [
		"g++",
		"C:\\msys64\\ucrt64\\bin\\g++.exe",
		"C:\\msys64\\mingw64\\bin\\g++.exe",
		"C:\\mingw64\\bin\\g++.exe",
	];

	for (const c of candidates) {
		if (c === "g++") {
			try {
				const { execSync } = require("child_process");
				execSync("g++ --version", { stdio: "ignore" });
				cachedGppPath = c;
				return c;
			} catch {
				// ignore
			}
		} else if (fs.existsSync(c)) {
			cachedGppPath = c;
			return c;
		}
	}

	cachedGppPath = "g++";
	return cachedGppPath;
}

export function getDefaultWorkspacePath(vaultBasePath: string): string {
	return path.join(vaultBasePath, "workspace", "run-code");
}

export async function runCpp(code: string, workspacePath: string): Promise<CppRunResult> {
	if (!fs.existsSync(workspacePath)) {
		fs.mkdirSync(workspacePath, { recursive: true });
	}

	const timestamp = Date.now();
	const srcFile = path.join(workspacePath, `tmp_${timestamp}.cpp`);
	const exeFile = path.join(workspacePath, `tmp_${timestamp}.exe`);

	fs.writeFileSync(srcFile, code, "utf-8");

	const gpp = findGpp();
	const compileFlags = extractFlags(code);
	const args = buildArgs(srcFile, exeFile, compileFlags);

	try {
		// Compile
		try {
			await execFileAsync(gpp, args, {
				cwd: workspacePath,
				timeout: 30000,
				windowsHide: true,
			} as ExecFileOptions);
		} catch (compileErr: any) {
			const stderr = compileErr.stderr || compileErr.message || "";
			return { text: `[Compile Error]\n${stderr}`, isError: true };
		}

		// Run
		try {
			const { stdout, stderr } = await execFileAsync(exeFile, [], {
				cwd: workspacePath,
				timeout: 10000,
				windowsHide: true,
			} as ExecFileOptions);
			let output = stdout || "";
			if (stderr) {
				output += (output ? "\n" : "") + "[Runtime Error]\n" + stderr;
			}
			if (!output.trim()) output = "(no output)";
			return { text: output.trimEnd(), isError: false };
		} catch (runErr: any) {
			let output = runErr.stdout || "";
			const stderr = runErr.stderr || "";
			if (stderr) output += (output ? "\n" : "") + "[Runtime Error]\n" + stderr;
			if (runErr.signal === "SIGTERM" || runErr.killed) {
				output += (output ? "\n" : "") + "(killed: timeout)";
			}
			if (!output.trim()) output = runErr.message || "(execution failed)";
			return { text: output.trimEnd(), isError: true };
		}
	} finally {
		try { if (fs.existsSync(srcFile)) fs.unlinkSync(srcFile); } catch {}
		try { if (fs.existsSync(exeFile)) fs.unlinkSync(exeFile); } catch {}
	}
}
