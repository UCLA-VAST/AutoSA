#!/usr/bin/env python3

import sys
import argparse
import re
import os
import json
import subprocess
import itertools
import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.linear_model import LinearRegression
from sklearn import metrics
import joblib
import xml.etree.ElementTree as ET
import time
import multiprocessing
import random
from statistics import mean
import copy
import logging
import functools
import shutil
import datetime
from pathlib import Path

import optimizer_prune as opt_prune
import resource_model as res_model
import latency_model as lat_model

def timer(func):
    """ Print the runtime of the decorated function.

    """
    @functools.wraps(func)
    def wrapper_timer(*args, **kwargs):
        start_time = time.perf_counter()
        value = func(*args, **kwargs)
        end_time = time.perf_counter()
        run_time = end_time - start_time
        print(
            f'[AutoSA-Optimizer {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}] INFO: Finished function: {func.__name__} in {run_time:.4f} secs')
        return value
    return wrapper_timer

def generate_loop_candidates(loops, config, stage):
    """ Generate candidate loops

    This function samples each loop dimension given the sample numbers set in
    the config, then builds a Cartesian product of all sampled loops to generate
    all possible loop combinations to search.

    Due to the current implementation limitation, we have the following limitation
    on the loop candidates:
    - Array partitionining: the loop candidates should be left-exclusive and right-inclusive.
      This prevents generating single PEs along certain dimension which causes
      codegen breakdown.
    - Latency hiding: the loop candidates should be left-inclusive and right-exclusive.
      Similarly, making it right-exclusive to avoid possible single PE case.
    - SIMD, L2 array partitioning: both left- and right-inclusive
    Note: for both latency hiding and SIMD, if we choose tiling factor as 1, the
    corresponding stage will be skipeed in AutoSA.

    If the sample mode is set in exhausive, we will search all divisible factors of
    the loop bound.
    If the sample mode is set in log, we will generate samples of exponentials of 2.
    If the sample mode is set in linear, we will generate 'n' linear samples.
    If the sample mode is set in random, we will generate 'n' random samples.

    Parameters
    ----------
    loops: list
        A list of loop upperbounds
    config: dict
        Global configuration
    stage: str
        Optimization stage name
    """
    if stage not in [
        'space_time',
        'array_part',
        'array_part_L2',
        'latency_hiding',
        'SIMD_vectorization']:
        raise NameError(f'Stage {stage} is not defined.')

    sample_mode = config['setting'][config['mode']]['sample'][stage]['mode']
    sample_n = config['setting'][config['mode']]['sample'][stage]['n']
    sample_loop_limit = config['setting'][config['mode']]['sample'][stage]['loop_limit']

    l_inclusive = 1
    r_inclusive = 1
    if stage == 'array_part':
        l_inclusive = 0
    elif stage == 'latency_hiding':
        r_inclusive = 0

    # Sample each loop dim
    sample_list = []
    for loop in loops:
        if sample_mode == 'log':
            ub = int(
                np.floor(
                    np.log2(
                        loop if sample_loop_limit == -1 else min(loop, sample_loop_limit))))
            lb = 0
        else:
            ub = loop if sample_loop_limit == -1 else min(loop, sample_loop_limit)
            lb = 1
        if not r_inclusive:
            ub = ub - 1
        if not l_inclusive:
            lb = lb + 1
        if sample_mode == 'exhaustive':
            samples = [s for s in range(lb, ub + 1) if loop % s == 0]
        elif sample_mode == 'log':
            samples = [
                np.power(
                    2,
                    int(s)) for s in range(
                    lb,
                    ub +
                    1) if loop %
                np.power(
                    2,
                    int(s)) == 0]
        elif sample_mode == 'linear':
            samples = [s for s in range(lb, ub + 1) if loop % s == 0]
            # Uniformly sample 'n' factors
            stride = 1 if len(samples) <= sample_n else int(
                len(samples) / sample_n)
            samples = [samples[i] for i in range(0, len(samples), stride)]
        elif sample_mode == 'random':
            samples = [s for s in range(lb, ub + 1) if loop % s == 0]
            # Randomly sample 'n' factors
            if sample_n < len(samples):
                samples = random.sample(samples, sample_n)
        else:
            raise NameError(f'Sample mode {sample_mode} is not defined.')
        sample_list.append(samples)

    # Generate Cartesian product
    sample_loops = list(itertools.product(*sample_list))
    sample_loops = [list(tup) for tup in sample_loops]

    return sample_loops

def multi_process(loops, func, config):
    """ Perform multi-processing for function "func".

    Parameters
    ----------
    loops:
        A list of loop candidates.
    func:
        The function to be executed by each process.
    config: dict
        Global configuration.
    """
    num_proc = min(multiprocessing.cpu_count(),
                   config['setting'][config['mode']]['multiprocess']['n_job'])
    # Split the loops into chunks
    chunk_size = int(np.ceil(float(len(loops)) / num_proc))
    loop_chunks = [loops[i: i + min(chunk_size, len(loops) - i)]
                   for i in range(0, len(loops), chunk_size)]
    pool = multiprocessing.Pool(processes=num_proc)
    # Allocate new work spaces for each forked process
    for i in range(num_proc):
        if i == 0:
            continue
        prj_dir = config['work_dir'][:-1] + str(i)
        if os.path.exists(prj_dir):
            continue
        os.mkdir(f'{prj_dir}')
        os.mkdir(f'{prj_dir}/output')
        os.mkdir(f'{prj_dir}/output/latency_est')
        os.mkdir(f'{prj_dir}/output/resource_est')
        os.mkdir(f'{prj_dir}/output/src')
        ret = execute_sys_cmd(
            f'cp {config["work_dir"]}/autosa_config.json {prj_dir}/', config)

    config['logger'].info(f'Forking {num_proc} processes...')
    verbose = config['verbose']
    stdout = config['stdout']
    logger = config['logger']
    config['verbose'] = 0
    config['stdout'] = subprocess.DEVNULL
    config['logger'] = None
    n_designs = config['monitor']['n_designs']
    config['monitor']['n_designs'] = 0

    # Execute the function
    results = pool.starmap(func, [(loop_chunks[i], copy.deepcopy(config),
        config['work_dir'][:-1] + str(i), 1) for i in range(len(loop_chunks))])
    # Aggregate the monitor information
    for result in results:
        n_designs += result['monitor']['n_designs']
    config['monitor']['n_designs'] = n_designs

    if config['mode'] == 'search':
        # Aggregate the results
        config['search_results'] = merge_search_results(
            [result['search_results'] for result in results],
            config['setting']['search']['metric'],
            config['setting']['search']['log']['n_record'],
            config['hw_info'])

    config['verbose'] = verbose
    config['stdout'] = stdout
    config['logger'] = logger

    return

def cmp_designs(design1, design2, metric):
    """ Compare two designs.

    Parameters
    ----------
    design1: dict
        Design 1.
    design2: dict
        Design 2.
    metric: str
        Metric to evaluate the design.
    """
    if design1['found'] == False:
        return design2
    if design2['found'] == False:
        return design1

    if metric == 'latency':
        if design1['latency'] < design2['latency']:
            return design1
        else:
            return design2
        # TODO: if the latency equals, we could compare to get the design with lower resouce usage.
    elif metric == 'power':
        if design1['power'] < design2['power']:
            return design1
        else:
            return design2

