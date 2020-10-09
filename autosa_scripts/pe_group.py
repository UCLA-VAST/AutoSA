#!/usr/bin/env python3

import sympy
import sys
import argparse
import re
import numpy as np

def locate_data_trans_block(line_id, lines):
    prev_line_id = line_id - 1
    while prev_line_id >= 0:
        prev_line = lines[prev_line_id]
        if prev_line.find('{') != -1:
            block_start = prev_line_id
            break
        prev_line_id -= 1
    nxt_line_id = line_id + 1
    while nxt_line_id < len(lines):
        nxt_line = lines[nxt_line_id]
        if nxt_line.find('}') != -1:
            block_end = nxt_line_id
            break
        nxt_line_id += 1

    return block_start, block_end

def modify_index(lines, var_map, PE_dims):
    #print(var_map)

    new_lines = []
    for line in lines:
        for var in var_map:
            new_var = var
            for dim_idx in range(len(PE_dims)):
                new_var += f'[s{dim_idx}]'
            line = re.sub(rf'{var}', f'{new_var}', line)
            if line.find(var) != -1 and var_map[var]['simd'] == 1:
                # TODO: Consider the index only appears once
                pos = line.find(var)
                end_pos = pos
                for p in range(pos, len(line)):
                    if line[p] == ' ' or line[p] == ')':
                        end_pos = p - 1
                        break
                #print(pos)
                #print(end_pos)
                ref = line[pos : end_pos + 1]
                #print(ref)
                index = ref[ref.find('['):]
                indices = []
                while len(index) > 0:
                    start_pos = index.find('[')
                    end_pos = index.find(']')
                    indices.append(index[start_pos:end_pos+1])
                    index = index[end_pos + 1:]
                #print(index)
                #print(indices)
                last_index = indices[-1][1:-1]
                new_ref = ref[:ref.find('[')]
                for index in indices[:-1]:
                    new_ref += index
                new_ref += '.data['
                new_ref += last_index
                new_ref += ']'
                #print(ref)
                #print(new_ref)
                line = line.replace(ref, new_ref)

        new_lines.append(line)

    return new_lines

def insert_data_trans(lines, data_trans_info, PE_dims):    
    for group_name in data_trans_info:
        info = data_trans_info[group_name]
        #print(group_name)
        #print(data_trans_info[group_name]['PE_index_start'])
        #print(data_trans_info[group_name]['PE_index_end'])
        dir = [info['PE_index_end'][dim] - info['PE_index_start'][dim] for dim in range(len(info['PE_index_start']))]
        #print(dir)
        if dir == [0, 1]:
            new_lines = [\
                '#pragma unroll\n',
                f'for (int s0 = 0; s0 < {PE_dims[0]}; s0++) {{\n',
                f'  local_{group_name}[s0][0][0] = read_channel_intel(fifo_{group_name}_PE[s0][0]);\n',
                '}\n'
                '#pragma unroll\n',
                f'for (int s0 = 0; s0 < {PE_dims[0]}; s0++) {{\n',
                '  #pragma unroll\n',
                f'  for (int s1 = 1; s1 < {PE_dims[1]}; s1++) {{\n',
                f'    local_{group_name}[s0][s1][0] = __fpga_reg(__fpga_reg(local_{group_name}[s0][0][0]));\n'
                '  }\n'
                '}\n'
            ]            
        elif dir == [1, 0]:
            new_lines = [\
                '#pragma unroll\n',
                f'for (int s1 = 0; s1 < {PE_dims[1]}; s1++) {{\n',
                f'  local_{group_name}[0][s1][0] = read_channel_intel(fifo_{group_name}_PE[0][s1]);\n',
                '}\n'
                '#pragma unroll\n',
                f'for (int s0 = 1; s0 < {PE_dims[0]}; s0++) {{\n',
                '  #pragma unroll\n',
                f'  for (int s1 = 0; s1 < {PE_dims[1]}; s1++) {{\n',
                f'    local_{group_name}[s0][s1][0] = __fpga_reg(__fpga_reg(local_{group_name}[0][s1][0]));\n'
                '  }\n'
                '}\n'
            ]            
        else:
            raise NotImplementedError('Unsupport Direction')
        lines = new_lines + lines

    return lines

