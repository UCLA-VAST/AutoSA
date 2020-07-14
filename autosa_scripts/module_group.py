#!/usr/bin/env python3

import sympy
import sys
import argparse
import re
import json
import numpy as np


def compose_final_file(output_f, prefix_content, module_defs, top_kernel):
    with open(output_f, 'w') as f:
        f.writelines(prefix_content)
        for module_name in module_defs:
            module_def = module_defs[module_name]
            f.write('/* Module Definition */\n')
            f.writelines(module_def)
            f.write('/* Module Definition */\n\n')

        f.writelines(top_kernel['prefix_content'])
        f.write(' ' * 4 + '/* FIFO Declaration */\n')
        for fifo_name in top_kernel['fifo_decls']:
            fifo_decl = top_kernel['fifo_decls'][fifo_name]
            f.writelines(fifo_decl)
        f.write(' ' * 4 + '/* FIFO Declaration */\n\n')

        for module_call in top_kernel['module_calls']:
            f.write(' ' * 4 + '/* Module Call */\n')
            f.writelines(module_call['content'])
            f.write(' ' * 4 + '/* Module Call */\n\n')
        f.write('}\n')
        # Note: this one is for extern "C" in the OpenCL kernel
        f.write('}\n')


def extract_fifos_from_module_call(module_call):
    """

    Returns a list containing all the fifos in the module call.
    """
    fifos = []
    for line in module_call:
        if line.find('/* fifo */') != -1:
            m = re.search(r'\*/ (.+),', line)
            if m:
                fifo = m.group(1)
                fifos.append(fifo)
            else:
                m = re.search(r'\*/ (.+)', line.strip())
                if m:
                    fifo = m.group(1)
                    fifos.append(fifo)
    return fifos


def compose_group_wrapper(
        x_start,
        y_start,
        group_modules,
        module_fifo_decls,
        module_ext_fifos):
    """ Compose the module definition of the group wrapper module

    Retuns a list [module_name, module_def, module_call]
    """
    module_name = 'PE_module_group_wrapper_' + \
        str(x_start) + '_' + str(y_start)
    # Build the module definition
    module_def = []
    # Head
    module_def.append('void ' + module_name + '(\n')
    first = 1
    for fifo in module_ext_fifos:
        fifo_name = fifo['fifo_name']
        fifo_type = fifo['fifo_type']
        if not first:
            module_def.append(',\n')
        module_def.append(' ' * 4 + fifo_type + ' &' + fifo_name)
        first = 0
    module_def.append(')\n')
    module_def.append('{\n')
    module_def.append('#pragma HLS INLINE OFF\n')
    module_def.append('#pragma HLS DATAFLOW\n')

    # fifo declarations
    module_def.append(' ' * 4 + '/* FIFO Declaration */\n')
    for fifo_name in module_fifo_decls:
        fifo_decl = module_fifo_decls[fifo_name]
        module_def += fifo_decl
    module_def.append(' ' * 4 + '/* FIFO Declaration */\n\n')

    # module calls
    for module_call in group_modules:
        content = module_call['content']
        module_def.append(' ' * 4 + '/* Module Call */\n')
        module_def += content
        module_def.append(' ' * 4 + '/* Module Call */\n\n')

    module_def.append('}\n')

    # Build the module call
    module_call = []
    module_call.append(' ' * 4 + module_name + '(\n')
    # Insert the external fifos
    first = 1
    for fifo in module_ext_fifos:
        fifo_name = fifo['fifo_name']
        if not first:
            module_call.append(',\n')
        module_call.append(' ' * 8 + '/* fifo */ ' + fifo_name)
        first = 0
    module_call.append('\n')
    module_call.append(' ' * 4 + ');\n')
    return [module_name, module_def, module_call]


