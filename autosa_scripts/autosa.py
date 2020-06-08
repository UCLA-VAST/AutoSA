#!/usr/bin/env python3
import sys
import subprocess
import os

if __name__ == "__main__":
  n_arg = len(sys.argv)
  argv = sys.argv
  argv[0] = './src/autosa'
  output_dir = './autosa.tmp/output'
  target = 'autosa_hls_c'
  src_file_prefix = 'kernel'
  xilinx_host = 'opencl'

  for arg in argv:
    if 'output-dir' in arg:
      output_dir = arg.split('=')[-1]
    if 'target' in arg:
      target = arg.split('=')[-1]
  if n_arg > 1:
    src_file = argv[1]
    src_file_prefix = os.path.basename(src_file).split('.')[0]
  if n_arg > 1 and target == 'autosa_hls_c':
    # Check whether to generate HLS or OpenCL host for Xilinx FPGAs
    for arg in argv:
      if 'AutoSA-hls' in arg:
        xilinx_host = 'hls'

  # Check if the output directory exists
  if not os.path.isdir("./autosa.tmp"):
    os.mkdir("./autosa.tmp")
  if not os.path.isdir(output_dir):
    os.mkdir(output_dir)
    os.mkdir(output_dir + '/src')
    os.mkdir(output_dir + '/latency_est')
    os.mkdir(output_dir + '/resource_est')

  # Execute the AutoSA
  process = subprocess.run(argv)
  if process.returncode != 0:
    sys.exit()
  if not os.path.exists(output_dir + '/src/completed'):
    sys.exit()

  # Generate the top module
  print("[AutoSA] Post-processing the generated code...")
  if not os.path.exists(output_dir + '/src/' + src_file_prefix + '_top_gen.cpp'):
    sys.exit()
  cmd = 'g++ -o '   + output_dir + '/src/top_gen ' + output_dir + \
        '/src/' + src_file_prefix + '_top_gen.cpp ' + \
        '-I./src/isl/include -L./src/isl/.libs -lisl'
  process = subprocess.run(cmd.split())
  my_env = os.environ.copy()
  cwd = os.getcwd()
  if 'LD_LIBRARY_PATH' in my_env:
    my_env['LD_LIBRARY_PATH'] += os.pathsep +  cwd + '/src/isl/.libs'
  else:
    my_env['LD_LIBRARY_PATH'] = os.pathsep +  cwd + '/src/isl/.libs'
  cmd = output_dir + '/src/top_gen'
  process = subprocess.run(cmd.split(), env=my_env)

  # Generate the final code
  if target == 'autosa_hls_c':
    cmd = './autosa_scripts/codegen.py -c ' + output_dir + \
          '/src/top.cpp -d ' + output_dir + '/src/' + src_file_prefix + \
          '_kernel_modules.cpp -t ' + target + ' -o ' + output_dir + '/src/' + \
          src_file_prefix + '_kernel.cpp'
  elif target == 'autosa_opencl':
    cmd = './autosa_scripts/codegen.py -c ' + output_dir + \
          '/src/top.cpp -d ' + output_dir + '/src/' + src_file_prefix + \
          '_kernel_modules.cl -t ' + target + ' -o ' + output_dir + '/src/' + \
          src_file_prefix + '_kernel.cl'

  if target == 'autosa_hls_c':
    cmd += ' --host '
    cmd += xilinx_host
  process = subprocess.run(cmd.split())

  cmd = 'cp ' + argv[1] + ' ' + output_dir + '/src/'
  process = subprocess.run(cmd.split())
  headers = src_file.split('.')
  headers[-1] = 'h'
  headers = ".".join(headers)
  if os.path.exists(headers):
    cmd = 'cp ' + headers + ' ' + output_dir + '/src/'
    process = subprocess.run(cmd.split())
  if os.path.exists(output_dir + '/src/completed'):
    cmd = 'rm ' + output_dir + '/src/completed'
    process = subprocess.run(cmd.split())
  if target == 'autosa_hls_c' and xilinx_host == 'opencl':
    cmd = 'rm ' + output_dir + '/src/' + src_file_prefix + '_kernel.h'
    process = subprocess.run(cmd.split())

  # Clean up the temp files
  cmd = 'rm ' + output_dir + '/src/top_gen'
  process = subprocess.run(cmd.split())
  cmd = 'rm ' + output_dir + '/src/top.cpp'
  process = subprocess.run(cmd.split())
  cmd = 'rm ' + output_dir + '/src/' + src_file_prefix + '_top_gen.cpp'
  process = subprocess.run(cmd.split())
  cmd = 'rm ' + output_dir + '/src/' + src_file_prefix + '_top_gen.h'
  process = subprocess.run(cmd.split())
  if target == 'autosa_hls_c':
    cmd = 'rm ' + output_dir + '/src/' + src_file_prefix + '_kernel_modules.cpp'
  elif target == 'autosa_opencl':
    cmd = 'rm ' + output_dir + '/src/' + src_file_prefix + '_kernel_modules.cl'
  process = subprocess.run(cmd.split())
