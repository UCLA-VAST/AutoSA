import json
import random
import numpy as np
import bisect

import utils
from design import Design

class SingleTask(object):
    """ Single workload searching task.
    """
    def __init__(self, design, workload, hw_cst):
        self.design = design
        self.workload = workload

        self.hw_cst = hw_cst
        self.fre = 300 # 300 MHz
        self.dw = 4 # bytes
        self.dt = "float"
        self.fuse = 0
        self.last_fuse = 0 # the last fusion task in the network
        self.use_uram = 0
        self.serialize = 0
        # Fixed architecture solution
        self.arch_sol = None
        self.arch_cst = None
        self.arch_feature = None
        self.fixed = 0        
        # Other configs
        self.configs = {}
        self.aux_funcs = {}

    def __repr__(self):
        #ret = f't_{self.workload["name"]}_'
        ret = ""
        for param in self.workload["params"]:
            ret += param            
            ret += "_"
            ret += f'{self.workload["params"][param]}'
        ret += f'_d_{self.design.name}'
        ret += f'_cst_{self.hw_cst}'
        ret += f'_f_{self.fuse}{self.last_fuse}'
        ret += f'_u_{self.use_uram}'
        ret += f'_s_{self.serialize}'
        if self.fixed == 1:
            ret += f'_fixed_'
            for k, v in self.arch_sol.items():
                ret += f'{k}{v}'
        if len(self.configs) > 0:
            ret += f'_config_'
            for k, v in self.configs.items():
                if k == "fix_param":
                    ret += "fix_param_"
                    for p_pair in v:
                        ret += p_pair[0]
                        ret += "_"
                        ret += str(p_pair[1])
                elif k == "equate_params":
                    ret += "equate_params_"
                    for p_pair in v:
                        ret += p_pair[0]
                        ret += "_"
                        ret += p_pair[1]
                elif k == "prev_workload":
                    ret += "prev_workload_"
                    ret += self.configs['prev_workload']['name']
                elif k == "prev_sol":
                    ret += "prev_sol_"
                    for p in self.configs['prev_sol']:
                        ret += p
                        ret += "_"
                        ret += str(self.configs['prev_sol'][p])
                elif k == "prev_latency":
                    ret += "prev_latency_"
                    ret += str(self.configs['prev_latency'])
                else:
                    ret += f'{k}{v}'

        return ret

    def adjust_params(self, params):
        """ Adjust the parameters based on its contraints.
        """
        def filter_non_power_of_two(x):
            if np.log2(x) != int(np.log2(x)):
                return True
            return False
        
        # Making all factors to be even numbers to have more divisors
        #for p, param in self.design.params_config["tunable"].items():
        #    params[p] = int(np.ceil(params[p] / 2) * 2)        
        for p in params:
            params[p] = int(params[p])

        # Making all divisor factors to be divisors of the dependent variable
        for p, param in self.design.params_config["tunable"].items():
            #print(param)
            if "divisors" in param:
                if "tags" in param and "power_of_two" in param["tags"]:
                    choices = utils.get_divisors(int(params[param["divisors"][0]]), filter_non_power_of_two)
                else:
                    choices = utils.get_divisors(int(params[param["divisors"][0]]), None)
                idx = bisect.bisect(choices, params[p])
                if idx >= len(choices):
                    idx -= 1
                if idx > 1:
                    if abs(choices[idx - 1] - params[p]) < abs(choices[idx] - params[p]):
                        idx -= 1
                #print(params[param["divisors"][0]])
                #print("idx", idx)
                #print("len", len(choices))
                params[p] = choices[idx]

        # Adjust the fixed parameters        
        if 'fix_param' in self.configs:
            for fix_p in self.configs['fix_param']:
                for p, param in self.design.params_config["tunable"].items():                
                    if p.startswith(fix_p[0]):
                        params[p] = int(fix_p[1])
        if 'equate_params' in self.configs:
            for p_pair in self.configs['equate_params']:
                params[p_pair[1]] = params[p_pair[0]]

        return params

    def generate_random_sample(self):
        """ Generate a random sample in the design space.
        """
        workload_params = {}
        for param in self.workload["params"]:
            workload_params[param] = self.workload["params"][param]
        return self.design.random_sampling(workload_params)        

    def check_arch_legality(self, arch_features):
        """ Check if the current architecture is legal.
        """
        if self.fixed == 0:
            return True
        # dims
        for idx in range(len(arch_features['dims'])):
            if arch_features['dims'][idx] > self.arch_cst['dims'][idx]:
                return False
        # SIMD
        if arch_features['SIMD'] > self.arch_cst['SIMD']:
            return False
        # data pack
        for arr in arch_features['data_pack']:
            for idx in range(len(arch_features['data_pack'][arr])):
                if arch_features['data_pack'][arr][idx] > self.arch_cst['data_pack'][arr][idx]:
                    return False
        # resource usage
        for module in arch_features['resource']:
            if module.endswith("unit_memory"):
                if arch_features["resource"][module] > self.arch_cst['resource'][module]:
                    return False

        return True

    def adjust_latency_buffer(self, latency, latency_meta, params):
        """ Adjust latency and for customized search tasks.
        cin_read_mode:
        0: normal ping-pong mode, no need to adjust
        1: load cin one time from the external memory
        2: load cin from on-chip BRAM buffer
        3: load cin from on-chip URAM buffer
        cout_write_mode:
        0: write to external memory
        1: write to on-chip buffer
        w_read_mode:
        0: normal ping-pong mode, no need to adjust
        1: load w from on-chip URAM buffer
        Note: Only works for kernel4
        """
        if ('cin_read_mode' not in self.configs) or ('cout_write_mode' not in self.configs):
            return latency, latency_meta

        """
        Latency prologue
        """        
        w_latency_list = []
        for item, value in latency_meta["latency_prologue"].items():
            if item.startswith('w'):
                w_latency_list.append({"item": item, "value": value})
        cin_latency_list = []
        for item, value in latency_meta['latency_prologue'].items():
            if item.startswith('cin'):
                cin_latency_list.append({"item": item, "value": value})
        # Sort the latency list by item names
        def take_item(elem):
            return elem['item']
        w_latency_list.sort(key=take_item)
        cin_latency_list.sort(key=take_item)

        w_latency = 0
        if 'w_read_mode' not in self.configs or (self.configs['w_read_mode'] == 0):
            for w in w_latency_list:
                w_latency = max(w_latency, w['value'])        
        elif self.configs['w_read_mode'] == 1:
            w_latency_list = w_latency_list[:-1]
            for w in w_latency_list:
                w_latency = max(w_latency, w['value'])

        cin_latency = 0
        if self.configs['cin_read_mode'] == 0:            
            for cin in cin_latency_list:
                cin_latency = max(cin_latency, cin['value'])
        if self.configs['cin_read_mode'] == 1:
            # Modify the cin latency            
            for cin in cin_latency_list:
                cin_latency = max(cin_latency, cin['value'])                            
            cin_latency = self.call_aux_func('update_cin_latency')(cin_latency, self, params)            
        elif self.configs['cin_read_mode'] == 2:                        
            pass
        elif self.configs['cin_read_mode'] == 3:
            # Peel off the last one accessing the DRAM
            cin_latency_list = cin_latency_list[:-1]            
            for cin in cin_latency_list:
                cin_latency = max(cin_latency, cin['value'])        
        latency_prologue = max(w_latency, cin_latency)

        """
        Latency main
        """
        cout_latency_list = []
        for item, value in latency_meta['latency_main'].items():
            if item.startswith('cout'):
                cout_latency_list.append({"item": item, "value": value})        
        w_latency_list = []
        for item, value in latency_meta['latency_main'].items():
            if item.startswith('w'):
                w_latency_list.append({"item": item, "value": value})                        
        cin_latency_list = []
        for item, value in latency_meta['latency_main'].items():
            if item.startswith('cin'):
                cin_latency_list.append({"item": item, "value": value})        
        cout_latency_list.sort(key=take_item)  
        w_latency_list.sort(key=take_item)  
        cin_latency_list.sort(key=take_item)  

        #latency_main = max(latency_meta['latency_main']['PE_latency'], w_latency)
        latency_main = latency_meta['latency_main']['PE_latency']
        w_latency = 0
        if 'w_read_mode' not in self.configs or (self.configs['w_read_mode'] == 0):
            for w in w_latency_list:
                w_latency = max(w_latency, w['value'])            
        else:
            w_latency_list = w_latency_list[:-1]
            for w in w_latency_list:
                w_latency = max(w_latency, w['value'])

        cin_latency = 0
        if self.configs['cin_read_mode'] == 0:            
            for cin in cin_latency_list:
                cin_latency = max(cin_latency, cin['value'])            
        elif self.configs['cin_read_mode'] == 1:
            pass
        elif self.configs['cin_read_mode'] == 2:
            pass
        elif self.configs['cin_read_mode'] == 3:
            # Peel off the last one accessing the DRAM
            cin_latency_list = cin_latency_list[:-1]            
            for cin in cin_latency_list:
                cin_latency = max(cin_latency, cin['value'])
        
        cout_latency = 0        
        if self.configs['cout_write_mode'] == 0:            
            for cout in cout_latency_list:
                cout_latency = max(cout_latency, cout['value'])            
        elif self.configs['cout_write_mode'] == 1:
            # Peel off the last one accessing the DRAM
            cout_latency_list = cout_latency_list[:-1]            
            for cout in cout_latency_list:
                cout_latency = max(cout_latency, cout['value'])
        latency_main = max(latency_main, cin_latency, w_latency, cout_latency)
        
        """
        Latency epilogue
        """
        cout_latency_list = []
        for item, value in latency_meta['latency_epilogue'].items():
            if item.startswith('cout'):
                cout_latency_list.append({"item": item, "value": value})        
        cout_latency_list.sort(key=take_item)

        cout_latency = 0
        if self.configs['cout_write_mode'] == 0:            
            for cout in cout_latency_list:
                cout_latency = max(cout_latency, cout['value'])           
        elif self.configs['cout_write_mode'] == 1:
            # Peel off the last one accessing the DRAM
            cout_latency_list = cout_latency_list[:-1]        
            for cout in cout_latency_list:
                cout_latency = max(cout_latency, cout['value'])
        latency_epilogue = cout_latency

        #print(latency_prologue, latency_main, latency_epilogue)
        if self.fuse == 1 and self.last_fuse == 1:            
            n_iter = np.ceil(self.workload['params']['r'] / params['r_t1']) * \
                     np.ceil(self.workload['params']['c'] / params['c_t1'])
            latency = n_iter * (latency_prologue + latency_main / n_iter + latency_epilogue) * n_iter
        else:
            latency = latency_prologue + latency_main + latency_epilogue
        
        latency_meta = {
            "latency_prologue": latency_prologue,
            "latency_main": latency_main,
            "latency_epilogue": latency_epilogue
        }

        return latency, latency_meta

    def adjust_latency_multi_acc(self, latency, latency_meta, params):
        """ Adjust latency for multi-acc setting
        """
        # Update the setup latency
        if ('prev_workload' not in self.configs) or ('prev_sol' not in self.configs) or \
           ('prev_latency' not in self.configs):
            return latency
        
        prev_workload = self.configs['prev_workload']
        prev_sol = self.configs['prev_sol']
        prev_latency = self.configs['prev_latency']
        o1 = prev_workload["params"]['o']
        tr1 = min(prev_sol['r_t1'], prev_workload["params"]['r'])
        tc1 = min(prev_sol['c_t1'], prev_workload["params"]['c'])
        tr1_post = tr1
        tc1_post = tc1
        for tag in prev_workload["tags"]:
            if tag.startswith("maxpool"):
                stride = int(tag.split('_')[-1])
                tr1_post /= stride
                tc1_post /= stride
        tr1_post = max(int(tr1_post), 1)
        tc1_post = max(int(tc1_post), 1)

        tr2 = min(params["r_t1"], self.workload["params"]["r"])
        tc2 = min(params["c_t1"], self.workload["params"]["c"])
        k = self.workload["params"]["p"]
        data_pack = params["i_t2"]

        c0 = np.ceil((tr2 + k - 1) / tr1_post)
        c1 = np.ceil((tc2 + k - 1) / tc1_post)
        trp = min(c0 * tr1, prev_workload["params"]["r"])
        tcp = min(c1 * tc1, prev_workload["params"]["c"])
        #if (prev_sol["r_t1"] == params["r_t1"]) and \
        #   (prev_sol["c_t1"] == params["c_t1"]):
        #    tri = np.ceil(params["i_t1"] / prev_sol["o_t1"]) * prev_sol["o_t1"]
        #    setup = prev_latency / (np.ceil(prev_workload["params"]['o'] / tri))
        #else:
        setup = prev_latency / (np.ceil(prev_workload["params"]["r"] / trp) * np.ceil(prev_workload["params"]["c"] / tcp))
        
        latency_meta = {
            "latency_orig": latency
        }

        return latency + setup, latency_meta

    def adjust_latency(self, latency, latency_meta, params):
        """ Adjust latency and for customized search tasks.            
        """
        adjust_buffer = False
        adjust_multi_acc = False
        for key in ['cin_read_mode', 'cout_write_mode', 'w_read_mode']:
            for config_key in self.configs:
                if key == config_key:
                    adjust_buffer = True
                    break
        if adjust_buffer:
            latency, latency_meta = self.adjust_latency_buffer(latency, latency_meta, params)
            
        for key in ['prev_workload', 'prev_sol', 'prev_latency']:
            for config_key in self.configs:
                if key == config_key:
                    adjust_multi_acc = True
                    break
        if adjust_multi_acc:
            latency, latency_meta = self.adjust_latency_multi_acc(latency, latency_meta, params)
                    
        return latency, latency_meta
    
    def adjust_resource(self, resource, resource_meta, params):
        """ Update the cin buffer for fused design.
        """
        if 'update_cin_buf' in self.aux_funcs:
            def est_BRAM18K(ele_size, ele_num, pack):
                #return np.ceil(ele_size * 8 * pack / 18) * np.ceil(ele_num / pack / 1024)
                return np.ceil(ele_size * 8 * pack / 36) * np.ceil(ele_num / pack / 512)

            if self.use_uram == 0:
                # Update cin_buf
                for item in resource_meta:
                    if item.startswith("cin"):
                        cin_buf_size = est_BRAM18K(resource_meta[item]['ele_size'], resource_meta[item]['buf_size'], resource_meta[item]['data_pack_factor'])
                        cin_buf_num = resource_meta[item]['num']
                        break
                resource["BRAM18K"] -= (cin_buf_size * cin_buf_num)
                cin_buf_size = max(self.call_aux_func('update_cin_buf')(self, params, resource_meta[item]['ele_size'] * 8 * resource_meta[item]['data_pack_factor'], resource_meta[item]['buf_size'] / resource_meta[item]['data_pack_factor']), cin_buf_size)
                resource["BRAM18K"] += (cin_buf_size * cin_buf_num)
            else:
                # Compute cin_buf
                uram = resource["URAM"]
                for item in resource_meta:
                    if item.startswith("cin"):
                        data_pack = resource_meta[item]['data_pack_factor']                        
                        break
                uram = max(self.call_aux_func('update_cin_buf')(self, params, data_pack) * 2, uram)
                resource["URAM"] = uram

        return resource

    def compute_arch_cst(self, params):
        arch_cst = self.design.compute_arch_cst(params)
        params = self.design.infer_params(params)
        if params:
            if not self.design.bound_check(params):
                arch_cst = None
            else:
                resource, resource_meta = self.design.est_resource(params)
                if len(self.configs) > 0:
                    resource = self.adjust_resource(resource, resource_meta, params)
                arch_cst['resource'] = resource
        else:
            arch_cst = None

        return arch_cst

    def evaluate(self, params, metric="latency"):
        if metric not in ["latency", "off_chip_comm", "energy", "dsp_num"]:
            raise RuntimeError(f"Not supported metric: {metric}")

        params = self.design.infer_params(params)
        if params:
            if not self.design.bound_check(params):                
                return 0, None, None                
            latency, latency_meta = self.design.est_latency(params)
            if len(self.configs) > 0:
                latency, latency_meta = self.adjust_latency(latency, latency_meta, params)            
            if self.fixed == 1:
                # Check the architecture constraints
                arch_cst_cur = self.compute_arch_cst(params)
                if not self.check_arch_legality(arch_cst_cur):
                    return 0, None, None                        
                resource = self.arch_cst['resource']
            else:
                resource, resource_meta = self.design.est_resource(params)
                if len(self.configs) > 0:
                    resource = self.adjust_resource(resource, resource_meta, params)                

            # Compute the other activity
            activity = self.design.est_activity(params)

            if metric == "latency":                
                if latency:
                    return 1 / latency, resource, {'latency': latency_meta, 'activity': activity}
                else:
                    return 0, None, None
            elif metric == "off_chip_comm":
                if activity:
                    latency_meta['latency'] = latency
                    return 1 / activity["off_chip_acc_num"], resource, {'latency': latency_meta, 'activity': activity}
                else:
                    return 0, None, None
            elif metric == "energy":
                if activity:
                    latency_meta['latency'] = latency
                    energy = self.compute_energy(activity)
                    return 1 / energy, resource, {'latency': latency_meta, 'activity': activity}
                else:
                    return 0, None, None
            elif metric == "dsp_num":
                if activity:
                    latency_meta['latency'] = latency
                    return resource["DSP"], resource, {'latency': latency_meta, 'activity': activity}
                else:
                    return 0, None, None
        else:
            return 0, None, None        

    def compute_energy(self, activity):
        """ Estimate the energy consumption of the design.
        """           
        '''
        def est_static_power(x, fre=300):
            """
            returns in Watts
            """
            x = x * 100
            return (6.72 - 0.307 * x + 7.24 * 1e-3 * x * x) * (fre / 300)

        # Default values (W at 300MHz)
        res_unit_power = {
            "BRAM18K": 0.0005033482143,
		    "DSP": 0.0008828125
        }
        # Compute the unit transaction energy
        res_unit_energy = {
		    "BRAM18K": res_unit_power["BRAM18K"] / (300 * 1e6) / 2 * 1e12,
		    "DSP": res_unit_power["DSP"] / (300 * 1e6) * 1e12 * 5 # FP32
	    }

        # DRAM default value
        dram_unit_energy = 427.9 # (pJ) 16-bit 2GB DDR3 at 100MHz (from Wang HPCA)
        # Scale the value 
        dram_unit_energy *= self.dw / 2
        hop_unit_energy = 0

        on_chip_energy = res_unit_energy["DSP"] * activity["compute_stmt_call_num"]
        on_chip_energy += res_unit_energy["BRAM18K"] * activity["io_module_mem_acc_num"] + \
                          res_unit_energy["BRAM18K"] * (activity["pe_module_mem_acc_num"] + activity["pe_module_reg_acc_num"])
        on_chip_energy += hop_unit_energy * activity["noc_hop_num"]
        off_chip_energy = dram_unit_energy * activity["off_chip_acc_num"]                

        return (on_chip_energy + off_chip_energy) / 1e9        
        '''
                
        # Eyeriss model (normalized)
        res_unit_energy = {        
            "RF": 1,
            "ALU": 1,
            "GlobalBuf": 6
        }
        dram_unit_energy = 200
        hop_unit_energy = 2

        '''
        # Interstellar model (pJ)
        res_unit_energy = {        
            "RF": 0.03, 
            "ALU": 0.075,
            "GlobalBuf": 6
        }
        dram_unit_energy = 200
        hop_unit_energy = 0.035              
        '''

        on_chip_energy = res_unit_energy["ALU"] * activity["compute_stmt_call_num"]
        on_chip_energy += res_unit_energy["GlobalBuf"] * activity["io_module_mem_acc_num"] + \
                          res_unit_energy["GlobalBuf"] * activity["pe_module_mem_acc_num"] + \
                          res_unit_energy["RF"] * activity["pe_module_reg_acc_num"]        
        on_chip_energy += hop_unit_energy * activity["noc_hop_num"]
        off_chip_energy = dram_unit_energy * activity["off_chip_acc_num"]

        return (on_chip_energy + off_chip_energy) / 1e9        

    def compute_dsp_eff(self, latency, dsp):
        """ Compute the DSP efficiency of the current design.
        Note: Only works for FP32 on Xilinx FPGA
        """
        return (self.compute_ops() / (dsp / 5 * 2)) / latency

    def compute_ops(self):
        """ Compute the total amount of operations of the workload.
        """        
        if "gemm" in self.workload["tags"]:
            return self.workload["params"]["i"] * self.workload["params"]["j"] * self.workload["params"]["k"] * 2
        elif "conv" in self.workload["tags"]:
            return self.workload["params"]["i"] * self.workload["params"]["o"] * self.workload["params"]["r"] * self.workload["params"]["c"] * self.workload["params"]["p"] * self.workload["params"]["q"] * 2
        else:
            raise RuntimeError(f"Not supported workload: {self.workload['name']}")

    def compute_bw(self, params):
        """ Compute the bandwidth requirement of the task.
        Note: Only works for 32-bit data
        """
        latency, _ = self.design.est_latency(params)
        off_chip_trans = self.est_off_chip_trans(params)
        bw = off_chip_trans * self.dw / (latency / (self.fre * 1e6)) / 1e9 # GB/s
        
        return bw

    def est_off_chip_trans(self, params):        
        activity = self.design.est_activity(params)
        off_chip_acc_num_meta = activity['off_chip_acc_num_meta']
        if "conv" in self.workload["tags"]:
            cin_trans = 0
            w_trans = 0
            cout_trans = 0
            for module in off_chip_acc_num_meta:
                if module.startswith("cin"):
                    cin_trans = off_chip_acc_num_meta[module]
                if module.startswith("w"):
                    w_trans = off_chip_acc_num_meta[module]
                if module.startswith("cout"):
                    cout_trans = off_chip_acc_num_meta[module]
            if "cin_read_mode" in self.configs:
                if self.configs["cin_read_mode"] == 2 or self.configs["cin_read_mode"] == 3:
                    cin_trans = 0
            if "cout_write_mode" in self.configs:
                if self.configs["cout_write_mode"] == 1:
                    cout_trans = 0
            if "w_read_mode" in self.configs:
                if self.configs["w_reads_mode"] == 1:
                    w_trans = 0
            return cin_trans + w_trans + cout_trans
        else:
            return activity["off_chip_acc_num"]        
        
        '''
        if "gemm" in self.workload["tags"]:            
            i, j, k = self.workload["params"]['i'], self.workload["params"]['j'], self.workload["params"]['k']
            i_t1, j_t1, k_t1 = params['i_t1'], params['j_t1'], params['k_t1']
            trans = np.ceil(i / i_t1) * np.ceil(j / j_t1) * np.ceil(k / k_t1) * (i_t1 * k_t1 + j_t1 * k_t1) + \
                    np.ceil(i / i_t1) * np.ceil(j / j_t1) * (i_t1 * j_t1)
        elif "conv" in self.workload["tags"]:
            i, o, r, c, p, q = self.workload["params"]["i"], self.workload["params"]["i"], \
                               self.workload["params"]["r"], self.workload["params"]["c"], \
                               self.workload["params"]["p"], self.workload["params"]["q"]
            i_t1, o_t1, r_t1, c_t1 = params["i_t1"], params["o_t1"], \
                                     params["r_t1"], params["c_t1"]
            cin_trans = i_t1 * (r_t1 + p - 1) * (c_t1 + q - 1)
            w_trans = i_t1 * o_t1 * p * q
            cout_trans = o_t1 * r_t1 * c_t1
            if "cin_read_mode" in self.configs:
                if self.configs["cin_read_mode"] == 2 or self.configs["cin_read_mode"] == 3:
                    cin_trans = 0
            if "cout_write_mode" in self.configs:
                if self.configs["cout_write_mode"] == 1:
                    cout_trans = 0
            if "w_read_mode" in self.configs:
                if self.configs["w_reads_mode"] == 1:
                    w_trans = 0
            trans = np.ceil(i / i_t1) * np.ceil(o / o_t1) * np.ceil(r / r_t1) * np.ceil(c / c_t1) * \
                    (cin_trans + w_trans) + \
                    np.ceil(o / o_t1) * np.ceil(r / r_t1) * np.ceil(c / c_t1) * cout_trans
        else:
            raise RuntimeError(f"Not supported task: {self.task['name']}")

        return trans
        '''        

    def compute_ctc(self, params):
        """ Compute the compute-to-communication ratio of the task.
        """
        ops = self.compute_ops()
        off_chip_trans = self.est_off_chip_trans(params)
        comm = off_chip_trans * self.dw
        ctc = ops / comm

        return ctc

    def set_arch_cst(self, arch_cst):
        self.fixed = 1
        self.arch_cst = arch_cst.copy()
    
    def clear_arch_cst(self):
        self.fixed = 0
        self.arch_cst = None

    def set_arch_sol(self, sol):
        self.arch_sol = sol

    def set_aux_func(self, tag, func_name):
        """ Set the auxiliary functions.
        tag refers to the function tag.
        func_name points to pre-defined functions.
        """
        self.aux_funcs[tag] = func_name

    def call_aux_func(self, tag):
        # Preset functions
        # Update the cin load latency
        def update_cin_latency_last(lat, task, sol):
            lat *= np.ceil(task.workload["params"]['i'] / sol['i_t1'])
            return lat
        # Update the cin on-chip buffer
        def update_cin_buf_bram_last(task, sol, width, depth):
            depth *= np.ceil(task.workload["params"]['i'] / sol['i_t1'])
            #mem = np.ceil(width / 18) * np.ceil(depth / 1024)
            mem = np.ceil(width / 36) * np.ceil(depth / 512)
            return mem
        # Update the cin on-chip buffer
        def update_cin_buf_uram_last(task, sol, data_pack):
            depth = task.workload["params"]['i'] * sol['r_t1'] * sol['c_t1']
            mem = np.ceil(task.dw * 8 * data_pack / 72) * np.ceil(depth / data_pack / 4096)
            return mem
        # Update the cin load latency
        def update_cin_latency(lat, task, sol):
            lat *= (np.ceil(task.workload["params"]['i'] / sol['i_t1']) * \
                    np.ceil(task.workload["params"]['r'] / sol['r_t1']) * \
                    np.ceil(task.workload["params"]['c'] / sol['c_t1']))
            return lat
        # Update the cin on-chip buffer
        def update_cin_buf_bram(task, sol, width, depth):
            depth *= (np.ceil(task.workload["params"]['i'] / sol['i_t1']) * \
                      np.ceil(task.workload["params"]['r'] / sol['r_t1']) * \
                      np.ceil(task.workload["params"]['c'] / sol['c_t1']))            
            #mem = np.ceil(width / 18) * np.ceil(depth / 1024)
            mem = np.ceil(width / 36) * np.ceil(depth / 512)
            return mem
        # Update the cin on-chip buffer    
        def update_cin_buf_uram(task, sol, data_pack):
            depth = task.workload["params"]['i'] * task.workload["params"]['r'] * task.workload["params"]['c']
            mem = np.ceil(task.dw * 8 * data_pack / 72) * np.ceil(depth / data_pack / 4096)
            return mem

        if self.aux_funcs[tag] == 'update_cin_latency_last':
            return update_cin_latency_last
        elif self.aux_funcs[tag] == 'update_cin_buf_bram_last':
            return update_cin_buf_bram_last
        elif self.aux_funcs[tag] == 'update_cin_buf_uram_last':
            return update_cin_buf_uram_last
        elif self.aux_funcs[tag] == 'update_cin_latency':
            return update_cin_latency
        elif self.aux_funcs[tag] == 'update_cin_buf_bram':
            return update_cin_buf_bram
        elif self.aux_funcs[tag] == 'update_cin_buf_uram':
            return update_cin_buf_uram
        else:
            raise RuntimeError(f'Not supported function: {tag}')

    def clear_aux_func(self):
        self.aux_funcs = {}

