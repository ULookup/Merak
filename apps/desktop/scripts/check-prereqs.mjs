import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const desktopDir = resolve(scriptDir, '..');
const repoRoot = resolve(desktopDir, '../..');
const resourcesDir = resolve(desktopDir, 'resources');

function commandExists(command, args = ['--version']) {
  const result = spawnSync(command, args, { stdio: 'ignore', shell: true });
  return result.status === 0;
}

const runtimeCandidates = [
  process.env.MERAK_RUNTIME_EXE,
  resolve(resourcesDir, 'merak.exe'),
  resolve(repoRoot, 'build/cli/Release/merak.exe'),
  resolve(repoRoot, 'build/cli/Debug/merak.exe'),
  resolve(repoRoot, 'build/cli/merak.exe'),
  resolve(repoRoot, 'Release/merak.exe'),
  resolve(repoRoot, 'Debug/merak.exe'),
].filter(Boolean);

const missing = [];

if (!commandExists('cargo')) {
  missing.push({
    title: 'Rust/Cargo is not available',
    fix: 'Install Rust, then reopen the terminal so `cargo --version` works.',
  });
}

if (!runtimeCandidates.some((candidate) => existsSync(candidate))) {
  missing.push({
    title: 'Merak runtime executable was not found',
    fix: 'Build the C++ CLI first, or set MERAK_RUNTIME_EXE to an existing merak.exe.',
  });
}

if (missing.length > 0) {
  console.error('Merak desktop build is missing required local prerequisites.\n');
  for (const item of missing) {
    console.error(`- ${item.title}`);
    console.error(`  ${item.fix}`);
  }
  console.error('\nRuntime locations checked:');
  for (const candidate of runtimeCandidates) {
    console.error(`- ${candidate}`);
  }
  process.exit(1);
}

console.log('Merak desktop prerequisites look ready.');
