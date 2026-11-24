import csv
import os
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, 'results', 'raw')
OUT = os.path.join(ROOT, 'results')

SURFACE = '#fcfcfb'
INK = '#0b0b0b'
INK2 = '#52514e'
GRID = '#e4e3df'
S1, S2, S3, S4 = '#2a78d6', '#eb6834', '#1baf7a', '#eda100'

plt.rcParams.update({
    'font.size': 20, 'font.family': 'DejaVu Sans', 'axes.spines.top': False, 'axes.spines.right': False,
    'axes.edgecolor': INK2, 'axes.labelcolor': INK, 'xtick.color': INK2, 'ytick.color': INK2,
    'axes.facecolor': SURFACE, 'figure.facecolor': SURFACE, 'text.color': INK, 'legend.labelcolor': INK,
})
COUNT = FuncFormatter(lambda v, _: f'{int(v):,}')


def rows(name):
    with open(os.path.join(RAW, name + '.csv')) as f:
        return list(csv.DictReader(f))


def col(r, k):
    return [float(x[k]) for x in r]


def counts(r):
    return [int(x['programs']) for x in r]


def save(fig, ax, name, legend='upper left'):
    ax.grid(True, which='major', color=GRID, linewidth=1)
    ax.set_axisbelow(True)
    if legend:
        ax.legend(frameon=False, loc=legend)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name), dpi=100)
    plt.close(fig)
    print('wrote', name)


def line(ax, x, y, color, label, marker):
    ax.plot(x, y, marker=marker, color=color, label=label, linewidth=3, markersize=10,
            markeredgecolor=SURFACE, markeredgewidth=2)


def log_counts(ax):
    ax.set_xscale('log', base=2)
    ax.xaxis.set_major_formatter(COUNT)
    ax.set_xlabel('Programs in the batch')


def sweep(model, title):
    r = rows(f'{model}_sweep')
    n = counts(r)
    fig, ax = plt.subplots(figsize=(19.2, 10.8))
    line(ax, n, col(r, 'cpu_ms'), S3, 'C++, one CPU thread (sequential)', 'o')
    line(ax, n, col(r, 'gpu_total_ms'), S2, 'GPU end to end, full results', 's')
    line(ax, n, col(r, 'gpu_flags_ms'), S4, 'GPU end to end, pass/fail flags only', '^')
    line(ax, n, col(r, 'gpu_kernel_ms'), S1, 'GPU simulation kernel', 'D')
    log_counts(ax)
    ax.set_xticks([1024, 8192, 65536, 524288, 4194304])
    ax.set_yscale('log')
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f'{v:g}'))
    ax.set_ylabel('Time per batch (ms, log scale)')
    ax.set_title(f'{title}: time per batch (lower is faster)', loc='left', pad=20)
    save(fig, ax, f'chart_time_{model}.png')

    fig, ax = plt.subplots(figsize=(19.2, 10.8))
    line(ax, n, col(r, 'speedup_kernel'), S1, 'Simulation kernel', 'D')
    line(ax, n, col(r, 'speedup_flags'), S4, 'End to end, pass/fail flags only', '^')
    line(ax, n, col(r, 'speedup_total'), S2, 'End to end, full results', 's')
    ax.axhline(1, color=INK2, linewidth=2, linestyle='--')
    ax.text(300000, 1.07, 'same speed as one CPU thread', fontsize=16, color=INK2)
    log_counts(ax)
    ax.set_xticks([1024, 8192, 65536, 524288, 4194304])
    ax.set_yscale('log')
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f'{v:g}x'))
    ax.set_ylabel('Speedup over one CPU thread (log scale)')
    ax.set_title(f'{title}: speedup of the RX 6600 XT', loc='left', pad=20)
    save(fig, ax, f'chart_speedup_{model}.png', 'center right')


def bars(ax, labels, values, colors, fmt):
    xs = range(len(values))
    b = ax.bar(xs, values, color=colors, width=0.6, edgecolor=SURFACE, linewidth=2)
    ax.set_xticks(list(xs))
    ax.set_xticklabels(labels)
    for rect, v in zip(b, values):
        ax.text(rect.get_x() + rect.get_width() / 2, rect.get_height(), fmt(v), ha='center', va='bottom', fontsize=18, color=INK)
    return b


def grouping():
    size = '1048576'
    names = [('none', 'Batch order'), ('count', 'Sorted by\ninstruction count'), ('cycles', 'Sorted by\ncycle count (upper bound)')]
    kernel, eff = [], []
    for key, _ in names:
        r = [x for x in rows(f'p2_order_{key}') if x['programs'] == size][0]
        kernel.append(float(r['gpu_kernel_ms']))
        eff.append(float(r['simt_eff']))
    fig, ax = plt.subplots(figsize=(19.2, 10.8))
    bars(ax, [f'{label}\n{100 * e:.0f}% of lanes busy' for (_, label), e in zip(names, eff)], kernel, [S1, S1, S1],
         lambda v: f'{v:.2f} ms')
    ax.set_ylabel('Kernel time (ms)')
    ax.set_title('Five-stage pipeline, 1,048,576 programs: grouping programs of similar length', loc='left', pad=20)
    save(fig, ax, 'chart_grouping.png', None)


def lds():
    size = '1048576'
    fig, axes = plt.subplots(1, 2, figsize=(19.2, 10.8), sharey=False)
    for ax, model, title in zip(axes, ['p0', 'p2'], ['Non-pipelined model', 'Five-stage pipeline']):
        labels, values, colors = [], [], []
        for block in ['64', '128', '256']:
            for kind, color in [('global', S1), ('lds', S2)]:
                r = [x for x in rows(f'{model}_{kind}_b{block}') if x['programs'] == size][0]
                labels.append(f'{block}\n{"LDS" if kind == "lds" else "global"}')
                values.append(float(r['gpu_kernel_ms']))
                colors.append(color)
        bars(ax, labels, values, colors, lambda v: f'{v:.1f}')
        ax.set_title(title, loc='left', pad=12)
        ax.set_xlabel('Threads per block, memory kept in')
        ax.grid(True, axis='y', color=GRID)
        ax.set_axisbelow(True)
    axes[0].set_ylabel('Kernel time (ms), 1,048,576 programs')
    fig.suptitle('Data memory in the local data share (LDS) or in global memory', x=0.01, ha='left')
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, 'chart_lds.png'), dpi=100)
    plt.close(fig)
    print('wrote chart_lds.png')


def transfer():
    fig, ax = plt.subplots(figsize=(19.2, 10.8))
    series = [('p2_pageable', 'Pageable memory, one stream', S3, 'o'), ('p2_pinned_s1', 'Pinned memory, one stream', S1, 's'),
              ('p2_pinned_s2', 'Pinned memory, two streams', S4, '^'), ('p2_pinned_s4', 'Pinned memory, four streams', S2, 'D')]
    for name, label, color, marker in series:
        r = rows(name)
        line(ax, counts(r), col(r, 'gpu_total_ms'), color, label, marker)
    log_counts(ax)
    ax.set_ylim(bottom=0)
    ax.set_ylabel('End-to-end time, full results (ms)')
    ax.set_title('Five-stage pipeline: moving data to and from the GPU', loc='left', pad=20)
    save(fig, ax, 'chart_transfer.png')


if __name__ == '__main__':
    only = set(sys.argv[1:])
    if not only or 'sweep' in only:
        sweep('p2', 'Five-stage pipeline')
        sweep('p0', 'Non-pipelined model')
    if not only or 'grouping' in only:
        grouping()
    if not only or 'lds' in only:
        lds()
    if not only or 'transfer' in only:
        transfer()