def generate_sa_sizes_cmd(sa_sizes):
    """ Generate the command line argument to specify the sa_sizes.

    Concatenate each size in the sa_sizes to generate the final argument.

    Parameters
    ----------
    sa_sizes: list
        A list containing the sizes for each optimization stage.
    """
    length = len(sa_sizes)
    first = 1
    cmd = '--sa-sizes="{'
    for size in sa_sizes:
        if not first:
            cmd += ';'
        cmd += size
        first = 0

    cmd += '}"'
    return cmd


@timer
def train_resource_models_xilinx(config):
    """ Train the resource model for Xilinx program.

    This function first collects all HLS synthesized designs from the previous stage.
    These designs are grouped by kernels.
    Then, it trains a resource model for each kernel using linear regression.
    The trained models are placed in /training/resource_models/

    """
    tmp_dir = config['tmp_dir']
    config['work_dir'] = f'{tmp_dir}/optimizer/synth'
    jobs = os.listdir(config['work_dir'])
    training_samples = {}
    for job in jobs:
        job_dir = f'{config["work_dir"]}/{job}'
        kernels = os.listdir(job_dir)
        for kernel in kernels:
            kernel_dir = f'{job_dir}/{kernel}'
            designs = os.listdir(kernel_dir)
            if kernel not in training_samples:
                training_samples[kernel] = []
            for design in designs:
                design_dir = f'{kernel_dir}/{design}/output'
                training_samples[kernel].append(design_dir)
    # Train the resource model for each kernel
    work_dir = f'{tmp_dir}/optimizer/training/resource_models'
    if os.path.exists(work_dir):
        shutil.rmtree(work_dir)
    os.mkdir(work_dir)
    for kernel in training_samples:
        # Create the directory
        cur_work_dir = f'{work_dir}/{kernel}'
        os.mkdir(cur_work_dir)
        # Collect the design infos
        designs = training_samples[kernel]
        design_infos = []
        for design_dir in designs:
            design_info = res_model.extract_design_info(design_dir, 1)
            design_infos.append(design_info)
            config['logger'].info(design_dir)
        # Convert the design infos to a dataframe
        modules, fifos, df = res_model.convert_design_infos_to_df(design_infos)
        # Train the models
        config['logger'].info(f'Train the resource models for {kernel}...')
        res_model.train(df, modules, fifos, design_infos, cur_work_dir, config['logger'])

@timer
def train_latency_models_xilinx(config):
    """ Train the latency model

    Note: We will assume all loops with II = 1 and depth = 1.
    """
    return

def execute_autosa_cmd(config):
    """ Compose the AutoSA command and run.

    Parameters
    ----------
    config: dict
        Global configuration.

    Returns
    -------
    ret: int
        The command return code.
    """
    # Check if time out
    if config['monitor']['time_out_start'] != -1:
        elapsed_time = time.time() - config['monitor']['time_out_start']
        if float(elapsed_time) / 60 > config['setting']['search']['time_out']:
            return -1

    cmd = ' '.join(config['cmds'])
    #config['logger'].info(f'Execute CMD: {cmd}')
    config['logger'].debug(f'Execute CMD: {cmd}')
    p = subprocess.Popen(cmd, shell=True, stdout=config['stdout'])
    ret = p.wait()
    return ret

def execute_sys_cmd(cmd, config):
    """ Execute the system command.

    Parameters
    ----------
    cmd: str
        Command to execute.
    config: dict
        Global configuration
    """
    config['logger'].debug(f'Execute CMD: {cmd}')
    p = subprocess.Popen(cmd, shell=True, stdout=config['stdout'])
    ret = p.wait()
    return ret

def generate_autosa_cmd_str(cmds):
    """ Generate the cmd to print.    
    """
    cmd_str = ''
    is_first = True
    for cmd in cmds:
        #if cmd.find(' --tuning') != -1:
        #    cmd = cmd.replace(' --tuning', '')
        if not is_first:
            cmd_str += ' '
        cmd_str += cmd
        is_first = False

    return cmd_str

def save_design_files(config):
    """ Save the current design.

    """
    # Load the kernel id
    design_dir = f'{config["work_dir"]}/output'
    with open(f'{design_dir}/resource_est/design_info.json', 'r') as f:
        design_info = json.load(f)
    kernel_id = design_info['kernel_id']
    if not os.path.exists(f'{config["work_dir"]}/kernel{kernel_id}'):
        os.mkdir(f'{config["work_dir"]}/kernel{kernel_id}')
    prj_path = f'{config["work_dir"]}/kernel{kernel_id}'
    designs = os.listdir(prj_path)
    design_id = len(designs)
    design_path = f'{config["work_dir"]}/kernel{kernel_id}/design{design_id}'
    os.mkdir(design_path)

    # Save the cmd
    with open(design_path + '/design.info', 'w') as f:
        f.write(generate_autosa_cmd_str(config['cmds']))

    # if config['mode'] == 'search':
        # Store the estimated latency and resource info
        # TODO

    # Copy the files
    ret = execute_sys_cmd(
        f'cp -r {config["work_dir"]}/output {design_path}/',
        config)

def clear_design_files(config):
    """ Clean up the design folder files

    """
    execute_sys_cmd(f'rm {config["work_dir"]}/output/latency_est/*', config)
    execute_sys_cmd(f'rm {config["work_dir"]}/output/resource_est/*', config)
    execute_sys_cmd(f'rm {config["work_dir"]}/output/src/*', config)

def explore_design(config):
    """ Explore the final design.

    In the training mode, we will save the current design.
    Later, we will sample some designs to be synthesized for
    training the resource/latency models.
    In the search mode, we will evaluate the resource and latency of the current
    design and update the config accordingly.

    """
    tmp_dir = config['tmp_dir']
    # Update the monitor
    config['monitor']['n_designs'] += 1

    if config['mode'] == 'training':
        save_design_files(config)
        clear_design_files(config)
        return
    elif config['mode'] == 'search':
        cur_design = {
            'latency': -1,
            'resource': {},
            'power': -1,
            'cmd': generate_autosa_cmd_str(config['cmds'])
        }
        config['monitor']['last_design'] = cur_design
        design_dir = f'{config["work_dir"]}/output'
        if config['setting']['search']['metric'] == 'latency':
            #start_time = time.perf_counter()
            # Predict the latency
            latency_info = lat_model.extract_latency_info(design_dir)
            latency = lat_model.predict_design_latency(
                latency_info, config['setting']['search']['cycle_period'],
                config['search_results']['opt']['latency'])
            #runtime = time.perf_counter() - start_time
            #print(f'resource runtime: {runtime}')
            if config['search_results']['opt']['found']:
                if latency > config['search_results']['opt']['latency']:
                    clear_design_files(config)
                    return
            cur_design['latency'] = int(latency)
        elif config['setting']['search']['metric'] == 'power':
            # Predict the power
            clear_design_files(config)
            raise NotImplementedError(f'DSE for power is not supported.')

        # Predict the resource usage
        #start_time = time.perf_counter()
        design_info = res_model.extract_design_info(design_dir, 0)
        modules, fifos, df = res_model.convert_design_infos_to_df([design_info])
        kernel_id = design_info['kernel_id']
        # Resource model path
        res_model_path = f'{tmp_dir}/optimizer/training/resource_models/kernel{kernel_id}'
        res = res_model.predict_design_resource_usage(
            df, modules, fifos, design_info,
            res_model_path,
            config['setting']['search']['resource_target'])
        cur_design['resource'] = res

        if not res_model.resource_valid(res, config['hw_info'], \
            config['setting']['search']['pruning']['resource']['range'],
            config['setting']['search']['resource_target']):
            clear_design_files(config)
            return
        #runtime = time.perf_counter() - start_time
        #print(f'resource runtime: {runtime}')

        # Compare and update the search results
        config['search_results'] = update_search_results(
            config['search_results'], cur_design,
            config['setting']['search']['log']['n_record'],
            'latency', config['hw_info'])

        # For certain time interval, print out the best design found so far
        if config['setting']['search']['update_time_interval'] != -1:
            if 'update_last_time' not in config['monitor']:
                config['monitor']['update_last_time'] = time.time()
            else:
                elapsed_time = time.time() - config['monitor']['update_last_time']
                if float(elapsed_time) / 60 > config['setting']['search']['update_time_interval']:
                    # print the best results so far
                    config['logger'].info(print_best_design(config['search_results']['opt'], config['hw_info']))
                    config['monitor']['update_last_time'] = time.time()

    clear_design_files(config)
    return