def modify_channels(lines, data_trans_info, PE_dims):
    # In the channel declaration, delete all the fifo_{group}_PE
    new_lines = []
    drain_groups = []
    for line in lines:
        find = False
        for group in data_trans_info:
            if line.find('/* PE fifo */') != -1 and line.find(f'fifo_{group}_PE') != -1:
                find = True
        if line.find('/* PE fifo */') != -1 and line.find(f'_PE_') != -1 and line.find('drain') != -1:
            m = re.search(r'fifo_(.+?)_PE', line)
            drain_group = m.group(1)
            if drain_group not in drain_groups:
                drain_groups.append(drain_group)
            find = True
        if not find:
            new_lines.append(line)    

    for line_id in range(len(lines)):
        line = lines[line_id]
        if line.find('/* Channel Declaration */') != -1:
            channel_decl_start = line_id
            break
    for group in data_trans_info:
        info = data_trans_info[group]
        dir = [info['PE_index_end'][dim] - info['PE_index_start'][dim] for dim in range(len(info['PE_index_start']))]
        if dir == [0, 1]:
            line = f'/* PE fifo */ channel {info["data_type"]} fifo_{group}_PE[{PE_dims[0]}][1] __attribute__((depth(2)));\n'
        elif dir == [1, 0]:
            line = f'/* PE fifo */ channel {info["data_type"]} fifo_{group}_PE[1][{PE_dims[1]}] __attribute__((depth(2)));\n'
        else:
            raise NotImplementedError('Unsupport Direction')
        new_lines.insert(channel_decl_start + 1, line)
    for group in drain_groups:
        line = f'/* PE fifo */ channel float fifo_{group}_PE[{PE_dims[0]}][{PE_dims[1]}] __attribute__((depth(2)));\n'
        new_lines.insert(channel_decl_start + 1, line)

    # Replace all channel calls
    for group in data_trans_info:
        fifo_prefix = f'fifo_{group}_PE_'
        for line_id in range(len(new_lines)):
            line = new_lines[line_id]
            if line.find(fifo_prefix) != -1:
                modify = False
                if line.find('write_channel_intel') != -1:
                    m = re.search(r'\((.+?)\)', line)
                    fifo_name = m.group(1).split(',')[0]                    
                    modify = True
                elif line.find('read_channel_intel') != -1:
                    m = re.search(r'\((.+?)\)', line)
                    fifo_name = m.group(1)
                    modify = True
                if modify:                    
                    #print(fifo_name)
                    index = fifo_name[len(fifo_prefix):].split('_')
                    new_fifo_name = fifo_prefix[:-1]
                    for ind in index:
                        new_fifo_name += f'[{ind}]'
                    #print(new_fifo_name)
                    line = line.replace(fifo_name, new_fifo_name)
                    new_lines[line_id] = line

    #print(lines)
    #print(drain_groups)
    for group in drain_groups:
        fifo_prefix = f'fifo_{group}_PE_'
        for line_id in range(len(new_lines)):
            line = new_lines[line_id]
            if line.find(fifo_prefix) != -1:
                modify = False
                if line.find('write_channel_intel') != -1:
                    m = re.search(r'\((.+?)\)', line)
                    fifo_name = m.group(1).split(',')[0]                    
                    modify = True         
                elif line.find('read_channel_intel') != -1:
                    m = re.search(r'\((.+?)\)', line)
                    fifo_name = m.group(1)
                    modify = True                           
                if modify:       
                    # Check if inside a PE definition
                    inside_PE = False
                    prev_line_id = line_id - 1                                        
                    while prev_line_id >= 0:                        
                        prev_line = new_lines[prev_line_id]                                                             
                        if prev_line.find('/* Module') != -1:
                            break
                        if prev_line.find('void PE') != -1:
                            inside_PE = True
                            break      
                        prev_line_id -= 1                                                      
                    #print(inside_PE)                        
                    #print(fifo_prefix)
                    #print(fifo_name)
                    index = fifo_name[len(fifo_prefix):].split('_')
                    new_fifo_name = fifo_prefix[:-1]
                    if inside_PE:
                        for i in range(len(PE_dims)):
                            new_fifo_name += f'[s{i}]'
                    else:
                        for ind in index:                        
                            new_fifo_name += f'[{ind}]'
                    #print(new_fifo_name)
                    line = line.replace(fifo_name, new_fifo_name)
                    new_lines[line_id] = line

    # Delete all dummy functions
    module_start = False
    delete_module = False
    delete_pos = []
    for line_id in range(len(new_lines)):
        line = new_lines[line_id]
        if line.find('/* Module Definition */') != -1:
            module_start = not module_start
            if module_start:
                module_start_pos = line_id
                delete_module = False
            if not module_start:
                module_end_pos = line_id
                if delete_module:
                    delete_pos.append([module_start_pos, module_end_pos])
            if module_start:
                nxt_line = new_lines[line_id + 3]            
                if nxt_line.find('dummy') != -1:
                    delete_module = True
    offset = 0
    for p in delete_pos:
        new_lines = new_lines[:p[0] - offset] + new_lines[p[1] + 1 - offset:]
        offset += (p[1] - p[0] + 1)                

    return new_lines

