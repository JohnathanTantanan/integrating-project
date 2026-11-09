import { freshCore, stepFor } from '../sagev/src/core/machine';
import type { PipelineId } from '../sagev/src/core/machine';
import { STATUS_HALTED, STATUS_LIMIT } from './format';
import type { Program, Result } from './format';

export const MODELS: Record<string, PipelineId> = { p2: 2, p0: 0 };

export function runOnSageV(p: Program, pipeline: PipelineId): Result {
  const step = stepFor(pipeline);
  let state = { ...freshCore(pipeline, true), memory: new Uint8Array(p.memory), registers: new Uint32Array(p.registers) };
  state.registers[0] = 0;
  while (!state.isHalted && state.clockCycle < p.cycleLimit) state = step(state, p.instructionCount);
  return {
    registers: new Uint32Array(state.registers),
    memory: new Uint8Array(state.memory),
    pc: state.pc >>> 0,
    cycles: state.clockCycle,
    retired: state.instructionsRetired,
    stalls: state.stallCount,
    flushes: state.flushCount,
    status: state.isHalted ? STATUS_HALTED : STATUS_LIMIT,
    pass: 1,
  };
}
