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
        self.infer_params_func = None
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
        f.write(f"\t\treturn ceil(ele_size*8*pack / 18) * ceil(ele_num/pack/1024)\n\n")

        for module in desp["memory"]:
            module_mem = desp["memory"][module]
            f.write(f"\t{module}_unit_memory = est_BRAM18K({module_mem['ele_size']}, ")
            f.write(f"{module_mem['buf_size']}, ")
            if "data_pack_factor" in module_mem:
                f.write(f"{module_mem['data_pack_factor']})\n")
            else:
                f.write(f"1)\n")        
        #f.write("\tprint(A_IO_L2_in_unit_memory)\n")
        #f.write("\tprint(B_IO_L2_in_unit_memory)\n")        
        #f.write("\tprint(PE_unit_memory)\n")
        #f.write("\tprint(C_drain_IO_L1_out_unit_memory)\n")

        f.write("\tBRAM18K = ")
        is_first = True
        for module in desp["memory"]:
            if not is_first:
                f.write(" + ")
            module_mem = desp["memory"][module]
            f.write(f"{module}_unit_memory")
            if module_mem["double_buffer"]:
                f.write(f" * 2")
            else:
                f.write(f" * 1")
            f.write(f" * {module_mem['num']}")            
            is_first = False            
        f.write("\n\n")

        #for module in desp["memory"]:
        #    module_mem = desp["memory"][module]
        #    f.write(f"\tprint({module_mem['num']})\n")

        f.write("\treturn {\"DSP\": DSP, \"BRAM18K\": BRAM18K}\n")
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
                ret = "(" + lat["size"] + "/" + lat["data_pack_factor"] + ")"
            else:
                raise RuntimeError(f"Unsupported latency node type {lat['type']}")

            return ret

        # Latency prologue
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
                info["modules"][module] = extract_latency_expr(module_lat, info)
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue
            f.write(f"\t{module}_single_latency = ")                        
            f.write(info["modules"][module])
            f.write(f"\n")        
        f.write("\tlatency_prologue = max(")
        is_first = True
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue            
            if not is_first:
                f.write(", ")
            f.write(f"{module}_single_latency")
            is_first = False
        f.write(")\n\n")

        # Latency epilogue
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
                info["modules"][module] = extract_latency_expr(module_lat, info)
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue
            f.write(f"\t{module}_single_latency = ")                        
            f.write(info["modules"][module])
            f.write(f"\n")        
        f.write("\tlatency_epilogue = max(")
        is_first = True
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue            
            if not is_first:
                f.write(", ")
            f.write(f"{module}_single_latency")
            is_first = False
        f.write(")\n\n")

        # Latency main
        info = {"has_for_child": 0, "name": None, "modules": {}}
        for i in range(2):
            # Run second time to fill in the incomplete expression            
            for module in desp["latency"]:
                module_lat = desp["latency"][module]  
                info["name"] = module
                info["valid"] = True
                info["under_mark"] = None
                info["in"] = -1
                info["modules"][module] = extract_latency_expr(module_lat, info)            
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue
            f.write(f"\t{module}_latency = ")                        
            f.write(info["modules"][module])
            f.write(f"\n")        
        f.write("\tlatency_main = max(")
        is_first = True
        for module in info["modules"]:
            if "inter" in module or "intra" in module:
                continue            
            if not is_first:
                f.write(", ")
            f.write(f"{module}_latency")
            is_first = False
        f.write(")\n\n")

        #f.write("\tprint(latency_prologue, latency_main, latency_epilogue)\n\n")

        f.write("\tlatency = latency_prologue + latency_main + latency_epilogue\n\n")
        
        f.write("\treturn latency\n")
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

    def register(self, desp, py_f):
        """ Register the design in the descriptor file
        Generate all the necessary functions for evaluating the performance of the 
        target design.         
        """        
        #print(desp["compute"])        
        with open(py_f, 'w') as f:
            f.write("from math import ceil\n\n")

            # Generate resource est func        
            self.print_resource_est_func(f, desp)

            # Generate latency est func
            self.print_latency_est_func(f, desp)

            # Tuning parameters
            #self.params_config = desp["params"]
            self.params_config = {"external": {}, "tunable": {}, "infer": {}}
            for param in desp["params"]:
                if param["tunable"]:
                    self.params_config["tunable"][param["name"]] = param
                else:
                    if "external" in param["tags"]:
                        self.params_config["external"][param["name"]] = param
                    elif "auto_infer" in param["tags"]:
                        self.params_config["infer"][param["name"]] = param
        
            # Generate infer parameter func
            self.print_infer_params_func(f, desp)

        sys.path.append(os.path.dirname(py_f))
        basename = os.path.basename(py_f).split(".")[0]        
        module = __import__(basename)
        self.est_resource_func = module.est_resource
        self.est_latency_func = module.est_latency
        self.infer_params_func = module.infer_params
        self.desp = desp

    def est_latency(self, params):
        if not self.est_latency_func:
            raise RuntimeError(f"Latency function for design {self.name} undefined")
        else:
            return self.est_latency_func(params)
    
    def est_resource(self, params):
        if not self.est_latency_func:
            raise RuntimeError(f"Resource function for design {self.name} undefined")
        else:
            return self.est_resource_func(params)

    def infer_params(self, params):
        if not self.infer_params_func:
            raise RuntimeError(f"Internal parameter inference function for design {self.name} undefined")
        else:
            return self.infer_params_func(params)