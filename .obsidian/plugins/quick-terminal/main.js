const { Plugin, PluginSettingTab, Setting, Notice } = require('obsidian');
const { spawn } = require('child_process');
const path = require('path');

const DEFAULT_SETTINGS = {
    terminalPath: 'C:\\Program Files\\PowerShell\\7\\pwsh.exe',
    terminalArgs: '',
    workingDirMode: 'note' // 'note' | 'vault'
};

class QuickTerminalPlugin extends Plugin {
    async onload() {
        await this.loadSettings();

        // 添加侧边栏按钮
        this.addRibbonIcon('terminal-square', '打开外部终端', (evt) => {
            this.openTerminal();
        });

        // 添加命令
        this.addCommand({
            id: 'open-external-terminal',
            name: '打开外部终端',
            callback: () => {
                this.openTerminal();
            }
        });

        // 添加设置面板
        this.addSettingTab(new QuickTerminalSettingTab(this.app, this));
    }

    async loadSettings() {
        this.settings = Object.assign({}, DEFAULT_SETTINGS, await this.loadData());
    }

    async saveSettings() {
        await this.saveData(this.settings);
    }

    openTerminal() {
        const activeFile = this.app.workspace.getActiveFile();
        
        let workingDir;
        if (this.settings.workingDirMode === 'note' && activeFile) {
            workingDir = path.dirname(activeFile.path);
        } else {
            workingDir = this.app.vault.adapter.basePath;
        }

        const terminalPath = this.settings.terminalPath;
        let args = [];
        
        if (this.settings.terminalArgs) {
            args = this.settings.terminalArgs.split(' ').filter(arg => arg.length > 0);
        }

        const isWindows = process.platform === 'win32';

        try {
            let child;
            if (isWindows) {
                // Windows: Electron 是 GUI 无控制台，detached:true 会导致子进程也弹不出窗口
                // 用 cmd /c start 强制创建独立控制台窗口
                child = spawn('cmd', ['/c', 'start', '', terminalPath, ...args], {
                    cwd: workingDir,
                    stdio: 'ignore',
                    windowsHide: false
                });
            } else {
                child = spawn(terminalPath, args, {
                    cwd: workingDir,
                    detached: true,
                    stdio: 'ignore'
                });
            }

            child.on('spawn', () => {
                new Notice('外部终端已打开');
            });

            child.on('error', (err) => {
                new Notice(`打开终端失败: ${err.message}`);
                console.error('Terminal error:', err);
            });

            // 解除引用，让终端在 Obsidian 关闭后继续运行
            child.unref();
        } catch (err) {
            new Notice(`打开终端失败: ${err.message}`);
            console.error('Failed to open terminal:', err);
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

        containerEl.createEl('h2', { text: 'Quick Terminal 设置' });

        // 终端路径设置
        new Setting(containerEl)
            .setName('终端路径')
            .setDesc('外部终端的可执行文件路径')
            .addText(text => text
                .setPlaceholder('例如: C:\\Program Files\\PowerShell\\7\\pwsh.exe')
                .setValue(this.plugin.settings.terminalPath)
                .onChange(async (value) => {
                    this.plugin.settings.terminalPath = value;
                    await this.plugin.saveSettings();
                }));

        // 终端参数设置
        new Setting(containerEl)
            .setName('启动参数')
            .setDesc('传递给终端的参数（用空格分隔）')
            .addText(text => text
                .setPlaceholder('例如: -NoExit')
                .setValue(this.plugin.settings.terminalArgs)
                .onChange(async (value) => {
                    this.plugin.settings.terminalArgs = value;
                    await this.plugin.saveSettings();
                }));

        // 工作目录模式
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

        // 常用终端路径提示
        containerEl.createEl('h3', { text: '常用终端路径参考', cls: 'setting-item-heading' });
        
        const pathsList = containerEl.createEl('ul');
        pathsList.createEl('li', { text: 'PowerShell 7: C:\\Program Files\\PowerShell\\7\\pwsh.exe' });
        pathsList.createEl('li', { text: 'PowerShell 5.1: C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe' });
        pathsList.createEl('li', { text: 'CMD: C:\\Windows\\System32\\cmd.exe' });
        pathsList.createEl('li', { text: 'Windows Terminal: wt.exe' });
    }
}

module.exports = QuickTerminalPlugin;