class MultiTask(object):
    """ Search task object used by the tuner.
    # TODO: To be modified
    """
    def __init__(self, design, search_tasks, hw_cst, fuse=0, max_latency=-1, split=0, use_uram=0):
        self.design = design
        self.tasks = search_tasks

        self.hw_cst = hw_cst
        self.fre = 200 # 200 MHz
        self.dw = 4 # bytes
        self.dt = "float"
        self.fuse = fuse
        self.max_latency = max_latency
        self.split = split
        self.use_uram = use_uram
        for task in self.tasks:
            task.use_uram = use_uram
        # Fixed architecture solution
        self.fixed = 0
        self.arch_sol = None
        self.arch_cst = None
        # Other configs
        self.configs = {}
        if isinstance(self.design, Design):
            # Initialize the external params, using the largest dimensions        
            self.workload = {"params": {}}
            for p, param in self.design.params_config["external"].items():
                self.workload["params"][param["name"]] = 1
            for task in self.tasks:
                for p, param in self.design.params_config["external"].items():
                    self.workload["params"][param["name"]] = max(self.workload["params"][param["name"]], task.workload["params"][param["name"]])

    def __repr__(self):
        ret = ""
        for task in self.tasks:
            ret += str(task)        
        if isinstance(self.design, Design):
            ret += f'_d_{self.design.name}'
        else:
            for design in self.design:
                ret += f'_d_{design.name}'
        ret += f'_cst_{self.hw_cst}'
        ret += f'_f_{self.fuse}'
        ret += f'_s_{self.split}'
        ret += f'_u_{self.use_uram}'
        if len(self.configs) > 0:
            ret += f'_config_'
            for k, v in self.configs:
                ret += f'{k}{v}'

        return ret    

    def generate_random_sample(self):
        """ Generate a random sample in the design space.
        """
        workload_params = {}
        for param in self.workload["params"]:
            workload_params[param] = self.workload["params"][param]
        return self.design.random_sampling(workload_params)    

    def compute_dsp_eff(self, latency, dsp):
        """ Compute the DSP efficiency of the current design.
        Note: Only works for FP32 on Xilinx FPGA
        """
        return (self.compute_ops() / (dsp / 5 * 2)) / latency

    def compute_ops(self):
        """ Compute the total amount of operations of the task.
        """
        total_ops = 0
        for task in self.tasks:
            if "gemm" in task.workload["tags"]:
                total_ops += task.workload["params"]["i"] * task.workload["params"]["j"] * task.workload["params"]["k"] * 2
            elif "conv" in task.workload["tags"]:
                total_ops += task.workload["params"]["i"] * task.workload["params"]["o"] * task.workload["params"]["r"] * task.workload["params"]["c"] * task.workload["params"]["p"] * task.workload["params"]["q"] * 2            
            else:
                raise RuntimeError(f"Not supported workload: {task.workload['tags']}")
        return total_ops

    def compute_arch_cst(self, params):
        """ Compute the architecture constraints.
        """
        arch_cst = None
        for task in self.tasks:
            cur_arch_cst = task.compute_arch_cst(params)
            # Take the one with looser contraints
            if not arch_cst:
                arch_cst = cur_arch_cst
            else:
                # dims
                for idx in range(len(arch_cst['dims'])):
                    arch_cst['dims'][idx] = max(arch_cst['dims'][idx], cur_arch_cst['dims'][idx])
                # SIMD
                arch_cst["SIMD"] = max(arch_cst["SIMD"], cur_arch_cst["SIMD"])
                # data pack
                for arr in arch_cst['data_pack']:
                    for idx in range(len(arch_cst['data_pack'][arr])):
                        arch_cst['data_pack'][arr][idx] = max(arch_cst['data_pack'][arr][idx], cur_arch_cst['data_pack'][arr][idx])
                # resource
                for module in arch_cst['resource']:
                    if module.endswith("unit_memory"):
                        arch_cst["resource"][module] = max(arch_cst["resource"][module], cur_arch_cst["resource"][module])
        
        return arch_cst

    def set_arch_cst(self, arch_cst):
        """ Set the architecture constraints.
        """
        self.fixed = 1
        self.arch_cst = arch_cst.copy()
        # Set the subtasks
        for task in self.tasks:
            task.set_arch_cst(arch_cst.copy())

    def clear_arch_cst(self):
        self.fixed = 0
        self.arch_cst = None
        for task in self.tasks:
            task.clear_arch_cst()

    def set_arch_sol(self, sol):
        self.fixed = 1
        self.arch_sol = sol
        for task in self.tasks:
            task.set_arch_sol(sol)