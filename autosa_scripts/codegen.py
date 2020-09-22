#!/usr/bin/env python3

import sympy
import sys
import argparse
import re
import numpy as np


def delete_arg_from_arg_list(line, arg, content):
    """ Delete the argument from the argument list

    Parameters
    ----------
    line: list
        codeline containing the argument list
    arg: list
        argument to be deleted
    line_id: int
        the current line id
    content: list
        the printed content before current line
    """
    line = line.strip()
    # print(line)
    if line[-1] != ',':
        # print('test\n')
        # print(line)
        # print(content[-1])
        comma_pos = content[-1].find(',')
        content[-1] = content[-1][:comma_pos] + '\n'

    """
    line = re.sub(r'( )(' + re.escape(arg) + r')(,)',
                  '', line)
    line = re.sub(r'( )(' + re.escape(arg) + r')(\))',
                  r'\g<3>', line)
    line = re.sub(r'(\()(' + re.escape(arg) + r')(, )',
                  r'\g<1>', line)
    line = re.sub(r'(\()(' + re.escape(arg) + r')(\))',
                  r'\g<1>\g<3>', line)
    """

def print_module_def(
        f,
        arg_map,
        module_def,
        inline_module_defs,
        def_args,
        call_args_type):
    """ Print out module definitions for Intel OpenCL

    This function prints out the module definition with all arguments in the code
    replaced by the calling arguments.
    We will first extract the module ids and fifos from the module definition
    argument lists. These arguments are deleted from the argument lists as we will
    plug in the exact module ids and fifos from a call of this modules.
    As an example, the original module
      void A_IO_L3_in(int idx, fifo_type fifo)
    will be modified to
      void A_IO_L3_in_[arg_map[idx]]()

    Parameters
    ----------
    f: 
        file handle
    arg_map: 
        maps from module definition args to module call args
    module_def: 
        a list storing the module definition texts
    inline_module_defs: 
        a dict containing all the inline module definitions
    def_args:
        a list storing the module definition arguments
    call_args_type: 
        a list storing the type of each module call arg
    """
    # Print inline module definitions
    if inline_module_defs:
        # Each inline module should be only printed once.
        # We assume the module ids and fifos are unchanged in multiple inline module
        # calls. Therefore, only the first encounter will be handled.
        inline_module_handled = []
        for inline_module in inline_module_defs:
            # Search for the inline modules
            for line_id in range(len(module_def)):
                line = module_def[line_id]
                if line.find(inline_module + '(') != -1:
                    # The current line contains the inline module call
                    if inline_module in inline_module_handled:
                        # Replace the module call
                        line_indent = line.find(inline_module)
                        line = ' ' * line_indent + inline_module
                        for i in range(len(def_args)):
                            def_arg = def_args[i]
                            arg_type = call_args_type[i]
                            if arg_type == 'module id':
                                line += '_'
                                line += arg_map[def_arg]
                        line += '(\n'
                        module_def[line_id] = line
                        continue
                    else:
                        inline_module_handled.append(inline_module)
                    # Print the inline module definition
                    inline_module_call_args = []
                    inline_module_call_args_type = []
                    inline_module_def_args = []
                    inline_module_arg_map = {}
                    inline_module_name = inline_module
                    inline_module_def = inline_module_defs[inline_module_name]
                    # Extract the arg list in module definition
                    for inline_module_line in inline_module_def:
                        if inline_module_line.find('void') != -1:
                            m = re.search(r'\((.+?)\)', inline_module_line)
                            if m:
                                def_args_old = m.group(1)
                    def_args_old = def_args_old.split(', ')
                    for arg in def_args_old:
                        arg = arg.split()[-1]
                        inline_module_def_args.append(arg)
                    # Extract the arg list in module call
                    next_line_id = line_id + 1
                    next_line = module_def[next_line_id]
                    while next_line.find(');') == -1:
                        m = re.search(r'/\*(.+?)\*/', next_line)
                        if m:
                            arg_type = m.group(1).strip()
                            inline_module_call_args_type.append(arg_type)
                            m = re.search(r'\*/ (.+)', next_line)
                            if m:
                                call_arg = m.group(1).split(',')[0]
                                inline_module_call_args.append(call_arg)
                        next_line_id += 1
                        next_line = module_def[next_line_id]
                    # Build a mapping between the def_arg to call_arg
                    for i in range(len(inline_module_def_args)):
                        def_arg = inline_module_def_args[i]
                        call_arg = inline_module_call_args[i]
                        inline_module_arg_map[def_arg] = call_arg
                    # Replace the module ids and fifos from the upper module
                    for def_arg in inline_module_arg_map:
                        call_arg = inline_module_arg_map[def_arg]
                        if call_arg in arg_map:
                            inline_module_arg_map[def_arg] = arg_map[call_arg]
                    print_module_def(
                        f,
                        inline_module_arg_map,
                        inline_module_def.copy(),
                        None,
                        inline_module_def_args,
                        inline_module_call_args_type)
                    # Replace the inline module call with the new inline module
                    # name
                    line_indent = line.find(inline_module)
                    line = ' ' * line_indent + inline_module
                    for i in range(len(def_args)):
                        def_arg = def_args[i]
                        arg_type = call_args_type[i]
                        if arg_type == 'module id':
                            line += '_'
                            line += arg_map[def_arg]
                    line += '(\n'
                    module_def[line_id] = line

    # Extract module ids and fifos from def_args
    module_id_args = []
    fifo_args = []
    # print(def_args)
    # print(call_args_type)
    for i in range(len(def_args)):
        def_arg = def_args[i]
        arg_type = call_args_type[i]
        if arg_type == 'module id':
            module_id_args.append(def_arg)
        if arg_type == 'fifo':
            fifo_args.append(def_arg)

    # Start printing
    print_content = []
    print_content.append('/* Module Definition */\n')
    line_id = 0
    for line in module_def:
        if line.find('void') != -1:
            # This line is kernel argument.
            # All module id and fifo arguments are deleted
            m = re.search(r'(.+?)\(', line)
            if m:
                prefix = m.group(1)
            m = re.search(r'\((.+?)\)', line)
            if m:
                def_args = m.group(1)
            def_args = def_args.split(', ')
            new_def_args = []
            for i in range(len(def_args)):
                if call_args_type[i] != 'module id' and call_args_type[i] != 'fifo':
                    new_def_args.append(def_args[i])
            # f.write(prefix + '(')
            # Print the module_name
            print_content.append(prefix)
            for module_id in module_id_args:
                print_content.append('_' + arg_map[module_id])
            print_content.append('(')
            first = True
            for arg in new_def_args:
                if not first:
                    print_content.append(', ')
                print_content.append(arg)
                first = False
            print_content.append(')\n')
        else:
            # module ids
            for module_id in module_id_args:
                if line.find(module_id) != -1:
                    # Test if it is inside an argument list
                    m = re.search(
                        r'/\* module id \*/ ' +
                        re.escape(module_id),
                        line)
                    if m:
                        # Delete if from the argument list
                        delete_arg_from_arg_list(
                            line, module_id, print_content)
                        line = None
                        break
                    else:
                        # Plug in module ids
                        line = re.sub(
                            r'([^a-zA-Z_])(' +
                            re.escape(module_id) +
                            r')([^a-zA-Z0-9_])',
                            r'\g<1>' +
                            re.escape(
                                arg_map[module_id]) +
                            r'\g<3>',
                            line)
            # fifos
            if line:
                for fifo in fifo_args:
                    if line.find(fifo) != -1:
                        # Test if it is inside a read/write API call
                        if line.find('read_channel_intel') != - \
                                1 or line.find('write_channel_intel') != -1:
                            # Plug in fifos
                            line = re.sub(
                                r'([^a-zA-Z_])(' +
                                re.escape(fifo) +
                                r')([^a-zA-Z0-9_])',
                                r'\g<1>' +
                                re.escape(
                                    arg_map[fifo]) +
                                r'\g<3>',
                                line)
                        else:
                            # Test if it is inside an argument list
                            m = re.search(
                                r'/\* fifo \*/ ' + re.escape(fifo), line)
                            if m:
                                # Delete it from the argument list
                                delete_arg_from_arg_list(
                                    line, fifo, print_content)
                                line = None
                                break
            if line is not None:
                print_content.append(line)
        line_id += 1
    print_content.append('/* Module Definition */\n\n')

    f.writelines(print_content)