def modify_body(lines, PE_dims, var_map):
    """
    This function modifies the PE body.
    For the user statement, it is wrapped with unrolled space loops
    For the data transfer statements, they are replaced with two loop blocks,
    one for initializing the boundary, the other for reusing the data.
    """    
    loop_bodies = []
    # Locate the user statements
    for line_id in range(len(lines)):
        line = lines[line_id]
        if line.find('hls_pipeline') != -1:
            # extract the loop body
            body_start = line_id
            r_minus_l = -1
            nxt_line_id = line_id + 1            
            while nxt_line_id < len(lines):
                nxt_line = lines[nxt_line_id]
                if nxt_line.find('}') != -1:
                    r_minus_l += 1
                if nxt_line.find('{') != -1:
                    r_minus_l -= 1
                if r_minus_l == 0:
                    body_end = nxt_line_id - 1
                    break
                nxt_line_id += 1
            loop_body = lines[body_start : body_end + 1]
            #print(loop_body)
            loop_bodies.append({'pos': [body_start, body_end], 'lines': loop_body})
    
    # Modidy the loop bodies
    #for body in loop_bodies:
    body_offset = 0
    for idx in range(len(loop_bodies)):
        body = loop_bodies[idx]
        body_lines = body['lines']        
        group_names = []
        has_data_trans = True
        data_trans_info = extract_data_trans_info(body_lines, PE_dims)
        # Remove the in transfer
        while has_data_trans:
            has_data_trans = False
            for line_id in range(len(body_lines)):
                line = body_lines[line_id]
                if line.find('read_channel_intel') != -1:
                    has_data_trans = True
                    # Locate the read block and the write block
                    block_start, block_end = locate_data_trans_block(line_id, body_lines)
                    m = re.search(r'\((.+?)\)', line)    
                    fifo_name = m.group(1)
                    group_name = fifo_name.split('_')[1]
                    group_names.append(group_name)
                    break
            if has_data_trans:
                body_lines = body_lines[:block_start] + body_lines[block_end + 1:]
        # Remove the out transfer
        has_data_trans = True
        while has_data_trans:
            has_data_trans = False
            for line_id in range(len(body_lines)):
                line = body_lines[line_id]
                if line.find('write_channel_intel') != -1:
                    m = re.search(r'\((.+?)\)', line)
                    fifo_name = m.group(1).split(',')[0]
                    group_name = fifo_name.split('_')[1]
                    if group_name in group_names:
                        has_data_trans = True
                        block_start, block_end = locate_data_trans_block(line_id, body_lines)
            if has_data_trans:
                body_lines = body_lines[:block_start] + body_lines[block_end + 1:]
        #print(body_lines)
        # Wrap the body with space loops
        for dim_idx in range(len(PE_dims)):
            dim = PE_dims[dim_idx]            
            line = f'#pragma unroll\nfor (int s{dim_idx} = 0; s{dim_idx} < {dim}; s{dim_idx}++) {{\n'
            body_lines.insert(dim_idx, line)                        
        for dim in PE_dims:
            body_lines.append('}\n')

        # Modify the index
        body_lines = modify_index(body_lines, var_map, PE_dims)
        #print(body_lines)

        # Insert the data transfer stmts
        body_lines = insert_data_trans(body_lines, data_trans_info, PE_dims)
        #loop_bodies[idx]['lines'] = body_lines

        # Replace the loop bodies
        body_pos = body['pos']        
        lines = lines[: body_offset + body_pos[0]] \
                + body_lines \
                + lines[body_offset + body_pos[1] + 1 :]   
        body_offset += len(body_lines) - (body_pos[1] - body_pos[0] + 1)

    return lines

