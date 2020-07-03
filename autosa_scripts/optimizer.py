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

import optimizer_prune as opt_prune

def timer(func):
  """ Print the runtime of the decorated function.

  """
  @functools.wraps(func)
  def wrapper_timer(*args, **kwargs):
    start_time = time.perf_counter()
    value = func(*args, **kwargs)
    end_time = time.perf_counter()
    run_time = end_time - start_time
    print(f'Finished {func.__name__} in {run_time:.4f} secs')
    return value
  retrun wrapper_timer

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
  num_proc = min(multiprocessing.cpu.count(), config['setting'][config['mode']]['multiprocess']['n_job'])
  # Split the loops into chunks
  chunk_size = int(np.ceil(float(len(loops)) / num_proc))
  loop_chunks = [loops[i : i + min(chunk_size, len(loops) - i)] \
    for i in range(0, len(loops), chunk_size)]
  pool = multiprocessing.Pool(processes = num_proc)
  # Allocate new work spaces for each forked process
  for i in range(num_proc):
    if i == 0:
      continue
    prj_dir = config['work_dir'][:-1] + str(i)    
    ret = execute_sys_cmd(f'mkdir -p {prj_dir}')
    ret = execute_sys_cmd(f'mkdir -p {prj_dir}/output')
    ret = execute_sys_cmd(f'mkdir -p {prj_dir}/output/latency_est')
    ret = execute_sys_cmd(f'mkdir -p {prj_dir}/output/resource_est')
    ret = execute_sys_cmd(f'mkdir -p {prj_dir}/output/src')
    ret = execute_sys_cmd(f'cp {config["work_dir"]}/autosa_config.json {prj_dir}/')

  config['logger'].info(f'Forking {num_proc} processes')       
  verbose = config['verbose']   
  stdout = config['stdout']
  config['verbose'] = 0
  config['stdout'] = subprocess.DEVNULL
  
  # Execute the function
  results = pool.starmap(func, \
    [(loop_chunks[i], copy.deepcopy(config), config['work_dir'][:-1] + str(i))]) \
      for i in range(len(loop_chunks))])
  # Aggregate the results
  for result in results:
    if config['opt_latency'] == -1 or \
       (result['opt_latency'] != -1 and result['opt_latency'] < config['opt_latency']):
      config['opt_latency'] = result['opt_latency']
      config['opt_resource'] = result['opt_resource']     

  config['verbose'] = verbose
  config['stdout'] = stdout

  return

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
  cmd = '--sa-sizes={'
  for size in sa_sizes:
    if not first:
      cmd += ';'
    cmd += size
    first = 0

  cmd += '}'
  return cmd

@timer
def train_resource_models_xilinx(config):
  return

@timer
def train_latency_models_xilinx(config):
  return

@timer
def synth_train_samples(config)
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
  cmd = ' '.join(config['cmds'])
  config['logger'].info(f'Execute CMD: {cmd}')
  ret = subprocess.Popen(cmd, shell=True, stdout=config['stdout'])
  return ret.returncode

def execute_sys_cmd(cmd):
  """ Execute the system command.

  Parameters
  ----------
  cmd: str
    Command to execute.
  """
  config['logger'].info(f'Execute CMD: {cmd}')
  ret = subprocess.Popen(cmd, shell=True, stdout=config['stdout'])
  return ret.returncode

def explore_latency_hiding(config):
  return

def explore_array_part_L2(config):
  """ Explore the stage of second-level array partitioning.

  """
  if config['autosa_config']['array_part_L2']['mode'] == 'manual':
    # Fetch the tuning info
    with open(f'config["work_dir"]/output/tuning.json') as f:
      tuning = json.load(f)
    loops = tuning['array_part_L2']['tilable_loops']
    coincident = tuning['array_part_L2']['coincident']
    # Generate the tiling factors to proceed
    loops_pool = generate_loop_candidates(loops, config)    
    if config['setting'][config['mode']]['pruning']['array_part_L2']['enable']:
      loops_pool = opt_prune.array_part_L2_loops_pruning(loops_pool, config) # TODO
    
  else:
    explore_latency_hiding(config)  