def generate_intel_kernel(
        kernel,
        headers,
        module_defs,
        module_calls,
        fifo_decls):
    """ Generate the final Intel code

    This function plugs in the module definitions into each module call and replace
    index ids and fifo arguments.

    Parameters
    ----------
    kernel: 
        the output file
    headers: 
        list containing the headers to be printed
    module_defs: 
        dict containing the module definitions
    module_calls: 
        list containing the module calls
    fifo_decls: 
        list containing the fifo declarations
    """
    inline_module_defs = {}
    with open(kernel, 'w') as f:
        # Print out headers
        for header in headers:
            f.write(header + '\n')
        f.write('\n')

        f.write('#pragma OPENCL EXTENSION cl_intel_channels : enable\n\n')

        # Print out channels
        f.write('/* Channel Declaration */\n')
        for fifo_decl in fifo_decls:
            f.write(fifo_decl + '\n')
        f.write('/* Channel Declaration */\n\n')

        # Extract the inline modules
        # These modules are those that exist in the module_defs but not in the
        # module_calls.
        for module_name in module_defs:
            inline_module = 1
            for module_call in module_calls:
                line = module_call[0]
                m = re.search(r'(.+?)\(', line)
                if m:
                    cur_module_name = m.group(1)
                if module_name == cur_module_name:
                    inline_module = 0
                    break
            if inline_module:
                inline_module_defs[module_name] = module_defs[module_name]

        # print out module definitions
        for module_call in module_calls:
            # f.write('/* Module Definition */\n')
            def_args = []
            call_args = []
            call_args_type = []
            arg_map = {}
            # Extract the module name
            line = module_call[0]
            m = re.search(r'(.+?)\(', line)
            if m:
                module_name = m.group(1)
            module_def = module_defs[module_name]
            # extract the arg list in module definition
            for line in module_def:
                if line.find('void') != -1:
                    m = re.search(r'\((.+?)\)', line)
                    if m:
                        def_args_old = m.group(1)
            def_args_old = def_args_old.split(', ')
            for arg in def_args_old:
                arg = arg.split()[-1]
                def_args.append(arg)

            # extract the arg list in module call
            for line in module_call:
                m = re.search(r'/\*(.+?)\*/', line)
                if m:
                    arg_type = m.group(1).strip()
                    call_args_type.append(arg_type)
                    n = re.search(r'\*/ (.+)', line)
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
            print_module_def(
                f,
                arg_map,
                module_def.copy(),
                inline_module_defs,
                def_args,
                call_args_type)
            # f.write('/* Module Definition */\n\n')


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

    Parameters
    ----------
    lines: 
        contains the codelines of the program
    """
    # Handle hls_dependence
    handle_dep_pragma = 1

    code_len = len(lines)
    pos = 0
    while pos < code_len:
        line = lines[pos]
        if line.find("// hls_pipeline") != - \
                1 or line.find("// hls_dependence") != -1:
            is_pipeline = 0
            is_dep = 0
            if line.find('// hls_pipeline') != -1:
                is_pipeline = 1
            else:
                is_dep = 1
            # Find if there is any other hls_pipeline/hls_dependence annotation
            # below
            n_l_bracket = 0
            n_r_bracket = 0
            next_pos = pos + 1
            find_pipeline = 0
            init_state = 1
            while next_pos < code_len and n_r_bracket <= n_l_bracket:
                if is_pipeline and lines[next_pos].find('hls_pipeline') != -1:
                    find_pipeline = 1
                    break
                if is_dep and lines[next_pos].find(
                        'hls_dependence') != -1 and handle_dep_pragma:
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
                if lines[prev_pos].find('while') != -1:
                    break
                if lines[prev_pos].find('{') != -1:
                    n_l_bracket += 1
                if lines[prev_pos].find('}') != -1:
                    n_r_bracket += 1
                if lines[prev_pos].find('for') != -1:
                    if n_l_bracket > n_r_bracket:
                        # check if the pragma is already inserted
                        if is_pipeline and lines[prev_pos +
                                                 1].find('#pragma HLS PIPELINE II=1\n') == -1:
                            find_for = 1
                        if is_dep and lines[prev_pos + 2].find(
                                '#pragma HLS DEPENDENCE') == -1 and handle_dep_pragma:
                            find_for = 1
                        # check if there is any other for loop with
                        # hls_pipeline annotation inside
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
                    new_line = ' ' * indent + "#pragma HLS DEPENDENCE variable=" + \
                        var_name + " inter false\n"
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


def float_to_int(matchobj):
    str_expr = matchobj.group(0)
    if float(str_expr) == int(float(str_expr)):
        return str(int(float(str_expr)))
    else:
        return str_expr


def index_simplify(matchobj):
    str_expr = matchobj.group(0)
    if str_expr == '[arb]' or str_expr == '[!arb]':
        return str_expr
    if '++' in str_expr:
        return str_expr
    expr = sympy.sympify(str_expr[1: len(str_expr) - 1])
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
#  # We will try to replace floats with integers if values won't change
#  new_str_expr = re.sub('\d+\.\d+', float_to_int, new_str_expr)

    if 'floor' in new_str_expr or 'ceil' in new_str_expr or '.0' in new_str_expr:
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

    Parameters
    ----------
    lines: 
        contains the codelines of the program
    """
    code_len = len(lines)
    # Simplify array index expressions
    for pos in range(code_len):
        line = lines[pos]
        line = re.sub(r'\[(.+?)\]', index_simplify, line)
        lines[pos] = line

    # Simplify mod expressions
    for pos in range(code_len):
        line = lines[pos]
        line = re.sub(r'\((.+?)\) %', mod_simplify, line)
        lines[pos] = line

    return lines

