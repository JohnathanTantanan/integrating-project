import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { assemble } from '../sagev/src/core/Assembler';
import { MAX_INSTRUCTIONS, createMemory, writeInstruction } from '../sagev/src/core/Memory';
import { mulberry32 } from '../sagev/src/tutor/rand';
import { randomProgram } from '../sagev/tests/reference';
import { encodeBatch, encodeResults } from './format';
import type { Program } from './format';
import { MODELS, runOnSageV } from './sagev';

const arg = (name: string, fallback: string) => {
  const i = process.argv.indexOf(`--${name}`);
  return i > 0 ? process.argv[i + 1] : fallback;
};

const count = Number(arg('count', '16384'));
const seed = Number(arg('seed', '20261109'));
const out = arg('out', 'data');
const limit = Number(arg('limit', '4000'));

const rand = mulberry32(seed);
const programs: Program[] = [];
const sources: string[] = [];
while (programs.length < count) {
  const source = randomProgram(rand);
  const { instructions, dataMemory, errors } = assemble(source);
  if (errors.length > 0 || instructions.length === 0 || instructions.length > MAX_INSTRUCTIONS) continue;
  const memory = createMemory();
  for (const [addr, value] of Object.entries(dataMemory)) memory[Number(addr)] = value;
  for (const inst of instructions) writeInstruction(memory, inst.address, inst.opcode);
  if (rand() < 0.25) {
    const used = new Set(Object.keys(dataMemory).map(Number));
    for (const inst of instructions) for (let b = 0; b < 4; b++) used.add(inst.address + b);
    for (let a = 0; a < memory.length; a++) if (!used.has(a)) memory[a] = Math.floor(rand() * 256);
  }
  const registers = new Uint32Array(32);
  for (let r = 1; r < 8; r++) registers[r] = (Math.floor(rand() * 64) - 16) >>> 0;
  const cut = rand() < 0.03;
  const program = { memory, registers, instructionCount: instructions.length, cycleLimit: limit };
  if (cut) program.cycleLimit = 1 + Math.floor(rand() * Math.max(1, runOnSageV(program, 2).cycles));
  programs.push(program);
  sources.push(source);
}

mkdirSync(out, { recursive: true });
writeFileSync(join(out, 'batch.bin'), encodeBatch(programs));
writeFileSync(join(out, 'sources.txt'), sources.map((s, i) => `# program ${i}\n${s}\n`).join('\n'));
for (const [name, pipeline] of Object.entries(MODELS)) {
  const t = Date.now();
  const expected = programs.map(p => runOnSageV(p, pipeline));
  writeFileSync(join(out, `expected_${name}.bin`), encodeResults(expected));
  const limited = expected.filter(r => r.status !== 1).length;
  const cycles = expected.reduce((a, r) => a + r.cycles, 0);
  console.log(`${name}: ${count} programs, ${cycles} cycles, ${limited} stopped at their cycle limit, ${((Date.now() - t) / 1000).toFixed(1)} s on SageV`);
}
