export const BATCH_MAGIC = 0x31425653;
export const RESULT_MAGIC = 0x31525653;
export const MEM_BYTES = 256;
export const BATCH_RECORD = MEM_BYTES + 32 * 4 + 4 + 4;
export const RESULT_RECORD = 32 * 4 + MEM_BYTES + 7 * 4;
export const HEADER = 16;

export const STATUS_HALTED = 1;
export const STATUS_LIMIT = 2;

export interface Program {
  memory: Uint8Array;
  registers: Uint32Array;
  instructionCount: number;
  cycleLimit: number;
}

export interface Result {
  registers: Uint32Array;
  memory: Uint8Array;
  pc: number;
  cycles: number;
  retired: number;
  stalls: number;
  flushes: number;
  status: number;
  pass: number;
}

const header = (magic: number, count: number, record: number) => {
  const b = Buffer.alloc(HEADER);
  b.writeUInt32LE(magic, 0);
  b.writeUInt32LE(1, 4);
  b.writeUInt32LE(count, 8);
  b.writeUInt32LE(record, 12);
  return b;
};

function readHeader(buf: Buffer, magic: number, record: number): number {
  if (buf.readUInt32LE(0) !== magic) throw new Error('wrong file type');
  if (buf.readUInt32LE(12) !== record) throw new Error('record size mismatch');
  const count = buf.readUInt32LE(8);
  if (buf.length !== HEADER + count * record) throw new Error('truncated file');
  return count;
}

export function encodeBatch(programs: Program[]): Buffer {
  const body = Buffer.alloc(programs.length * BATCH_RECORD);
  programs.forEach((p, i) => {
    let o = i * BATCH_RECORD;
    body.set(p.memory, o);
    o += MEM_BYTES;
    for (let r = 0; r < 32; r++, o += 4) body.writeUInt32LE(p.registers[r] >>> 0, o);
    body.writeUInt32LE(p.instructionCount, o);
    body.writeUInt32LE(p.cycleLimit, o + 4);
  });
  return Buffer.concat([header(BATCH_MAGIC, programs.length, BATCH_RECORD), body]);
}

export function decodeBatch(buf: Buffer): Program[] {
  const count = readHeader(buf, BATCH_MAGIC, BATCH_RECORD);
  return Array.from({ length: count }, (_, i) => {
    let o = HEADER + i * BATCH_RECORD;
    const memory = new Uint8Array(buf.subarray(o, o + MEM_BYTES));
    o += MEM_BYTES;
    const registers = new Uint32Array(32);
    for (let r = 0; r < 32; r++, o += 4) registers[r] = buf.readUInt32LE(o);
    return { memory, registers, instructionCount: buf.readUInt32LE(o), cycleLimit: buf.readUInt32LE(o + 4) };
  });
}

export function encodeResults(results: Result[]): Buffer {
  const body = Buffer.alloc(results.length * RESULT_RECORD);
  results.forEach((r, i) => {
    let o = i * RESULT_RECORD;
    for (let k = 0; k < 32; k++, o += 4) body.writeUInt32LE(r.registers[k] >>> 0, o);
    body.set(r.memory, o);
    o += MEM_BYTES;
    for (const v of [r.pc, r.cycles, r.retired, r.stalls, r.flushes, r.status, r.pass]) {
      body.writeUInt32LE(v >>> 0, o);
      o += 4;
    }
  });
  return Buffer.concat([header(RESULT_MAGIC, results.length, RESULT_RECORD), body]);
}

export function decodeResults(buf: Buffer): Result[] {
  const count = readHeader(buf, RESULT_MAGIC, RESULT_RECORD);
  return Array.from({ length: count }, (_, i) => {
    let o = HEADER + i * RESULT_RECORD;
    const registers = new Uint32Array(32);
    for (let k = 0; k < 32; k++, o += 4) registers[k] = buf.readUInt32LE(o);
    const memory = new Uint8Array(buf.subarray(o, o + MEM_BYTES));
    o += MEM_BYTES;
    const v = Array.from({ length: 7 }, (_, k) => buf.readUInt32LE(o + 4 * k));
    return { registers, memory, pc: v[0], cycles: v[1], retired: v[2], stalls: v[3], flushes: v[4], status: v[5], pass: v[6] };
  });
}