def shrink_bit_width(lines, target):
    """ Calculate the bitwidth of the iterator and shrink it to the proper size

    We will examine the for loops. Examine the upper bound of the loop. If the
    upper bound is a number, we will compute the bitwidth of the iterator.
    For Intel target, we will also look for iterator definitions marked with
    "/* UB: [...] */". The shallow bitwidth is calculated and replace the previous
    data type.

    Parameters
    ----------
    lines: 
        contains the codelines of the program
    target: 
        xilinx|intel
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
                    if target == 'xilinx':
                        new_iter_t = 'ap_uint<' + str(bitwidth) + '>'
                    elif target == 'intel':
                        new_iter_t = 'uint' + str(bitwidth) + '_t'
                    line = re.sub('int', new_iter_t, line)
                    lines[pos] = line
            m = re.search('<(.+?);', line)
            if m:
                ub = m.group(1).strip()
                if ub.isnumeric():
                    #print(pos)
                    # Replace it with shallow bit width
                    bitwidth = int(np.ceil(np.log2(float(ub)))) + 1
                    new_iter_t = 'ap_uint<' + str(bitwidth) + '>'
                    line = re.sub('int', new_iter_t, line)
                    lines[pos] = line

    for pos in range(code_len):
        line = lines[pos]
        m = re.search(r'/\* UB: (.+?) \*/', line)
        if m:
            ub = m.group(1).strip()
            if ub.isnumeric():
                # Replace it with shallow bit width
                bitwidth = int(np.ceil(np.log2(float(ub) + 1))) + 1
                if target == 'xilinx':
                    new_iter_t = 'ap_uint<' + str(bitwidth) + '>'
                elif target == 'intel':
                    new_iter_t = 'uint' + str(bitwidth) + '_t'
                #line = re.sub('int', new_iter_t, line)
                line = re.sub(
                    r'(int)' +
                    r'\s' +
                    r'([a-zA-Z])',
                    new_iter_t +
                    r' \g<2>',
                    line)
                lines[pos] = line

    return lines


def lift_split_buffers(lines):
    """ Lift the split buffers in the program

    For each module, if we find any split buffers with the name "buf_data_split",
    we will lift them out of the for loops and put them in the variable declaration
    section at the beginning of the module.

    Parameters
    ----------
    lines: 
        contains the codelines of the program
    """
    code_len = len(lines)
    for pos in range(code_len):
        line = lines[pos]
        if line.find('variable=buf_data_split') != -1:
            # Search for the variable declaration section
            decl_pos = -1
            prev_pos = pos - 1
            while prev_pos >= 0:
                prev_line = lines[prev_pos]
                if prev_line.find('Variable Declaration') != -1:
                    decl_pos = prev_pos
                    break
                prev_pos -= 1
            # Move the two code lines at [pos - 1] and [pos] to [decl_pos] and
            # [decl_pos + 1]
            indent = lines[decl_pos].find('/*')
            line1 = ' ' * indent + lines[pos - 1].lstrip()
            line2 = ' ' * indent + lines[pos].lstrip()
            del lines[pos - 1]
            del lines[pos - 1]
            lines.insert(decl_pos, line1)
            lines.insert(decl_pos + 1, line2)

    return lines

def build_dummy_module_def(group_name, fifo_type, module_in, PE_ids):
    """ Build the definition of the dummy module

    Parameters
    ----------
    group_name: str
    fifo_type: str
    module_in: int
    PE_ids: list
    """
    dir_str = 'out' if module_in == 0 else 'in'
    index_str = ['idx', 'idy', 'idz']
    fifo_name = f'fifo_{group_name}_{dir_str}'

    lines = []
    lines.append('/* Module Definition */\n')
    lines.append(f'void {group_name}_PE_dummy_{dir_str}(')
    for pos in range(len(PE_ids)):
        lines.append(f'int {index_str[pos]}, ')
    lines.append(f'hls::stream<{fifo_type}> &{fifo_name}){{\n')
    if module_in == 0:
        lines.append(f'  if (!{fifo_name}.full())\n')
        lines.append(f'    {fifo_name}.write(0);\n')
    else:
        lines.append(f'  {fifo_type} fifo_data = {fifo_name}.read();\n')
    lines.append(f'}}\n')
    lines.append(f'/* Module Definition */\n')

    return lines

def build_dummy_module_call(group_name, fifo_name, module_in, PE_ids):
    """ Build the call of the dummy module

    Parameters
    ----------
    group_name: str
    fifo_name: str
    module_in: int
    PE_ids: list
    """
    dir_str = 'out' if module_in == 0 else 'in'

    lines = []
    lines.append('\n')
    lines.append('  /* Module Call */\n')
    lines.append(f'  {group_name}_PE_dummy_{dir_str}(\n')
    for id in PE_ids:
        lines.append(f'    /* module id */ {id},\n')
    lines.append(f'    /* fifo */ {fifo_name}\n')    
    lines.append(f'  );\n')
    lines.append(f'  /* Module Call */\n')

    return lines

def insert_dummy_modules(def_lines, call_lines):
    """ Insert the missing dummy modules

    Collect the FIFO information of PEs (fifo_name, fifo_type). 
    Delete those FIFOs that are connected to other modules.
    Insert dummy modules for the rest of FIFOs.

    Parameters
    ----------
    def_lines: list
        Contains the codelines of the module definitions
    call_lines: list
        Contains the codelines of the module calls
    """
    PE_fifos = []
    for line in def_lines:
        if line.find('void PE_wrapper') != -1:
            # Parse the argument list
            m = re.search(r'\((.+?)\)', line)            
            args = m.group(1).strip().split(',')            
            for arg in args:
                if arg.find('fifo') != -1:
                    m = re.search(r'stream<(.+?)>', arg)
                    fifo_type = m.group(1)
                    fifo_name = arg.split('&')[-1]
                    PE_fifos.append({'type': fifo_type, 'name': fifo_name})
    #print(PE_fifos)
    # Collect all used fifos
    used_fifos = {}
    kernel_start = 0
    for line in call_lines:
        if line.find('void kernel0') != -1:
            kernel_start = 1
        if kernel_start:
            if line.find('* fifo *') != -1:                                                
                fifo = line.strip().split('*')[2][2:]
                if fifo[-1] == ',':
                    fifo = fifo[:-1]
                # Only process PE level fifos
                if fifo.find('PE') == -1:
                    continue
                if fifo not in used_fifos:
                    used_fifos[fifo] = -1
                else:
                    del used_fifos[fifo]                    
    #print(used_fifos)
    # Locate the fifo position
    inside_module = False
    inside_PE = False
    fifo_pos = 0    
    PE_call_start = -1
    PE_call_end = -1
    line_id = 0
    for line in call_lines:
        if line.find('Module Call') != -1:
            inside_module = not inside_module
            if inside_PE:
                PE_call_end = line_id
            inside_PE = False
        if inside_module:
            if line.find('PE_wrapper') != -1:
                inside_PE = True
                fifo_pos = 0
                if PE_call_start == -1:
                    PE_call_start = line_id - 1                
            if inside_PE:
                if line.find('fifo') != -1:
                    for used_fifo in used_fifos:
                        if line.find(used_fifo) != -1:
                            used_fifos[used_fifo] = fifo_pos
                    fifo_pos += 1
        line_id += 1
    #print(used_fifos)
    # Insert the dummy module definitions
    offset_line = 0
    for used_fifo in used_fifos:
        fifo_info = PE_fifos[used_fifos[used_fifo]]
        # Extract the module direction
        if fifo_info['name'].endswith('in'):
            module_in = 0
        else:
            module_in = 1
        # Extract the group name
        if fifo_info['name'].endswith('in'):
            group_name = fifo_info['name'][5:-3]
        else:
            group_name = fifo_info['name'][5:-4]
        # Extract the PE ids
        PE_ids = used_fifo[len(f'fifo_{group_name}_PE_'):].split('_')
        #print(used_fifo, module_in, group_name, PE_ids)

        # Build the dummy module definition
        module_def = build_dummy_module_def(group_name, fifo_info['type'], module_in, PE_ids)
        #print(module_def)        
        def_lines += module_def
        def_lines.append('\n')

        # Build the dummy module call
        module_call = build_dummy_module_call(group_name, used_fifo, module_in, PE_ids) # TODO
        if module_in == 0:
            for i in range(len(module_call)):
                call_lines.insert(PE_call_start - 1 + i, module_call[i])
            offset_line += len(module_call)
        else:
            for i in range(len(module_call)):
                call_lines.insert(PE_call_end + 1 + offset_line + i, module_call[i])

    #print(PE_call_start, PE_call_end)

    return def_lines, call_lines

def reorder_module_calls(lines):
    """ Reorder the module calls in the program

    For I/O module calls, we will reverse the sequence of calls for output modules.
    Starting from the first module, enlist the module calls until the boundary module
    is met.
    Reverse the list and print it.

    Parameters
    ----------
    lines: list
        contains the codelines of the program
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
                nxt_line = nxt_line.strip()                
                if nxt_line.find('<') != -1:
                    module_name = nxt_line.split('<')[0]
                else:
                    module_name = nxt_line.split('(')[0]    
                if module_name.find('wrapper'):
                    module_name = module_name[:-8]
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
                        # Pop out the previous module calls except the last one
                        if new_module:
                            module_calls = module_calls[-1:]
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

