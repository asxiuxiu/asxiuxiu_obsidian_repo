import { execFile, ExecFileOptions } from "child_process";
import { promisify } from "util";
import * as fs from "fs";
import * as path from "path";

const execFileAsync = promisify(execFile);

export interface CppRunResult {
	text: string;
	isError: boolean;
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

	try {
		// Compile
		try {
			await execFileAsync(gpp, [srcFile, "-o", exeFile, "-std=c++20", "-Wall", "-O2"], {
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
