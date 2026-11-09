import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { decodeResults, encodeResults } from './format';

const arg = (name: string, fallback: string) => {
  const i = process.argv.indexOf(`--${name}`);
  return i > 0 ? process.argv[i + 1] : fallback;
};

const engine = arg('engine-exe', 'build/sagev-batch.exe');
const kind = arg('engine', 'hip');
const batchPath = arg('batch', 'data/batch.bin');
const expectedPath = arg('expected', 'data/expected_p0.bin');
const expected = decodeResults(readFileSync(expectedPath));

const edits: [string, (i: number) => void][] = [
  ['x5', i => { expected[i].registers[5] ^= 1; }],
  ['x31', i => { expected[i].registers[31] += 1; }],
  ['mem[0x00]', i => { expected[i].memory[0] ^= 0x80; }],
  ['mem[0xFF]', i => { expected[i].memory[255] ^= 1; }],
  ['pc', i => { expected[i].pc += 4; }],
  ['cycles', i => { expected[i].cycles += 1; }],
  ['retired', i => { expected[i].retired += 1; }],
  ['status', i => { expected[i].status ^= 3; }],
];
const step = Math.floor(expected.length / (edits.length + 1));
const corrupted = edits.map(([name, edit], k) => {
  const i = step * (k + 1) + k;
  edit(i);
  return { name, i };
});
writeFileSync('data/expected_bad.bin', encodeResults(expected));

try {
  execFileSync(engine, ['run', '--model', 'p0', '--engine', kind, '--batch', batchPath, '--expected', 'data/expected_bad.bin', '--out', 'data/result_bad.bin'], { stdio: 'pipe' });
} catch {}
const results = decodeResults(readFileSync('data/result_bad.bin'));
const failed = results.flatMap((r, i) => (r.pass === 1 ? [] : [i]));
const want = corrupted.map(c => c.i);
const ok = failed.length === want.length && failed.every((i, k) => i === want[k]);
for (const c of corrupted) console.log(`${c.name.padEnd(10)} program ${c.i}: ${results[c.i].pass === 0 ? 'flagged' : 'MISSED'}`);
console.log(ok ? `${kind}: exactly the ${want.length} corrupted programs were flagged, out of ${results.length}` : `${kind}: flagged ${failed.length} programs, expected ${want.length}`);
process.exit(ok ? 0 : 1);
