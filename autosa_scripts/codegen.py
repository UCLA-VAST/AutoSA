#!/usr/bin/env python3.6

import sympy
import sys
import argparse
import re
import numpy as np

def print_module_def(f, arg_map, module_def, def_args, call_args_type):
  """Print out module definitions

  This function prints out the module definition with all arguments
  replaced by the calling arugments.

  Args:
    f: file handle
    arg_map: maps from module definition args to module call args
    module_def (list): stores the module definition texts
    def_args (list): stores the module definition arguments
    call_args_type (list): stores the type of each module call arg
  """
  # Extract module ids and fifos from def_args
  module_id_args = []
  fifo_args = []
  for i in range(len(def_args)):
    def_arg = def_args[i]
    arg_type = call_args_type[i]
    if arg_type == 'module id':
      module_id_args.append(def_arg)
    if arg_type == 'fifo':
      fifo_args.append(def_arg)

  for line in module_def:
    if line.find('__kernel') != -1:
      # This line is kernel argument.
      # All module id and fifo arguments are deleted
      m = re.search('(.+)\(', line)
      if m:
        prefix = m.group(1)
      m = re.search('\((.+?)\)', line)
      if m:
        def_args = m.group(1)
      def_args = def_args.split(', ')
      new_def_args = []
      for i in range(len(def_args)):
        if call_args_type[i] != 'module id' and call_args_type[i] != 'fifo':
          new_def_args.append(def_args[i])
      f.write(prefix + '(')
      first = True
      for arg in new_def_args:
        if not first:
          f.write(', ')
        f.write(arg)
        first = False
      f.write(')\n')
    elif line.find('// module id') != -1:
      # This line is module id initialization
      # All module ids are replaced by call args
      for i in range(len(module_id_args)):
        def_arg = module_id_args[i]
        call_arg = arg_map[def_arg]
        line = line.replace(def_arg, call_arg)
      f.write(line)
    elif line.find('read_channel_intel') != -1 or line.find('write_channel_intel') != -1:
      # This line is fifo read/write
      # All fifo name is replaced by call args
      for i in range(len(fifo_args)):
        def_arg = fifo_args[i]
        call_arg = arg_map[def_arg]
        line = line.replace(def_arg, call_arg)
      f.write(line)
    else:
      f.write(line)

def generate_intel_kernel(kernel, headers, module_defs, module_calls, fifo_decls):
  with open(kernel, 'w') as f:
    # print out headers
    for header in headers:
      f.write(header + '\n')
    f.write('\n')

    # print out channels
    f.write('/* Channel Declaration */\n')
    for fifo_decl in fifo_decls:
      f.write(fifo_decl + '\n')
    f.write('/* Channel Declaration */\n\n')

    # print out module definitions
    for module_call in module_calls:
      f.write('/* Module Definition */\n')
      def_args = []
      call_args = []
      call_args_type = []
      arg_map = {}
      # Extract the module name
      line = module_call[0]
      m = re.search('(.+?)\(', line)
      if m:
        module_name = m.group(1)
      module_def = module_defs[module_name]
      # extract the arg list in module definition
      for line in module_def:
        if line.find('__kernel') != -1:
          m = re.search('\((.+?)\)', line)
          if m:
            def_args_old = m.group(1)
      def_args_old = def_args_old.split(', ')
      for arg in def_args_old:
        arg = arg.split()[-1]
        def_args.append(arg)

      # extract the arg list in module call
      for line in module_call:
        m = re.search('/\*(.+?)\*/', line)
        if m:
          arg_type = m.group(1).strip()
          call_args_type.append(arg_type)
          n = re.search('\*/ (.+)', line)
          if n:
            call_arg = n.group(1).strip(',')
            call_args.append(call_arg)

      # build a mapping between the def_arg to call_arg
      for i in range(len(def_args)):
        call_arg_type = call_args_type[i]
        if call_arg_type == 'module id' or call_arg_type == 'fifo':
          def_arg = def_args[i]
          call_arg = call_args[i]
          arg_map[def_arg] = call_arg

      # print out the module definition with call args plugged in
      print_module_def(f, arg_map, module_def, def_args, call_args_type)
      f.write('/* Module Definition */\n\n')

