export interface RunCodeSettings {
	showRunButton: boolean;
	workspacePath: string;
	moonshotApiKey: string;
}

export const DEFAULT_SETTINGS: RunCodeSettings = {
	showRunButton: true,
	workspacePath: "",
	moonshotApiKey: "",
};