def simd_loop_filter(loops, tuning):
    """ Filter out the SIMD candidate loops based on the tuning information.

    We select the legal simd loop with the highest score.
    If there is no such loop, we will set "loops" to all "1"s.
    AutoSA will not tile loops with the tiling factor as one for latency hiding or
    SIMD vectorization.
    If one such loop is found, we will set all loop bounds to 1 except the target loop.

    Parameters
    ----------
    loops: list
        upper bounds of all candidate SIMD loops
    tuning: dict
        tuning information for the SIMD stage
    """
    scores = tuning['simd']['scores']
    legal = tuning['simd']['legal']
    # Find the candidate loop with the highest score
    simd_loop_idx = -1
    max_score = -1
    for i in range(len(legal)):
        if legal[i] == 0:
            continue
        if scores[i] > max_score:
            max_score = scores[i]
            simd_loop_idx = i

    if simd_loop_idx < 0:
        filter_loops = [1 for i in range(len(loops))]
    else:
        filter_loops = [1 for i in range(len(loops))]
        filter_loops[simd_loop_idx] = loops[simd_loop_idx]

    return filter_loops


def explore_simd_vectorization(config):
    """ Explore the stage of SIMD vectorization.

    When AutoSA reaches this stage, we will have the systolic array dimension
    in the tuning information. If the pruning is enabled at this stage,
    we will first filter out the designs not satisfying the pruning requirements
    for the PE structures. (SIMD_vectorization_PE_pruning)
    Next, we will limit the candidate loop upperbounds by examining the scores and
    legality information in the tuning info. Only the upperbound for the legal loop
    with the maximal score is kept, and all the rest is set to 1. (simd_loop_filter)
    After the above steps, we will go through the standard precedurs as to generate
    the candidate loops, compile the program, and move forward to the next stage.

    """
    pruning_en = config['setting'][config['mode']]['pruning']['SIMD_vectorization']['enable']
    if config['autosa_config']['simd']['mode'] == 'manual':
        with open(f'{config["work_dir"]}/output/tuning.json') as f:
            tuning = json.load(f)
        if 'simd' not in tuning:
            # No SIMD opportunities found, we will skip this stage
            explore_design(config)
        else:    
            PE_pruning_postpone = 0
            if pruning_en:                
                # Perform early pruning based on the PE numbers
                config['tuning'] = tuning
                if 'sa_dims' in config['tuning']['simd']:
                    #print(PE_pruning_postpone)          
                    if opt_prune.SIMD_vectorization_PE_pruning(config):
                        return
                else:
                    PE_pruning_postpone = 1
            #print(PE_pruning_postpone)                    
            loops = tuning['simd']['tilable_loops']
            # Filter the SIMD loops
            loops = simd_loop_filter(loops, tuning)
            loops_pool = generate_loop_candidates(
                loops, config, "SIMD_vectorization")

            if len(loops_pool) == 0:
                simd_en = config['autosa_config']['simd']['enable']
                sa_sizes = config['sa_sizes'].copy()
                config['autosa_config']['simd']['enable'] = 0
                with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
                    json.dump(config['autosa_config'], f, indent=4)

                ret = execute_autosa_cmd(config)
                if ret != 0:
                    config['logger'].error(f'CMD failed with error code {ret}')
                    config['autosa_config']['simd']['enable'] = simd_en
                    config['sa_sizes'] = sa_sizes
                    return
                if PE_pruning_postpone:
                    with open(f'{config["work_dir"]}/output/tuning.json') as f:
                        tuning = json.load(f)              
                    config['tuning'] = tuning  
                    if opt_prune.SIMD_vectorization_PE_pruning(config, 1):
                        config['autosa_config']['simd']['enable'] = simd_en
                        config['sa_sizes'] = sa_sizes
                        return
                explore_design(config)
                config['autosa_config']['simd']['enable'] = simd_en
                config['sa_sizes'] = sa_sizes
                with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
                    json.dump(config['autosa_config'], f, indent=4)
            else:
                if config['mode'] == 'search' and config['setting']['search']['metric'] == 'latency' \
                    and pruning_en:
                    loops_pool = opt_prune.reorder_simd_loops(loops_pool)
                for loop in loops_pool:
                    sa_sizes = config['sa_sizes'].copy()
                    config['sa_sizes'].append(
                        f'kernel[]->simd{str(loop).replace(" ", "")}')
                    config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])

                    #start_time = time.perf_counter()
                    ret = execute_autosa_cmd(config)
                    #run_time = time.perf_counter() - start_time
                    #print(f'runtime: {run_time}')

                    if ret != 0:
                        config['logger'].error(f'CMD failed with error code {ret}')
                        config['sa_sizes'] = sa_sizes
                        continue
                    if PE_pruning_postpone:
                        with open(f'{config["work_dir"]}/output/tuning.json') as f:
                            tuning = json.load(f)              
                        config['tuning'] = tuning  
                        if opt_prune.SIMD_vectorization_PE_pruning(config, 1):                            
                            config['sa_sizes'] = sa_sizes
                            continue

                    explore_design(config)
                    config['sa_sizes'] = sa_sizes

                    if config['mode'] == 'search' and config['setting']['search']['metric'] == 'latency' \
                        and pruning_en:
                        if opt_prune.SIMD_vectorization_latency_pruning(config):
                            return
    else:
        explore_design(config)

    return


