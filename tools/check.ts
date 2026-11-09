import { readFileSync } from 'node:fs';
import { decodeBatch, decodeResults } from './format';
import type { Result } from './format';
import { MODELS, runOnSageV } from './sagev';
import { mulberry32 } from '../sagev/src/tutor/rand';

const arg = (name: string, fallback?: string) => {
  const i = process.argv.indexOf(`--${name}`);
  if (i > 0) return process.argv[i + 1];
  if (fallback === undefined) throw new Error(`missing --${name}`);
  return fallback;
};

const model = arg('model');
const programs = decodeBatch(readFileSync(arg('batch')));
const results = decodeResults(readFileSync(arg('results')));
const expected = decodeResults(readFileSync(arg('expected')));
const sample = Number(arg('sample', '500'));

const FIELDS = ['pc', 'cycles', 'retired', 'stalls', 'flushes', 'status'] as const;

function differences(got: Result, want: Result): string[] {
  const out: string[] = [];
  for (let r = 0; r < 32; r++) if (got.registers[r] !== want.registers[r]) out.push(`x${r} ${got.registers[r]} != ${want.registers[r]}`);
  for (let a = 0; a < 256; a++) if (got.memory[a] !== want.memory[a]) { out.push(`mem[0x${a.toString(16)}] ${got.memory[a]} != ${want.memory[a]}`); break; }
  for (const f of FIELDS) if (got[f] !== want[f]) out.push(`${f} ${got[f]} != ${want[f]}`);
  return out;
}

let mismatches = 0;
let failedFlags = 0;
results.forEach((r, i) => {
  const want = expected[i % expected.length];
  const diff = differences(r, want);
  if (r.pass !== 1) failedFlags++;
  if (diff.length > 0) {
    mismatches++;
    if (mismatches <= 10) console.log(`program ${i}: ${diff.slice(0, 4).join('; ')}`);
  }
  if ((r.pass === 1) !== (diff.length === 0)) {
    mismatches++;
    if (mismatches <= 10) console.log(`program ${i}: pass flag ${r.pass} disagrees with the comparison`);
  }
});

const rand = mulberry32(7);
let resampled = 0;
for (let k = 0; k < Math.min(sample, results.length); k++) {
  const i = Math.floor(rand() * results.length);
  const diff = differences(results[i], runOnSageV(programs[i % programs.length], MODELS[model]));
  if (diff.length > 0) {
    resampled++;
    if (resampled <= 5) console.log(`resample ${i}: ${diff.slice(0, 4).join('; ')}`);
  }
}

console.log(`${model}: ${results.length} results, ${mismatches} mismatches against SageV's expected results, ${failedFlags} pass flags false, `
  + `${resampled} mismatches in ${Math.min(sample, results.length)} programs re-run on SageV's core now`);
process.exit(mismatches + failedFlags + resampled === 0 ? 0 : 1);
