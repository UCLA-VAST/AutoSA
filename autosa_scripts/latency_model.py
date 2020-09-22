import os
import json
import re
import xml.etree.ElementTree as ET
import numpy as np
import pandas as pd
import joblib
from sklearn.linear_model import LinearRegression
from sklearn import metrics
from sklearn.model_selection import train_test_split
from scipy.stats.mstats import gmean
from statistics import mean
import shutil
import math
import argparse

def extract_latency_info(design_dir):
    """ Extract loop information of the design.

    Returns a dictionary containing the following infomation:
    - loop_infos: dict
    - module_list: list
    - array_info: dict
    - module_grouped: dict

    Parameters
    ----------
    design_dir: str
        The design directory
    """
    loop_path = f'{design_dir}/latency_est'
    loop_info_files = os.listdir(loop_path)
    loop_info_all = {}
    module_names = []

    for f_name in loop_info_files:
        if f_name == 'array_info.json':
            with open(loop_path + '/' + f_name) as f:
                array_info = json.load(f)
        else:
            with open(loop_path + '/' + f_name) as f:
                loop_info_module = json.load(f)
                module_name = loop_info_module['module_name']
                loop_info_all[module_name] = loop_info_module
                module_names.append(module_name)

    module_grouped = {}
    # Place inter_trans and intra_trans module under the outer module
    for module_name in module_names:
        # intra_trans
        if module_name.find('intra_trans') != -1:
            module_name_prefix = module_name[:-12]
            if module_name_prefix not in module_grouped:
                module_grouped[module_name_prefix] = {}
            module_grouped[module_name_prefix]['intra_trans'] = module_name

            module_name_prefix = module_name_prefix + '_boundary'
            if module_name_prefix not in module_grouped:
                module_grouped[module_name_prefix] = {}
            module_grouped[module_name_prefix]['intra_trans'] = module_name
        
        # inter_trans
        elif module_name.find('inter_trans') != -1:
            if module_name.find('boundary') != -1:
                module_name_prefix = module_name[:-21] + '_boundary'
            else:
                module_name_prefix = module_name[:-12]

            if module_name_prefix not in module_grouped:
                module_grouped[module_name_prefix] = {}
            module_grouped[module_name_prefix]['inter_trans'] = module_name
        else:
            if module_name not in module_grouped:
                module_grouped[module_name] = {}

    ret = {'loop_infos': loop_info_all, 'module_list': module_names, \
           'module_grouped': module_grouped, 'array_info': array_info}

    return ret

def convert_latency_infos_to_df(latency_infos):
    """ Convert the latency infos into a dataframe.

    """
    return

def is_loop_struct_leaf_empty(loop_struct):
    """ Examine if the leaf node of the loop struct is empty.

    Parameters
    ----------
    loop_struct: dict
        loop structure in JSON format
    """
    if "loop" in loop_struct:
        child = loop_struct['loop']['child']
        if child == None:
            return 1
        else:
            return is_loop_struct_leaf_empty(child)
    elif "mark" in loop_struct:
        child = loop_struct['mark']['child']
        if child == None:
            return 1
        else:
            return is_loop_struct_leaf_empty(child)
    elif "user" in loop_struct:
        child = loop_struct['user']['user_expr']
        if child == None:
            return 1
        else:
            return 0
    elif "block" in loop_struct:
        children = loop_struct['block']['child']
        if children == None:
            return 1
        else:
            for child in children:
                is_empty = is_loop_struct_leaf_empty(child)
                if is_empty == 0:
                    return 0
            return 1
    elif "if" in loop_struct:
        if_struct = loop_struct['if']
        then_block = if_struct['then']
        is_empty = is_loop_struct_leaf_empty(then_block)
        if is_empty == 0:
            return 0
        if 'else' in if_struct:
            else_block = if_struct['else']
            is_empty = is_loop_struct_leaf_empty(else_block)
            if is_empty == 0:
                return 0
            return 1
    return 1

def loop_struct_has_non_simd_loop(loop_struct, config):
    """ Examine if the leaf node of the loop struct has any non-SIMD loop.

    """
    if "loop" in loop_struct:
        if config['under_simd'] == 1:
            return 0
        else:
            return 1
    elif "mark" in loop_struct:
        mark = loop_struct['mark']
        mark_name = mark['mark_name']
        if mark_name == 'simd':
            config['under_simd'] = 1
        child = mark['child']
        if child == None:
            return 0
        else:
            return loop_struct_has_non_simd_loop(child, config)
    elif "user" in loop_struct:
        return 0
    elif "block" in loop_struct:
        children = loop_struct['block']['child']
        if children == None:
            return 0
        else:
            for child in children:
                has_non_simd_loop = loop_struct_has_non_simd_loop(child, config)
                if has_non_simd_loop == 1:
                    return 1
            return 0
    elif "if" in loop_struct:
        if_struct = loop_struct['if']
        then_block = if_struct['then']
        has_non_simd_loop = loop_struct_has_non_simd_loop(then_block, config)
        if has_non_simd_loop == 1:
            return 1
        if 'else' in if_struct:
            else_block = if_struct['else']
            has_non_simd_loop = loop_struct_has_non_simd_loop(else_block, config)
            if has_non_simd_loop == 1:
                return 1
        return 0

    return 0