def explore_latency_hiding(config):
    """ Explore the stage of latency hiding.


    """
    if config['autosa_config']['latency']['mode'] == 'manual':
        # Fetch the tuning info
        with open(f'{config["work_dir"]}/output/tuning.json') as f:
            tuning = json.load(f)
        if 'latency' not in tuning:
            # This stage is skippd by AutoSA, we will also skip it
            latency_hiding_en = config['autosa_config']['latency']['enable']
            sa_sizes = config['sa_sizes'].copy()
            config['autosa_config']['latency']['enable'] = 0
            with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
                json.dump(config['autosa_config'], f, indent=4)
            ret = execute_autosa_cmd(config)
            if ret != 0:
                config['logger'].error(f'CMD failed with error code {ret}')
                config['autosa_config']['latency']['enable'] = latency_hiding_en
                config['sa_sizes'] = sa_sizes
                return
            explore_simd_vectorization(config)

            config['autosa_config']['latency']['enable'] = latency_hiding_en
            config['sa_sizes'] = sa_sizes
            with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
                json.dump(config['autosa_config'], f, indent=4)
            return

        loops = tuning['latency']['tilable_loops']        
        loops_pool = generate_loop_candidates(loops, config, "latency_hiding")
        if config['setting'][config['mode']
                             ]['pruning']['latency_hiding']['enable']:
            config['tuning'] = tuning
            loops_pool = opt_prune.latency_hiding_loops_pruning(
                loops_pool, config)

        if len(loops_pool) == 0:
            # Latency hiding is a must. In this case, we will stop exploration and return.
            return
        else:
            for loop in loops_pool:
                # Hack: For GEMM4
                #loop[-1] = 1

                sa_sizes = config['sa_sizes'].copy()
                config['sa_sizes'].append(
                    f'kernel[]->latency{str(loop).replace(" ", "")}')
                config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
                ret = execute_autosa_cmd(config)
                if ret != 0:
                    config['logger'].error(f'CMD failed with error code {ret}')
                    config['sa_sizes'] = sa_sizes
                    continue
                explore_simd_vectorization(config)
                config['sa_sizes'] = sa_sizes
    else:
        explore_simd_vectorization(config)

    return


def explore_array_part_L2(config):
    """ Explore the stage of second-level array partitioning.

    """
    if config['autosa_config']['array_part_L2']['mode'] == 'manual':
        # Fetch the tuning info
        with open(f'{config["work_dir"]}/output/tuning.json') as f:
            tuning = json.load(f)
        loops = tuning['array_part_L2']['tilable_loops']
        coincident = tuning['array_part_L2']['coincident']
        # Generate the tiling factors to proceed
        loops_pool = generate_loop_candidates(loops, config, 'array_part_L2')
        if config['setting'][config['mode']
                             ]['pruning']['array_part_L2']['enable']:
            config['tuning'] = tuning
            loops_pool = opt_prune.array_part_L2_loops_pruning(
                loops_pool, config)

        if len(loops_pool) == 0:
            # No available tiling options, we will disable this step and skip
            # it.
            array_part_L2_en = config['autosa_config']['array_part_L2']['enable']
            sa_sizes = config['sa_sizes'].copy()
            config['autosa_config']['array_part_L2']['enable'] = 0
            with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
                json.dump(config['autosa_config'], f, indent=4)

            ret = execute_autosa_cmd(config)
            if ret != 0:
                config['logger'].error(f'CMD failed with error code {ret}')
                config['autosa_config']['array_part_L2']['enable'] = array_part_L2_en
                config['sa_sizes'] = sa_sizes
                return
            explore_latency_hiding(config)
            # Revert the changes
            config['autosa_config']['array_part_L2']['enable'] = array_part_L2_en
            config['sa_sizes'] = sa_sizes
            with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
                json.dump(config['autosa_config'], f, indent=4)
        else:
            for loop in loops_pool:
                sa_sizes = config['sa_sizes'].copy()
                config['sa_sizes'].append(
                    f'kernel[]->array_part_L2{str(loop).replace(" ", "")}')
                config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
                ret = execute_autosa_cmd(config)
                if ret != 0:
                    config['logger'].error(f'CMD failed with error code {ret}')
                    config['sa_sizes'] = sa_sizes
                    continue
                explore_latency_hiding(config)
                config['sa_sizes'] = sa_sizes
    else:
        explore_latency_hiding(config)


def explore_array_part_single_job(loops, config, work_dir, is_multi_process=0):
    """ Explore the stage of array partitioning with single process.

    Parameters
    ----------
    loops:
        Candidate loops.
    config:
        Global configuration.
    work_dir: str
        The current work directory.
    is_multi_process: int
        Is multi process launched.
    """
    # Modify the commands
    config['cmds'][1] = f'--config={work_dir}/autosa_config.json'
    config['cmds'][2] = f'--output-dir={work_dir}/output'
    config['work_dir'] = work_dir
    config['logger'] = logging.getLogger('AutoSA-Optimizer')

    # Progress meter
    total_tasks = len(loops)
    finished_tasks = 0
    for loop in loops:
        sa_sizes = config['sa_sizes'].copy()
        config['sa_sizes'].append(
            f'kernel[]->array_part{str(loop).replace(" ", "")}')
        config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
        ret = execute_autosa_cmd(config)
        if ret != 0:
            config['logger'].error(f'CMD failed with error code {ret}')
            config['sa_sizes'] = sa_sizes
            continue
        if config['two_level_buffer']:
            explore_array_part_L2(config)
        else:
            explore_latency_hiding(config)
        config['sa_sizes'] = sa_sizes
        finished_tasks += 1
        config['logger'].info(f'Progress(PID: {os.getpid()}): [{finished_tasks}/{total_tasks}]')

    if is_multi_process:
        config['logger'] = None
    return config


def explore_array_part(config):
    """ Explore the stage of array partitioning.

    If this stage is set in Manual mode, this function will load the tuning
    info which contains all the tilable loops.
    This function will then generate all possible loop tiling combination.
    If stage pruning is enabled, these loop candidates will be pruned
    based on certain heuristics.
    Next, this function will iterate through these combinations and proceed to
    the next stage.
    If multi-processing is enabled, the optimizer folder directory will
    be updated to allocate a workspace for each forked process.
    We will distribute these loops equally to all the processes to proceed.

    Otherwise, we will skip this stage and jump to the next stage.
    As for the next stage, we will go to:
    - array_part_L2 if config['two_level_buffer'] is enabled
    - latency_hiding if config['two_level_buffer'] is disabled

    We apply the following heuristic to prune the candidate loops.
    - The product of tiling factors should be no less than the #PE lower bound.

    Parameters
    ----------
    config: dict
        Global configuration.
    """
    if config['autosa_config']['array_part']['mode'] == 'manual':
        # The program will terminate after array partitioning
        # Fetch the tuning info
        with open(f'{config["work_dir"]}/output/tuning.json') as f:
            tuning = json.load(f)
        loops = tuning['array_part']['tilable_loops']
        # Generate the tiling factors to proceed
        loops_pool = generate_loop_candidates(loops, config, 'array_part')
        if config['setting'][config['mode']
                             ]['pruning']['array_part']['enable']:
            # Apply pruning on the candidate loops
            loops_pool = opt_prune.array_part_loops_pruning(loops_pool, config)

        if len(loops_pool) == 0:
            # No available tiling options, we will disable this step and skip it.
            # At the same time, two-level-buffer is also disabled
            array_part_en = config['autosa_config']['array_part']['enable']
            array_part_L2_en = config['autosa_config']['array_part_L2']['enable']
            sa_sizes = config['sa_sizes'].copy()
            config['autosa_config']['array_part']['enable'] = 0
            config['autosa_config']['array_part_L2']['enable'] = 0
            with open(f'config["work_dir"]/autosa_config.json', 'w') as f:
                json.dump(config['autosa_config'], f, indent=4)

            ret = execute_autosa_cmd(config)
            if ret != 0:
                config['logger'].error(f'CMD failed with error code {ret}')
                config['autosa_config']['array_part']['enable'] = array_part_en
                config['autosa_config']['array_part_L2']['enable'] = array_part_L2_en
                config['sa_sizes'] = sa_sizes
                return
            explore_latency_hiding(config)
            # Revert the changes
            config['autosa_config']['array_part']['enable'] = array_part_en
            config['autosa_config']['array_part_L2']['enable'] = array_part_L2_en
            config['sa_sizes'] = sa_sizes
            with open(f'config["work_dir"]/autosa_config.json', 'w') as f:
                json.dump(config['autosa_config'], f, indent=4)
        else:
            if config['setting'][config['mode']]['multiprocess']['n_job'] > 1 and len(loops_pool) > 1:
                multi_process(
                    loops_pool,
                    explore_array_part_single_job,
                    config)
            else:
                explore_array_part_single_job(
                    loops_pool, config, config['work_dir'])
    else:
        if config['autosa_config']['array_part_L2']['enable']:
            explore_array_part_L2(config)
        else:
            explore_latency_hiding(config)