def contains_pipeline_for(pos, lines):
  """ Examine if there is any for loop with hls_pipeline annotation inside the current for loop

  """
  n_l_bracket = 0
  n_r_bracket = 0
  code_len = len(lines)
  init_state = 1
  while pos < code_len and n_r_bracket <= n_l_bracket:
    if lines[pos].find('{') != -1:
      n_l_bracket += 1
    if lines[pos].find('}') != -1:
      n_r_bracket += 1
    if lines[pos].find('for') != -1:
      if init_state:
        init_state = 0
      else:
        if lines[pos + 1].find('hls_pipeline') != -1:
          return 1
    if n_l_bracket == n_r_bracket and not init_state:
      break
    pos += 1
  return 0

def insert_xlnx_pragmas(lines):
  """ Insert HLS pragmas for Xilinx program

  Replace the comments of "// hls_pipeline" and "// hls_unroll" with
  HLS pragmas
  For "// hls pipeline", find the previous for loop before hitting any "}".
  Insert "#pragma HLS PIPELINE II=1" below the for loop.
  For "// hls unroll", find the previous for loop before hitting the "simd" mark.
  Insert "#pragma HLS UNROLL" below the for loop.
  For "// hls_dependence.x", the position is the same with hls_pipeline.
  Insert "#pragma HLS DEPENDENCE variable=x inter false".

  Args:
    lines: contains the codelines of the program
  """
  # Temporarily disabled
  handle_dep_pragma = 0

  code_len = len(lines)
  pos = 0
  while pos < code_len:
    line = lines[pos]
    if line.find("// hls_pipeline") != -1 or line.find("// hls_dependence") != -1:
      is_pipeline = 0
      is_dep = 0
      if line.find('// hls_pipeline') != -1:
        is_pipeline = 1
      else:
        is_dep = 1
      # Find if there is any other hls_pipeline/hls_dependence annotation below
      n_l_bracket = 0
      n_r_bracket = 0
      next_pos = pos + 1
      find_pipeline = 0
      init_state = 1
      while next_pos < code_len and n_r_bracket <= n_l_bracket:
        if is_pipeline and lines[next_pos].find('hls_pipeline') != -1:
          find_pipeline = 1
          break
        if is_dep and lines[next_pos].find('hls_dependence') != -1 and handle_dep_pragma:
          find_pipeline = 1
          break
        if lines[next_pos].find('{') != -1:
          n_l_bracket += 1
          init_state = 0
        if lines[next_pos].find('}') != -1:
          n_r_bracket += 1
        if n_l_bracket == n_r_bracket and not init_state:
          break
        next_pos += 1
      if find_pipeline:
        pos += 1
        continue

      # Find the for loop above before hitting any "}"
      prev_pos = pos - 1
      find_for = 0
      n_l_bracket = 0
      n_r_bracket = 0
      while prev_pos >= 0:
        if lines[prev_pos].find('{') != -1:
          n_l_bracket += 1
        if lines[prev_pos].find('}') != -1:
          n_r_bracket += 1
        if lines[prev_pos].find('for') != -1:
          if n_l_bracket > n_r_bracket:
            # check if the pragma is already inserted
            if is_pipeline and lines[prev_pos + 1].find('#pragma HLS PIPELINE II=1\n') == -1:
              find_for = 1
            if is_dep and lines[prev_pos + 2].find('#pragma HLS DEPENDENCE') == -1 and handle_dep_pragma:
              find_for = 1
            # check if there is any other for loop with hls_pipeline annotation inside
            if contains_pipeline_for(prev_pos, lines):
              find_for = 0
            break
        prev_pos -= 1
      if find_for == 1:
        # insert the pragma right after the for loop
        indent = lines[prev_pos].find('for')
        if line.find("hls_pipeline") != -1:
          new_line = ' ' * indent + "#pragma HLS PIPELINE II=1\n"
        else:
          line_cp = line
          var_name = line_cp.strip().split('.')[-1]
          new_line = ' ' * indent + "#pragma HLS DEPENDENCE variable=" + var_name + " inter false\n"
        lines.insert(prev_pos + 1, new_line)
        del lines[pos + 1]
    elif line.find("// hls_unroll") != -1:
      # Find the for loop above before hitting any "simd"
      prev_pos = pos - 1
      find_for = 0
      while prev_pos >= 0 and lines[prev_pos].find('simd') == -1:
        if lines[prev_pos].find('for') != -1:
          find_for = 1
          break
        prev_pos -= 1
      if find_for == 1:
        # insert the pragma right after the for loop
        indent = lines[prev_pos].find('for')
        new_line = ' ' * indent + "#pragma HLS UNROLL\n"
        lines.insert(prev_pos + 1, new_line)
        del lines[pos + 1]
    pos = pos + 1

  return lines

