#!/usr/bin/env python3
import sys
import subprocess
import os
import time

def exec_sys_cmd(cmd):
    p = subprocess.Popen(cmd, shell=True)
    ret = p.wait()
    return ret

if __name__ == "__main__":
    # Some default values
    output_dir = './autosa.tmp/output'
    target = 'autosa_hls_c'
    src_file_prefix = 'kernel'
    xilinx_host = 'opencl'
    tuning = False
    isl_flag = '--isl-schedule-whole-component' # This flag forces ISL to perform loop fusion as much as possible

    # Parse and update the arguments
    n_arg = len(sys.argv)
    argv = sys.argv
    argv[0] = './src/autosa'
    tuning_idx = -1
    insert_isl_flag = True
    for i in range(n_arg):
        arg = argv[i]            
        if '--output-dir' in arg:
            output_dir = arg.split('=')[-1]
        if '--target' in arg:
            target = arg.split('=')[-1]
        if '--tuning' in arg:            
            tuning = True
            tuning_idx = i
        if '--isl-schedule-whole-component' in arg:
            insert_isl_flag = False
    if n_arg > 1:
        src_file = argv[1]
        src_file_prefix = os.path.basename(src_file).split('.')[0]
    if n_arg > 1 and target == 'autosa_hls_c':
        # Check whether to generate HLS or OpenCL host for Xilinx FPGAs
        for arg in argv:
            if '--hls' in arg:
                xilinx_host = 'hls'
    if tuning:
        del argv[tuning_idx]
    if insert_isl_flag:
        argv.append(isl_flag)

    # Check if the output directory exists
    if not os.path.isdir(output_dir):
        raise RuntimeError('Output directory is not specified.')

    # Execute the AutoSA    
    #start_time = time.perf_counter()
    process = subprocess.run(argv)
    if process.returncode != 0:
        print("[AutoSA] Error: Exit abnormally!")
        sys.exit(process.returncode)
    else:        
        if not os.path.exists(output_dir + '/src/completed'):
            sys.exit(process.returncode)    
    exec_sys_cmd(f'rm {output_dir}/src/completed')                   
    #runtime = time.perf_counter() - start_time
    #print(f'runtime: {runtime}')

    # Generate the top module
    print("[AutoSA] Post-processing the generated code...")
    #start_time = time.perf_counter()
    if not os.path.exists(f'{output_dir}/src/{src_file_prefix}_top_gen.cpp'):
        raise RuntimeError(f'{output_dir}/src/{src_file_prefix}_top_gen.cpp not exists.')
    cmd = 'g++ -o ' + output_dir + '/src/top_gen ' + output_dir + \
          '/src/' + src_file_prefix + '_top_gen.cpp ' + \
          '-I./src/isl/include -L./src/isl/.libs -lisl'
    exec_sys_cmd(cmd)
    my_env = os.environ.copy()
    cwd = os.getcwd()
    if 'LD_LIBRARY_PATH' in my_env:
        my_env['LD_LIBRARY_PATH'] += os.pathsep + cwd + '/src/isl/.libs'
    else:
        my_env['LD_LIBRARY_PATH'] = os.pathsep + cwd + '/src/isl/.libs'
    cmd = output_dir + '/src/top_gen'
    process = subprocess.run(cmd.split(), env=my_env)
    #runtime = time.perf_counter() - start_time
    #print(f'runtime: {runtime}')

    if not tuning:
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
        exec_sys_cmd(cmd)            

        # Copy the input code to the output directory           
        exec_sys_cmd(f'cp {argv[1]} {output_dir}/src/')
        headers = src_file.split('.')
        headers[-1] = 'h'
        headers = ".".join(headers)
        if os.path.exists(headers):
            exec_sys_cmd(f'cp {headers} {output_dir}/src/')        

        # Clean up the temp files        
        if target == 'autosa_hls_c' and xilinx_host == 'opencl':
            exec_sys_cmd(f'rm {output_dir}/src/{src_file_prefix}_kernel.h')            
        exec_sys_cmd(f'rm {output_dir}/src/top_gen')
        exec_sys_cmd(f'rm {output_dir}/src/top.cpp')
        exec_sys_cmd(f'rm {output_dir}/src/{src_file_prefix}_top_gen.cpp')    
        exec_sys_cmd(f'rm {output_dir}/src/{src_file_prefix}_top_gen.h')    
        if target == 'autosa_hls_c':
            exec_sys_cmd(f'rm {output_dir}/src/{src_file_prefix}_kernel_modules.cpp')
        elif target == 'autosa_opencl':
            exec_sys_cmd(f'rm {output_dir}/src/{src_file_prefix}_kernel_modules.cl')    