def explore_space_time(config):
    """ Explore the stage of space-time transformation.

    If this stage is set in Manual mode, we will load the tuning info
    and iterate through all possible kernels to proceed.
    Otherwise, AutoSA automatically selects one kernel to proceed.
    We will directly jump to the next stage: array partitioning.

    Parameters
    ----------
    config: dict
        Global configuration.
    """
    if config['autosa_config']['space_time']['mode'] == 'manual':
        # The program will terminate after the space-time transformation
        # Fetch the tuning info
        with open(f'{config["work_dir"]}/output/tuning.json') as f:
            tuning = json.load(f)
        if 'space_time' not in tuning:
            # Users have assigned the space-time options, we will skip this stage
            explore_array_part(config)
        else:
            n_kernel = tuning['space_time']['n_kernel']

            # Iterate through different kernels
            #for kernel_id in [0]:
            for kernel_id in range(n_kernel):
                config['logger'].info(f'Search kernel {kernel_id}...')
                sa_sizes = config['sa_sizes'].copy()
                config['sa_sizes'].append(f'kernel[]->space_time[{kernel_id}]')
                config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
                ret = execute_autosa_cmd(config)
                if ret != 0:
                    config['logger'].error(f'CMD failed with error code {ret}')
                    config['sa_sizes'] = sa_sizes
                    continue
                explore_array_part(config)
                config['sa_sizes'] = sa_sizes
    else:
        explore_array_part(config)


@timer
def explore_design_space(config):
    """ Explore the design space through multiple stages

    We will expand the design space through multiple stages:
    space-time transformation ->
    array partitioning ->
    latency hiding ->
    SIMD vectorization

    At each stage, we will generate a new cmd and execute it to obtain the tuning
    information for the next stage.
    The cmd list:
    - config['cmds'][0]: the original user command
    - config['cmds'][1]: the AutoSA config file
    - config['cmds'][2]: the AutoSA output directory
    - config['cmds'][3]: the AutoSA sizes

    Parameters
    ----------
    config: dict
        Global configuration.
    """
    # Execute the cmd
    config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
    ret = execute_autosa_cmd(config)
    if ret != 0:
        config['logger'].error(f'CMD failed with error code {ret}')
        config['sa_sizes'] = []
        return
    # Enter the first stage: space-time transformation
    explore_space_time(config)

def synth_train_samples_single_job(config, job_id):
    """ Launch HLS synthesis for each single process

    """
    config['logger'] = logging.getLogger('AutoSA-Optimizer')
    autosa_prj_path = os.environ['AUTOSA_ROOT']
    work_dir = f'{config["work_dir"]}/job{job_id}'
    kernels = os.listdir(work_dir)
    for kernel in kernels:
        path = f'{work_dir}/{kernel}'
        designs = os.listdir(path)
        for design in designs:
            prj_path = f'{path}/{design}/output'
            # Copy the HLS TCL script to the project
            ret = execute_sys_cmd(
                f'cp {autosa_prj_path}/autosa_scripts/hls_scripts/hls_script_synth.tcl {prj_path}/hls_script.tcl',
                config)
            # Execute the TCL
            cwd = os.getcwd()
            os.chdir(prj_path)
            ret = execute_sys_cmd('vivado_hls -f hls_script.tcl', config)
            os.chdir(cwd)

@timer
def generate_train_samples(config):
    """ Generate the training samples.

    """
    # Prepare the directory and files
    tmp_dir = config['tmp_dir']
    if os.path.exists(f'{tmp_dir}/optimizer/training'):
        shutil.rmtree(f'{tmp_dir}/optimizer/training')
    os.mkdir(f'{tmp_dir}/optimizer/training')
    os.mkdir(f'{tmp_dir}/optimizer/training/job0')
    # Initialize file directory
    Path(f'{config["work_dir"]}/output').mkdir(exist_ok=True)
    Path(f'{config["work_dir"]}/output/src').mkdir(exist_ok=True)
    Path(f'{config["work_dir"]}/output/latency_est').mkdir(exist_ok=True)
    Path(f'{config["work_dir"]}/output/resource_est').mkdir(exist_ok=True)
    with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
        json.dump(config['autosa_config'], f, indent=4)

    while config['monitor']['n_designs'] < config['setting']['synth']['sample']['n']:
        # Collect enough training samples
        explore_design_space(config)
    config['logger'].info(f'{config["monitor"]["n_designs"]} designs are generated.')

@timer
def synth_train_samples(config):
    """ Synthesize the trainig samples.

    We will sample a few designs generated from the previous training exploration.
    Next, we call Vivado HLS to synthesize each design.

    """
    tmp_dir = config['tmp_dir']
    config['work_dir'] = f'{tmp_dir}/optimizer/training'
    # Collect all designs into a list
    design_paths = {}
    for n in range(config['setting']['training']['multiprocess']['n_job']):
        f_path = f'{config["work_dir"]}/job{n}'
        f_list = os.listdir(f_path)
        for f in f_list:
            if 'kernel' in f:
                if f not in design_paths:
                    design_paths[f] = []
                d_path = f'{f_path}/{f}'
                d_list = os.listdir(d_path)
                for d in d_list:
                    prj_path = f'{d_path}/{d}'
                    design_paths[f].append(prj_path)
    # Random sample a few designs for each kernel and build the synthesis folder
    config['work_dir'] = f'{tmp_dir}/optimizer/synth'
    if os.path.exists(config['work_dir']):
        shutil.rmtree(config['work_dir'])
    os.mkdir(config['work_dir'])
    num_proc = min(multiprocessing.cpu_count(),
                   config['setting']['synth']['multiprocess']['n_job'])
    for i in range(num_proc):
        prj_dir = config['work_dir'] + f'/job{i}'
        os.mkdir(prj_dir)
    tasks = []
    for kernel in design_paths:
        designs = design_paths[kernel]
        n_sample = config['setting']['synth']['sample']['n']
        if n_sample < len(designs):
            designs = random.sample(designs, n_sample)
        # Push to the list
        for design in designs:
            tasks.append((kernel, design))
    # Uniformly distribute the tasks to each processor
    chunk_size = int(np.ceil(float(len(tasks)) / num_proc))
    task_chunks = [tasks[i: i + min(chunk_size, len(tasks) - i)]
                   for i in range(0, len(tasks), chunk_size)]
    for job_id in range(len(task_chunks)):
        task_chunk = task_chunks[job_id]
        for task in task_chunk:
            kernel = task[0]
            design_path = task[1]
            design = design_path.rsplit('/', 1)[-1]
            if not os.path.exists(
                    f'{config["work_dir"]}/job{job_id}/{kernel}'):
                os.mkdir(f'{config["work_dir"]}/job{job_id}/{kernel}')
            new_design_path = f'{config["work_dir"]}/job{job_id}/{kernel}/{design}'
            # copy the design files
            ret = execute_sys_cmd(
                f'cp -r {design_path} {new_design_path}', config)

    # Execute the HLS synthesis
    pool = multiprocessing.Pool(processes=num_proc)
    config['logger'].info(f'Launch HLS synthesis with {num_proc} processes...')
    logger = config['logger']
    config['logger'] = None
    ret = pool.starmap(
        synth_train_samples_single_job, [
            (config, i) for i in range(num_proc)])
    config['logger'] = logger


