#!/usr/bin/env python3
import sys
import os
from glob import glob
from pprint import pprint
import multiprocessing as mp
import gc

import pandas as pd

from kerncraft.kernel import KernelDescription
from kerncraft.cacheprediction import LayerConditionPredictor, CacheSimulationPredictor

from build_and_run import get_machine_model, get_kernels


def get_hostnames():
    return ['ivyep1', 'skylakesp2', 'naples1', 'rome1', 'warmup']


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
    data = []
    machine = get_machine_model(hostname)
    kc_kernel = get_kc_kernel(kernel)
    no_lcp = False
    for arg in args:
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
        
        cs_row = {'source': 'pycachesim'}
        cs_row.update(row)
        for cache_info in csp_infos['memory hierarchy'][:-1]:  # ignoring mem
            level = cache_info['level']
            if cache_info['index'] == 0:
                cs_row[f"{level}_accesses_float"] = cache_info['total loads']
            else:
                cs_row[f"{level}_accesses_float"] = cache_info['total lines load']
            cs_row[f"{level}_misses_float"] = cache_info['total lines misses']
            cs_row[f"{level}_evicts_float"] = cache_info['total lines evicts']
        cs_row['raw'] = csp_infos
        data.append(cs_row)
        
        if not no_lcp:
            lc_row = {'source': 'layer-conditions'}
            lc_row.update(row)
            for lvl, cache_info in enumerate(lcp_infos['cache']):
                level = f"L{lvl+1}"
                if lvl == 0:
                    lc_row[f"{level}_accesses_float"] = 0
                else:
                    lc_row[f"{level}_accesses_float"] = lcp_infos['cache'][i-1]['misses']
                lc_row[f"{level}_evicts_float"] = cache_info['evicts']
                lc_row[f"{level}_misses_float"] = cache_info['misses']
            lc_row['raw'] = lcp_infos
            data.append(lc_row)
        print(".", end='', flush=True)
    return data


def simulate_host(hostname):
    for kernel, args in get_kernels(hostname=hostname):
        filename = f'results/predicted/{hostname}_{kernel}.pkl.gz'
        print(hostname, kernel, end=" ")
        if os.path.exists(filename):
            print(f"exists {filename}")
            continue
        data = simulate(hostname, kernel, args)
        df = pd.DataFrame(data)
        os.makedirs('results/predicted', exist_ok=True)
        df.to_pickle(f'results/predicted/{hostname}_{kernel}.pkl.gz')
        print(f"saved {filename}")


def main():
    pool = mp.Pool(processes=8)
    pool.map(simulate_host, get_hostnames())

    #for hostname in get_hostnames():
    #    simulate_host(hostname)

    # for each host, kernel, size configuration found in results
    # run simulation using kerncrafts's approach
    # save to simulated dataframe
    pass


if __name__ == "__main__":
    main()
