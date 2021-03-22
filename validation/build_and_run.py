#!/usr/bin/env python3
import os
import sys
from glob import glob
from pathlib import Path
from subprocess import check_call, check_output, CalledProcessError
import shutil
import socket
from collections import defaultdict
import re
from functools import reduce, lru_cache
from itertools import combinations_with_replacement, product
import platform

from ruamel import yaml
import pandas

import machinestate
import hpc_inspect
from hpc_inspect.inspector import generate_steps
from kerncraft.models import benchmark
from kerncraft.machinemodel import MachineModel
from kerncraft.prefixedunit import PrefixedUnit


@lru_cache()
def get_hostname():
    hostname = socket.gethostname()
    with open(Path(hpc_inspect.__file__).parent.parent / 'config/hosts.yml') as f:
        hostmap = yaml.load(f, Loader=yaml.Loader)

    if hostname not in hostmap:
        for h, d in hostmap.items():
            if hostname in d['nodelist']:
                hostname = h
                break
    return hostname


def get_kernels(kernels=None, hostname=get_hostname()):
    if kernels is None:
        kernels = []
        for f in glob("kernels/*.c"):
            f = f.rsplit('.', 1)[0].split('/', 1)[1]
            if f == "dummy":
                continue
            kernels.append(f)

    # get cachesizes
    mm = get_machine_model(hostname)
    cachesizes = []
    for cache_info in mm['memory hierarchy'][:-1]:
        cachesizes.append(int(float(cache_info['size per group'])))

    kernels_args = []
    for k in kernels:
        args = []
        if k in ['load', 'store', 'copy', 'update', '1d-3pt']:
            args = [[max(10, 10000000//a), a] for a in 
                    generate_steps(128, 64*1024*1024, steps=100,
                                   multiple_of=1, no_powers_of_two=False)]
        elif k.startswith('2d-'):
            steps = generate_steps(32, 2*1024*1024, steps=50,
                                   multiple_of=1, no_powers_of_two=False)
            args = [[max(2, 10000000//(a*b)), a, b]
                     for a, b in product(steps, steps)
                     if 2*a*b < 1024**3/8]
        elif k in ['matvec']:
            steps = generate_steps(32, 2*1024*1024, steps=50,
                                   multiple_of=1, no_powers_of_two=False)
            args = [[max(2, 10000000//(a*b)), a, b]
                     for a, b in product(steps, steps)
                     if a*b + 2*a < 1024**3/8]
        elif k in ['transmatvec']:
            steps = generate_steps(32, 2*1024*1024, steps=50,
                                   multiple_of=1, no_powers_of_two=False)
            args = [[max(2, 10000000//(a*b)), a, b]
                     for a, b in product(steps, steps)
                     if a*b + 2*b < 1024**3/8]
        elif k.startswith('3d-'):
            steps = generate_steps(8, 2*1024*1024, steps=50,
                                   multiple_of=1, no_powers_of_two=False)
            # aiming for a minimum of 200% of largest cache size
            cs = max(cachesizes)*2
            args = []
            for b, c in product(steps, steps):
                # assuming two arrays with eight byte elements, with at least dimension 8
                a = max(8, cs//(2*b*c*8))
                if a*b*c*2*8 < 8*1024**3:
                    args.append([max(2, 10000000//(a*b*c)), a, b, c])
        
        kernels_args.append((k, args))

    return kernels_args


def perfctr(cmd, cores, group='MEM', code_markers=True, verbose=0):
    """
    Run *cmd* with likwid-perfctr and returns result as dict.

    *group* may be a performance group known to likwid-perfctr or an event string.

    if CLI argument cores > 1, running with multi-core, otherwise single-core
    """
    # Making sure likwid-perfctr is available:
    if benchmark.find_executable('likwid-perfctr') is None:
        print("likwid-perfctr was not found. Make sure likwid is installed and found in PATH.",
                file=sys.stderr)
        sys.exit(1)

    # FIXME currently only single core measurements support!
    perf_cmd = ['likwid-perfctr', '-f', '-O', '-g', group]

    cpu = 'S0:0'
    if cores > 1:
        cpu += '-'+str(cores-1)

    # Pinned and measured on cpu
    perf_cmd += ['-C', cpu]

    # code must be marked using likwid markers
    perf_cmd.append('-m')

    perf_cmd += cmd
    if verbose > 1:
        print(' '.join(perf_cmd))
    try:
        with benchmark.fix_env_variable('OMP_NUM_THREADS', None):
            output = check_output(perf_cmd).decode('utf-8').split('\n')
    except CalledProcessError as e:
        print("Executing benchmark failed: {!s}".format(e), file=sys.stderr)
        sys.exit(1)

    # TODO multicore output is different and needs to be considered here!
    results = {}
    cur_region_name = None
    cur_region_data = {}
    for line in output:
        if line == "STRUCT,Info,3" and cur_region_name is not None:
            results[cur_region_name] = cur_region_data
            cur_region_name = None
            cur_region_data = {}
        m = re.match(r"TABLE,Region ([a-z\-0-9_]+),", line)
        if m:
            cur_region_name = m.group(1)
        line = line.split(',')
        try:
            # Metrics
            cur_region_data[line[0]] = float(line[1])
            continue
        except ValueError:
            # Would not convert to float
            pass
        except IndexError:
            # Not a parable line (did not contain any commas)
            continue
        try:
            # Event counters
            if line[2] == '-' or line[2] == 'nan':
                counter_value = 0
            else:
                counter_value = int(line[2])
            if re.fullmatch(r'[A-Z0-9_]+', line[0]) and \
                    re.fullmatch(r'[A-Z0-9]+(:[A-Z0-9]+=[0-9A-Fa-fx]+)*', line[1]):
                cur_region_data.setdefault(line[0], {})
                cur_region_data[line[0]][line[1]] = counter_value
                continue
        except (IndexError, ValueError):
            pass
        if line[0].endswith(":") and len(line) == 3 and line[2] == "":
            # CPU information strings
            cur_region_data[line[0]] = line[1]
            continue
    results[cur_region_name] = cur_region_data
    return results, output


def build_kernel(kernel):
    arch = platform.machine()
    Path('build').mkdir(exist_ok=True)
    if os.path.exists(f"build/{kernel}.{platform.machine()}"):
        return
    
    # build object
    cflags = [
        os.environ["LIKWID_DEFINES"],
        os.environ["LIKWID_INC"],
        "-O1"]
    check_call(["gcc"] + cflags + ["-c", "kernels/"+kernel+".c", "-o", f"build/{kernel}.{arch}.o"])
    if not Path(f'build/dummy.{arch}.o').exists():
        check_call(["gcc"] + cflags + ["-c", "kernels/dummy.c", "-o", f"build/dummy.{arch}.o"])

    # link exec
    check_call(["gcc", os.environ["LIKWID_LIB"],
        f"build/dummy.{arch}.o",
        f"build/{kernel}.{arch}.o",
        "-llikwid",
        "-o", f"build/{kernel}.{arch}"])


def clean(objs=True, all=False):
    if all:
        shutil.rmtree("build/")
        return
    if objs:
        for f in Path('build/').glob("*.o"):
            f.unlink()


@lru_cache()
def get_machine_model(hostname=get_hostname()):
    """Returns MachineModel for current host"""
    with open(Path(hpc_inspect.__file__).parent.parent / 'config/hosts.yml') as f:
        hostmap = yaml.load(f, Loader=yaml.Loader)

    if hostname not in hostmap:
        for h, d in hostmap.items():
            if hostname in d['nodelist']:
                hostname = h
                break
        else:
            raise KeyError("hostname {!r} for found in INSPECT's hosts config.".format(
                hostname))

    return MachineModel(Path(hpc_inspect.__file__).parent.parent /
                        "machine_files" /
                        hostmap[hostname]['machine_filename'])

def run_kernel(kernel, args):
    machine = get_machine_model()
    # get per cachelevel performance counter information:
    event_counters = {}
    cache_metrics = defaultdict(dict)
    for i, cache_info in enumerate(machine['memory hierarchy']):
        name = cache_info['level']
        for k, v in cache_info['performance counter metrics'].items():
            if v is None:
                # Some info can not be measured, we skip it
                continue
            try:
                cache_metrics[name][k], event_dict = machine.parse_perfmetric(v)
            except SyntaxError as e:
                print('Syntax error in machine file perf. metric: {}'.format(v),
                      e, file=sys.stderr)
                continue
            event_counters.update(event_dict)
    
    bench_filename = f"build/{kernel}.{platform.machine()}"
    raw_results = []
    global_infos = {}
    # Compile minimal runs to gather all required events
    minimal_runs = benchmark.build_minimal_runs(list(event_counters.values()))
    measured_ctrs = defaultdict(dict)
    for run in minimal_runs:
        ctrs = ','.join([benchmark.eventstr(e) for e in run])
        r, o = perfctr([bench_filename] + list(map(lambda t: ' '.join(map(str, t)), args)),
                       cores=1, group=ctrs)
        global_infos = {}
        for m in [re.match(r"(:?([a-z_\-0-9]+):)?([a-z]+): ([a-z\_\-0-9]+)", l) for l in o]:
            if m is not None:
                try:
                    v = int(m.group(4))
                except ValueError:
                    v = m.group(4)
                if m.group(1) is None:
                    global_infos[m.group(3)] = v
                else:
                    r[m.group(2)][m.group(3)] = v

        raw_results.append(o)
        for k in r:
            measured_ctrs[k].update(r[k])

    # Analytical metrics needed for futher calculation
    cl_size = int(machine['cacheline size'])
    elementsize = global_infos["elementsize"]
    base_iterations = cl_size // elementsize

    event_counter_results = {}
    cache_metric_results = {}
    cache_transfers_per_cl = {}
    for kernel_run in measured_ctrs:
        # Match measured counters to symbols
        event_counter_results[kernel_run] = {}
        for sym, ctr in event_counters.items():
            event, regs, parameters = ctr[0], benchmark.register_options(ctr[1]), ctr[2]
            if parameters:
                parameter_str = ':'.join(parameters)
                regs = [r+':'+parameter_str for r in regs]
            for r in regs:
                if r in measured_ctrs[kernel_run][event]:
                    event_counter_results[kernel_run][sym] = measured_ctrs[kernel_run][event][r]
        
        cache_metric_results[kernel_run] = defaultdict(dict)
        for cache, mtrcs in cache_metrics.items():
            for m, e in mtrcs.items():
                cache_metric_results[kernel_run][cache][m] = e.subs(event_counter_results[kernel_run])

        total_iterations = \
            measured_ctrs[kernel_run]['iterations'] * measured_ctrs[kernel_run]['repetitions']
        # Inter-cache transfers per CL
        cache_transfers_per_cl[kernel_run] = {
            cache: {k: PrefixedUnit(v / (total_iterations / base_iterations),
                                    'CL/{}It.'.format(base_iterations))
                    for k, v in d.items()}
            for cache, d in cache_metric_results[kernel_run].items()}
            
        cache_transfers_per_cl[kernel_run]['L1']['loads'].unit = \
            'LOAD/{}It.'.format(base_iterations)
        cache_transfers_per_cl[kernel_run]['L1']['stores'].unit = \
            'LOAD/{}It.'.format(base_iterations)

    return cache_transfers_per_cl, global_infos, raw_results


def print_cache_metrics(cache_metric_results):
    print("{:^8} |".format("cache"), end='')
    for metrics in cache_metric_results.values():
        for metric_name in sorted(metrics):
            print(" {:^14}".format(metric_name), end='')
        print()
        break
    for cache, metrics in sorted(cache_metric_results.items()):
        print("{!s:^8} |".format(cache), end='')
        for k, v in sorted(metrics.items()):
            print(" {!s:^14}".format(v), end='')
        print()
    print()


def main(basepath, kernels=None):
    os.chdir(basepath)
    os.makedirs("results/", exist_ok=True)
    ms = machinestate.MachineState()
    ms.generate()
    ms.update()
    ms_dict = ms.get()
    hostname = get_hostname()

    for kernel, args in get_kernels(kernels):
        print(kernel, end=": ", flush=True)
        data = []
        filename = "results/"+hostname+"_"+kernel+".pkl.gz"
        if os.path.exists(filename):
            print("skipping ({} already exists)".format(filename))
            continue
        build_kernel(kernel)
        with open(f'build/{kernel}.{platform.machine()}', 'rb') as f:
            kernel_bin = f.read()
        with open('kernels/'+kernel+'.c', 'r') as f:
            kernel_src = f.read()
        print("built", end=" ", flush=True)
        cache_metric_results_per_cl, global_infos, rr = run_kernel(kernel, args)
        for kernel_info, cmrpcl in cache_metric_results_per_cl.items():
            kernel_name, repeats, *dimensions = kernel_info.split('_')
            row = {'hostname': hostname, 'kernel': kernel_name, 'dimensions': dimensions,
                   'repeats': repeats, 'source': 'measured'}
            row.update(global_infos)
            for cache, metrics in sorted(cache_metric_results_per_cl[kernel_info].items()):
                for k, v in sorted(metrics.items()):
                    row[cache+'_'+k] = float(v)
                    row[cache+'_'+k+'.unit'] = v.unit
            row['raw'] = rr
            row['machinestate'] = ms_dict
            row['binary'] = kernel_bin
            row['benchcode'] = kernel_src
            data.append(row)
        df = pandas.DataFrame(data)
        df.to_pickle(filename, protocol=4)
        print("saved ({})".format(filename))
    clean()


if __name__ == "__main__":
    kernels = sys.argv[1:] or None
    main(Path(__file__).parent, kernels)