#!/usr/bin/env python3
import sys
import os
from glob import glob
from pprint import pprint
import multiprocessing as mp

import pandas as pd

from kerncraft.kernel import KernelDescription
from kerncraft.cacheprediction import LayerConditionPredictor, CacheSimulationPredictor

from build_and_run import get_machine_model, get_kernels


def get_hostnames():
    return ['ivyep1', 'skylakesp2', 'naples1', 'rome1', 'warmup', 'qpace4']


def get_kc_kernel(kernel_name):
    if kernel_name == '1d-3pt':
        loops = [{'index': 'x', 'start': '1', 'stop': 'D0-1', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0']},
                  'b': {'type': ('double',), 'dimension': ['D0']}}
        srcs = {'a': [['x-1'], ['x'], ['x+1']],
                'b': []}
        dsts = {'a': [],
                'b': [['x']]}
    elif kernel_name == '2d-5pt':
        loops = [{'index': 'y', 'start': '1', 'stop': 'D0-1', 'step': '1'},
                 {'index': 'x', 'start': '1', 'stop': 'D1-1', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0', 'D1']},
                  'b': {'type': ('double',), 'dimension': ['D0', 'D1']}}
        srcs = {'a': [['y-1', 'x'],
                      ['y', 'x-1'], ['y', 'x'], ['y', 'x+1'],
                      ['y+1', 'x']],
                'b': []}
        dsts = {'a': [],
                'b': [['y', 'x']]}
    elif kernel_name == '3d-7pt':
        loops = [{'index': 'z', 'start': '1', 'stop': 'D0-1', 'step': '1'},
                 {'index': 'y', 'start': '1', 'stop': 'D1-1', 'step': '1'},
                 {'index': 'x', 'start': '1', 'stop': 'D2-1', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0', 'D1', 'D2']},
                  'b': {'type': ('double',), 'dimension': ['D0', 'D1', 'D2']}}
        srcs = {'a': [['z-1', 'y', 'x'], ['z', 'y-1', 'x'],
                      ['z', 'y', 'x-1'], ['z', 'y', 'x'], ['z', 'y', 'x+1'],
                      ['z', 'y+1', 'x'], ['z+1', 'y', 'x']],
                'b': []}
        dsts = {'a': [],
                'b': [['z', 'y', 'x']]}
    elif kernel_name == '3d-r3-11pt':
        loops = [{'index': 'z', 'start': '3', 'stop': 'D0-3', 'step': '1'},
                 {'index': 'y', 'start': '3', 'stop': 'D1-3', 'step': '1'},
                 {'index': 'x', 'start': '1', 'stop': 'D2-1', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0', 'D1', 'D2']},
                  'b': {'type': ('double',), 'dimension': ['D0', 'D1', 'D2']}}
        srcs = {'a': [['z-3', 'y', 'x'], ['z-1', 'y', 'x'],
                      ['z', 'y-3', 'x'], ['z', 'y-1', 'x'],
                      ['z', 'y', 'x-1'], ['z', 'y', 'x'], ['z', 'y', 'x+1'],
                      ['z', 'y+1', 'x'], ['z', 'y+3', 'x'],
                      ['z+1', 'y', 'x'], ['z+3', 'y', 'x']],
                'b': []}
        dsts = {'a': [],
                'b': [['z', 'y', 'x']]}
    elif kernel_name == 'matvec':
        loops = [{'index': 'row', 'start': '0', 'stop': 'D0', 'step': '1'},
                 {'index': 'col', 'start': '0', 'stop': 'D1', 'step': '1'}]
        arrays = {'mat': {'type': ('double',), 'dimension': ['D0', 'D1']},
                  'vec': {'type': ('double',), 'dimension': ['D1']},
                  'resv': {'type': ('double',), 'dimension': ['D1']}}
        srcs = {'mat': [['row', 'col']],
                'vec': [['col']],
                'resv': []}
        dsts = {'mat': [],
                'vec': [],
                'resv': []}  # store to resv is not in inner-loop-body
    elif kernel_name == 'transmatvec':
        loops = [{'index': 'row', 'start': '0', 'stop': 'D0', 'step': '1'},
                 {'index': 'col', 'start': '0', 'stop': 'D1', 'step': '1'}]
        arrays = {'mat': {'type': ('double',), 'dimension': ['D1', 'D0']},
                  'vec': {'type': ('double',), 'dimension': ['D1']},
                  'resv': {'type': ('double',), 'dimension': ['D1']}}
        srcs = {'mat': [['col', 'row']],
                'vec': [['col']],
                'resv': []}
        dsts = {'mat': [],
                'vec': [],
                'resv': []}  # store to resv is not in inner-loop-body
    elif kernel_name == 'load':
        loops = [{'index': 'i', 'start': '0', 'stop': 'D0', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0']}}
        srcs = {'a': [['i']]}
        dsts = {'a': []}
    elif kernel_name == 'store':
        loops = [{'index': 'i', 'start': '0', 'stop': 'D0', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0']}}
        srcs = {'a': []}
        dsts = {'a': [['i']]}
    elif kernel_name == 'copy':
        loops = [{'index': 'i', 'start': '0', 'stop': 'D0', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0']},
                  'b': {'type': ('double',), 'dimension': ['D0']}}
        dsts = {'a': [['i']],
                'b': []}
        srcs = {'a': [],
                'b': [['i']]}
    elif kernel_name == 'update':
        loops = [{'index': 'i', 'start': '0', 'stop': 'D0', 'step': '1'}]
        arrays = {'a': {'type': ('double',), 'dimension': ['D0']}}
        srcs = {'a': [['i']]}
        dsts = {'a': [['i']]}
    
    desc = {'loops': loops, 'arrays': arrays,
            'data sources': srcs, 'data destinations': dsts,
            'flops': {}}
    return KernelDescription(desc)


def simulate(hostname, kernel, args):
    filename = f'results/predicted/{hostname}_{kernel}.pkl.gz'
    print(hostname, kernel, end=" ")
    if os.path.exists(filename):
        print(f"exists {filename}")
        return
    data = []
    machine = get_machine_model(hostname)
    kc_kernel = get_kc_kernel(kernel)
    no_lcp = False
    for c, arg in enumerate(args):
        # reset kernel state
        kc_kernel.clear_state()

        for i, a in enumerate(arg[1:]):
            kc_kernel.set_constant(f'D{i}', a)
        csp = CacheSimulationPredictor(kc_kernel, machine)
        csp_infos = csp.get_infos()
        lcp_infos = None
        if not no_lcp:
            try:
                lcp = LayerConditionPredictor(kc_kernel, machine)
                lcp_infos = lcp.get_infos()
            except ValueError:
                no_lcp = True
        row = {
            'hostname': hostname,
            'kernel': kernel,
            'dimensions': arg[1:]}
        
        # Typically ['L1', 'L2', 'L3', 'MEM']
        levels = [mh['level'] for mh in machine['memory hierarchy']]
        stat_types = ['loads', 'misses', 'stores', 'evicts']
        
        cs_row = {'source': 'pycachesim'}
        cs_row.update(row)
        
        for st in stat_types:
            for level, stat in zip(levels, getattr(csp, f'get_{st}')()):
                if level == "MEM" and st in ['misses', 'evicts']: continue  # makes no sense
                cs_row[level+'_'+st] = float(stat)
        
        cs_row['raw'] = csp_infos
        data.append(cs_row)
        
        if not no_lcp:
            lc_row = {'source': 'layer-conditions'}
            lc_row.update(row)
            for st in stat_types:
                for level, stat in zip(levels, getattr(lcp, f'get_{st}')()):
                    if level == "MEM" and st in ['misses', 'evicts']: continue  # makes no sense
                    lc_row[level+'_'+st] = float(stat)
            lc_row['raw'] = lcp_infos
            data.append(lc_row)
        if c % (len(args) // 10) == 0:
            print(".", end='', flush=True)
    df = pd.DataFrame(data)
    os.makedirs('results/predicted', exist_ok=True)
    df.to_pickle(f'results/predicted/{hostname}_{kernel}.pkl.gz')
    print(f"saved {filename}")
    return data


def simulate_host(hostname, kernels=None):
    kernels_args = get_kernels(hostname=hostname)
    if kernels is not None:
        kernels_args = [(kn, ka) for kn, ka in kernels_args if kn in kernels]


    for kernel, args in get_kernels(hostname=hostname):
        if kernels is not None and kernel not in kernels:
            continue



def main():
    hosts = get_hostnames()
    if len(sys.argv) > 1:
        if any([h in sys.argv for h in get_hostnames()]):
            hosts = [h for h in get_hostnames() if h in sys.argv]

    if '--serial' in sys.argv:
        for hostname in hosts:
            kernels = get_kernels(hostname=hostname)
            if any([kn in sys.argv for kn, ka in get_kernels(hostname=hostname)]):
                kernels = [(kn, ka) for kn, ka in get_kernels(hostname=hostname) if kn in sys.argv]
            for kernel, args in kernels:
                 simulate(hostname, kernel, args)
    else:
        with mp.Pool() as p:
            for hostname in hosts:
                kernels = get_kernels(hostname=hostname)
                if any([kn in sys.argv for kn, ka in get_kernels(hostname=hostname)]):
                    kernels = [(kn, ka) for kn, ka in get_kernels(hostname=hostname) if kn in sys.argv]
                if '--serial' in sys.argv:
                    for kernel, args in kernels:
                     simulate(hostname, kernel, args)
                else:
                    p.starmap_async(simulate, [(hostname, kernel, args) for kernel, args in kernels])
            p.close()
            p.join()


if __name__ == "__main__":
    main()
