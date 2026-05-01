const { Plugin, PluginSettingTab, Setting, Notice } = require('obsidian');
const { spawn } = require('child_process');
const path = require('path');

function getDefaultSettings() {
    const isWindows = process.platform === 'win32';
    const isMac = process.platform === 'darwin';
    return {
        terminalPath: isWindows ? 'C:\\Program Files\\PowerShell\\7\\pwsh.exe' : '',
        terminalArgs: '',
        workingDirMode: 'note',
        macosTerminalApp: isMac ? 'Terminal' : 'custom',
    };
}

function parseArgs(argsStr) {
    if (!argsStr || !argsStr.trim()) return [];
    const args = [];
    let current = '';
    let inQuotes = false;
    let quoteChar = null;

    for (let i = 0; i < argsStr.length; i++) {
        const char = argsStr[i];

        if (!inQuotes && (char === '"' || char === "'")) {
            inQuotes = true;
            quoteChar = char;
        } else if (inQuotes && char === quoteChar) {
            inQuotes = false;
            quoteChar = null;
        } else if (!inQuotes && /\s/.test(char)) {
            if (current.length > 0) {
                args.push(current);
                current = '';
            }
        } else {
            current += char;
        }
    }

    if (current.length > 0) {
        args.push(current);
    }

    return args;
}

class QuickTerminalPlugin extends Plugin {
    async onload() {
        await this.loadSettings();

        this.addRibbonIcon('terminal-square', '打开外部终端', (evt) => {
            this.openTerminal();
        });

        this.addCommand({
            id: 'open-external-terminal',
            name: '打开外部终端',
            callback: () => {
                this.openTerminal();
            }
        });

        this.addSettingTab(new QuickTerminalSettingTab(this.app, this));
    }

    async loadSettings() {
        const defaults = getDefaultSettings();
        const loaded = await this.loadData();
        this.settings = Object.assign({}, defaults, loaded);
    }

    async saveSettings() {
        await this.saveData(this.settings);
    }

