# Validation of pycachesim and Layer Conditions

This folder contains scripts, benchmark codes and gathered measurement results to support validation of pycachesim and the Layer Condition model.

The folder `kernels/` contains c-code based benchmarks to generat and measure different data access scenarios. All kernels are enabled for LIKWID performance counter measurements and have been optimized for fast-and-thorough measurements. Meaning that they can be used to easily gather thousands of measurements within minutes. Total runtime of the whole suite, including repeated measurements to gather all necessary events taks about an hour. Gathered results are found in `results/`. The validation results gathered on the testcluster at NHR@FAU is found with the v0.2.5 pycachsim release files (`validataion-results.pkl.gz`) on github. Later versions may provide updated results.

An analysis of the date and simulations is found in `Analysis.ipynb`, it is best viewed using nbviewer via: https://nbviewer.jupyter.org/github/RRZE-HPC/pycachesim/blob/master/validation/Analysis.ipynb

## Usage
This requires an installation of the following python packages `pycachesim`, `kerncraft`, `hpc_inspect` and for measurements a local installation of `LIKWID`.

### Gathering Measurements on a Machine
1. Select or generate (i.e., using Kerncraft's `likwid_bench_auto` command) an appropriate Kerncraft machine file for your architecture. A complete machine file is not necessary, important is the `memory hierarchy` section with the `performance counter metrics` dictionaries.
2. Add this machine to the `INSPECT/config/hosts.yml`. Again, a complete config is not necessary, it sufficient to have a section with a matching hostname in the nodelist and `machine_filename`.
3. In your environment set `LIKWID_DEFINES=-DLIKWID_PERFMON`, `LIKWID_LIB=-L<your_likwid_install_prefix>/lib`, `LIKWID_INC=-I<your_likwid_install_prefix>/include` and `LD_LIBRARY_PATH=<your_likwid_install_prefix>/lib`.
4. Run `./build_and_run.py`.

Measurements will be stored to `results/`.

### Gathering Simulation Data
Follow steps 1 and 2 from above, add hostname to `simulate.get_hostname` and run `./simulate.py`.

Simulation results will be stored to `results/predicted/`.