def create_group_wrapper(
        x_start,
        y_start,
        group_modules,
        module_defs,
        top_kernel):
    """ Create a wrapper module for all the modules in the current group

    First figure out the internal fifos in this group.
    Internal fifos are those fifos that have been used by modules inside the
    group.
    These internal fifos will be removed from the top_kernel['fifo_decls']
    and moved inside the current wrapper module.
    Next, for the external fifos, place them in the argument lists of the current
    group.
    Append the defition of this wrapper modules to module_defs.
    Append a new module call of this wrapper module to the
    top_kernel['module_calls']
    and remove the module calls of sub modules in this group from top_kernel['module_calls'].

    Args:
      x_start: the start x index of PE module ids
      y_start: the start y index of PE module ids
      group_modules: list containing all module calls in the current group
      module_defs: dict containing the module definitions
      top_kernel: dict containing the top kernel content
    """
    # print(x_start, y_start)
    internal_fifos = []
    external_fifos = []
    for module in group_modules:
        # print(module['module_name'])
        fifos = extract_fifos_from_module_call(module['content'])
        # print(fifos)
        for fifo in fifos:
            if fifo in external_fifos:
                internal_fifos.append(fifo)
                external_fifos.remove(fifo)
            else:
                external_fifos.append(fifo)

    # Remove internal fifos from the top_kernels and place them inside the current
    # wrapper.
    module_fifo_decls = {}
    for fifo in internal_fifos:
        fifo_decl = top_kernel['fifo_decls'][fifo]
        del top_kernel['fifo_decls'][fifo]
        module_fifo_decls[fifo] = fifo_decl
    module_ext_fifos = []
    for fifo in external_fifos:
        ext_fifo_item = {}
        ext_fifo_item['fifo_name'] = fifo
        # Extract the fifo type
        fifo_decl = top_kernel['fifo_decls'][fifo]
        first_line = fifo_decl[0]
        m = re.search(r'\*/ (.+?) fifo', first_line)
        if m:
            fifo_type = m.group(1)
            ext_fifo_item['fifo_type'] = fifo_type
        module_ext_fifos.append(ext_fifo_item)

    # Compose the definition and call of the wrapper module
    [module_name, module_def, module_call] = compose_group_wrapper(
        x_start, y_start, group_modules, module_fifo_decls, module_ext_fifos)
    # Insert the new definition into the module_defs
    module_defs[module_name] = module_def

    # Remove the module calls of this group from top_kernel['module_calls']
    module_offset = len(top_kernel['module_calls'])
    for module in group_modules:
        module_offset = min(module_offset,
                            top_kernel['module_calls'].index(module))
        top_kernel['module_calls'].remove(module)
    # Insert a new module call at the position 'module_offset'
    module_call_item = {'module_name': module_name, 'content': module_call}
    top_kernel['module_calls'].insert(module_offset, module_call_item)


def module_grouping(
        output_f,
        prefix_content,
        module_defs,
        top_kernel,
        group_config):
    """

    Args:
      output_f: output kernel file
      prefix_content: list containing the file content before the first module
      definition
      module_defs: dict containing the module definitions
      top_kernel: dict containing the top kernel content
      {
        'prefix_content': list containign the file content before the first fifo declaration
        'fifo_decls': dict containing the fifo declarations
        'module_calls': list containing the module calls
                        a module call is a dict containing fields:
                        module_name, module_ids, content
      }
    """
    # Examine if this file is legal to be grouped
    # Currently, we only allow module ids and fifos in the PE-level modules
    group_legal = True
    module_calls = top_kernel['module_calls']
    for module_call in module_calls:
        module_name = module_call['module_name']
        if 'PE' in module_name or 'IO_L1' in module_name:
            # This is a PE-level module
            module_call_content = module_call['content']
            for i in range(1, len(module_call_content)):
                line = module_call_content[i]
                m = re.search(r'/\* (.+?) \*/', line)
                if m:
                    arg_type = m.group(1)
                    if arg_type != 'module id' and arg_type != 'fifo':
                        group_legal = False
                        break
                    if arg_type == 'module id':
                        # Extract the module id
                        m = re.search(r'\*/ (.+?),', line)
                        if m:
                            module_id = m.group(1)
                            module_call['module_ids'].append(int(module_id))

    if not group_legal:
        print(
            '[AutoSA] Error: Unable to group modules. PE-level modules contain non-fifo' +
            ' or non-module-id arguments.\n')
        compose_final_file(output_f, prefix_content, module_defs, top_kernel)
        return

    # Extract the PE grid size
    grid_x = 0
    grid_y = 0
    pe_dim = 0
    for module_call in module_calls:
        module_name = module_call['module_name']
        if 'PE' in module_name and 'dummy' not in module_name:
            pe_dim = len(module_call['module_ids'])
            grid_x = max(module_call['module_ids'][0], grid_x)
            grid_y = max(module_call['module_ids'][1], grid_y)
    # TODO: At present, this scripts only work for 2D arrays
    grid_x += 1
    grid_y += 1
    group_modules_list = []
    for x_start in range(0, grid_x, group_config['x']):
        for y_start in range(0, grid_y, group_config['y']):
            # Grasp all the PE-level modules in the current group
            group_modules = []
            for module_call in module_calls:
                module_name = module_call['module_name']
                if 'PE' in module_name and 'dummy' not in module_name:
                    if module_call['module_ids'][0] in range(
                            x_start,
                            x_start +
                            group_config['x']) and module_call['module_ids'][1] in range(
                            y_start,
                            y_start +
                            group_config['y']):
                        group_modules.append(module_call)
                if 'IO_L1' in module_name:
                    # Extract the PE module ids from the last fifo
                    module_call_content = module_call['content']
                    last_fifo_line = module_call_content[-2]
                    m = re.search(r'\*/ (.+)', last_fifo_line.strip())
                    if m:
                        last_fifo = m.group(1)
                        last_fifo = last_fifo.split('_')
                        pe_x = int(last_fifo[-2])
                        pe_y = int(last_fifo[-1])
                        if pe_x in range(
                                x_start,
                                x_start +
                                group_config['x']) and pe_y in range(
                                y_start,
                                y_start +
                                group_config['y']):
                            group_modules.append(module_call)

            group_modules_list.append({'x_start': x_start, 'y_start': y_start,
                                       'group_modules': group_modules.copy()})

    for group in group_modules_list:
        # Create group wrapper modules
        create_group_wrapper(group['x_start'], group['y_start'],
                             group['group_modules'], module_defs, top_kernel)

    # Compose the final file
    compose_final_file(output_f, prefix_content, module_defs, top_kernel)