def extract_data_trans_info(lines, PE_dims):
    """ Extract the data transfer information 

    """
    data_trans_info = {}
    for line_id in range(len(lines)):
        line = lines[line_id]
        if line.find('read_channel_intel') != -1:
            # Check the start and end of the block
            block_start, block_end = locate_data_trans_block(line_id, lines)            
            block_lines = lines[block_start : block_end + 1]
            # Parse the data type
            block_line = block_lines[1]
            data_type = block_line.strip().split(' ')[0]
            #print(data_type)
            # Parse the start PE index
            block_line = block_lines[2]
            m = re.search(r'\((.+?)\)', block_line)
            fifo_name = m.group(1)
            PE_index_start = fifo_name.split('_')[-len(PE_dims):]
            PE_index_start = [int(s) for s in PE_index_start]
            #print(PE_index_start)
            # Parse the IO group name
            group_name = fifo_name.split('_')[1]
            #print(group_name)
            data_trans_info[group_name] = {\
                'in_block_lines': block_lines, 'in_block_pos': [block_start, block_end], \
                'PE_index_start': PE_index_start, 'data_type': data_type}
        if line.find('write_channel_intel') != -1:
            m = re.search(r'\((.+?)\)', line)
            fifo_name = m.group(1).split(',')[0]
            group_name = fifo_name.split('_')[1]
            if group_name in data_trans_info:                
                # Check the start and end of the block
                block_start, block_end = locate_data_trans_block(line_id, lines)
                block_lines = lines[block_start : block_end + 1]
                # Parse the end PE index
                block_line = block_lines[3]
                m = re.search(r'\((.+?)\)', block_line)
                fifo_name = m.group(1).split(',')[0]
                PE_index_end = fifo_name.split('_')[-len(PE_dims):]
                PE_index_end = [int(s) for s in PE_index_end]
                #print(PE_index_end)
                group_name = fifo_name.split('_')[1]
                data_trans_info[group_name]['PE_index_end'] = PE_index_end
                data_trans_info[group_name]['out_block_lines'] = block_lines
                data_trans_info[group_name]['out_block_pos'] = [block_start, block_end]

    return data_trans_info

def compose_PE(data_trans_info, PE_dims, PE_defs):
    PE_def = PE_defs[0]
    # Extract the variable declariton and main body */
    PE_lines = PE_def['def']
    var_start = False
    var_end = False
    var_lines = []
    body_lines = []
    for line_id in range(len(PE_lines)):
        line = PE_lines[line_id]
        if line.find('Variable Declaration') != -1:
            var_start = not var_start
            if not var_start:
                var_end = True
            continue
        if var_start:
            var_lines.append(line)
        if var_end:
            body_lines.append(line)
    var_lines = var_lines[1:] # Remove the module id
    body_lines = body_lines[:-2] # Remove the function end bracket

    lines = []
    lines.append('/* Module Definition */\n')
    lines.append('__attribute__((max_global_work_dim(0)))\n')
    lines.append('__attribute__((autorun))\n')
    lines.append('kernel void PE()\n')
    lines.append('{\n')

    var_map = {}
    # Print the variable definitions 
    lines.append('  /* Variable Declaration */\n')
    for var_line in var_lines:
        simd = 0
        m = re.search(r'local_(.+?)\[', var_line)
        group_name = m.group(1)
        data_type = var_line.strip().split(' ')[0]
        index = var_line[var_line.find('['):var_line.find(';')]
        indices = []
        while len(index) > 0:
            start_pos = index.find('[')
            end_pos = index.find(']')
            indices.append(index[start_pos:end_pos+1])
            index = index[end_pos + 1:]
        #print(group_name, data_type, indices)            
        for dim in PE_dims:
            index = f'[{dim}]'
            indices.insert(0, index)
        if group_name in data_trans_info:
            if data_trans_info[group_name]['data_type'] != data_type:
                # SIMD > 1
                simd = 1
                data_type = data_trans_info[group_name]['data_type']
                indices = indices[:-1]
        #print(group_name, data_type, indices)            
        new_index = ''
        for index in indices:
            new_index += index
        new_var_line = f'  {data_type} local_{group_name}{new_index};'
        #print(new_var_line)      
        var_map[f'local_{group_name}'] = {'simd': simd}
        lines.append(new_var_line + '\n')
        
    lines.append('  /* Variable Declaration */\n')

    # Print the body
    new_body_lines = modify_body(body_lines, PE_dims, var_map)
    for line in new_body_lines:
        lines.append(line)

    lines.append('}\n')
    lines.append('/* Module Definition */\n')

    return lines

