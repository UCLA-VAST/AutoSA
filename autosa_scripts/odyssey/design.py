import numpy as np
import json
import sys
import os
from numpy import ceil, floor

class Design(object):
    def __init__(self, name):
        self.name = name # design name        
        self.est_resource_func = None
        self.est_latency_func = None
        self.est_activity_func = None
        self.infer_params_func = None
        self.random_sampling_func = None
        self.bound_check_func = None
        self.params_config = None      
        self.desp = None  

    def print_resource_est_func(self, f, desp):
        f.write("def est_resource(params):\n")
        # Load parameters
        f.write("\t")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(p["name"])
            is_first = False
        f.write(" = ")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(f'params[\"{p["name"]}\"]')
            is_first = False
        f.write("\n\n")

        f.write("\t# DSP\n")
        f.write(f"\tDSP = {desp['compute']['PE']['num']} * ")
        f.write(f"{desp['compute']['PE']['unroll_factor']} * ")
        if desp["compute"]["PE"]["ele_type"] == "float":
            f.write(f"5\n")
        else:
            raise RuntimeError(f"Unsupported data type {desp['compute']['PE']['ele_type']} in resource estimation")        
        f.write("\n")

        # Print function est_BRAM18K
        f.write("\t# BRAM18K\n")
        f.write("\tdef est_BRAM18K(ele_size, ele_num, pack):\n")
        #f.write(f"\t\treturn ceil(ele_size*8*pack / 18) * ceil(ele_num/pack/1024)\n\n")
        f.write(f"\t\treturn ceil(ele_size*8*pack / 36) * ceil(ele_num/pack/512)\n\n")

        f.write(f"\tres_meta = {{}}\n")
        # Check if drain module can be merged.
        # Note: It should be supported in the codegen of AutoSA. However, currently, 
        # we move it here in the tuner.
        mem_meta_info = {}
        out_module = {}
        out_drain_module = {}
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.endswith('_out'):
                item = {'buf_size': module_mem['buf_size'], 
                        'num': module_mem['num']}
                if module.find('drain') != -1:
                    item['merged'] = 0
                    out_drain_module[module_mem['array']] = item
                else:                    
                    if module_mem['array'] not in out_module:
                        out_module[module_mem['array']] = [item]
                    else:
                        out_module[module_mem['array']].append(item)
        for array in out_drain_module:
            if array in out_module:
                for m in out_module[array]:                
                    if m['buf_size'] == out_drain_module[array]['buf_size'] and \
                       m['num'] == out_drain_module[array]['num']:
                       out_drain_module[array]['merged'] = 1

        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.find('drain') != -1 and out_drain_module[module_mem['array']]['merged'] == 1:
                continue
            f.write(f"\t{module}_unit_memory = est_BRAM18K({module_mem['ele_size']}, ")
            f.write(f"{module_mem['buf_size']}, ")
            if "data_pack_factor_inter" in module_mem:
                f.write(f"{module_mem['data_pack_factor_inter']})\n")
            else:
                f.write(f"1)\n")
            f.write(f"\tres_meta[\"{module}\"] = {{\"ele_size\": {module_mem['ele_size']}, \"buf_size\": {module_mem['buf_size']}, \"data_pack_factor\": 1, \"num\": {module_mem['num']}}}\n")
            if module_mem['double_buffer']:
                f.write(f"\tres_meta[\"{module}\"][\"num\"] *= 2\n")
            if "data_pack_factor" in module_mem:
                f.write(f"\tres_meta[\"{module}\"][\"data_pack_factor\"] = {module_mem['data_pack_factor_inter']}\n")
        #f.write("\tprint(A_IO_L1_in_unit_memory)\n")
        #f.write("\tprint(A_IO_L2_in_unit_memory)\n")
        #f.write("\tprint(B_IO_L2_in_unit_memory)\n")        
        #f.write("\tprint(PE_unit_memory)\n")
        #f.write("\tprint(C_1_IO_L2_out_unit_memory)\n")        
        #f.write("\tprint(C_drain_IO_L1_out_unit_memory)\n")

        f.write("\tBRAM18K = ")
        is_first = True
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.find('drain') != -1 and out_drain_module[module_mem['array']]['merged'] == 1:
                continue
            if not is_first:
                f.write(" + ")            
            f.write(f"{module}_unit_memory")
            if module_mem["double_buffer"]:
                f.write(f" * 2")
            else:
                f.write(f" * 1")
            f.write(f" * {module_mem['num']}")            
            is_first = False            
        f.write("\n\n")

        f.write("\t# URAM\n")
        f.write("\tURAM = 0\n\n")

        f.write("\tres = {\"DSP\": DSP, \"BRAM18K\": BRAM18K, \"URAM\": URAM}\n")
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.find('drain') != -1 and out_drain_module[module_mem['array']]['merged'] == 1:
                continue
            f.write(f"\tres['{module}_unit_memory'] = {module}_unit_memory\n")

        f.write("\n\treturn res, res_meta\n")
        f.write("\n")

    def print_latency_est_func(self, f, desp):
        f.write("def est_latency(params):\n")
        # Load parameters
        f.write("\t")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(p["name"])
            is_first = False
        f.write(" = ")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(f'params[\"{p["name"]}\"]')
            is_first = False
        f.write("\n\n")

        def extract_latency_expr(lat, info):
            ret = ""
            if lat["type"] == "block":
                info["has_for_child"] = 0
                no_for_child = True
                is_first = True
                ret += "("
                for child in lat["child"]:
                    if not is_first:
                        ret += " + "                    
                    ret += extract_latency_expr(child, info)                    
                    if info["has_for_child"] == 1:
                        no_for_child = False
                    is_first = False
                ret += ")"
                if no_for_child:
                    ret = "1"
            elif lat["type"] == "for":                
                child = lat["child"]
                expr = extract_latency_expr(child, info)                
                if info["valid"]:
                    ret = lat["bounds"][1] + " * " + expr
                else:
                    ret = expr
                info["has_for_child"] = 1
            elif lat["type"] == "mark":      
                if info["under_mark"] and lat["content"] == info["under_mark"]:
                    info["valid"] = True
                if lat["content"] == "simd":
                    if info["valid"]:
                        ret = "1"
                    else:
                        ret = "0"
                else:
                    child = lat["child"]
                    ret = extract_latency_expr(child, info)
                if info["under_mark"] and lat["content"] == info["under_mark"]:
                    info["valid"] = False
            elif lat["type"] == "user":
                user_expr = lat["child"]["user_expr"]
                if 'inter_intra' in user_expr or 'intra_inter' in user_expr:                    
                    if user_expr[:-2].split(".")[-1] == "1":
                        double_buffer = 1
                    else:
                        double_buffer = 0                    
                    # Plug in submodule latency
                    if f"{info['name']}_inter" in info["modules"]:
                        inter_expr = info["modules"][f"{info['name']}_inter"]
                    else:
                        inter_expr = None
                    if f"{info['name']}_intra" in info["modules"]:
                        intra_expr = info["modules"][f"{info['name']}_intra"]
                    else:
                        intra_expr = None

                    if inter_expr and intra_expr:
                        if info["in"] == 1 or info["in"] == 0:
                            ret = inter_expr
                        else:
                            if double_buffer:
                                ret = f"max({inter_expr}, {intra_expr})"
                            else:
                                ret = f"({inter_expr} + {intra_expr})"
                        info["has_for_child"] = 1
                    else:                        
                        ret = "1"                        
                    if not info["valid"]:
                        ret = "0"
                elif "inter_trans" in user_expr:
                    # Plug in submodule latency
                    if f"{info['name']}_inter" in info["modules"]:
                        ret = info["modules"][f"{info['name']}_inter"]
                    else:
                        ret = "1"
                    if not info["valid"]:
                        ret = "0"
                elif "intra_trans" in user_expr:
                    # Plug in submodule latency                    
                    if f"{info['name']}_intra" in info["modules"]:
                        ret = info["modules"][f"{info['name']}_intra"]
                    else:
                        ret = "1"
                    if not info["valid"]:
                        ret = "0"
                else:
                    ret = "1"
            elif lat["type"] == "if":
                # Only examine the first child
                child = lat["child"][0]
                ret = extract_latency_expr(child, info)
            elif lat["type"] == "array_tile":      
                if info["module_attr"]["to_dram"] == 1:
                    if info["module_attr"]["serialize"] == 0:
                        # Consider the DRAM latency here.
                        ret = "(" + f"{lat['size']}/{lat['last_dim']}*(20+{lat['last_dim']}/(512/8/{lat['ele_size']}))" + ")"
                    else:
                        ret = "(" + lat["size"] + "/" + f"min({lat['data_pack_factor']}, 512/8/{lat['ele_size']})" + ")"
                else:
                    ret = "(" + lat["size"] + "/" + lat["data_pack_factor"] + ")"                    
            else:
                raise RuntimeError(f"Unsupported latency node type {lat['type']}")

            return ret

        # Check if drain module can be omitted
        # Note: It should be supported in the codegen of AutoSA. However, currently,
        # we move it here in the tuner.        
        out_module = {}
        out_drain_module = {}
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.endswith('_out'):
                item = {'buf_size': module_mem['buf_size'], 
                        'num': module_mem['num']}
                if module.find('drain') != -1:
                    item['merged'] = 0
                    out_drain_module[module_mem['array']] = item
                else:                    
                    if module_mem['array'] not in out_module:
                        out_module[module_mem['array']] = [item]
                    else:
                        out_module[module_mem['array']].append(item)
        for array in out_drain_module:
            if array in out_module:
                for m in out_module[array]:                
                    if m['buf_size'] == out_drain_module[array]['buf_size'] and \
                       m['num'] == out_drain_module[array]['num']:
                       out_drain_module[array]['merged'] = 1

        # Latency prologue
        latency_prologue_items = []
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            for module in desp["latency"]:
                if desp["attr"][module]["in"] != 1:
                    continue
                if "inter" in module or "intra" in module:                    
                    # Keep all the latency AST under the mark.
                    info["valid"] = True
                    info["under_mark"] = None
                    info["in"] = 1
                else:
                    # Only keep the latency AST under the mark.
                    info["valid"] = False
                    info["under_mark"] = "array"
                    info["in"] = 1
                module_lat = desp["latency"][module]  
                info["name"] = module     
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_latency_expr(module_lat, info)
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue
            if module.find('drain') != -1 and out_drain_module[module_mem['array']]['merged'] == 1:
                continue
            f.write(f"\t{module}_single_latency = ")                        
            f.write(info["modules"][module])
            f.write(f"\n")      
            latency_prologue_items.append(f"{module}_single_latency")
        f.write("\tlatency_prologue = max(")
        is_first = True
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue 
            if module.find('drain') != -1 and out_drain_module[module_mem['array']]['merged'] == 1:
                continue           
            if not is_first:
                f.write(", ")
            f.write(f"{module}_single_latency")
            is_first = False
        f.write(")\n\n")

        # Latency epilogue
        latency_epilogue_items = []
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            for module in desp["latency"]:
                if desp["attr"][module]["in"] != 0:
                    continue
                if "inter" in module or "intra" in module:
                    info["valid"] = True
                    info["under_mark"] = None
                    info["in"] = 0
                else:
                    info["valid"] = False
                    info["under_mark"] = "array"
                    info["in"] = 0
                module_lat = desp["latency"][module]  
                info["name"] = module                
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_latency_expr(module_lat, info)
        for module in info["modules"]:            
            if "inter" in module or "intra" in module:
                continue
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue
            f.write(f"\t{module}_single_latency = ")                        
            f.write(info["modules"][module])
            latency_epilogue_items.append(f"{module}_single_latency")
            f.write(f"\n")        
        cnt = 0
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue    
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                 
            cnt += 1
        if cnt == 1:
            f.write("\tlatency_epilogue = ")
        else:
            f.write("\tlatency_epilogue = max(")
        is_first = True
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue    
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                    
            if not is_first:
                f.write(", ")
            f.write(f"{module}_single_latency")
            is_first = False
        if cnt == 1:            
            f.write("\n\n")
        else:
            f.write(")\n\n")

        # Latency main
        latency_main_items = []
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["in"] = -1
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_latency_expr(module_lat, info)            
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                  
            f.write(f"\t{module}_latency = ")                        
            f.write(info["modules"][module])
            f.write(f"\n")        
            latency_main_items.append(f"{module}_latency")
        f.write("\tlatency_main = max(")
        is_first = True
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue   
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                      
            if not is_first:
                f.write(", ")
            f.write(f"{module}_latency")
            is_first = False
        f.write(")\n\n")

        #f.write("\tprint(latency_prologue, latency_main, latency_epilogue)\n\n")

        f.write("\tlatency = latency_prologue + latency_main + latency_epilogue\n\n")
        
        f.write("\t# Meta information, used for conv fusion only\n")
        f.write("\tlatency_meta = {\"latency_prologue\": {}, \"latency_main\": {}, \"latency_epilogue\": {}}\n")
        # Prologue        
        for item in latency_prologue_items:            
            f.write(f"\tlatency_meta[\"latency_prologue\"][\"{item}\"] = {item}\n")
        # Epilogue
        for item in latency_epilogue_items:            
            f.write(f"\tlatency_meta[\"latency_epilogue\"][\"{item}\"] = {item}\n")
        # Main
        for item in latency_main_items:            
            f.write(f"\tlatency_meta[\"latency_main\"][\"{item}\"] = {item}\n")

        f.write("\treturn latency, latency_meta\n")
        f.write("\n")

    def print_activity_est_func(self, f, desp):
        f.write("def est_activity(params):\n")
        # Load parameters
        f.write("\t")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(p["name"])
            is_first = False
        f.write(" = ")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(f'params[\"{p["name"]}\"]')
            is_first = False
        f.write("\n\n")

        def extract_stmt_call_num_expr(lat, info):
            ret = ""
            if lat["type"] == "block":
                info["has_for_child"] = 0
                no_for_child = True
                is_first = True
                ret += "("
                for child in lat["child"]:
                    if not is_first:
                        ret += " + "                    
                    ret += extract_stmt_call_num_expr(child, info)                    
                    if info["has_for_child"] == 1:
                        no_for_child = False
                    is_first = False
                ret += ")"
                if no_for_child:
                    ret = "1"
            elif lat["type"] == "for":                
                child = lat["child"]
                expr = extract_stmt_call_num_expr(child, info)                
                #if not info["ignore_inter"]:
                #    if info["valid"]:
                #        ret = lat["bounds"][1] + " * " + expr
                #    else:
                #        ret = expr
                #else:
                #ret = expr
                ret = lat["bounds"][1] + " * " + expr
                info["has_for_child"] = 1
            elif lat["type"] == "mark":      
                if info["under_mark"] and lat["content"] == info["under_mark"]:
                    info["valid"] = True
                if lat["content"] == "simd":
                    if info["valid"]:
                        ret = "1"
                    else:
                        ret = "0"
                else:
                    child = lat["child"]
                    ret = extract_stmt_call_num_expr(child, info)
                if info["under_mark"] and lat["content"] == info["under_mark"]:
                    info["valid"] = False
            elif lat["type"] == "user":
                user_expr = lat["child"]["user_expr"]
                if 'inter_intra' in user_expr or 'intra_inter' in user_expr:                    
                    if user_expr[:-2].split(".")[-1] == "1":
                        double_buffer = 1
                    else:
                        double_buffer = 0                    
                    # Plug in submodule latency
                    if f"{info['name']}_inter" in info["modules"]:
                        inter_expr = info["modules"][f"{info['name']}_inter"]
                    else:
                        inter_expr = None
                    if f"{info['name']}_intra" in info["modules"]:
                        intra_expr = info["modules"][f"{info['name']}_intra"]
                    else:
                        intra_expr = None

                    if inter_expr and intra_expr:
                        if info["in"] == 1 or info["in"] == 0:
                            ret = inter_expr
                        else:
                            if info['target'] in ["on_chip_transfer_io"]:
                                ret = f"({inter_expr})"
                            elif info['target'] in ["on_chip_transfer_pe"]:
                                ret = f"({intra_expr})"
                            else:
                                ret = f"({inter_expr} + {intra_expr})"                            
                        info["has_for_child"] = 1
                    else:                        
                        ret = "1"                        
                    if not info["valid"]:
                        ret = "0"
                #elif "inter_trans" in user_expr:
                #    # Plug in submodule latency
                #    if f"{info['name']}_inter" in info["modules"]:
                #        ret = info["modules"][f"{info['name']}_inter"]
                #    else:
                #        ret = "1"
                #    if not info["valid"]:
                #        ret = "0"
                #elif "intra_trans" in user_expr:
                #    # Plug in submodule latency                    
                #    if f"{info['name']}_intra" in info["modules"]:
                #        ret = info["modules"][f"{info['name']}_intra"]
                #    else:
                #        ret = "1"
                #    if not info["valid"]:
                #        ret = "0"                
                else: 
                    if info["target"] in ["on_chip_transfer_pe", "on_chip_transfer_io", "pe_compute_op", "on_chip_acc"]:
                        ret = "0"
                    else:
                        ret = "1"
            elif lat["type"] == "if":
                # Only examine the first child
                child = lat["child"][0]
                ret = extract_stmt_call_num_expr(child, info)
            elif lat["type"] == "array_tile":           
                if info["target"] in ["on_chip_acc"]:
                    ret = "(" + lat["size"] + "/" + lat["data_pack_factor"] + ")"
                else:
                    ret = "(" + lat["size"] + ")"
            else:
                raise RuntimeError(f"Unsupported latency node type {lat['type']}")

            return ret
        
        # Merge drain modules if necessary
        out_module = {}
        out_drain_module = {}
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.endswith('_out'):
                item = {'buf_size': module_mem['buf_size'], 
                        'num': module_mem['num']}
                if module.find('drain') != -1:
                    item['merged'] = 0
                    out_drain_module[module_mem['array']] = item
                else:                    
                    if module_mem['array'] not in out_module:
                        out_module[module_mem['array']] = [item]
                    else:
                        out_module[module_mem['array']].append(item)
        for array in out_drain_module:
            if array in out_module:
                for m in out_module[array]:                
                    if m['buf_size'] == out_drain_module[array]['buf_size'] and \
                       m['num'] == out_drain_module[array]['num']:
                       out_drain_module[array]['merged'] = 1

        # Extract the off-chip access expression
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:                
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["target"] = "off_chip_acc"
                info["in"] = -1
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_stmt_call_num_expr(module_lat, info)

        f.write("\tactivity = {}\n")
        f.write("\tactivity[\"off_chip_acc_num_meta\"] = {}\n")
        # Off-chip access
        # outermost I/O module latency * data_pack_factor
        f.write("\toff_chip_acc_num = 0\n")
        for module in info["modules"]:
            if desp["attr"][module]["to_dram"] != 1:
                continue
            if "inter" in module or "intra" in module:
                continue
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                      
            f.write(f"\t{module}_off_chip_acc_num = ")
            f.write(info["modules"][module])
            f.write("\n")
            f.write(f"\tactivity[\"off_chip_acc_num_meta\"][\"{module}\"] = {module}_off_chip_acc_num\n")
            f.write(f"\toff_chip_acc_num += {module}_off_chip_acc_num\n")
        
        f.write("\tactivity[\"off_chip_acc_num\"] = off_chip_acc_num\n\n")

        # NOC access        
        # For each I/O group,
        # sum_{io_level}(#io_modules(level)*inter_latency*data_pack_factor_inter) + #pe_modules*intra_latency*data_pack_factor_intra
        # Extract the on-chip data transfer expression
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:                
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["target"] = "on_chip_transfer_io"
                info["in"] = -1
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_stmt_call_num_expr(module_lat, info)

        f.write("\tnoc_hop_num = 0\n")        
        for module in desp["io"]:                 
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                 
            f.write(f"\t{module}_io_noc_hop_num = (1 + {desp['io'][module]['dims'][-1]}) / 2\n")            
            f.write(f"\t{module}_io_noc_hop_num *= {info['modules'][module]}\n")
            if len(desp['io'][module]['dims']) > 1:
                for idx in range(len(desp['io'][module]['dims']) - 1):
                    f.write(f"\t{module}_io_noc_hop_num *= {desp['io'][module]['dims'][idx]}\n")
            f.write(f"\tnoc_hop_num += {module}_io_noc_hop_num\n")
            
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:                
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["target"] = "on_chip_transfer_pe"
                info["in"] = -1
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_stmt_call_num_expr(module_lat, info)
        for module in desp["io"]:
            if module.find('drain') != -1:
                array_name = module[:module.find("_drain_IO")]                
                if out_drain_module[array_name]['merged'] == 1:
                    continue                    
            if desp["attr"][module]["to_pe"]:                
                f.write(f"\t{module}_pe_noc_hop_num = {desp['compute']['PE']['num']}\n")
                f.write(f"\t{module}_pe_noc_hop_num *= {info['modules'][module]}\n")
                f.write(f"\t{module}_pe_noc_hop_num *= {desp['memory'][module]['data_pack_factor_intra']}\n")
                f.write(f"\tnoc_hop_num += {module}_pe_noc_hop_num\n")

        f.write("\tactivity[\"noc_hop_num\"] = noc_hop_num\n\n")
        
        # Computations
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:                
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["target"] = "pe_compute_op"                
                info["in"] = -1
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_stmt_call_num_expr(module_lat, info)
        
        # Compute operation
        # PE latency * simd
        f.write("\tcompute_stmt_call_num = 0\n")
        f.write(f"\tcompute_stmt_call_num = {desp['compute']['PE']['unroll_factor']}\n")        
        f.write(f"\tcompute_stmt_call_num *= {info['modules']['PE']}\n")
        f.write(f"\tcompute_stmt_call_num *= {desp['compute']['PE']['num']}\n")
        f.write("\tactivity[\"compute_stmt_call_num\"] = compute_stmt_call_num\n\n")

        # IO module access        
        # sum(inter latency * data_pack_factor_inter + intra latency * data_pack_factor_inter)
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:                
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["target"] = "on_chip_acc"
                info["in"] = -1
                info["module_attr"] = desp["attr"][module]
                info["modules"][module] = extract_stmt_call_num_expr(module_lat, info)

        f.write("\tio_module_mem_acc_num = 0\n")
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if module.find('drain') != -1 and out_drain_module[module_mem['array']]['merged'] == 1:
                continue
            if "PE" in module:
                continue            
            f.write(f"\t{module}_mem_acc_num = {info['modules'][module]}\n")
            f.write(f"\t{module}_mem_acc_num *= {desp['memory'][module]['data_pack_factor_inter']}\n")
            f.write(f"\tio_module_mem_acc_num += {module}_mem_acc_num\n")
        
        f.write("\tactivity[\"io_module_mem_acc_num\"] = io_module_mem_acc_num\n\n")
        
        # PE module access
        # PE latency * simd * 4 (op1, op2, res(R), res(W))        
        f.write("\tpe_module_reg_acc_num = 0\n")
        f.write("\tpe_module_mem_acc_num = 0\n")
        if "PE" in desp["memory"]:
            f.write("\tpe_module_reg_acc_num = 2\n") # op1, op2
            f.write("\tpe_module_mem_acc_num = 2\n") # res(R), res(W)
        else:
            f.write("\tpe_module_reg_acc_num = 4\n") # op1, op2, res(R), res(W)
            f.write("\tpe_module_mem_acc_num = 0\n") #         
        f.write(f"\tpe_module_reg_acc_num *= {desp['compute']['PE']['unroll_factor']}\n")
        f.write(f"\tpe_module_reg_acc_num *= {info['modules']['PE']}\n")
        f.write(f"\tpe_module_reg_acc_num *= {desp['compute']['PE']['num']}\n")
        f.write(f"\tpe_module_mem_acc_num *= {desp['compute']['PE']['unroll_factor']}\n")
        f.write(f"\tpe_module_mem_acc_num *= {info['modules']['PE']}\n")
        f.write(f"\tpe_module_mem_acc_num *= {desp['compute']['PE']['num']}\n")
        f.write("\tactivity[\"pe_module_reg_acc_num\"] = pe_module_reg_acc_num\n")
        f.write("\tactivity[\"pe_module_mem_acc_num\"] = pe_module_mem_acc_num\n\n")

        f.write("\treturn activity\n")
        f.write("\n")
        
    def print_infer_params_func(self, f, desp):
        f.write("def infer_params(params):\n")
        # Load parameters
        f.write("\t")
        is_first = True
        for p in desp["params"]:
            if "tags" in p and "auto_infer" in p["tags"]:
                continue
            if not is_first:
                f.write(", ")            
            f.write(p["name"])
            is_first = False
        f.write(" = ")
        is_first = True
        for p in desp["params"]:
            if "tags" in p and "auto_infer" in p["tags"]:
                continue
            if not is_first:
                f.write(", ")            
            f.write(f'params[\"{p["name"]}\"]')
            is_first = False
        f.write("\n\n")

        for p in desp["params"]:
            if "tags" in p and "auto_infer" in p["tags"]:
                f.write(f"\t{p['name']}_choices = [n*{p['bounds'][0]} for n in range(1, {p['bounds'][1]}//{p['bounds'][0]}+1) if {p['bounds'][1]}%(n*{p['bounds'][0]})==0]\n")
                f.write(f"\tif len({p['name']}_choices) == 0:\n")
                f.write(f"\t\treturn None\n")
                f.write(f"\tparams[\"{p['name']}\"] = max({p['name']}_choices)\n")
        f.write("\n")                
        f.write("\treturn params\n\n")

    def print_random_sampling_func(self, f, desp):
        f.write("def random_sampling(params):\n")
        f.write(f"\tdef filter_non_power_of_two(x):\n")
        f.write(f"\t\tif np.log2(x) != int(np.log2(x)):\n")
        f.write(f"\t\t\treturn True\n")
        f.write(f"\t\treturn False\n\n")
        # Print the task params
        for p in self.params_config["external"]:
            f.write(f"\t{p} = params[\"{p}\"]\n")
        f.write("\twhile True:\n")
        params_to_process = []
        for param in self.params_config["tunable"]:
            params_to_process.append(self.params_config["tunable"][param])
        #while len(params_to_process) > 0:            
        while True:
            update = False
            for param in params_to_process:
                if "divisors" not in param: 
                    #print("first ", param["name"])                   
                    f.write(f"\t\tsample = random.randint(int({param['bounds'][0]}), int({param['bounds'][1]}))\n")
                    f.write(f"\t\t{param['name']} = sample\n")
                    f.write(f"\t\tparams[\"{param['name']}\"] = sample\n")
                    params_to_process.remove(param)
                    update = True
            if not update:
                break
        while len(params_to_process) > 0:            
            for param in params_to_process:                
                if "divisors" in param and param["divisors"] not in params_to_process:                    
                    #print("second ", param["name"])
                    if "tags" in param and "power_of_two" in param["tags"]:
                        f.write(f"\t\tsample = random.sample(utils.get_divisors(int({param['bounds'][1]}), filter_non_power_of_two), 1)[-1]\n")
                    else:
                        f.write(f"\t\tsample = random.sample(utils.get_divisors(int({param['bounds'][1]}), None), 1)[-1]\n")
                    f.write(f"\t\t{param['name']} = sample\n")
                    f.write(f"\t\tparams[\"{param['name']}\"] = sample\n")
                    params_to_process.remove(param)
        # Latency hiding
        if "PE" not in desp["memory"]:        
            f.write(f"\t\tbreak\n")
        else:
            f.write(f"\t\tlatency_factors = 1\n")
            for p, param in self.params_config["tunable"].items():
                if param["attr"] == "latency_tiling_factor":
                    f.write(f"\t\tlatency_factors *= {param['name']}\n")
                if param["attr"] == "SIMD_tiling_factor":
                    f.write(f"\t\tsimd_factor = {param['name']}\n")
            data_type = desp["memory"]["PE"]["ele_type"]
            if data_type == "float":
                f.write(f"\t\tif latency_factors >= 8 * simd_factor:\n")
                f.write(f"\t\t\tbreak\n")
            else:
                raise RuntimeError(f"Unsupported data type in random sample generation: {data_type}")
        f.write("\n")                
        f.write("\treturn params\n\n")        

    def print_bound_check_func(self, f, desp):
        f.write("def bound_check(params):\n")
        f.write(f"\tdef filter_non_power_of_two(x):\n")
        f.write(f"\t\tif np.log2(x) != int(np.log2(x)):\n")
        f.write(f"\t\t\treturn True\n")
        f.write(f"\t\treturn False\n\n")
        # Load parameters
        f.write("\t")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(p["name"])
            is_first = False
        f.write(" = ")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(f'params[\"{p["name"]}\"]')
            is_first = False
        f.write("\n\n")
        for p in desp["params"]:
            if "bounds" in p:
                f.write(f"\tif {p['name']} < {p['bounds'][0]}:\n")
                f.write(f"\t\treturn False\n")
                # If the parameter is the first-level tiling factors, 
                # ignore the upper bounds.
                if not p['name'].endswith('t1'):
                    f.write(f"\tif {p['name']} > {p['bounds'][1]}:\n")
                    f.write(f"\t\treturn False\n")
            if "tags" in p and "power_of_two" in p["tags"]:
                f.write(f"\tif filter_non_power_of_two({p['name']}):\n")
                f.write(f"\t\treturn False\n")
        # Latency hiding
        if "PE" in desp["memory"]:
            f.write(f"\tlatency_factors = 1\n")
            for p, param in self.params_config["tunable"].items():
                if param["attr"] == "latency_tiling_factor":
                    f.write(f"\tlatency_factors *= {param['name']}\n")
                if param["attr"] == "SIMD_tiling_factor":
                    f.write(f"\tsimd_factor = {param['name']}\n")
            data_type = desp["memory"]["PE"]["ele_type"]
            if data_type == "float":
                f.write(f"\tif latency_factors < 8 * simd_factor:\n")
                f.write(f"\t\treturn False\n")
            else:
                raise RuntimeError(f"Unsupported data type in random sample generation: {data_type}")
        
        f.write("\treturn True\n\n")        

    def print_compute_arch_cst_func(self, f, desp):
        f.write("def compute_arch_cst(params):\n")
        # Load parameters
        f.write("\t")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(p["name"])
            is_first = False
        f.write(" = ")
        is_first = True
        for p in desp["params"]:
            if not is_first:
                f.write(", ")
            f.write(f'params[\"{p["name"]}\"]')
            is_first = False
        f.write("\n\n")

        f.write("\tarch_features = {}\n")
        
        # Compute basic architecture information                
        f.write(f"\tarch_features['dims'] = []\n")
        for dim in desp['compute']['PE']['dims']:
            f.write(f"\tarch_features[\"dims\"].append({dim})\n")
            f.write(f"\tif arch_features[\"dims\"][-1] == 0:\n")        
            f.write(f"\t\treturn None\n")
        f.write(f"\tarch_features[\"SIMD\"] = {desp['compute']['PE']['unroll_factor']}\n")

        # data packing factors
        f.write("\tarch_features[\"data_pack\"] = {}\n")
        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            if 'data_pack_factor' in module_mem:
                f.write(f"\tarch_features[\"data_pack\"][\"{module_mem['array']}\"] = [{module_mem['data_pack_factor']}]\n")

        f.write("\n\treturn arch_features\n\n")

    def register(self, desp, py_f):
        """ Register the design in the descriptor file
        Generate all the necessary functions for evaluating the performance of the 
        target design.         
        """        
        # Tuning parameters            
        self.params_config = {"external": {}, "tunable": {}, "infer": {}}
        for param in desp["params"]:
            if param["tunable"]:
                self.params_config["tunable"][param["name"]] = param
            else:
                if "external" in param["tags"]:
                    self.params_config["external"][param["name"]] = param
                elif "auto_infer" in param["tags"]:
                    self.params_config["infer"][param["name"]] = param
        
        # Print design function            
        with open(py_f, 'w') as f:
            f.write("from math import ceil\n")
            f.write("import numpy as np\n")
            f.write("import random\n")
            f.write("import utils\n\n")

            # Generate resource est func        
            self.print_resource_est_func(f, desp)

            # Generate latency est func
            self.print_latency_est_func(f, desp)            
        
            # Generate activity est func
            self.print_activity_est_func(f, desp)

            # Generate infer parameter func
            self.print_infer_params_func(f, desp)

            # Generate the random sampling func
            self.print_random_sampling_func(f, desp)

            # Generate the bound check func
            self.print_bound_check_func(f, desp)

            # Generate the compute arch cst func
            self.print_compute_arch_cst_func(f, desp)                

        sys.path.append(os.path.dirname(py_f))
        basename = os.path.basename(py_f).split(".")[0]        
        module = __import__(basename)
        self.est_resource_func = module.est_resource
        self.est_latency_func = module.est_latency
        self.est_activity_func = module.est_activity
        self.infer_params_func = module.infer_params
        self.random_sampling_func = module.random_sampling
        self.bound_check_func = module.bound_check
        self.compute_arch_cst_func = module.compute_arch_cst
        self.desp = desp

    def est_latency(self, params):
        if not self.est_latency_func:
            raise RuntimeError(f"Latency estimation function for design {self.name} undefined")
        else:
            return self.est_latency_func(params)
    
    def est_resource(self, params):
        if not self.est_latency_func:
            raise RuntimeError(f"Resource estimation function for design {self.name} undefined")
        else:
            return self.est_resource_func(params)

    def est_activity(self, params):
        if not self.est_activity_func:
            raise RuntimeError(f"Activity estimation function for design {self.name} undefined")
        else:
            return self.est_activity_func(params)

    def infer_params(self, params):
        if not self.infer_params_func:
            raise RuntimeError(f"Internal parameter inference function for design {self.name} undefined")
        else:
            return self.infer_params_func(params)

    def random_sampling(self, params):
        if not self.random_sampling_func:
            raise RuntimeError(f"Random sampling function for design {self.name} undefined")
        else:
            return self.random_sampling_func(params)

    def bound_check(self, params):
        if not self.bound_check_func:
            raise RuntimeError(f"Bound check function for design {self.name} undefined")
        else:
            return self.bound_check_func(params)            

    def compute_arch_cst(self, params):
        if not self.compute_arch_cst_func:
            raise RuntimeError(f"Compute architecture constraints function for design {self.name} undefined")
        else:
            params = self.infer_params(params)
            if params:
                arch_cst = self.compute_arch_cst_func(params)
                res = self.est_resource(params)
                arch_cst['res_usage'] = res
                return arch_cst
            else:
                return None