def run(input_f, output_f, config, host='opencl'):
    """ Module group

    This function will group the PE-level modules (PE and IO_L1)
    according to the group configuration files.
    Specifically, given the grouping constraint {x, y}, we will group all PE-level
    modules into blocks with dimensions x and y.
    We will insert new wrapper functions to wrap the original modules in the
    group.
    FIFOs connecting these modules internally will be placed inside the wrapper.

    Note: This script only supports:
          - 2D array
          - Xilinx OpenCL kernel

    Args:
      input_f: input kernel file
      output_f: output kernel file
      config: grouping configuration file
      host: Xilinx host target
    """
    # Load the group configuration file
    group_config = {}
    with open(config, 'r') as f:
        group_config = json.load(f)

    # Extract:
    # - file content before the first module definition
    # - module definitions
    # - top kernel
    #   - file content before the first fifo declaration
    #   - fifo declarations
    #   - module calls
    #   - fifo content after the last module call
    lines = []
    with open(input_f, 'r') as f:
        lines = f.readlines()

    prefix_content = []
    module_defs = {}
    top_kernel = {'prefix_content': [], 'fifo_decls': {}, 'module_calls': []}
    prefix_content_flag = 1
    module_defs_flag = 0
    top_kernel_flag = 0
    module_def_add = False
    module_def = []

    top_kernel_prefix_content_flag = 1
    top_kernel_fifo_decls_flag = 0
    top_kernel_module_calls_flag = 0
    top_kernel_fifo_decls_add = False
    top_kernel_module_calls_add = False
    module_call = []

    for line in lines:
        if prefix_content_flag:
            if line.find('Module Definition') != -1:
                prefix_content_flag = 0
                module_defs_flag = 1
            else:
                prefix_content.append(line)
        if module_defs_flag:
            if line.find('extern \"C\"') != -1:
                # TODO: only opencl is supported
                module_defs_flag = 0
                top_kernel_flag = 1
            else:
                if module_def_add:
                    module_def.append(line)
                    if (line.find('void')) != -1:
                        m = re.search(r'void (.+?)\(', line.strip())
                        if m:
                            module_name = m.group(1)
                if line.find('/* Module Definition */') != -1:
                    if module_def_add:
                        module_def.pop(len(module_def) - 1)
                        module_defs[module_name] = module_def.copy()
                        module_def.clear()
                    module_def_add = not module_def_add
        if top_kernel_flag:
            if top_kernel_prefix_content_flag:
                if line.find('/* FIFO Declaration */') != -1:
                    top_kernel_prefix_content_flag = 0
                    top_kernel_fifo_decls_flag = 1
                else:
                    top_kernel['prefix_content'].append(line)
            if top_kernel_fifo_decls_flag:
                if line.find('/* FIFO Declaration */') != -1:
                    if not top_kernel_fifo_decls_add:
                        top_kernel_fifo_decls_add = not top_kernel_fifo_decls_add
                    else:
                        top_kernel_fifo_decls_flag = 0
                        top_kernel_module_calls_flag = 1
                else:
                    if line.find('hls::stream') != -1:
                        m = re.search(r'> (.+?);', line)
                        if m:
                            fifo_name = m.group(1)
                            top_kernel['fifo_decls'][fifo_name] = [line]
                    if line.find('HLS STREAM') != -1:
                        m = re.search(r'variable=(.+?) ', line)
                        if m:
                            fifo_name = m.group(1)
                            top_kernel['fifo_decls'][fifo_name].append(line)
            if top_kernel_module_calls_flag:
                if line.find('/* Module Call */') != -1:
                    if top_kernel_module_calls_add:
                        module_call_object = {'module_name': module_name,
                                              'module_ids': [],
                                              'content': module_call.copy()}
                        top_kernel['module_calls'].append(module_call_object)
                        module_call.clear()
                    top_kernel_module_calls_add = not top_kernel_module_calls_add
                else:
                    if top_kernel_module_calls_add:
                        module_call.append(line)
                        m = re.search(r'(.+?)\(', line.strip())
                        if m:
                            module_name = m.group(1)

    # Group modules and print out to 'output_f'
    module_grouping(
        output_f,
        prefix_content,
        module_defs,
        top_kernel,
        group_config)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='==== AutoSA Utils: Module Grouping ====')
    parser.add_argument('-i', '--input', required=True, help='kernel file')
    parser.add_argument(
        '-o',
        '--output',
        required=True,
        help='modified kernel file')
    parser.add_argument(
        '-c',
        '--config',
        required=True,
        help='grouping configuration')
    parser.add_argument(
        '--host',
        required=False,
        help='Xilinx host target: hls|opencl',
        default='opencl')

    args = parser.parse_args()
    run(args.input, args.output, args.config, args.host)
