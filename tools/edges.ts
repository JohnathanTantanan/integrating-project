import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { assemble } from '../sagev/src/core/Assembler';
import { createMemory, writeInstruction } from '../sagev/src/core/Memory';
import { encodeBatch, encodeResults } from './format';
import type { Program } from './format';
import { MODELS, runOnSageV } from './sagev';

const out = process.argv.includes('--out') ? process.argv[process.argv.indexOf('--out') + 1] : 'data';

const sources: string[] = [];
for (let w = 0; w < 64; w++) sources.push(`ADDI x1, x0, ${-1 - w}\nSW x1, ${4 * w}(x0)\nLW x2, ${4 * w}(x0)`);
for (const a of [253, 254, 255]) sources.push(`ADDI x1, x0, -1\nADDI x3, x0, ${a}\nSW x1, 0(x3)\nLW x2, 0(x3)`);

const programs: Program[] = sources.map(source => {
  const { instructions, dataMemory, errors } = assemble(source);
  if (errors.length > 0) throw new Error(`${source}: ${errors.map(e => e.message).join('; ')}`);
  const memory = createMemory();
  for (const [addr, value] of Object.entries(dataMemory)) memory[Number(addr)] = value;
  for (const inst of instructions) writeInstruction(memory, inst.address, inst.opcode);
  return { memory, registers: new Uint32Array(32), instructionCount: instructions.length, cycleLimit: 4000 };
});

mkdirSync(out, { recursive: true });
writeFileSync(join(out, 'edge_batch.bin'), encodeBatch(programs));
for (const [name, pipeline] of Object.entries(MODELS)) {
  writeFileSync(join(out, `edge_expected_${name}.bin`), encodeResults(programs.map(p => runOnSageV(p, pipeline))));
}
console.log(`edge batch: ${programs.length} programs, a store to each of the 64 memory words and 3 stores that wrap past 0xFF`);