def index_simplify(matchobj):
  str_expr = matchobj.group(0)
  expr = sympy.sympify(str_expr[1 : len(str_expr) - 1])
  """ 
  This will sometimes cause bugs due to the different semantics in C
  E.g., x = 9, (x+3)/4 != x/4+3/4.
  We could use cxxcode, but it will generate floating expressions which are
  expensive on FPGA.
  At present, we check if there is floor or ceil in the expression.
  If so, we abort and use the original expression. Otherwise, we replace it
  with the simplified one.
  """
  expr = sympy.simplify(expr)
  new_str_expr = sympy.printing.ccode(expr)
  if 'floor' in new_str_expr or 'ceil' in new_str_expr or '.0L' in new_str_expr:
    return str_expr
  else:
    return '[' + new_str_expr + ']'

def mod_simplify(matchobj):
  str_expr = matchobj.group(0)
  str_expr = str_expr[1: len(str_expr) - 3]
  expr = sympy.sympify(str_expr)
  expr = sympy.simplify(expr)
  str_expr = str(expr)

  return '(' + str_expr + ') %'

def simplify_expressions(lines):
  """ Simplify the index expressions in the program

  Use Sympy to simplify all the array index expressions in the program.

  Args:
    lines: contains the codelines of the program
  """

  code_len = len(lines)
  # Simplify array index expressions
  for pos in range(code_len):
    line = lines[pos]
    line = re.sub('\[(.+?)\]', index_simplify, line)
    lines[pos] = line

  # Simplify mod expressions
  for pos in range(code_len):
    line = lines[pos]
    line = re.sub('\((.+?)\) %', mod_simplify, line)
    lines[pos] = line

  return lines

def shrink_bit_width(lines):
  """ Calculate the bitwidth of the iterator and shrink it to the proper size

  Args:
    lines: contains the codelines of the program    
  """

  code_len = len(lines)
  for pos in range(code_len):
    line = lines[pos]
    if line.find('for') != -1:
      # Parse the loop upper bound
      m = re.search('<=(.+?);', line)
      if m:
        ub = m.group(1).strip()
        if ub.isnumeric():
          # Replace it with shallow bit width
          bitwidth = int(np.ceil(np.log2(float(ub) + 1))) + 1
          new_iter_t = 'ap_uint<' + str(bitwidth) + '>'
          line = re.sub('int', new_iter_t, line)
          lines[pos] = line
      m = re.search('<(.+?);', line)
      if m:
        ub = m.group(1).strip()
        if ub.isnumeric():
          # Replace it with shallow bit width
          bitwidth = int(np.ceil(np.log2(float(ub)))) + 1
          new_iter_t = 'ap_uint<' + str(bitwidth) + '>'
          line = re.sub('int', new_iter_t, line)
          lines[pos] = line

  return lines

def reorder_module_calls(lines):
  """ Reorder the module calls in the program

  For I/O module calls, we will reverse the sequence of calls for output modules.
  Starting from the first module, enlist the module calls until the boundary module 
  is met.
  Reverse the list and output it.

  Args:
    lines: contains the codelines of the program
  """

  code_len = len(lines)
  module_calls = []
  module_start = 0
  module_call = []
  output_io = 0
  boundary = 0
  new_module = 0
  prev_module_name = ""
  first_line = -1
  last_line = -1
  reset = 0

  for pos in range(code_len):
    line = lines[pos]
    if line.find("/* Module Call */") != -1:
      if module_start == 0:
        module_start = 1
      else:
        module_start = 0

      if module_start:
        # Examine if the module is an output I/O module
        nxt_line = lines[pos + 1]
        if nxt_line.find("IO") != -1 and nxt_line.find("out") != -1:
          output_io = 1
          # Examine if the module is an boundary module
          if nxt_line.find("boundary") != -1:
            boundary = 1
        # Extract the module name
        module_name = nxt_line.strip()[:-9]
        if boundary:
          module_name = module_name[:-9]
        if prev_module_name == "":
          prev_module_name = module_name
          first_line = pos
        else:
          if prev_module_name != module_name:
            new_module = 1
            prev_module_name = module_name
            first_line = pos
            reset = 0
          else:
            if reset:
              first_line = pos
              reset = 0
            new_module = 0

      if not module_start:
        if output_io:
          last_line = pos
          module_call.append(line)
          module_calls.append(module_call.copy())
          module_call.clear()
          if boundary:
            # Reverse the list
            module_calls.reverse()
            # Insert it back
            left_lines = lines[last_line + 1:]
            lines = lines[:first_line]
            first = 1
            for c in module_calls:
              if not first:
                lines.append("\n")
              lines = lines + c
              first = 0
            lines = lines + left_lines
            # Clean up
            module_calls.clear()
            boundary = 0
            output_io = 0
            reset = 1
          if new_module:
            # Pop out the previous module calls except the last one
            module_calls = module_calls[-1:]

    if module_start and output_io:
      module_call.append(line)

  return lines