def explore_array_part_single_job(loops, config, work_dir):
  """ Explore the stage of array partitioning with single process.

  Parameters
  ----------
    loops: 
      Candidate loops.
    config: 
      Global configuration.
    work_dir: str
      The current work directory.
  """
  # Modify the commands
  config['cmds'][1] = f'--config={work_dir}/autosa_config.json'
  config['cmds'][2] = f'--output-dir={work_dir}/output'
  conifg['work_dir'] = work_dir
  
  # Progress meter
  total_tasks = len(loops)
  finished_tasks = 0
  for loop in loops:
    sa_sizes = config['sa_sizes'].copy()
    config['sa_sizes'].append(f'kernel[0]->array_part[{str(loop).replace(' ', '')}]')
    config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
    ret = execute_autosa_cmd(config)
    if ret != 0:
      config['logger'].error(f'CMD: {cmd} failed with error code {ret}')
      config['sa_sizes'] = sa_sizes
      continue
    if config['two_level_buffer']:
      explore_array_part_L2(config)
    else:
      explore_latency_hiding(config)
    config['sa_sizes'] = sa_sizes
    finished_tasks += 1
    config['logger'].info(f'Progress: [{finished_tasks}/{total_tasks}]')  

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
    with open(f'config["work_dir"]/output/tuning.json') as f:
      tuning = json.load(f)
    loops = tuning['array_part']['tilable_loops']
    # Generate the tiling factors to proceed
    loops_pool = generate_loop_candidates(loops, config) # TODO
    if config['setting'][config['mode']]['pruning']['array_part']['enable']:
      # Apply pruning on the candidate loops
      loops_pool = opt_prune.array_part_loops_pruning(loops_pool, config) # TODO

    if len(loops_pool) == 0:
      # No available tiling options, we will disable this step and skip it.
      # At the same time, two-level-buffer is also disabled
      array_part_en = config['autosa_config']['array_part']['enable']
      array_part_L2_en = config['autosa_config']['array_part_L2']['enable']
      config['autosa_config']['array_part']['enable'] = 0
      config['autosa_config']['array_part_L2']['enable'] = 0
      with open(f'config["work_dir"]/autosa_config.json', 'w') as f:
        json.dump(config['autosa_config'], f, indent=4)

      ret = execute_autosa_cmd(config)
      if ret != 0:
        config['logger'].error(f'CMD: {cmd} failed with error code {ret}')
        config['autosa_config']['array_part']['enable'] = array_part_en
        config['autosa_config']['array_part_L2']['enable'] = array_part_L2_en
        return
      explore_latency_hiding(config) # TODO
      # Revert the changes
      config['autosa_config']['array_part']['enable'] = array_part_en
      config['autosa_config']['array_part_L2']['enable'] = array_part_L2_en
      with open(f'config["work_dir"]/autosa_config.json', 'w') as f:
        json.dump(config['autosa_config'], f, indent=4)
    else:
      if config['setting'][config['mode']]['multiprocess']['n_job'] > 1:
        multi_process(loops_pool, explore_array_part_single_job, config)
      else:        
        explore_array_part_single_job(loops_pool, config, config['work_dir']) # TODO
  else:
    if config['autosa_config']['array_part_L2']['enable']:
      explore_array_part_L2(config) # TODO    
    else:
      explore_latency_hiding(config) # TODO
  
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
    with open(f'config["work_dir"]/output/tuning.json') as f:
      tuning = json.load(f)
    n_kernel = tuning['space_time']['n_kernel']

    # Iterate through different kernels
    for kernel_id in range(n_kernel):
      config['logger'].info(f'Search kernel {kernel_id}...')
      sa_sizes = config['sa_sizes'].copy()
      config['sa_sizes'].append(f'kernel[0]->space_time[{kernel_id}]')
      config['cmds'][3] = generate_sa_sizes_cmd(config['sa_sizes'])
      ret = execute_autosa_cmd(config)
      if ret != 0:
        config['logger'].error(f'CMD: {cmd} failed with error code {ret}')
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
  ret = execute_autosa_cmd(config)      
  if ret != 0:
    config['logger'].error(f'CMD: {cmd} failed with error code {ret}')
    return
  # Enter the first stage: space-time transformation
  explore_space_time(config)

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
  config['Logger'].info('Generate training samples...')
  explore_design_space(config) # TODO    

  # Synthesize designs  
  config['Logger'].info('Synthesize training samples...')
  synth_train_samples(config) # TODO    
  
  # Train the resource models
  config['logger'].info('Train resource models...')
  train_resource_models_xilinx(config) # TODO
  
  # Train the latency models
  config['logger'].info('Train latency models...')
  train_latency_models_xilinx(config) # TODO  

def search_xilinx(config):
  """ Perform search phase on Xilinx platform.

  """
  # TODO