    openTerminal() {
        const activeFile = this.app.workspace.getActiveFile();
        const basePath = this.app.vault.adapter.basePath;

        let workingDir;
        if (this.settings.workingDirMode === 'note' && activeFile) {
            workingDir = path.resolve(basePath, path.dirname(activeFile.path));
        } else {
            workingDir = basePath;
        }

        const isWindows = process.platform === 'win32';
        const isMac = process.platform === 'darwin';

        const handleError = (err) => {
            new Notice(`打开终端失败: ${err.message || err}`);
            console.error('Terminal error:', err);
        };

        const handleSuccess = () => {
            new Notice('外部终端已打开');
        };

        try {
            if (isWindows) {
                const terminalPath = this.settings.terminalPath || 'cmd';
                const args = parseArgs(this.settings.terminalArgs);
                const child = spawn('cmd', ['/c', 'start', '', terminalPath, ...args], {
                    cwd: workingDir,
                    stdio: 'ignore',
                    windowsHide: false
                });
                child.on('error', handleError);
                child.on('spawn', handleSuccess);
                child.unref();
            } else if (isMac) {
                const terminalApp = this.settings.macosTerminalApp || 'Terminal';
                const escapedDir = workingDir.replace(/"/g, '\\"');

                if (terminalApp === 'custom' && this.settings.terminalPath) {
                    const args = parseArgs(this.settings.terminalArgs);
                    const child = spawn(this.settings.terminalPath, args, {
                        cwd: workingDir,
                        detached: true,
                        stdio: 'ignore'
                    });
                    child.on('error', handleError);
                    child.on('spawn', handleSuccess);
                    child.unref();
                } else if (terminalApp === 'iTerm') {
                    // 方案A: 纯 iTerm2 AppleScript（无弹框）
                    const appleScriptArgsA = [
                        '-e', 'tell application "iTerm"',
                        '-e', 'activate',
                        '-e', 'try',
                        '-e', 'tell current window',
                        '-e', 'create tab with default profile',
                        '-e', 'tell current session',
                        '-e', `write text "cd \\"${escapedDir}\\""`,
                        '-e', 'end tell',
                        '-e', 'end tell',
                        '-e', 'on error',
                        '-e', 'create window with default profile',
                        '-e', 'tell current session of current window',
                        '-e', `write text "cd \\"${escapedDir}\\""`,
                        '-e', 'end tell',
                        '-e', 'end try',
                        '-e', 'end tell'
                    ];

                    const childA = spawn('/usr/bin/osascript', appleScriptArgsA, {
                        detached: true,
                        stdio: ['ignore', 'pipe', 'pipe']
                    });

                    let stderrA = '';
                    childA.stderr.on('data', (data) => { stderrA += data.toString(); });

                    childA.on('close', (codeA) => {
                        if (codeA === 0) {
                            handleSuccess();
                            return;
                        }
                        console.error('[QuickTerminal] iTerm AppleScript failed:', stderrA);

                        // 方案B: System Events 模拟 Cmd+T
                        const appleScriptArgsB = [
                            '-e', 'tell application "iTerm" to activate',
                            '-e', 'delay 0.3',
                            '-e', 'tell application "System Events"',
                            '-e', 'keystroke "t" using command down',
                            '-e', 'end tell',
                            '-e', 'tell application "iTerm"',
                            '-e', 'tell current window',
                            '-e', 'tell current session',
                            '-e', `write text "cd \\"${escapedDir}\\""`,
                            '-e', 'end tell',
                            '-e', 'end tell',
                            '-e', 'end tell'
                        ];

                        const childB = spawn('/usr/bin/osascript', appleScriptArgsB, {
                            detached: true,
                            stdio: ['ignore', 'pipe', 'pipe']
                        });

                        let stderrB = '';
                        childB.stderr.on('data', (data) => { stderrB += data.toString(); });

                        childB.on('close', (codeB) => {
                            if (codeB === 0) {
                                handleSuccess();
                            } else {
                                console.error('[QuickTerminal] System Events fallback failed:', stderrB);
                                // 方案C: open 直接打开
                                const fallback = spawn('/usr/bin/open', ['-na', 'iTerm', workingDir], {
                                    detached: true,
                                    stdio: 'ignore'
                                });
                                fallback.on('error', handleError);
                                fallback.on('spawn', () => {
                                    new Notice('iTerm2 已打开（AppleScript 不可用）');
                                });
                                fallback.unref();
                            }
                        });

                        childB.on('error', (err) => {
                            console.error('[QuickTerminal] System Events error:', err);
                            const fallback = spawn('/usr/bin/open', ['-na', 'iTerm', workingDir], {
                                detached: true,
                                stdio: 'ignore'
                            });
                            fallback.on('error', handleError);
                            fallback.on('spawn', () => {
                                new Notice('iTerm2 已打开（AppleScript 不可用）');
                            });
                            fallback.unref();
                        });

                        childB.unref();
                    });

                    childA.on('error', (err) => {
                        console.error('[QuickTerminal] iTerm AppleScript error:', err);
                        // 直接走方案B的逻辑（这里简化处理，走 open 回退）
                        const fallback = spawn('/usr/bin/open', ['-na', 'iTerm', workingDir], {
                            detached: true,
                            stdio: 'ignore'
                        });
                        fallback.on('error', handleError);
                        fallback.on('spawn', () => {
                            new Notice('iTerm2 已打开（AppleScript 不可用）');
                        });
                        fallback.unref();
                    });

                    childA.unref();
                } else {
                    // Terminal.app
                    const appleScriptArgs = [
                        '-e', 'tell application "Terminal"',
                        '-e', 'activate',
                        '-e', 'if (count of windows) = 0 then',
                        '-e', `do script "cd \\"${escapedDir}\\""`,
                        '-e', 'else',
                        '-e', `do script "cd \\"${escapedDir}\\"" in front window`,
                        '-e', 'end if',
                        '-e', 'end tell'
                    ];

                    const child = spawn('/usr/bin/osascript', appleScriptArgs, {
                        detached: true,
                        stdio: ['ignore', 'pipe', 'pipe']
                    });

                    let stderr = '';
                    child.stderr.on('data', (data) => { stderr += data.toString(); });

                    child.on('close', (code) => {
                        if (code !== 0) {
                            const msg = stderr || `osascript exited with code ${code}`;
                            new Notice(`终端脚本失败: ${msg}`);
                            console.error('[QuickTerminal] osascript stderr:', stderr);
                        } else {
                            handleSuccess();
                        }
                    });

                    child.on('error', handleError);
                    child.unref();
                }
            } else {
                const terminalPath = this.settings.terminalPath || 'xterm';
                const args = parseArgs(this.settings.terminalArgs);
                const child = spawn(terminalPath, args, {
                    cwd: workingDir,
                    detached: true,
                    stdio: 'ignore'
                });
                child.on('error', handleError);
                child.on('spawn', handleSuccess);
                child.unref();
            }
        } catch (err) {
            handleError(err);
        }
    }
}

class QuickTerminalSettingTab extends PluginSettingTab {
    constructor(app, plugin) {
        super(app, plugin);
        this.plugin = plugin;
    }