def xilinx_run(kernel_call, kernel_def, kernel='autosa.tmp/output/src/kernel_autosa_hls_c.cpp'):
  """ Generate the kernel file for Xilinx platform

  We will copy the content of kernel definitions before the kernel calls.

  Args:
    kernel_call: file containing kernel calls
    kernel_def: file containing kernel definitions
    kernel: output kernel file

  """

  # Load kernel definition file
  lines = []
  with open(kernel_def, 'r') as f:
    lines = f.readlines()

  # Simplify the expressions
  lines = simplify_expressions(lines)

  # Change the loop iterator type
  lines = shrink_bit_width(lines)

  # Insert the HLS pragmas
  lines = insert_xlnx_pragmas(lines)

  kernel = str(kernel)
  print("Please find the generated file: " + kernel)

  with open(kernel, 'w') as f:
    f.writelines(lines)
    # Load kernel call file
    with open(kernel_call, 'r') as f2:
      lines = f2.readlines()
      # Reorder module calls
      lines = reorder_module_calls(lines)
      f.writelines(lines)

def intel_run(kernel_call, kernel_def, kernel='autosa.tmp/output/src/kernel_autosa_opencl.cpp'):
  """ Generate the kernel file for Intel platform

  We will exrtract all teh fifo declarations and module calls.
  Then plug in the module definitions into each module call.

  Args:
    kernel_call: file contains kernel calls
    kernel_def: file contains kernel definitions
    kernel: output kernel file
  """

  # Load kernel call file
  module_calls = []
  fifo_decls = []
  with open(kernel_call, 'r') as f:
    add = False
    while True:
      line = f.readline()
      if not line:
        break
      # Extract the fifo declaration and add to the list
      if add:
        line = line.strip()
        fifo_decls.append(line)
      if line.find('/* FIFO Declaration */') != -1:
        if add:
          fifo_decls.pop(len(fifo_decls) - 1)
        add = not add

  with open(kernel_call, 'r') as f:
    add = False
    module_call = []
    while True:
      line = f.readline()
      if not line:
        break
      # Extract the module call and add to the list
      if add:
        line = line.strip()
        module_call.append(line)
      if line.find('/* Module Call */') != -1:
        if add:
          module_call.pop(len(module_call) - 1)
          module_calls.append(module_call.copy())
          module_call.clear()
        add = not add

  module_defs = {}
  headers = []
  with open(kernel_def, 'r') as f:
    while True:
      line = f.readline()
      if not line:
        break
      if line.find('#include') != -1:
        line = line.strip()
        headers.append(line)

  with open(kernel_def, 'r') as f:
    add = False
    module_def = []
    while True:
      line = f.readline()
      if not line:
        break
      # Extract the module definition and add to the dict
      if add:
        module_def.append(line)
        # Extract the module name
        if (line.find('__kernel')) != -1:
          m = re.search('void (.+?)\(', line)
          if m:
            module_name = m.group(1)
      if line.find('/* Module Definition */') != -1:
        if add:
          module_def.pop(len(module_def) - 1)
          module_defs[module_name] = module_def.copy()
          module_def.clear()
        add = not add

  # compose the kernel file
  kernel = str(kernel)
  generate_intel_kernel(kernel, headers, module_defs, module_calls, fifo_decls)

if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='==== AutoSA CodeGen ====')
  parser.add_argument('-c', '--kernel-call', metavar='KERNEL_CALL', required=True, help='kernel function call')
  parser.add_argument('-d', '--kernel-def', metavar='KERNEL_DEF', required=True, help='kernel function definition')
  parser.add_argument('-t', '--target', metavar='TARGET', required=True, help='hardware target: autosa_hls_c/autosa_opencl')
  parser.add_argument('-o', '--output', metavar='OUTPUT', required=False, help='output kernel file')

  args = parser.parse_args()

  if args.target == 'autosa_opencl':
    print("Intel OpenCL is not supported!")
    # intel_run(args.kernel_call, args.kernel_def, args.output)
  elif args.platform == 'autosa_hls_c':
    xilinx_run(args.kernel_call, args.kernel_def, args.output)