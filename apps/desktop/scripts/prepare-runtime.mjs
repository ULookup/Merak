import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const desktopDir = resolve(scriptDir, '..');
const repoRoot = resolve(desktopDir, '../..');
const resourcesDir = resolve(desktopDir, 'resources');
const target = resolve(resourcesDir, 'merak.exe');

const candidates = [
  process.env.MERAK_RUNTIME_EXE,
  resolve(repoRoot, 'build/cli/Release/merak.exe'),
  resolve(repoRoot, 'build/cli/Debug/merak.exe'),
  resolve(repoRoot, 'build/cli/merak.exe'),
  resolve(repoRoot, 'Release/merak.exe'),
  resolve(repoRoot, 'Debug/merak.exe'),
].filter(Boolean);

if (existsSync(target)) {
  console.log(`Merak runtime already prepared: ${target}`);
  process.exit(0);
}

const source = candidates.find((candidate) => existsSync(candidate));

if (!source) {
  console.error(
    [
      'Merak desktop build needs the local runtime executable.',
      '',
      'Build the C++ CLI first, or set MERAK_RUNTIME_EXE to an existing merak.exe.',
      'Checked:',
      ...candidates.map((candidate) => `- ${candidate}`),
    ].join('\n'),
  );
  process.exit(1);
}

mkdirSync(resourcesDir, { recursive: true });
copyFileSync(source, target);
console.log(`Prepared Merak runtime: ${source} -> ${target}`);
