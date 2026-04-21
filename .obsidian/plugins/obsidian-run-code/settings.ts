export interface RunCodeSettings {
	showRunButton: boolean;
	workspacePath: string;
}

export const DEFAULT_SETTINGS: RunCodeSettings = {
	showRunButton: true,
	workspacePath: "",
};