def init_logger(training, search, verbose):
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

  Returns
  -------
  logger: 
    AutoSA logger.
  """
  logger = logging.getLogger('AutoSA-Optimizer')
  s_handler = logging.StreamHandler()
  if training:
    f_handler = logging.FileHandler('autosa.tmp/optimizer/training.log')
  elif search:
    f_handler = logging.FileHandler('autosa.tmp/optimizer/search.log')
  if verbose >= 1:
    s_handler.setLevel(logging.INFO)
    f_handler.setLevel(logging.INFO)
  else:
    s_handler.setLevel(logging.WARNING)
    f_handler.setLevel(logging.WARNING)      
  s_format = logging.Formatter('[%(name)s %(asctime)s] %(levelname)s: %(message)s')
  f_format = logging.Formatter('[%(name)s %(asctime)s] %(levelname)s: %(message)s')
  s_handler.setFormatter(s_format)
  f_handler.setFormatter(f_format)
  logger.addHandler(c_handler)
  logger.addHandler(f_handler)

  return logger

def init_config(setting, verbose, hw_info, cmd, training, search):
  """ Init AutoSA Optimizer global configuration.

  Init the global configuration used in Optimizer.

  Paramters
  ---------
  setting: dict
    AutoSA Optimizer setting.
  verbose: int
    Print verbose level.

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

  Returns
  -------
  config: dict
    Initialized global configuration.
  """
  config = {}
  config['setting'] = setting
  config['verbose'] = verbose
  if verbose == 2:
    # Print AutoSA info
    config['stdout'] = None
  else:
    config['stdout'] = subprocess.DEVNULL
  autosa_prj_path = os.environ['AUTOSA_PATH']
  if training:
    config['work_dir'] = f'{autosa_prj_path}/autosa.tmp/training/job0'
  else:
    config['work_dir'] = f'{autosa_prj_path}/autosa.tmp/search/job0'
  with open(hw_info) as f:
    config['hw_info'] = json.load(f)
  config['logger'] = logging.getLogger('AutoSA-Optimizer')
  config['cmds'] = [cmd]
  config['cmds'].append(f'--AutoSA-config={config["work_dir"]}/autosa_config.json')
  config['cmds'].append(f'--AuotSA-output-dir={config["work_dir"]}/output')
  config['cmds'].append('')
  config['sa_sizes'] = []
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

def xilinx_run(cmd, info, setting, training, search, verbose):
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
  """
  logger = init_logger(training, search, verbose)  
  config = init_config(setting, verbose, hw_info, cmd, training, search)
  if AUTOSA_PATH not in os.environ:
    raise NameError('Environment variable AUTOSA_PATH is not set.')
  autosa_prj_path = os.environ['AUTOSA_PATH']
  ret = execute_sys_cmd(f'mkdir -p {autosa_prj_path}/autosa.tmp/optimizer')
  if training:
    if os.path.exists(f'{autosa_prj_path}/autosa.tmp/optimizer/training'):
      ret = execute_sys_cmd(f'rm -rf {autosa_prj_path}/autosa.tmp/optimizer/training')
    ret = execute_sys_cmd(f'mkdir -p {autosa_prj_path}/autosa.tmp/optimizer/training')
    ret = execute_sys_cmd(f'mkdir -p {autosa_prj_path}/autosa.tmp/optimizer/training/job0')
  else:
    if os.path.exists(f'{autosa_prj_path}/autosa.tmp/optimizer/search'):
      ret = execute_sys_cmd(f'rm -rf {autosa_prj_path}/autosa.tmp/optimizer/search')
    ret = execute_sys_cmd(f'mkdir -p {autosa_prj_path}/autosa.tmp/optimizer/search')
    ret = execute_sys_cmd(f'mkdir -p {autosa_prj_path}/autosa.tmp/optimizer/search/job0')
  ret = execute_sys_cmd(f'mkdir -p {config["work_dir"]}/output')
  ret = execute_sys_cmd(f'mkdir -p {config["work_dir"]}/output/src')
  ret = execute_sys_cmd(f'mkdir -p {config["work_dir"]}/output/latency_est')
  ret = execute_sys_cmd(f'mkdir -p {config["work_dir"]}/output/resource_est')

  # Init the AutoSA configuration
  autosa_config = {"space_time": {"mode": "auto"}, \
                   "array_part": {"enable": 1, "mode": "manual"}, \
                   "array_part_L2": {
                      "enable": config['two_level_buffer']}, 
                      "mode": "manual"}, \
                   "latency": {"enable": 1, "mode": "manual"}, \
                   "simd": {"enable": 1, "mode": "manual"}, \
                   "hbm": {"enable": config['hbm'], "mode": "manual"}}
  with open(f'{config['work_dir']}/autosa_config.json', 'w') as f:
    json.dump(autosa_config, f, indent=4)
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
parser.add_argument('-c', '--cmd', metavar='CMD', required=True, help='AutoSA command line')
  parser.add_argument('-i', '--info', metavar='INFO', required=True, help='hardware resource information')
  parser.add_argument('-s', '--setting', metavar='SETTING', required=False, default='autosa_config/optimizer_settings.json', help='optimizer settings')
  parser.add_argument('-p', '--platform', metavar='PLATFORM', required=True, help='hardware platform: intel/xilinx')
  parser.add_argument('--training', action='store_true', help='run training phase')
  parser.add_argument('--search', action='store_true', help='run search phase')
  parser.add_argument('--verbose', type=int, required=False, default=1, help='provide verbose information [0-2]')

  args = parser.parse_args()

  # Parse the settings into a dict
  with open(args.setting) as f:
    setting = json.load(f)

  if args.platform == 'intel':
    print("Intel platform is not supported yet!") # TODO
  elif args.platform == 'xilinx':
    xilinx_run(args.cmd, args.info, setting, args.training, args.search, args.verbose)