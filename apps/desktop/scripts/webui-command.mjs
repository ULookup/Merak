import { spawn } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const command = process.argv[2];
const allowed = new Set(['dev', 'build']);

if (!allowed.has(command)) {
  console.error(`Usage: node scripts/webui-command.mjs <${[...allowed].join('|')}>`);
  process.exit(1);
}

const scriptDir = dirname(fileURLToPath(import.meta.url));
const webuiDir = resolve(scriptDir, '../../../webui');
const npmCommand = process.platform === 'win32' ? 'npm.cmd' : 'npm';
const args = command === 'dev' ? ['run', 'dev', '--', '--host', '127.0.0.1'] : ['run', 'build'];

const child = spawn(npmCommand, args, {
  cwd: webuiDir,
  stdio: 'inherit',
  env: process.env,
});

child.on('exit', (code, signal) => {
  if (signal) {
    process.kill(process.pid, signal);
    return;
  }
  process.exit(code ?? 1);
});