def train_xilinx(config):
    """ Train the resource and latency models on Xilinx platforms.

    This function first creates training samples by randomly sampling all
    the design points.
    Then it calls Vivado HLS to synthesize all designs.
    Next it collects the results and trains the resource and latency models
    using linear regression.

    Parameters
    ----------
    config: dict
        Global configuration.
    """
    config['mode'] = 'training'

    # Generate sample designs
    config['logger'].info('Generate training samples...')
    generate_train_samples(config)

    # Synthesize designs
    config['logger'].info('Synthesize training samples...')
    synth_train_samples(config)

    # Train the resource models
    config['logger'].info('Train resource models...')
    train_resource_models_xilinx(config)

    ## Train the latency models
    # config['logger'].info('Train latency models...')
    # train_latency_models_xilinx(config) # TODO

def get_default_pruning_policy(mode):
    """ Return the default search pruning policy.

    """
    #TODO
    return

def get_sample_policy(mode, n_random=2):
    """ Return the search sampling policy.

    Parameters
    ----------
    mode: str
        Sampling mode.
    n_random: int
        The higher the random level, the more samples are generated.
    """
    if mode == 'random':
        ret = {
            "array_part": {
                "mode": "random",
                "n": n_random,
                "loop_limit": -1
            },
            "array_part_L2": {
                "mode": "random",
                "n": n_random,
                "loop_limit": -1
            },
            "latency_hiding": {
                "mode": "random",
                "n": n_random,
                "loop_limit": 64
            },
            "SIMD_vectorization": {
                "mode": "random",
                "n": n_random,
                "loop_limit": 8
            }
        }
    elif mode == 'exhaustive':
        ret = {
            "array_part": {
                "mode": "exhaustive",
                "n": -1,
                "loop_limit": -1
            },
            "array_part_L2": {
                "mode": "exhaustive",
                "n": -1,
                "loop_limit": -1
            },
            "latency_hiding": {
                "mode": "exhaustive",
                "n": -1,
                "loop_limit": 64
            },
            "SIMD_vectorization": {
                "mode": "exhaustive",
                "n": -1,
                "loop_limit": 8
            }
        }
    else:
        raise RuntimeError(f'Unknown sampling mode: {mode}')

    return ret

def print_best_design(opt_design, hw_info=None):
    """ Pretty print the best design.

    Parameters
    ----------
    opt_design: dict
        Optimal design.

    Returns
    -------
    ret: str
        Printed design in a string.
    """
    ret = (
        f"\n======== Best design ========\n"
        f"Latency(Cycle): {int(opt_design['latency'])}\n"
        f"Power(W): {opt_design['power']}\n"
        f"Resource:\n"
    )

    if 'FF' in opt_design['resource']:
        ret += f"\tFF: {int(opt_design['resource']['FF'])}"
        if hw_info:
            ratio = float(opt_design['resource']['FF']) / hw_info['FF']
            ret += f" ({ratio:.2f})"
        ret += "\n"
    if 'LUT' in opt_design['resource']:
        ret += f"\tLUT: {int(opt_design['resource']['LUT'])}"
        if hw_info:
            ratio = float(opt_design['resource']['LUT']) / hw_info['LUT']
            ret += f" ({ratio:.2f})"
        ret += "\n"
    if 'BRAM18K' in opt_design['resource']:
        ret += f"\tBRAM18K: {int(opt_design['resource']['BRAM18K'])}"
        if hw_info:
            ratio = float(opt_design['resource']['BRAM18K']) / hw_info['BRAM18K']
            ret += f" ({ratio:.2f})"
        ret += "\n"
    if 'URAM' in opt_design['resource']:
        ret += f"\tURAM: {int(opt_design['resource']['URAM'])}"
        if hw_info:
            ratio = float(opt_design['resource']['URAM']) / hw_info['URAM']
            ret += f" ({ratio:.2f})"
        ret += "\n"
    if 'DSP' in opt_design['resource']:
        ret += f"\tDSP: {int(opt_design['resource']['DSP'])}"
        if hw_info:
            ratio = float(opt_design['resource']['DSP']) / hw_info['DSP']
            ret += f" ({ratio:.2f})"
        ret += "\n"
    ret += f"============================="

    return ret

def save_search_log(records, log):
    """ Save the DSE design records to log file.

    Parameters
    ----------
    records: list
        A list of best designs found in the tuning process.
    log: str
        Path to the log file.
    """
    with open(log, 'w') as f:
        json.dump(records, f, indent=4)