def xilinx_run(
        kernel_call,
        kernel_def,
        kernel='autosa.tmp/output/src/kernel_kernel.cpp',
        host='opencl'):
    """ Generate the kernel file for Xilinx platform

    We will copy the content of kernel definitions before the kernel calls.

    Parameters
    ----------
    kernel_call: 
        file containing kernel calls
    kernel_def: 
        file containing kernel definitions
    kernel: 
        output kernel file
    """

    # Load kernel definition file
    lines = []
    with open(kernel_def, 'r') as f:
        lines = f.readlines()
    call_lines = []
    with open(kernel_call, 'r') as f:
        call_lines = f.readlines()

    # Simplify the expressions
    lines = simplify_expressions(lines)

    # Change the loop iterator type
    lines = shrink_bit_width(lines, 'xilinx')

    # Insert the HLS pragmas
    lines = insert_xlnx_pragmas(lines)

    # Lift the split_buffers
    lines = lift_split_buffers(lines)

    ## Insert missing dummy modules
    #lines, call_lines = insert_dummy_modules(lines, call_lines)

    kernel = str(kernel)
    print("Please find the generated file: " + kernel)

    with open(kernel, 'w') as f:
        if host == 'opencl':
            # Merge kernel header file
            kernel_header = kernel.split('.')
            kernel_header[-1] = 'h'
            kernel_header = ".".join(kernel_header)
            with open(kernel_header, 'r') as f2:
                header_lines = f2.readlines()
                f.writelines(header_lines)
            f.write('\n')

        f.writelines(lines)

        # Reorder module calls
        call_lines = reorder_module_calls(call_lines)
        f.writelines(call_lines)

        ## Load kernel call file
        #with open(kernel_call, 'r') as f2:
        #    lines = f2.readlines()
        #    # Reorder module calls
        #    lines = reorder_module_calls(lines)
        #    f.writelines(lines)