    display() {
        const { containerEl } = this;
        containerEl.empty();
        const isMac = process.platform === 'darwin';
        const isWindows = process.platform === 'win32';

        containerEl.createEl('h2', { text: 'Quick Terminal 设置' });

        if (isMac) {
            new Setting(containerEl)
                .setName('终端应用')
                .setDesc('选择要打开的 macOS 终端应用')
                .addDropdown(dropdown => dropdown
                    .addOption('Terminal', 'Terminal.app')
                    .addOption('iTerm', 'iTerm2')
                    .addOption('custom', '自定义路径')
                    .setValue(this.plugin.settings.macosTerminalApp)
                    .onChange(async (value) => {
                        this.plugin.settings.macosTerminalApp = value;
                        await this.plugin.saveSettings();
                        this.display();
                    }));
        }

        const showCustomPath = !isMac || this.plugin.settings.macosTerminalApp === 'custom';

        if (showCustomPath) {
            new Setting(containerEl)
                .setName('终端路径')
                .setDesc('外部终端的可执行文件路径')
                .addText(text => text
                    .setPlaceholder(isWindows ? '例如: C:\\Program Files\\PowerShell\\7\\pwsh.exe' : '例如: /usr/bin/open')
                    .setValue(this.plugin.settings.terminalPath)
                    .onChange(async (value) => {
                        this.plugin.settings.terminalPath = value;
                        await this.plugin.saveSettings();
                    }));
        }

        new Setting(containerEl)
            .setName('启动参数')
            .setDesc('传递给终端的参数（支持引号包裹含空格的参数）')
            .addText(text => text
                .setPlaceholder('例如: -NoExit')
                .setValue(this.plugin.settings.terminalArgs)
                .onChange(async (value) => {
                    this.plugin.settings.terminalArgs = value;
                    await this.plugin.saveSettings();
                }));

        new Setting(containerEl)
            .setName('工作目录')
            .setDesc('终端启动时的工作目录')
            .addDropdown(dropdown => dropdown
                .addOption('note', '当前笔记所在目录')
                .addOption('vault', 'Vault 根目录')
                .setValue(this.plugin.settings.workingDirMode)
                .onChange(async (value) => {
                    this.plugin.settings.workingDirMode = value;
                    await this.plugin.saveSettings();
                }));

        containerEl.createEl('h3', { text: '使用注意事项', cls: 'setting-item-heading' });
        const noticeList = containerEl.createEl('ul');

        if (isWindows) {
            noticeList.createEl('li', { text: '默认使用 PowerShell 7 路径，若未安装请改为 PowerShell 5.1 或 CMD' });
            noticeList.createEl('li', { text: 'Windows Terminal (wt.exe) 需确保已加入系统 PATH' });
            noticeList.createEl('li', { text: '启动参数用空格分隔，含空格的值用引号包裹' });
        } else if (isMac) {
            noticeList.createEl('li', { text: 'Terminal.app / iTerm2 为内置支持，无需填写路径' });
            noticeList.createEl('li', { text: 'iTerm2 首次使用可能提示「自动化」权限，请在弹框中点击「允许」' });
            noticeList.createEl('li', { text: '若 iTerm2 弹辅助功能权限框，请前往 系统设置 → 隐私与安全性 → 辅助功能 → 勾选 Obsidian' });
            noticeList.createEl('li', { text: '自定义路径请填写终端可执行文件的完整路径（如 /usr/bin/open）' });
        } else {
            noticeList.createEl('li', { text: 'Linux 需手动填写终端路径，默认回退到 xterm' });
            noticeList.createEl('li', { text: '启动参数用空格分隔，含空格的值用引号包裹' });
        }

        containerEl.createEl('h3', { text: '常用终端路径参考', cls: 'setting-item-heading' });
        const pathsList = containerEl.createEl('ul');

        if (isWindows) {
            pathsList.createEl('li', { text: 'PowerShell 7: C:\\Program Files\\PowerShell\\7\\pwsh.exe' });
            pathsList.createEl('li', { text: 'PowerShell 5.1: C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' });
            pathsList.createEl('li', { text: 'CMD: C:\\Windows\\System32\\cmd.exe' });
            pathsList.createEl('li', { text: 'Windows Terminal: wt.exe' });
        } else if (isMac) {
            pathsList.createEl('li', { text: 'Terminal.app: 已内置支持，无需填写路径' });
            pathsList.createEl('li', { text: 'iTerm2: 已内置支持，无需填写路径' });
            pathsList.createEl('li', { text: '自定义: 填写可执行文件完整路径' });
        } else {
            pathsList.createEl('li', { text: 'GNOME Terminal: /usr/bin/gnome-terminal' });
            pathsList.createEl('li', { text: 'Konsole: /usr/bin/konsole' });
            pathsList.createEl('li', { text: 'xterm: /usr/bin/xterm' });
        }
    }
}

module.exports = QuickTerminalPlugin;