def search_xilinx(config):
    """ Perform search phase on Xilinx platform.

    """
    # Prepare the directory and files
    tmp_dir = config['tmp_dir']
    if os.path.exists(f'{tmp_dir}/optimizer/search'):
        shutil.rmtree(f'{tmp_dir}/optimizer/search')
    os.mkdir(f'{tmp_dir}/optimizer/search')
    os.mkdir(f'{tmp_dir}/optimizer/search/job0')
    # Initialize file directory
    Path(f'{config["work_dir"]}/output').mkdir(exist_ok=True)
    Path(f'{config["work_dir"]}/output/src').mkdir(exist_ok=True)
    Path(f'{config["work_dir"]}/output/latency_est').mkdir(exist_ok=True)
    Path(f'{config["work_dir"]}/output/resource_est').mkdir(exist_ok=True)
    with open(f'{config["work_dir"]}/autosa_config.json', 'w') as f:
        json.dump(config['autosa_config'], f, indent=4)

    config['mode'] = 'search'
    config['search_results'] = init_search_results()
    # Modify the command
    #config['cmds'][0] += ' --tuning'

    if config['setting'][config['mode']]['pruning']['random_start']['enable']:
        # Random search the design space
        config['search_results'] = init_search_results()
        # Update the sampling strategy
        user_policy = copy.deepcopy(config['setting'][config['mode']]['sample'])
        config['setting'][config['mode']]['sample'] = get_sample_policy('random',
            config['setting'][config['mode']]['pruning']['random_start']['n_random'])
        n_trial = 0
        while n_trial < config['setting'][config['mode']]['pruning']['random_start']['n_trial']:
            config['logger'].info(f'Run random search to warm up... [{n_trial + 1}/{config["setting"][config["mode"]]["pruning"]["random_start"]["n_trial"]}]')
            explore_design_space(config)
            config['logger'].info(print_best_design(config['search_results']['opt'], config['hw_info']))
            n_trial += 1
        config['setting'][config['mode']]['sample'] = user_policy

    config['logger'].info('Start searching...')
    # Set up the time-out counter
    if config['setting']['search']['time_out'] != -1:
        config['monitor']['time_out_start'] = time.time()
    if config['setting'][config['mode']]['mode'] == 'exhaustive':
        config['logger'].info('Search mode: Exhaustive')
        config['setting'][config['mode']]['sample'] = \
            get_sample_policy(config['setting'][config['mode']]['mode'])
        explore_design_space(config)
    elif config['setting'][config['mode']]['mode'] == 'random':
        config['logger'].info('Search mode: Random')
        config['setting'][config['mode']]['sample'] = \
            get_sample_policy(config['setting'][config['mode']]['mode'],
                config['setting'][config['mode']]['n_random'])
        explore_design_space(config)
    elif config['setting'][config['mode']]['mode'] == 'customized':
        config['logger'].info('Search mode: Customized')
        explore_design_space(config)

    #print(config['monitor']['n_designs'])

    # Print out the best design
    config['logger'].info(print_best_design(config['search_results']['opt'], config['hw_info']))
    # Store the tuning log
    tmp_dir = config['tmp_dir']
    log_path = f'{tmp_dir}/optimizer/search/DSE.log'
    config['logger'].info(f'Saving the DSE results to: {log_path}')
    save_search_log(config['search_results']['records'], log_path)

    return


def init_logger(training, search, verbose, tmp_dir):
    """ Init AutoSA logger.

    Initialize the AutoSA logger.

    Parameters
    ----------
    training: boolean
        Enable training phase.
    search: boolean
        Enable search phase.
    verbose: int
        Logger verbose level.
        0: Print minimal information from Optimizer.
        1: Print all information from Optimizer.
        2: Print information from Optimizer and AutoSA.
    tmp_dir: str
        Path to the temp files.

    Returns
    -------
    logger:
        AutoSA logger.
    """
    logger = logging.getLogger('AutoSA-Optimizer')
    formatter = logging.Formatter(
        '[%(name)s %(asctime)s] %(levelname)s: %(message)s',
        '%Y-%m-%d %H:%M:%S')
    logger.setLevel(logging.INFO)

    s_handler = logging.StreamHandler()
    if training:
        f_handler = logging.FileHandler(
            f'{tmp_dir}/optimizer/training.log', 'w')
    elif search:
        f_handler = logging.FileHandler(f'{tmp_dir}/optimizer/search.log', 'w')
    if verbose > 1:
        s_handler.setLevel(level=logging.DEBUG)
        f_handler.setLevel(level=logging.DEBUG)
    elif verbose == 1:
        s_handler.setLevel(level=logging.INFO)
        f_handler.setLevel(level=logging.INFO)
    else:
        s_handler.setLevel(level=logging.WARNING)
        f_handler.setLevel(level=logging.WARNING)

    s_handler.setFormatter(formatter)
    f_handler.setFormatter(formatter)
    logger.addHandler(s_handler)
    logger.addHandler(f_handler)

    return logger


def init_monitor():
    """ Init monitor for DSE.

    Returns
    -------
    monitor: dict
        "n_designs": number of designs that are examined
        "time_out_start": the starting time for time-out counter
    """
    monitor = {"n_designs": 0, "time_out_start": -1}

    return monitor

def init_search_results():
    """ Init search results for DSE.

    Note: The search results contain two parts: the opt design and the tuning
    records. The opt design is the best design found during the search process.
    The records contain the top designs found during the search process.

    """
    ret = {
        'opt': {
            'found': False,
            'latency': -1,
            'resource': {'FF': -1, 'LUT': -1, 'BRAM18K': -1, 'URAM': -1, 'DSP': -1},
            'power': -1,
            'cmd': None
        },
        'records': []
    }

    return ret

def update_search_results(results, cur_design, n_record, metric, hw_info):
    """ Update the search results.

    Parameters
    ----------
    results: dict
        A dict containing the current search results.
    cur_design: dict
        The current design to be compared.
    n_record: int
        The number of records to be logged in the search results.
    metric: str
        Evaluation metric.
    hw_info: dict
        A dictionary containing the hardware information.
    """
    if metric == 'latency':
        update_design = False
        if not results['opt']['found']:
            results['opt']['found'] = True
            update_design = True
        else:
            update_design = False
            if cur_design['latency'] < results['opt']['latency']:
                update_design = True
            elif cur_design['latency'] == results['opt']['latency']:
                # We compute a score for the resource usage.
                cur_res_score = res_model.compute_res_util_score(cur_design['resource'], hw_info)
                opt_res_score = res_model.compute_res_util_score(results['opt']['resource'], hw_info)
                if cur_res_score < opt_res_score:
                    update_design = True

        if update_design:
            # Update the opt
            results['opt']['latency'] = cur_design['latency']
            results['opt']['resource'] = cur_design['resource']
            results['opt']['cmd'] = cur_design['cmd']
            # Update the records
            results['records'].insert(0, results['opt'].copy())
            results['records'] = results['records'][:n_record]
    else:
        raise NotImplementedError(f'Update search results for power is not supported.')

    return results

def merge_search_results(results, metric, n_record, hw_info):
    """ Merge search results from DSE.

    We will first merge the records and then update the opt design.
    Each result is already sorted. Therefore, we will initialize the return list
    with the first result. For the following results, we will insert them into the
    return list by comparing the metrics.

    Parameters
    ----------
    results: list
        A list of results to merge.
    metric: str
        The DSE evaluation metric.
    n_record: int
        Number of top records to keep.
    hw_info: dict
        Hardware information.

    Returns
    -------
    ret: dict
        A dict containing the merged search results
    """
    ret = init_search_results()
    if metric == 'latency':
        is_first = 1
        for result in results:
            if len(result['records']) == 0:
                continue

            if is_first == 1:
                ret = result
                is_first = 0
            else:
                records = result['records']
                for record in records:
                    inserted = False
                    for cmp_id in range(len(ret['records'])):
                        cmp_record = ret['records'][cmp_id]
                        # Check if it is a duplicate record
                        if record['cmd'] == cmp_record['cmd']:
                            inserted = True
                            break

                        if record['latency'] < cmp_record['latency']:
                            ret['records'].insert(cmp_id, record)
                            inserted = True
                            break
                        elif record['latency'] == cmp_record['latency']:
                            cur_res_score = res_model.compute_res_util_score(record['resource'], hw_info)
                            cmp_res_score = res_model.compute_res_util_score(cmp_record['resource'], hw_info)
                            if cur_res_score < cmp_res_score:
                                ret['records'].insert(cmp_id, record)
                                inserted = True
                                break
                            elif cur_res_score == cmp_res_score:
                                # Duplicated
                                inserted = True
                                break

                    if inserted == False:
                        ret['records'].append(record)

                ret['opt'] = ret['records'][0]
                ret['records'] = ret['records'][:n_record]

        return ret
    else:
        raise NotImplementedError(f'Merge results for metric {metric} is not supported.')