def insert_intel_pragmas(lines):
    """ Insert Intel OpenCL pragmas for Intel program

    Replace the comments of "// hls_unroll" with OpenCL pragmas.
    For "hls unroll", find the previous for loop before hitting the "simd" mark.
    Insert "#pragma unroll" above the for loop.
    Replace the comments of "// hls_coalesce" with OpenCL pragma "#pragma loop_coalesce".

    Parameters
    ----------
    lines: 
        contains the codelines of the program
    """
    code_len = len(lines)
    pos = 0
    while pos < code_len:
        line = lines[pos]
        if line.find('// hls_unroll') != -1:
            # Find the for loop above before hitting any "simd"
            prev_pos = pos - 1
            find_for = 0
            while prev_pos >= 0 and lines[prev_pos].find('simd') == -1:
                if lines[prev_pos].find('for') != -1:
                    find_for = 1
                    break
                prev_pos -= 1
            if find_for == 1:
                # Insert the pragma right before the for loop
                indent = lines[prev_pos].find('for')
                new_line = ' ' * indent + "#pragma unroll\n"
                lines.insert(prev_pos, new_line)
                del lines[pos + 1]
#    if line.find('// hls_coalesce') != -1:
#      indent = line.find('// hls_coalesce')
#      new_line = ' ' * indent + "#pragma loop_coalesce\n"
#      del lines[pos]
#      lines.insert(pos, new_line)
        pos = pos + 1

    return lines