def run(input_f, output_f):
    """ Group PEs into a Monolithic Function

    This funciton is only used for the following case:
    - Intel OpenCL
    - The systolic array should be an output-stationary rectangular array
    We will first collect the array dims and the data transfer direction for each IO group.
    Next we will generate a new monolithic function of PE:
    - Variable declaration: 
      - Remove module ids
      - Extend all the local arrays with array dimensions. 
        - If the array is an external array, we will repack the array with the SIMD factor
    - For each statement, add the space loops with unroll pragma      
    """
    with open(input_f) as f:
        lines = f.readlines()

    # Collect the array dims
    PE_defs = []
    module_start = False
    is_PE = False    
    PE_indices = []
    for line_id in range(len(lines)):
        line = lines[line_id]
        if line.find('Module Definition') != -1:
            module_start = not module_start
            if module_start:
                module_start_pos = line_id
                is_PE = False
            else:
                module_end_pos = line_id
                if is_PE:
                    PE_defs.append({'def': lines[module_start_pos : module_end_pos + 1], \
                                    'pos': [module_start_pos, module_end_pos]})
            if module_start:
                #print(line_id)
                nxt_line_id = line_id + 1
                while nxt_line_id < len(lines):                    
                    nxt_line = lines[nxt_line_id]
                    if nxt_line.find('kernel void PE') != -1:
                        is_PE = True
                        m = re.search(r'void PE(.+?)\(', nxt_line)
                        #print(nxt_line)
                        if m:
                            PE_index = m.group(1).split('_')[1:]
                            PE_indices.append(PE_index)
                        if is_PE:
                            break
                    if nxt_line.find('Module Definition') != -1:
                        break
                    nxt_line_id += 1

    #print(PE_indices)
    PE_dims = [int(d) for d in PE_indices[0]]
    for ind in PE_indices:
        for dim in range(len(PE_dims)):
            PE_dims[dim] = max(PE_dims[dim], int(ind[dim]) + 1)
    #print(PE_dims)
    
    PE_lines = PE_defs[0]['def']
    # Parse the data transfer information
    data_trans_info = extract_data_trans_info(PE_lines, PE_dims)    

    # Compose the new PE function
    PE_lines = compose_PE(data_trans_info, PE_dims, PE_defs)

    line_offset = 0
    for PE_def in PE_defs:
        lines = lines[:PE_def['pos'][0] - line_offset] + lines[PE_def['pos'][1] + 1 - line_offset:]
        line_offset += (PE_def['pos'][1] - PE_def['pos'][0] + 1)

    lines = lines + PE_lines

    # Modify the channels
    lines = modify_channels(lines, data_trans_info, PE_dims)

    with open(output_f, 'w') as f:
        for line in lines:
            f.write(line)
    #    f.writelines(PE_lines)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Group PEs into a Monolithic Function')
    parser.add_argument('-i', required=True, help='input kernel function')
    parser.add_argument('-o', required=True, help='output kernel function')

    args = parser.parse_args()
    run(args.i, args.o)