def init_config(setting, verbose, hw_info, cmd, training, search, tmp_dir):
    """ Init AutoSA Optimizer global configuration.

    Init the global configuration used in Optimizer.

    Parameters
    ----------
    setting: dict
        AutoSA Optimizer setting.
    verbose: int
        Print verbose level.
    tmp_dir: str
        Path to the temporary files.

    Note
    ----
    Configuration is a dictionary containing the following info:
      setting: dict
        AutoSA Optimizer setting.
      verbose: int
        Print verbose level.
      stdout:
        Stdout pipe.
      work_dir: str
        The default working directory.
      hw_info: dict
        The hardware configuration.
      logger:
        The default logger.
      cmds: list
        A list of AutoSA commands.
          [0]: The user input command.
          [1]: AutoSA configuration file.
          [2]: AutoSA output directory.
          [3]: AutoSA sizes.
      sa_sizes: list
        A list of AutoSA tiling factors.
      two_level_buffer: boolean
        Is two_level_buffer enabled.
      hbm: boolean
        Is HBM enabled.
      kernel_file_path: str
        Input kernel file path.
      simd_info: dict
        Kernel SIMD information.
      tuning: dict
        Temporary tuning information from AutoSA.
      monitor: dict
        A dictionary storing the monitoring information of the DSE
          "n_designs": number of designs that are examined

    Returns
    -------
    config: dict
        Initialized global configuration.
    """
    config = {}
    config['setting'] = setting
    config['verbose'] = verbose
    config['tmp_dir'] = tmp_dir
    if verbose == 2:
        # Print AutoSA info
        config['stdout'] = None
    else:
        config['stdout'] = subprocess.DEVNULL
    if training:
        config['work_dir'] = f'{tmp_dir}/optimizer/training/job0'
    else:
        config['work_dir'] = f'{tmp_dir}/optimizer/search/job0'
    with open(hw_info) as f:
        config['hw_info'] = json.load(f)
    config['cmds'] = [cmd]
    config['cmds'].append(
        f'--autosa-config={config["work_dir"]}/autosa_config.json')
    config['cmds'].append(f'--autosa-output-dir={config["work_dir"]}/output')
    config['cmds'].append('')
    config['sa_sizes'] = []
    # Look up if sa_sizes are pre-set in the cmd
    if config['cmds'][0].find('sa-sizes') != -1:
        m = re.search(r'--sa-sizes="{(.+?)}"', config['cmds'][0])
        if m:
            for size in m.group(1).split(';'):
                config['sa_sizes'].append(size)
            # delete the sa_sizes from the cmd
            config['cmds'][0] = re.sub(r'--sa-sizes=".+?"', '', config['cmds'][0])
    if cmd.find('two-level-buffer') != -1:
        config['two_level_buffer'] = 1
    else:
        config['two_level_buffer'] = 0
    if cmd.find('hbm') != -1:
        config['hbm'] = 1
    else:
        config['hbm'] = 0
    # Load SIMD info file
    kernel_file_path = cmd.split()[1]
    kernel_file_path = kernel_file_path.rsplit('/', 1)[0]
    config['kernel_file_path'] = kernel_file_path
    config['simd_info'] = None
    with open(kernel_file_path + '/simd_info.json', 'r') as f:
        config['simd_info'] = json.load(f)

    return config


def xilinx_run(cmd, hw_info, setting, training, search, verbose, tmp_dir):
    """ Design space exploration on Xilinx platform.

    The following four stages are explored in the DSE:
    - space-time transformation
    - array partitioning
    - latnecy hiding
    - simd vectorization

    The DSE includes two phaese: training phase and search phase
    In the tranining phase, for each systolic array candidate, we generate
    a set of tuning parameters for the later three stages. This step
    creates a suite of designs. We will use training samples to train the
    regression models for the latency and resource usage of the design.

    After the training stage is done, we enter the search phase.
    In this phase, for each systolic array, we will explore all different
    tiling factors in the later three stages. After the tuning parameters
    of each stage is determined, we estimate the latency and resource usage
    of the design using the pre-trained regression model.
    Finally, the design with the least latency and under the resource contraints
    is selected.

    Folder structure:
    autosa.tmp
    - optimizer
      - [training.log | search.log]
      - training
        - job0
          - autosa_config.json
          - output
            - src
            - latency_est
            - resource_est
          - design0
          - design1
      - search
        - job0
        - job1

    Paramters
    ---------
    cmd: str
        Command line to run AutoSA.
    info: str
        Path to FPGA platform hardware resource information file.
    setting: dict
        Optimizer settings.
    training: boolean
        Enable traning phase.
    search: boolean
        Enable search phase.
    verbose: int
        Print verbose information.
    tmp_dir: str
        Path to the folder that stores the temp files.
    """

    if not os.path.exists(f'{tmp_dir}/optimizer'):
        os.mkdir(f'{tmp_dir}/optimizer')

    # Init logger and optimizer config
    logger = init_logger(training, search, verbose, tmp_dir)
    config = init_config(setting, verbose, hw_info, cmd, training, search, tmp_dir)
    config['logger'] = logger
    # Init monitor
    config['monitor'] = init_monitor()

    # Init the AutoSA configuration
    autosa_config = {"space_time": {"mode": "manual"},
                     "array_part": {"enable": 1, "mode": "manual"},
                     "array_part_L2": {
        "enable": config['two_level_buffer'],
        "mode": "manual"},
        "latency": {"enable": 1, "mode": "manual"},
        "simd": {"enable": 1, "mode": "manual"},
        "hbm": {"enable": config['hbm'], "mode": "manual"}}
    config['autosa_config'] = autosa_config

    # Training phase
    if training:
        config['logger'].info(f'Run training phase...')
        train_xilinx(config)

    # Search phase
    if search:
        config['logger'].info(f'Run search phase...')
        search_xilinx(config)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='==== AutoSA Optimizer ====')
    parser.add_argument(
        '-c',
        '--cmd',
        metavar='CMD',
        required=True,
        help='AutoSA command line')
    parser.add_argument(
        '-i',
        '--info',
        metavar='INFO',
        required=True,
        help='hardware resource information')
    parser.add_argument(
        '-s',
        '--setting',
        metavar='SETTING',
        required=False,
        default='autosa_config/optimizer_settings.json',
        help='optimizer settings')
    parser.add_argument(
        '-p',
        '--platform',
        metavar='PLATFORM',
        required=True,
        help='hardware platform: intel/xilinx')
    parser.add_argument(
        '--training',
        action='store_true',
        help='run training phase')
    parser.add_argument(
        '--search',
        action='store_true',
        help='run search phase')
    parser.add_argument(
        '--verbose',
        type=int,
        required=False,
        default=1,
        help='provide verbose information [0-2]')
    parser.add_argument(
        '--tmp-dir',
        required=False,
        default='./autosa.tmp',
        help='temporary file directory')

    args = parser.parse_args()

    # Parse the settings into a dict
    with open(args.setting) as f:
        setting = json.load(f)

    if args.platform == 'intel':
        print("Intel platform is not supported yet!")  # TODO
    elif args.platform == 'xilinx':
        xilinx_run(
            args.cmd,
            args.info,
            setting,
            args.training,
            args.search,
            args.verbose,
            args.tmp_dir)