def intel_run(
        kernel_call,
        kernel_def,
        kernel='autosa.tmp/output/src/kernel_kernel.cpp'):
    """ Generate the kernel file for Intel platform

    We will extract all the fifo declarations and module calls.
    Then plug in the module definitions into each module call.

    Parameters
    ----------
    kernel_call: 
        file containing kernel calls
    kernel_def: 
        file containing kernel definitions
    kernel: 
        output kernel file
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
                if (line.find('void')) != -1:
                    m = re.search(r'void (.+?)\(', line)
                    if m:
                        module_name = m.group(1)
            if line.find('/* Module Definition */') != -1:
                if add:
                    module_def.pop(len(module_def) - 1)
                    module_defs[module_name] = module_def.copy()
                    module_def.clear()
                    # Post-process the module definition
                    # Simplify the expressions
                    module_defs[module_name] = simplify_expressions(
                        module_defs[module_name])
                    # Insert the OpenCL pragmas
                    module_defs[module_name] = insert_intel_pragmas(
                        module_defs[module_name])
                    # Change the loop iterator type
                    module_defs[module_name] = shrink_bit_width(
                        module_defs[module_name], 'intel')
                add = not add

    # compose the kernel file
    kernel = str(kernel)
    generate_intel_kernel(
        kernel,
        headers,
        module_defs,
        module_calls,
        fifo_decls)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='==== AutoSA CodeGen ====')
    parser.add_argument(
        '-c',
        '--kernel-call',
        metavar='KERNEL_CALL',
        required=True,
        help='kernel function call')
    parser.add_argument(
        '-d',
        '--kernel-def',
        metavar='KERNEL_DEF',
        required=True,
        help='kernel function definition')
    parser.add_argument(
        '-t',
        '--target',
        metavar='TARGET',
        required=True,
        help='hardware target: autosa_hls_c|autosa_opencl')
    parser.add_argument(
        '-o',
        '--output',
        metavar='OUTPUT',
        required=False,
        help='output kernel file')
    parser.add_argument(
        '--host',
        metavar='HOST',
        required=False,
        help='Xilinx host target: hls|opencl',
        default='opencl')

    args = parser.parse_args()

    if args.target == 'autosa_opencl':
        intel_run(args.kernel_call, args.kernel_def, args.output)
    elif args.target == 'autosa_hls_c':
        xilinx_run(args.kernel_call, args.kernel_def, args.output, args.host)