def loop_struct_has_for_loop(loop_struct):
    """ Examine if the leaf node of the loop struct has any for loop.

    """
    if "loop" in loop_struct:
        return 1
    elif "mark" in loop_struct:
        child = loop_struct['mark']['child']
        if child == None:
            return 0
        else:
            return loop_struct_has_for_loop(child)
    elif "user" in loop_struct:
        child = loop_struct['user']['user_expr']
        return 0
    elif "block" in loop_struct:
        children = loop_struct['block']['child']
        if children == None:
            return 0
        else:
            for child in children:
                has_for_loop = loop_struct_has_for_loop(child)
                if has_for_loop == 1:
                    return 1
            return 0
    elif "if" in loop_struct:
        if_struct = loop_struct['if']
        then_block = if_struct['then']
        has_for_loop = loop_struct_has_for_loop(then_block)
        if has_for_loop == 1:
            return 1
        if 'else' in if_struct:
            else_block = if_struct['else']
            has_for_loop = loop_struct_has_for_loop(else_block)
            if has_for_loop == 1:
                return 1
        return 0

    return 0

def predict_module_latency_xilinx(loop_struct, config):
    """ Predict the module latency for Xilinx FPGAs.

    """
    latency = config['latency']
    if "loop" in loop_struct:
        config['under_loop'] = 1
        # Extract the loop information
        loop = loop_struct['loop']
        loop_info = loop['loop_info']
        lb = loop_info['lb']
        ub = loop_info['ub']
        iterator = loop_info['iter']
        # Check if lb/ub is real number
        if lb.isnumeric():
            lb_n = int(lb)
        else:
            lb_n = 0
            #raise NotImplementedError(f'Non-number loop lower bound ({lb}) is not supported.')
        if ub.isnumeric():
            ub_n = int(ub)
        else:
            raise NotImplementedError(f'Non-number loop upper bound ({ub}) is not supported.')
        config['context'][iterator] = {}
        config['context'][iterator]['lb'] = lb_n
        config['context'][iterator]['ub'] = ub_n
        if config['under_unroll'] == 0:
            latency = latency * (ub_n - lb_n + 1)
            config['latency'] = latency
        child = loop['child']
        # if it is an outer module, we will need to update loop_prefix at each loop level.
        if config['module_type'] == 1:
            if config['loop_prefix'] == 'Loop':
                config['loop_prefix'] = config['loop_prefix'] + str(config['loop_offset'])
            else:
                config['loop_prefix'] = config['loop_prefix'] + '.' + str(config['loop_offset'])
        # Store the current for loop
        config['last_for']['iter'] = iterator
        config['last_for']['lb'] = lb_n
        config['last_for']['ub'] = ub_n
        if config['under_coalesce'] == 1:
            config['last_for']['under_coalesce'] = 1
        else:
            config['last_for']['under_coalesce'] = 0        
        predict_module_latency_xilinx(child, config)
    elif "mark" in loop_struct:
        mark = loop_struct['mark']
        mark_name = mark['mark_name']
        # If we meet the 'hls_unroll' mark, the loop below no longer counts in to the loop iteration.
        if mark_name == 'simd':
            config['under_unroll'] = 1
        if mark_name == 'access_coalesce':
            config['under_coalesce'] = 1
        if mark_name == 'access_serialize':
            config['under_serialize'] = 1
        child = mark['child']
        predict_module_latency_xilinx(child, config)
    elif "user" in loop_struct:
        user = loop_struct['user']
        user_expr = user['user_expr']
        config['under_unroll'] = 0
        config['under_coalesce'] = 0        
        if config['module_type'] == 1:
            # For outer module, we directly return.
            config['under_serialize'] = 0
            if config['latency'] == 1:
                config['latency'] = 0
            return
        
        #if config['module_name'] == 'A_IO_L2_in':
        #    print(latency)
        # Set II and depth to 1 by default.
        II = 1
        depth = 1
        #print(latency, user_expr)
        if user_expr.find('dram') != -1:
            # This is a DRAM stmt, we will plug in the estimated model.
            # Extract the array name
            #module_name = config['module_name']
            #array_name = module_name.split('_')[0]
            #array_info = config['array_info'][array_name]

            if config['last_for']['under_coalesce'] == 1 and \
               config['under_serialize'] == 0:
                # This statement accesses the dram under a coalesced loop.
                burst_len = (config['last_for']['ub'] - config['last_for']['lb'])
                # The DRAM latency is etimated as 200ns
                dram_latency = 200 / config['cycle'] + burst_len + depth
                latency = latency / burst_len * dram_latency
            elif config['under_serialize'] == 1:
                # This statement accesses the dram with serialized data.
                latency = (latency - 1) * II + depth
            else:
                latency = latency * (200 / config['cycle'] + depth)
        else:
            latency = (latency - 1) * II + depth
        config['under_serialize'] = 0
        config['latency'] = latency        
    elif "block" in loop_struct:
        block = loop_struct['block']
        block_child = block['child']

        # Check if only one child is valid and the rest only contain the empty leaf node.
        # If so, continue from the non-empty leaf node w/o further action.
        n_child = 0
        for child in block_child:
            is_empty = is_loop_struct_leaf_empty(child)
            if is_empty == 0:
                n_child += 1
                single_child = child

        if n_child == 1:
            predict_module_latency_xilinx(single_child, config)
            return

        # Check if the current block contains "simd" mark.
        # If so, continue from "simd" branch w/o any further action.
        simd_child = 0
        for child in block_child:
            if "mark" in child:
                mark_name = child['mark']['mark_name']
                if mark_name == 'simd':
                    config['under_unroll'] = 1
                    child = child['mark']['child']
                    simd_child = 1
                    break
        if simd_child == 1:
            predict_module_latency_xilinx(child, config)
            return

        # Proceed as normal.
        # Check if the child contains any non-simd loop. If yes, we will
        # update the loop prefix.
        for child in block_child:
            local_config = {}
            local_config['under_simd'] = 0
            has_non_simd_loop = loop_struct_has_non_simd_loop(child, local_config)
            if has_non_simd_loop:
                if config['module_type'] != 1 and config['under_loop'] == 1:
                    if config['loop_prefix'] == 'Loop':
                        config['loop_prefix'] = config['loop_prefix'] + str(config['loop_offset'])
                    else:
                        config['loop_prefix'] = config['loop_prefix'] + '.' + str(config['loop_offset'])                    
                break
        loop_prefix = config['loop_prefix']
        loop_offset = 1
        under_loop = config['under_loop']

        # If the block is under loop and all childrens are user nodes,
        # we will proceed and dive into the user nodes.
        all_user_child = 1
        for child in block_child:
            has_for_loop = loop_struct_has_for_loop(child)
            if has_for_loop:
                all_user_child = 0
                break
        latency = config['latency']
        block_latency = 0
        for child in block_child:
            config['loop_offset'] = loop_offset
            config['loop_prefix'] = loop_prefix
            if under_loop == 1:
                config['under_loop'] = 0
            has_for_loop = loop_struct_has_for_loop(child)
            if all_user_child:
                # Select the statement with the longest latency.
                config['latency'] = latency
                predict_module_latency_xilinx(child, config)
                block_latency = max(block_latency, config['latency'])
            else:
                # Accumulate the latency.
                if has_for_loop:
                    config['latency'] = 1
                    predict_module_latency_xilinx(child, config)
                    loop_offset += 1
                    block_latency += config['latency']
        if all_user_child:
            latency = block_latency
        else:
            latency = latency * max(block_latency, 1)
        config['latency'] = latency
    elif "if" in loop_struct:
        # For if then clause, we will treat it as similar as block by
        # adding up the latency of all sub blocks.
        latency = config['latency']
        block_latency = 0
        if_struct = loop_struct['if']
        then_block = if_struct['then']
        if config['module_type'] != 1 and config['under_loop'] == 1:
            if config['loop_prefix'] == 'Loop':
                config['loop_prefix'] = config['loop_prefix'] + str(config['loop_offset'])
            else:
                config['loop_prefix'] = config['loop_prefix'] + '.' + str(config['loop_offset'])
        loop_prefix = config['loop_prefix']
        loop_offset = config['loop_offset']
        has_for_loop = loop_struct_has_for_loop(then_block)
        if has_for_loop:
            config['latency'] = 1
            predict_module_latency_xilinx(then_block, config)
            block_latency = max(block_latency, config['latency'])
        if 'else' in if_struct:
            loop_offset += 1
            config['loop_offset'] = loop_offset
            else_block = if_struct['else']
            has_for_loop = loop_struct_has_for_loop(else_block)
            if has_for_loop:
                config['latency'] = 1
                predict_module_latency_xilinx(else_block, config)
                block_latency = max(block_latency, config['latency'])
        #print('1: ', latency)
        #print('2: ', block_latency)
        latency = latency * max(block_latency, 1)
        config['latency'] = latency

