import csv
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'results', 'raw')


def rows(name):
    with open(os.path.join(RAW, name + '.csv')) as f:
        return list(csv.DictReader(f))


def num(v, places=3):
    return f'{float(v):,.{places}f}'


def count(v):
    return f'{int(v):,}'


def table(head, body):
    lines = ['| ' + ' | '.join(head) + ' |', '| ' + ' | '.join('---:' for _ in head) + ' |']
    lines += ['| ' + ' | '.join(r) + ' |' for r in body]
    return '\n'.join(lines)


def sweep(model):
    head = ['Programs', 'C++ (ms)', 'Kernel (ms)', 'Layout (ms)', 'End to end, full (ms)', 'End to end, flags (ms)',
            'Kernel speedup', 'Speedup, full', 'Speedup, flags', 'Lanes busy']
    body = [[count(r['programs']), num(r['cpu_ms']), num(r['gpu_kernel_ms']), num(r['gpu_layout_ms']), num(r['gpu_total_ms']),
             num(r['gpu_flags_ms']), num(r['speedup_kernel'], 2), num(r['speedup_total'], 2), num(r['speedup_flags'], 2),
             f"{100 * float(r['simt_eff']):.1f}%"] for r in rows(f'{model}_sweep')]
    return table(head, body)


def grouping():
    head = ['Programs', 'Batch order (ms)', 'Lanes busy', 'By instruction count (ms)', 'Lanes busy', 'By cycle count (ms)', 'Lanes busy']
    data = {k: rows(f'p2_order_{k}') for k in ('none', 'count', 'cycles')}
    body = []
    for i, r in enumerate(data['none']):
        line = [count(r['programs'])]
        for k in ('none', 'count', 'cycles'):
            x = data[k][i]
            line += [num(x['gpu_kernel_ms']), f"{100 * float(x['simt_eff']):.1f}%"]
        body.append(line)
    return table(head, body)


def lds():
    head = ['Model', 'Threads per block', 'Global memory (ms)', 'LDS (ms)', 'LDS / global']
    body = []
    for model, name in (('p0', 'Non-pipelined'), ('p2', 'Five-stage')):
        for block in ('64', '128', '256'):
            g = [x for x in rows(f'{model}_global_b{block}') if x['programs'] == '1048576'][0]
            l = [x for x in rows(f'{model}_lds_b{block}') if x['programs'] == '1048576'][0]
            body.append([name, block, num(g['gpu_kernel_ms']), num(l['gpu_kernel_ms']),
                         f"{float(l['gpu_kernel_ms']) / float(g['gpu_kernel_ms']):.2f}"])
    return table(head, body)


def transfer():
    names = [('p2_pageable', 'Pageable'), ('p2_pinned_s1', 'Pinned, 1 stream'), ('p2_pinned_s2', 'Pinned, 2 streams'),
             ('p2_pinned_s4', 'Pinned, 4 streams')]
    data = {k: rows(k) for k, _ in names}
    head = ['Programs'] + [f'{label}, full (ms)' for _, label in names] + [f'{label}, flags (ms)' for _, label in names]
    body = []
    for i, r in enumerate(data['p2_pageable']):
        body.append([count(r['programs'])] + [num(data[k][i]['gpu_total_ms']) for k, _ in names]
                    + [num(data[k][i]['gpu_flags_ms']) for k, _ in names])
    return table(head, body)


if __name__ == '__main__':
    out = os.path.join(ROOT, 'results', 'tables.md')
    with open(out, 'w') as f:
        f.write('# Generated tables\n\nWritten by `python tools/tables.py` from `results/raw/*.csv`.\n\n')
        f.write('## Five-stage pipeline, sequential C++ against the GPU\n\n' + sweep('p2') + '\n\n')
        f.write('## Non-pipelined model, sequential C++ against the GPU\n\n' + sweep('p0') + '\n\n')
        f.write('## Grouping programs of similar length (five-stage pipeline, kernel time)\n\n' + grouping() + '\n\n')
        f.write('## Data memory in LDS or in global memory (1,048,576 programs, kernel time)\n\n' + lds() + '\n\n')
        f.write('## Moving data (five-stage pipeline, end to end)\n\n' + transfer() + '\n')
    print('wrote', out)