def predict_design_latency(latency_info, cycle=5, early_stop=-1):
    """ Predict the latency for a single design.

    We assume that the II and depth for each stmt to be one.

    Parameters
    ----------
    latency_info: dict
        A dict containing the latency info of the design.
    cycle: int
        The cycle time. (in ns)
    early_stop: int
        The baseline latency. If set -1, early stop is disabled.
    """
    latency_all = {}
    config = {}
    config['cycle'] = cycle
    module_grouped = latency_info['module_grouped']
    array_info = latency_info['array_info']
    loop_infos = latency_info['loop_infos']
    
    drain_latency = 0
    drain_outer = 1

    for module_name in module_grouped:        
        if 'dummy' in module_name:
            # Simply skip the dummy module
            continue
        if module_name not in loop_infos:
            continue

        ## debug
        #if module_name != 'A_IO_L2_in':
        #    continue
        #print(module_name)
        ## debug

        module = module_grouped[module_name]
        #print(module)

        config['context'] = {}
        config['latency'] = 1
        config['loop_prefix'] = 'Loop'
        config['loop_offset'] = 1
        config['under_unroll'] = 0
        config['under_coalesce'] = 0
        config['under_serialize'] = 0
        config['under_loop'] = 0
        config['last_for'] = {}
        config['array_info'] = array_info
        config['module_name'] = module_name
        # 0: default 1: outer 2: inter_trans 3: intra_trans
        config['module_type'] = 0

        if 'inter_trans' in module or 'intra_trans' in module:
            # This is a filter module. We take it as double buffered by default.            
            config['module_type'] = 1
            module_loop_info = loop_infos[module_name]
            predict_module_latency_xilinx(module_loop_info, config)
            outer_latency = config['latency']

            # inter module
            config['module_type'] = 2
            config['latency'] = 1
            config['loop_prefix'] = 'Loop'
            config['loop_offset'] = 1
            sub_module_name = module['inter_trans']
            config['module_name'] = sub_module_name
            module_loop_info = loop_infos[sub_module_name]
            predict_module_latency_xilinx(module_loop_info, config)
            inter_trans_latency = config['latency']

            # intra module
            config['module_type'] = 3
            config['latency'] = 1
            config['loop_prefix'] = 'Loop'
            config['loop_offset'] = 1
            sub_module_name = module['intra_trans']
            config['module_name'] = sub_module_name
            module_loop_info = loop_infos[sub_module_name]
            predict_module_latency_xilinx(module_loop_info, config)
            intra_trans_latency = config['latency']

            ## debug
            #print(outer_latency)
            #print(inter_trans_latency)
            #print(intra_trans_latency)
            ## debug
            
            if module_loop_info['module_prop']['double_buffer'] == 1:
                module_latency = outer_latency * max(inter_trans_latency, intra_trans_latency)
                if module_loop_info['module_prop']['in'] == 1:
                    module_latency += intra_trans_latency
                else:
                    module_latency += inter_trans_latency                                    
            else:
                module_latency = outer_latency * (inter_trans_latency + intra_trans_latency)            
            # Hack: For GEMM4
            #if 'C' in module_name:            
            if 'drain' in module_name:                                
                drain_outer = max(1, outer_latency)

            latency_all[module_name] = module_latency
        else:
            module_loop_info = loop_infos[module_name]
            #print(config['module_name'])
            predict_module_latency_xilinx(module_loop_info, config)
            latency_all[module_name] = config['latency']            
            # Hack: For GEMM4
            #if 'C' in module_name:
            if 'drain' in module_name:
                drain_latency = max(drain_latency, config['latency'])

        # If we set early stop, we are using a baseline latency to compare.
        # If any of the module latency is greater than the baseline, we
        # will return immediately. 
        if early_stop != -1:
            if config['latency'] > early_stop:
                return config['latency']

    #print(latency_all)
    drain_last_tile_latency = drain_latency / drain_outer
    latency = 0
    for lat in latency_all:
        if latency_all[lat] > latency:
            latency = latency_all[lat]
    #print(latency)
    #print(drain_last_tile_latency)
    latency += drain_last_tile_latency

    return int(latency)

def unit_test_predict_design_latency(design_dir):
    """ Unit test for design latency prediction

    Paramters
    ---------
    design_dir: str
        Design directory
    """
    latency_info = extract_latency_info(design_dir)
    latency = predict_design_latency(latency_info, 5)
    print("latency: ", latency)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="==== AutoSA Latency Model ====")
    parser.add_argument('-d', required=True, help='design directory')

    args = parser.parse_args()
    unit_test_predict_design_latency(args.d)