import copy
import pprint
import numpy as np
import random

import utils
import tuners
from search_task import SingleTask, MultiTask

class ArchExplorer(object):
    """ Architecture explorer.
    """
    def __init__(self, cst, search_obj, max_epochs, max_time, search_config, designs, workloads):
        self.cst = cst
        self.search_obj = search_obj
        self.max_epochs = max_epochs
        self.max_time = max_time
        self.search_config = search_config
        self.designs = designs
        self.workloads = workloads

    def search(self):
        """ The gateway function to perform architecture search.
        The input is a list of design descriptions "designs"
        and a list of searching tasks "tasks".
        """
        best_record = utils.SearchRecord().reset()

        if self.search_config["explore_fusion"]:
            if self.search_config["explore_multi_acc"]:
                if self.search_config["method"] == "customized1":
                    best_record = self.search_fusion_multi_acc_customized1()
                elif self.search_config["method"] == "customized2":
                    best_record = self.search_fusion_multi_acc_customized2()
                    #best_record = self.search_fusion_multi_acc_customized2(design_idx=4)
            else:
                if self.search_config["method"] == "exhaustive":
                    best_record = self.search_fusion_single_acc_exhaustive() # TODO
                elif self.search_config["method"] == "customized1":
                    #best_record = self.search_fusion_single_acc_customized1(design_idx=4)
                    best_record = self.search_fusion_single_acc_customized1()
                elif self.search_config["method"] == "customized2":
                    best_record = self.search_fusion_single_acc_customized2()
                else:
                    raise NotImplementedError("Undefined multi-accelerator search method.")
        else:
            if self.search_config["explore_programmable"]:
                if self.search_config["method"] == "customized1":
                    best_record = self.search_programmable_single_acc_customized1() # TODO
                else:
                    raise NotImplementedError("Undefined single programmable accelerator search method.")
            else:
                if self.search_config["method"] == "customized1":
                    best_record = self.search_non_fusion_single_acc_customized1(design_idx=self.search_config["design_idx"])
                    #best_record = self.search_non_fusion_single_acc_customized1(design_idx=4)
                else:
                    raise NotImplementedError("Undefined single accelerator search method.")

        return best_record

    def tune(self, search_task, init_tasks=None, silent=0, use_cache=-1, meta=None):
        """ Call tuners for the searching task.
        init_tasks contains candidates for the initial population of the genetic search.
        meta contains additional information used during the tuning.
        """
        if use_cache == -1:
            use_cache = self.search_config['use_db']
        if use_cache:
            # Check if the search task has been searched
            if str(search_task) in self.search_config["search_records_db"]:
                return self.search_config["search_records_db"][str(search_task)]
                #return self.search_config["search_records_db"][str(search_task)], self.search_config["search_records_db"]

        if isinstance(search_task, SingleTask):
            if self.search_config['unit_task_method'] == "genetic":
                # Use genetic search
                search_record = tuners.genetic_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "random_pruning":
                search_record = tuners.random_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, pruning=1, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "random":
                search_record = tuners.random_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "exhaustive_pruning":                
                search_record = tuners.exhaustive_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, pruning=1, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "annealing":
                search_record = tuners.annealing_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "bayesian":
                search_record = tuners.bayesian_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "RL":
                search_record = tuners.RL_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, profiling=self.search_config["profiling"])
            elif self.search_config["unit_task_method"] == "open_tuner":
                search_record = tuners.opentuner_search(search_task, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=1, silent=silent, profiling=self.search_config["profiling"], args=self.search_config["args"])
            else:
                raise NotImplementedError("Undefined unit task method.")
        elif isinstance(search_task, MultiTask):
            if search_task.fuse == 0:
                if search_task.split == 0:
                    search_record = tuners.non_fuse_genetic_search(search_task, init_tasks, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                        n_worker=self.search_config['n_worker'], silent=silent, population_size=self.search_config['genetic_params']['population_size'][1], meta=meta)
                else:
                    if self.search_config["method"] == "customized1":
                        search_record = tuners.multi_acc_search1(search_task, init_tasks, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                            n_worker=self.search_config['n_worker'], silent=silent, population_size=self.search_config['genetic_params']['population_size'][1], \
                            meta=meta, explorer=self, profiling=self.search_config["profiling"])
                    elif self.search_config["method"] == "customized2":
                        search_record = tuners.multi_acc_search2(search_task, init_tasks, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                            n_worker=self.search_config['n_worker'], silent=silent, population_size=self.search_config['genetic_params']['population_size'][1], \
                            meta=meta, explorer=self, profiling=self.search_config["profiling"])
            elif search_task.fuse == 1:
                search_record = tuners.fuse_genetic_search(search_task, init_tasks, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=self.search_config['n_worker'], silent=silent, population_size=self.search_config['genetic_params']['population_size'][1], meta=meta, explorer=self)
            elif search_task.fuse == 2:
                search_record = tuners.all_fuse_genetic_search(search_task, init_tasks, self.cst, self.search_obj, self.max_epochs, self.max_time, \
                    n_worker=self.search_config['n_worker'], silent=silent, population_size=self.search_config['genetic_params']['population_size'][1], explorer=self)
            else:
                raise RuntimeError('Unknown search task type.')
        else:
            raise RuntimeError('Unknown search task type.')

        '''
        # Save the search results
        if str(search_task) in self.search_config["search_records_db"]:
            self.search_config["search_records_db"][str(search_task)].update(search_record)
        else:
            self.search_config["search_records_db"][str(search_task)] = search_record
        '''

        return search_record
        #return search_record, self.search_config["search_records_db"]

    def search_non_fusion_single_acc_exhaustive(self):
        raise NotImplementedError("Unimplemented single accelerator search method.")

    def search_non_fusion_single_acc_customized1(self, design_idx=-1, search_task_configs=None, early_stop=-1, silent=0, workload_idx=None, prev_array=None, one_gen=False):
        """ This function searches the best single accelerator for the search tasks.
        We assume the tasks are executed in sequence on the acclerator.
        The function first searches the best array configuration for each task.
        The results are served as the initial candidate pool to kick off the
        evolutionary search which searches for the best array configuration
        that maximizes the overall performance.
        Modify the search task configurations when the search_task_configs is valid.

        If early_stop is set (not equal to -1), the search will be terminated
        if the ideal latency is longer than the early_stop threshold.
        If URAM is used, we will run the non-fuse search for one time and identify the
        bottleneck of each layer. Following the increasing order of CTC ratio,
        we check three arrasy: cin, cout, and w.
        If any of them is the bottleneck, we will try to store them on-chip.
        This process stops until there is no more URAM available on-chip.

        "prev_array" is used for the TGPA-style multi-array setting.
        When prev_array is set, when searching the solution of the current array,
        the latency of each workload is adjusted to consider the setup latency.
        """
        design_list = self.designs
        if design_idx != -1:
            # Only search a certain design
            design_list = [self.designs[design_idx]]

        if workload_idx:
            workloads = [self.workloads[i] for i in workload_idx]
        else:
            workloads = self.workloads

        # Test1: Fix r-axis to one
        #search_task_configs = {}
        #for i in range(len(self.workloads)):
        #    search_task_configs[i] = {'fix_param': [['r', 1]]}

        # Test2: Equate c_t1 = r_t1
        #search_task_configs = {}
        #for i in range(len(self.workloads)):
        #    search_task_configs[i] = {'equate_params': [['r_t1', 'c_t1']]}

        def est_URAM(width, depth):
            """ Estimate URAM usage.
            """
            mem = np.ceil(width / 72) * np.ceil(depth / 4096)
            return mem

        def modify_task_configs_uram(layer_infos, workloads, configs):
            if not configs:
                configs = {}
                for layer_idx in range(len(layer_infos)):
                    configs[layer_idx] = {"cin_read_mode": 0, "w_read_mode": 0, "cout_write_mode": 0}
            c_mem = []
            for layer_idx in range(len(layer_infos)):
                c_mem.append([0, 0]) # input, output
            w_mem = 0
            def take_item(elem):
                return elem["item"]
            def take_value(elem):
                return elem["value"]
            def cal_c_mem(c_mem):
                total_c_mem = [m[0] + m[1] for m in c_mem]
                return max(total_c_mem)
            for layer_info in layer_infos:
                workload = workloads[layer_info["idx"]]
                if cal_c_mem(c_mem) + w_mem >= self.cst.hw_cst["URAM"]:
                    break
                PE_latency = layer_info["reward_meta"]["latency_main"]["PE_latency"]
                cin_latency = [{"item": x, "value": layer_info["reward_meta"]["latency_main"][x]} for x in layer_info["reward_meta"]["latency_main"] if x.startswith("cin")]
                cin_latency.sort(key=take_value)
                cout_latency = [{"item": x, "value": layer_info["reward_meta"]["latency_main"][x]} for x in layer_info["reward_meta"]["latency_main"] if x.startswith("cout")]
                cout_latency.sort(key=take_value)
                w_latency = [{"item": x, "value": layer_info["reward_meta"]["latency_main"][x]} for x in layer_info["reward_meta"]["latency_main"] if x.startswith("w")]
                w_latency.sort(key=take_value)
                bottlenecks = []
                if cin_latency[-1]['value'] != cin_latency[-2]['value']:
                    bottlenecks.append({"item": "cin", "value": cin_latency[-1]['value']})
                if cout_latency[-1]['value'] != cout_latency[-2]['value']:
                    bottlenecks.append({"item": "cout", "value": cout_latency[-1]['value']})
                if w_latency[-1]['value'] != w_latency[-2]['value']:
                    bottlenecks.append({"item": "w", "value": w_latency[-1]['value']})
                bottlenecks.sort(key=take_value, reverse=True)
                for b in bottlenecks:
                    if b["value"] <= PE_latency:
                        break
                    if b["item"] == "w":
                        # Compute the uram for w
                        datapack = 8
                        dw = 4 # Four bytes by default
                        width = dw * 8 * datapack
                        depth = workload["params"]["o"] * workload["params"]["i"] * \
                                workload["params"]["p"] * workload["params"]["q"] / datapack
                        uram = est_URAM(width, depth)
                        if cal_c_mem(c_mem) + w_mem + uram < self.cst.hw_cst["URAM"]:
                            configs[layer_info["idx"]]["w_read_mode"] = 1
                            w_mem += uram
                    if b["item"] == "cin" and layer_info["idx"] > 0:
                        # Compute the uram for cin
                        datapack = 8
                        dw = 4 # Four bytes by default
                        width = dw * 8 * datapack
                        depth = workload["params"]["i"] * (workload["params"]["r"] + workload["params"]["p"] - 1) * \
                                (workload["params"]["c"] + workload["params"]["q"] - 1) / datapack
                        uram = est_URAM(width, depth)
                        old_c_mem = copy.deepcopy(c_mem)
                        c_mem[layer_info["idx"]][0] = max(c_mem[layer_info["idx"]][0], uram)
                        c_mem[layer_info["idx"] - 1][1] = max(c_mem[layer_info["idx"] - 1][1], uram)
                        if cal_c_mem(c_mem) + w_mem < self.cst.hw_cst["URAM"]:
                            configs[layer_info["idx"]]["cin_read_mode"] = 3
                            configs[layer_info["idx"] - 1]["cout_write_mode"] = 1
                        else:
                            c_mem = old_c_mem
                    if b["item"] == "cout" and layer_info["idx"] < len(workloads) - 1:
                        # Compute the uram for cout
                        datapack = 8
                        dw = 4
                        width = dw * 8 * datapack
                        depth = workload["params"]["o"] * workload["params"]["r"] * workload["params"]["c"] / datapack
                        uram = est_URAM(width, depth)
                        old_c_mem = copy.deepcopy(c_mem)
                        c_mem[layer_info["idx"]][1] = max(c_mem[layer_info["idx"]][1], uram)
                        c_mem[layer_info["idx"] + 1][0] = max(c_mem[layer_info["idx"] + 1][0], uram)
                        if cal_c_mem(c_mem) + w_mem < self.cst.hw_cst["URAM"]:
                            configs[layer_info["idx"]]["cout_write_mode"] = 1
                            configs[layer_info["idx"] + 1]["cin_read_mode"] = 3
                        else:
                            c_mem = old_c_mem
            return configs, cal_c_mem(c_mem) + w_mem

        def modify_task_configs_prev_array(prev_array, configs):
            prev_workload = prev_array['workloads']
            prev_record = prev_array['record']
            if not configs:
                configs = {}
                for layer_idx in range(len(workloads)):
                    configs[layer_idx] = {"prev_sol": None, "prev_workload": None, "prev_latency": None}
            for layer_idx in range(len(workloads)):
                if layer_idx < len(prev_workload):
                    configs[layer_idx]['prev_workload'] = self.workloads[prev_workload[layer_idx]]
                    configs[layer_idx]['prev_sol'] = prev_record.task_sols[layer_idx]['sol']
                    configs[layer_idx]['prev_latency'] = prev_record.task_sols[layer_idx]['latency']
            return configs

        def one_pass(workloads, design_list, silent, early_stop, search_task_configs):
            # Search the best config for each task
            repeat = True
            repeat_iter = 0
            job_list = []
            while repeat:
                search_tasks = []
                # Single workload task
                for workload in workloads:
                    search_task = SingleTask(design_list[i], workload, self.cst)
                    search_tasks.append(search_task)
                # Modify the first search task, used for multi-acc search
                if search_task_configs:
                    for task_idx in range(len(search_tasks)):
                        search_tasks[task_idx].configs = search_task_configs[task_idx]
                # Silent the tuner if the #worker is greater than 1
                local_silent = silent
                if silent == 0:
                    local_silent = 1 if self.search_config["n_worker"] > 1 else 0
                one_batch_n_job = 0
                for t in search_tasks:
                    for job in job_list:
                        if job['job_hash'] == f'{str(t)}_{repeat_iter}':
                            # Avoid duplicate task
                            continue
                    job_list.append(
                        {'job_hash': f'{str(t)}_{repeat_iter}', 'func': self.tune, \
                         'args': [t, None, local_silent, 0]})
                    one_batch_n_job += 1
                # Fill in enough tasks for the initial population
                #if len(job_list) + one_batch_n_job > self.search_config['genetic_params']['population_size'][1]:
                #    repeat = False
                repeat_iter += 1
                if repeat_iter > 1:
                    repeat = False

            pool = utils.MyExecutor(self.search_config['n_worker'])
            results = pool.exec(job_list)
            init_tasks = []
            for r in results:
                if results[r].valid:
                    init_tasks.append(results[r])

            # Search the single array architecture
            if early_stop != -1:
                # Test if the ideal latency is longer than the early stop threshold.
                ideal_latency = utils.compute_tasks_latency(search_tasks, init_tasks)
                if ideal_latency > early_stop:
                    return best_record

            # Build the multi-workload search task
            search_tasks = []
            for workload in workloads:
                search_task = SingleTask(design_list[i], workload, self.cst)
                search_tasks.append(search_task)
            if search_task_configs:
                for task_idx in range(len(search_tasks)):
                    search_tasks[task_idx].configs = search_task_configs[task_idx]
            search_task = MultiTask(design_list[i], search_tasks, self.cst, fuse=0)
            meta = {"one_gen": one_gen, "xgb_params": self.search_config["xgb_params"]}
            search_record = self.tune(search_task, init_tasks, silent=silent, meta=meta)

            return search_record

        best_record = utils.SearchRecord().reset()
        if prev_array:
            search_task_configs = modify_task_configs_prev_array(prev_array, search_task_configs)
        for i in range(len(design_list)):
            if len(self.workloads) == 1:
                # Single task workload
                search_task = SingleTask(design_list[i], workloads[0], self.cst)
                if search_task_configs:
                    search_task.configs = search_task_configs[0]
                search_record = self.tune(search_task)
                if search_record.valid:
                    search_record.arch_sol = search_record.task_sols[0]['sol']
                    if prev_array:
                        total_latency = 0
                        for task_sol in search_record.task_sols:
                            task_sol['latency'] = task_sol['reward_meta']['latency']['latency_orig']
                            total_latency += task_sol['latency']
                        search_record.latency = total_latency
                best_record.update(search_record, save=1)
            else:
                search_record = one_pass(workloads, design_list, silent, early_stop, search_task_configs)
                if prev_array:
                    total_latency = 0
                    for task_sol in search_record.task_sols:
                        task_sol['latency'] = task_sol['reward_meta']['latency']['latency_orig']
                        total_latency += task_sol['latency']
                    search_record.latency = total_latency
                    if search_record.metric == "latency":
                        search_record.reward = 1 / total_latency
                best_record.update(search_record, save=1)
                if self.search_config['use_uram'] == 1 and "conv" in workloads[0]["tags"]:
                    import logging
                    logger = logging.getLogger('AutoSA-Tuner')
                    logger.info("Search again with URAM...")
                    # For CNN we test if any buffers can be fit on-chip
                    layer_info = []
                    for task_idx in range(len(search_record.task_sols)):
                        task_sol = search_record.task_sols[task_idx]
                        layer_info.append({
                            "idx": task_idx,
                            "CTC": task_sol["CTC"],
                            "reward_meta": task_sol["reward_meta"]["latency"]
                        })
                    # Sort them by CTC ratio
                    def getCTC(elem):
                        return elem["CTC"]
                    layer_info.sort(key=getCTC)
                    #pprint.pprint(layer_info)
                    #exit(0)
                    search_task_configs, uram = modify_task_configs_uram(layer_info, workloads, search_task_configs)
                    # Run the search again with updated search configs
                    search_record = one_pass(workloads, design_list, silent, early_stop, search_task_configs)
                    search_record.cst["URAM"] = uram
                    if prev_array:
                        total_latency = 0
                        for task_sol in search_record.task_sols:
                            task_sol['latency'] = task_sol['reward_meta']['latency']['latency_orig']
                            total_latency += task_sol['latency']
                        search_record.latency = total_latency
                        if search_record.metric == "latency":
                            search_record.reward = 1 / total_latency
                    best_record.update(search_record, save=1)

        return best_record

    def search_fusion_single_acc_customized1(self, design_idx=-1, search_task_configs=None):
        """ This function searches the best single accelerator configuration considering
        the task fusion.
        Note: We assume a linear dependence in the network.
        There are two steps.
        Step 1: Build a candidate pool of all the sub-graphs of interst. Search
        for the best array configurations of these tasks.
        Step 2: Use the candidate tasks in the previous step to kick off the
        evo search. For each array config, use the DP to find the best fusion scheme.
        """
        # Note: Consider FP32 only at 200MHz with 3 DDR ports
        params = {
            "thres_CTC": self.cst.hw_cst["DSP"] / 5 * 2 * 0.2 / (12.8 * 3)
        }

        best_record = utils.SearchRecord().reset()

        design_list = self.designs
        if design_idx != -1:
            # Only search a certain design
            design_idx_list = [design_idx]
        else:
            design_idx_list = list(range(len(self.designs)))

        for i in design_idx_list:
            fusion_candidates = []
            # Enqueue the single-workload tasks
            repeat = True
            repeat_iter = 0
            job_list = []
            while repeat:
                search_tasks = []
                for workload in self.workloads:
                    search_task = SingleTask(design_list[i], workload, self.cst)
                    search_tasks.append(search_task)
                # Modify the first search task, used for multi-acc search
                if search_task_configs:
                    search_tasks[0].configs = search_task_configs
                # Silent the tuner if the #worker is greater than 1
                silent = 1 if self.search_config["n_worker"] > 1 else 0
                one_batch_n_job = 0
                for t in search_tasks:
                    for job in job_list:
                        if job['job_hash'] == f'{str(t)}_{repeat_iter}':
                            # Avoid duplicate task
                            continue
                    job_list.append(
                        {'job_hash': f'{str(t)}_{repeat_iter}', 'func': self.tune, \
                         'args': [t, None, silent, 0]})
                    one_batch_n_job += 1
                # Fill in enough tasks for the initial population
                if len(job_list) + one_batch_n_job > self.search_config['genetic_params']['population_size'][1]:
                    repeat = False
                repeat_iter += 1
            pool = utils.MyExecutor(self.search_config['n_worker'])
            results = pool.exec(job_list)
            init_tasks = []
            for r in results:
                if results[r].valid:
                    init_tasks.append(results[r])

            # Sort the tasks based on the CTC ratio
            network_best_records = {}
            for record in init_tasks:
                if record.task_sols[0]['hash'] in network_best_records:
                    network_best_records[record.task_sols[0]['hash']].update(record)
                else:
                    network_best_records[record.task_sols[0]['hash']] = record

            network_best_records_sorted = []
            comm_bound_ops = []
            for k, v in network_best_records.items():
                network_best_records_sorted.append(v)
            CTC_thres = params["thres_CTC"]
            def takeCTC(elem):
                return elem.ctc
            network_best_records_sorted.sort(key=takeCTC)
            for record in network_best_records_sorted:
                if record.dsp_eff < 0.5:
                    CTC_thres = max(CTC_thres, record.ctc)
                else:
                    break
            for record in network_best_records_sorted:
                if record.ctc <= CTC_thres:
                    comm_bound_ops.append(record)

            # Enqueue the multi-workload tasks
            comm_bound_layers = []
            for layer_idx in range(len(self.workloads)):
                layer = self.workloads[layer_idx]
                for op in comm_bound_ops:
                    if layer["name"] in op.task_names:
                        comm_bound_layers.append({"ctc": op.ctc, "layers": [layer_idx]})

            searched_layers = []
            def hash_layers(layer_ids):
                ret = ""
                for id in layer_ids:
                    params = self.workloads[id]["params"]
                    for k,v in params.items():
                        ret += f"{k}{v}"
                    for tag in self.workloads[id]["tags"]:
                        ret += tag
                return ret

            def find_all_pairs(layer_ids):
                # Find all pairs in the network with the same workload config as the "layer_ids"
                layer_hash = hash_layers(layer_ids)
                ret = []
                for idx in range(len(self.workloads) - (len(layer_ids) - 1)):
                    cmp_layer_ids = list(range(idx, idx + len(layer_ids)))
                    if hash_layers(cmp_layer_ids) == layer_hash:
                        task_names = [self.workloads[i]["name"] for i in cmp_layer_ids]
                        ret.append({"idx": cmp_layer_ids, "names": task_names})
                return ret

            while len(comm_bound_layers) > 0:
                # Sort the list based on the increasing order of CTC
                def takeCTC(elem):
                    return elem["ctc"]
                comm_bound_layers.sort(key=takeCTC)

                # Start with the task with the lowest CTC
                op_to_fuse = comm_bound_layers[0]

                # Fuse it with neighbor layers
                if op_to_fuse['layers'][0] > 0:
                    prev_layers = self.workloads[op_to_fuse['layers'][0] - 1: op_to_fuse['layers'][0] + 1]
                    prev_layers_idx = list(range(op_to_fuse['layers'][0] - 1, op_to_fuse['layers'][0] + 1))
                    unfused_latency = 0
                    for layer in prev_layers:
                        for record in network_best_records_sorted:
                            if layer["name"] in record.task_names:
                                unfused_latency += record.latency
                                break
                    #layer_hash = ''
                    #for idx in prev_layers_idx:
                    #    layer_hash += str(idx)
                    layer_hash = hash_layers(prev_layers_idx)
                    if layer_hash not in searched_layers:
                        searched_layers.append(layer_hash)
                        search_record = self.search_fusion_single_acc_customized2(prev_layers_idx, design_idx=i, search_task_configs=search_task_configs)
                        if search_record.valid:
                            if search_record.latency < unfused_latency:
                                pairs = find_all_pairs(prev_layers_idx)
                                for pair in pairs:
                                    fusion_candidates.append(pair["names"])
                                init_tasks.insert(0, search_record)
                                if search_record.ctc < CTC_thres:
                                    for pair in pairs:
                                        comm_bound_layers.append({"ctc": search_record.ctc, "layers": pair["idx"]})
                if op_to_fuse['layers'][-1] < len(self.workloads) - 1:
                    nxt_layers = self.workloads[op_to_fuse['layers'][-1]: op_to_fuse['layers'][-1] + 2]
                    nxt_layers_idx = list(range(op_to_fuse['layers'][-1], op_to_fuse['layers'][-1] + 2))
                    unfused_latency = 0
                    for layer in nxt_layers:
                        for record in network_best_records_sorted:
                            if layer["name"] in record.task_names:
                                unfused_latency += record.latency
                                break
                    layer_hash = hash_layers(nxt_layers_idx)
                    if layer_hash not in searched_layers:
                        searched_layers.append(layer_hash)
                        search_record = self.search_fusion_single_acc_customized2(nxt_layers_idx, design_idx=i, search_task_configs=search_task_configs)
                        if search_record.valid:
                            if search_record.latency < unfused_latency:
                                pairs = find_all_pairs(nxt_layers_idx)
                                for pair in pairs:
                                    fusion_candidates.append(pair["names"])
                                init_tasks.insert(0, search_record)
                                if search_record.ctc < CTC_thres:
                                    for pair in pairs:
                                        comm_bound_layers.append({"ctc": search_record.ctc, "layers": pair["idx"]})
                # Pop out the op
                comm_bound_layers = comm_bound_layers[1:]

            # Kick off the local search
            search_tasks = []
            for workload in self.workloads:
                search_task = SingleTask(design_list[i], workload, self.cst)
                search_tasks.append(search_task)
            # Modify the first search task, used for multi-acc search
            if search_task_configs:
                search_tasks[0].configs = search_task_configs
            search_task = MultiTask(design_list[i], search_tasks, self.cst, fuse=1)
            import logging
            logger = logging.getLogger('AutoSA-Tuner')
            logger.info(f"fusion candidates: {fusion_candidates}")

            for idx in range(len(fusion_candidates)):
                fusion_candidates[idx] = ''.join(fusion_candidates[idx])
            meta = {'fusion_candidates': fusion_candidates}
            search_record = self.tune(search_task, init_tasks, meta=meta)

            best_record.update(search_record, save=1)

        return best_record

    def search_fusion_single_acc_customized2(self, workload_idx=None, design_idx=-1, search_task_configs=None, silent=0):
        """ This function searches the best single accelerator configuration considering
        the task fusion. All the layers are fused.
        Note: We assume a linear dependence in the network.
        There are two steps.
        Step 1: Build a candidate pool of all the sub-graphs of interst. Search
        for the best array configurations of these tasks.
        Step 2: Use the candidate tasks in the previous step to kick off the
        evo search.
        """
        best_record = utils.SearchRecord().reset()

        design_list = self.designs
        if design_idx != -1:
            # Only search a certain design
            design_idx_list = [design_idx]
        else:
            design_idx_list = list(range(len(self.designs)))
        workloads = [self.workloads[i] for i in workload_idx]

        for i in design_idx_list:
            # Enqueue the single-workload tasks
            repeat = True
            repeat_iter = 0
            job_list = []
            while repeat:
                search_tasks = []
                for workload in workloads:
                    search_task = SingleTask(design_list[i], workload, self.cst)
                    search_tasks.append(search_task)
                # Modify the first search task, used for multi-acc search
                if search_task_configs:
                    search_tasks[0].configs = search_task_configs
                # Modify the last layer
                last_task = copy.deepcopy(search_tasks[-1])
                last_task.fuse = 1
                last_task.last_fuse = 1
                last_task.use_uram = self.search_config["use_uram"]
                if last_task.use_uram:
                    last_task.configs['cin_read_mode'] = 3
                else:
                    last_task.configs['cin_read_mode'] = 2
                last_task.configs['cout_write_mode'] = 0
                last_task.set_aux_func('update_cin_latency', 'update_cin_latency_last')
                if last_task.use_uram == 0:
                    last_task.set_aux_func('update_cin_buf', 'update_cin_buf_bram_last')
                else:
                    last_task.set_aux_func('update_cin_buf', 'update_cin_buf_uram_last')
                search_tasks.append(last_task)

                # Silent the tuner if the #worker is greater than 1
                local_silent = silent
                if silent == 0:
                    local_silent = 1 if self.search_config["n_worker"] > 1 else 0
                one_batch_n_job = 0
                for t in search_tasks:
                    for job in job_list:
                        if job['job_hash'] == f'{str(t)}_{repeat_iter}':
                            # Avoid duplicate task
                            continue
                    job_list.append(
                        {'job_hash': f'{str(t)}_{repeat_iter}', 'func': self.tune, \
                         'args': [t, None, local_silent, 0]})
                    one_batch_n_job += 1
                # Fill in enough tasks for the initial population
                if len(job_list) + one_batch_n_job > self.search_config['genetic_params']['population_size'][1]:
                    repeat = False
                repeat_iter += 1

            pool = utils.MyExecutor(self.search_config['n_worker'])
            results = pool.exec(job_list)
            init_tasks = []
            for r in results:
                if results[r].valid:
                    init_tasks.append(results[r])

            # Local search
            search_tasks = []
            for workload in workloads:
                search_task = SingleTask(design_list[i], workload, self.cst)
                search_tasks.append(search_task)
            # Modify the first search task, used for multi-acc search
            if search_task_configs:
                search_tasks[0].configs = search_task_configs
            search_task = MultiTask(design_list[i], search_tasks, self.cst, fuse=2, use_uram=self.search_config["use_uram"])
            search_record = self.tune(search_task, init_tasks, silent=silent)

            best_record.update(search_record)

        return best_record

    def search_fusion_multi_acc_customized1(self, design_idx=-1, search_task_configs=None, silent=0):
        """ This function searches the best multi-array configuration.
        Run the single array search first.
        Then explore different partitions schemes by setting different DSP utilization threshold.
        For certain threshold, all the layers that achieve beyond the threshold are mapped
        to a homogeneneous systolic array. The rest layers are mapped to separate
        single systolic arrays.
        """
        best_record = utils.SearchRecord().reset()

        params = {
            "non_fuse_repeat": 1, # Run the single-array search for multiple times to stablelize the results
            "n_designs": 4, # Only select the top-k designs for consideration
            "util_interval": 0.1, # DSP utilization interval for generating partition candidates
            "n_partition_candidates": 3, # Only consider the top-k partitioning candidates
            "n_array_max": self.search_config["max_n_array"] # At most #arrays are supported
        }

        import logging
        logger = logging.getLogger('AutoSA-Tuner')

        design_list = self.designs
        if design_idx != -1:
            # Only search a certain design
            design_idx_list = [design_idx]
        else:
            design_idx_list = list(range(len(self.designs)))
        
        '''
        # Single array search        
        design_history = []
        single_array_record = utils.SearchRecord().reset()
        for i in design_idx_list:
            local_record = utils.SearchRecord().reset()
            for repeat in range(params["non_fuse_repeat"]):
                #local_record.update(self.search_non_fusion_single_acc_customized1(design_idx=i, silent=silent, one_gen=True))
                local_record.update(self.search_non_fusion_single_acc_customized1(design_idx=i, silent=silent))
            design_history.append({"idx": i, "record": local_record})
            single_array_record.update(local_record)
        single_array_record.throughput = 1 / single_array_record.latency
        '''
        
        import pickle
        #pickle.dump(design_history, open(f'tmp/design_history_{self.search_config["workload"]}', 'wb'))
        #pickle.dump(single_array_record, open(f'tmp/single_array_record_{self.search_config["workload"]}', 'wb'))
        design_history = pickle.load(open(f'tmp/design_history_{self.search_config["workload"]}', 'rb'))
        single_array_record = pickle.load(open(f'tmp/single_array_record_{self.search_config["workload"]}', 'rb'))        

        '''
        # For the scalability issue, we will only select the top-4 designs
        # as the candidate dataflows for further exploration.
        def take_record_latency(elem):
            return elem["record"].latency
        design_history.sort(key=take_record_latency)
        design_history = design_history[:min(params["n_designs"], len(design_history))]
        design_idx_list = [h["idx"] for h in design_history]                
        logger.info(f"Selected design idx: {design_idx_list}")
        design_list = [self.designs[i] for i in design_idx_list]
        '''

        # Partition initialization        
        # Setting 1: Parition the first x layers to single arrays, and place the rest on a single array        
        # Setting 2: Group layers that are similar together        
        def hash_partition(partition):
            ret = ""
            for p in partition:
                ret += "|"
                ret += ''.join(str(p))
                ret += "|"
            return ret

        partition_candidates = []    

        # Setting 1
        '''
        layer_sols = single_array_record.task_sols
        dsp_eff_list = [sol["DSP_eff"] for sol in layer_sols]
        max_dsp_eff = max(dsp_eff_list)
        op_list = [sol["ops"] for sol in layer_sols]
        total_ops = np.sum(op_list)
        for split_pos in range(1, len(layer_sols)):
            latency_list = []
            # SL array
            for sl_idx in range(split_pos):
                dsp_eff = max_dsp_eff
                t = op_list[sl_idx] / total_ops * dsp_eff
                lat = op_list[sl_idx] / t
                latency_list.append(lat)
            # ML array
            dsp_eff = np.mean(dsp_eff_list[split_pos:])
            t = np.sum(op_list[split_pos:]) / total_ops * dsp_eff
            lat = np.sum(op_list[split_pos:]) / t
            latency_list.append(lat)
            T = 1 / max(latency_list)
            partition = []
            for sl_idx in range(split_pos):
                partition.append([sl_idx])
            partition.append(list(range(split_pos, len(layer_sols))))
            if len(partition) > params["n_array_max"]:
                continue
            partition_candidates.append({
                "idx": len(partition_candidates),
                "partition": partition,
                "hash": hash_partition(partition),
                "throughput": T,
                "n_arrays": len(partition)
            })
        # Sort the partition candidates by throughput
        def take_throughput(elem):
            return elem["throughput"]
        partition_candidates.sort(key=take_throughput, reverse=True)
        logger.info(f"Partition candidates:\n{pprint.pformat(partition_candidates, indent=2)}")
        init_partition_candidates = [i for i in range(min(params["n_partition_candidates"], len(partition_candidates)))]
        '''
        
        # Setting 2
        import statistics
        layer_sols = single_array_record.task_sols
        dsp_eff_list = [sol["DSP_eff"] for sol in layer_sols]
        op_list = [sol["ops"] for sol in layer_sols]
        for i in range(len(dsp_eff_list)):
            print(i, dsp_eff_list[i])   
        import csv
        with open("dsp_eff.csv", "w") as f:
            columns = ["layer", "dsp_eff"]
            writer = csv.DictWriter(f, fieldnames=columns)
            writer.writeheader()
            for i in range(len(dsp_eff_list)):
                data = {
                    "layer": i + 1,
                    "dsp_eff": dsp_eff_list[i]
                }
                writer.writerow(data)

        split_pos_list = []
        # Always split the first layer, therefore start from the second layer
        window = [layer_sols[1]["DSP_eff"], layer_sols[2]["DSP_eff"]]
        stdev_cur = statistics.stdev(window)
        for i in range(3, len(self.workloads)):
            if len(window) > 2 and (
                dsp_eff_list[i] > max(window) * 1.1 or dsp_eff_list[i] * 1.15 < min(window)):
                #print(i, max(window))
                split_pos_list.append(i) # Split before i-th layer
                window = [layer_sols[i]["DSP_eff"]]
            else:
                window.append(layer_sols[i]["DSP_eff"])        
        split_pos_list.insert(0, 1) # Always split the first layer
        split_pos_list.append(len(self.workloads))        
        print(split_pos_list)
        #exit(0)
        max_min_list = [max(dsp_eff_list), min(dsp_eff_list)] 
        #print(max_min_list)
        stdev_max = statistics.stdev(max_min_list)
        #print(stdev_max)
        
        # Compute the mean and stdev        
        def profile_partition(split_pos_list, dsp_eff_list):
            stdev_list = []
            mean_list = []
            mean_ratio_list = []
            for i in range(1, len(split_pos_list)):
                window = [dsp_eff_list[d] for d in range(split_pos_list[i - 1], split_pos_list[i])]                
                mean_list.append(np.mean(window))
                if len(window) > 1:
                    stdev_list.append(statistics.stdev(window))
                else:
                    stdev_list.append(0)
            for i in range(1, len(mean_list)):
                ratio = abs((mean_list[i] - mean_list[i - 1]) / mean_list[i - 1])
                mean_ratio_list.append(ratio)
            return mean_list, stdev_list, mean_ratio_list
        def estimate_partition_throughput(partition, dsp_eff_list, op_list):
            latency = []
            max_dsp_eff = max(dsp_eff_list)
            for p in partition:
                ops = 0                
                for i in p:
                    ops += op_list[i]
                if len(p) == 1:
                    dsp_eff = max_dsp_eff
                else:
                    #dsp_eff = np.mean([dsp_eff_list[i] for i in p])
                    stdev_cur = statistics.stdev([dsp_eff_list[i] for i in p])
                    dsp_eff = (min(dsp_eff_list) - max(dsp_eff_list)) / 2 * (stdev_cur / stdev_max) + max(dsp_eff_list)
                    #dsp_eff = (min(dsp_eff_list) - max(dsp_eff_list)) * (stdev_cur / stdev_max) + max(dsp_eff_list)
                throughput_cur = ops / np.sum(op_list) * dsp_eff
                latency_cur = ops / throughput_cur
                latency.append(latency_cur)
            return 1 / max(latency)
        
        split_pos_list_old = copy.deepcopy(split_pos_list)        
        # Merge 
        mean_list, stdev_list, mean_ratio_list = profile_partition(split_pos_list, dsp_eff_list)
        cur_n_array = len(mean_list)            
        while cur_n_array > 0:
            if cur_n_array <= params["n_array_max"] - 1:
                partition = [[0]]
                for i in range(len(split_pos_list) - 1):
                    partition += [list(range(split_pos_list[i], split_pos_list[i + 1]))]
                throughput = estimate_partition_throughput(partition, dsp_eff_list, op_list)
                duplicate = False
                for p_tmp in partition_candidates:
                    if p_tmp["hash"] == hash_partition(partition):
                        duplicate = True
                        break
                if not duplicate:
                    partition_candidates.append({
                        "idx": len(partition_candidates),
                        "partition": partition,
                        "hash": hash_partition(partition),
                        "throughput": throughput,
                        "n_arrays": len(partition)
                    })
            # Sort the mean_ratio_list and merge the adjacent one with the smallest ratio                
            if cur_n_array > 1:                       
                sort_index = np.argsort(mean_ratio_list)
                array_to_merge_idx = sort_index[0]
                del(split_pos_list[array_to_merge_idx + 1])
                mean_list, stdev_list, mean_ratio_list = profile_partition(split_pos_list, dsp_eff_list)    
                cur_n_array = len(mean_list)
            else:
                cur_n_array -= 1           

        # Split
        split_pos_list = split_pos_list_old
        mean_list, stdev_list, mean_ratio_list = profile_partition(split_pos_list, dsp_eff_list)
        cur_n_array = len(mean_list)
        while cur_n_array <= params["n_array_max"] - 1:
            partition = [[0]]
            for i in range(len(split_pos_list) - 1):
                partition += [list(range(split_pos_list[i], split_pos_list[i + 1]))]
            throughput = estimate_partition_throughput(partition, dsp_eff_list, op_list)
            duplicate = False
            for p_tmp in partition_candidates:
                if p_tmp["hash"] == hash_partition(partition):
                    duplicate = True
                    break
            if not duplicate:
                partition_candidates.append({
                    "idx": len(partition_candidates),
                    "partition": partition,
                    "hash": hash_partition(partition),
                    "throughput": throughput,
                    "n_arrays": len(partition)
                })
            
            #print(stdev_list)
            sort_index = np.argsort(stdev_list)                
            array_to_split_index = sort_index[-1]
            if stdev_list[array_to_split_index] == 0:                    
                break
            # Try different positions
            #print(split_pos_list)
            #print(stdev_list)
            #print(array_to_split_index)
            if split_pos_list[array_to_split_index + 1] - split_pos_list[array_to_split_index] > 2:
                stdev_tmp_list = []                
                for i in range(split_pos_list[array_to_split_index], split_pos_list[array_to_split_index + 1]):
                    dsp_eff_tmp_list = dsp_eff_list[split_pos_list[array_to_split_index]: split_pos_list[array_to_split_index + 1]]                    
                    del(dsp_eff_tmp_list[i - split_pos_list[array_to_split_index]])                    
                    if len(dsp_eff_tmp_list) > 1:
                        stdev_tmp_list.append(statistics.stdev(dsp_eff_tmp_list))
                    else:
                        stdev_tmp_list.append(0)
                        break
                sort_index = np.argsort(stdev_tmp_list)      
                insert = 1
                if sort_index[0] > 0:
                    split_pos_list.insert(array_to_split_index + insert, split_pos_list[array_to_split_index] + sort_index[0])  
                    insert += 1
                if sort_index[0] < len(stdev_tmp_list) - 1:
                    split_pos_list.insert(array_to_split_index + insert, split_pos_list[array_to_split_index] + sort_index[0] + 1)  
                #split_pos_list.insert(array_to_split_index + 1, split_pos_list[array_to_split_index] + sort_index[0] + 1)
            else:
                split_pos_list.insert(array_to_split_index + 1, split_pos_list[array_to_split_index] + 1)
            mean_list, stdev_list, mean_ratio_list = profile_partition(split_pos_list, dsp_eff_list)    
            cur_n_array = len(mean_list)          
        
        #if len(mean_list) >= params["n_array_max"] - 1:
        #    # If the current #array is grater than the maximal array, merge them
        #    cur_n_array = len(mean_list)            
        #    while cur_n_array > 0:
        #        if cur_n_array <= params["n_array_max"] - 1:
        #            partition = [[0]]
        #            for i in range(len(split_pos_list) - 1):
        #                partition += [list(range(split_pos_list[i], split_pos_list[i + 1]))]
        #            throughput = estimate_partition_throughput(partition, dsp_eff_list, op_list)
        #            partition_candidates.append({
        #                "idx": len(partition_candidates),
        #                "partition": partition,
        #                "hash": hash_partition(partition),
        #                "throughput": throughput,
        #                "n_arrays": len(partition)
        #            })
        #        # Sort the mean_ratio_list and merge the adjacent one with the smallest ratio                
        #        if cur_n_array > 1:                       
        #            sort_index = np.argsort(mean_ratio_list)
        #            array_to_merge_idx = sort_index[0]
        #            del(split_pos_list[array_to_merge_idx + 1])
        #            mean_list, stdev_list, mean_ratio_list = profile_partition(split_pos_list, dsp_eff_list)    
        #            cur_n_array = len(mean_list)
        #        else:
        #            cur_n_array -= 1                                
        #else:
        #    # Else, split the array with the highest stdev
        #    cur_n_array = len(mean_list)
        #    while cur_n_array <= params["n_array_max"] - 1:
        #        partition = [[0]]
        #        for i in range(len(split_pos_list) - 1):
        #            partition += [list(range(split_pos_list[i], split_pos_list[i + 1]))]
        #        throughput = estimate_partition_throughput(partition, dsp_eff_list, op_list)
        #        partition_candidates.append({
        #            "idx": len(partition_candidates),
        #            "partition": partition,
        #            "hash": hash_partition(partition),
        #            "throughput": throughput,
        #            "n_arrays": len(partition)
        #        })
        #        
        #        #print(stdev_list)
        #        sort_index = np.argsort(stdev_list)                
        #        array_to_split_index = sort_index[-1]
        #        if stdev_list[array_to_split_index] == 0:                    
        #            break
        #        # Try different positions
        #        if split_pos_list[array_to_split_index + 1] - split_pos_list[array_to_split_index] > 2:
        #            stdev_tmp_list = []                
        #            for i in range(split_pos_list[array_to_split_index], split_pos_list[array_to_split_index + 1]):
        #                dsp_eff_tmp_list = dsp_eff_list[split_pos_list[array_to_split_index]: split_pos_list[array_to_split_index + 1]]                    
        #                del(dsp_eff_tmp_list[i - split_pos_list[array_to_split_index]])                    
        #                if len(dsp_eff_tmp_list) > 1:
        #                    stdev_tmp_list.append(statistics.stdev(dsp_eff_tmp_list))
        #                else:
        #                    stdev_tmp_list.append(0)
        #                    break
        #            sort_index = np.argsort(stdev_tmp_list)      
        #            insert = 1
        #            if sort_index[0] > 0:
        #                split_pos_list.insert(array_to_split_index + insert, split_pos_list[array_to_split_index] + sort_index[0])  
        #                insert += 1
        #            if sort_index[0] < len(stdev_tmp_list) - 1:
        #                split_pos_list.insert(array_to_split_index + insert, split_pos_list[array_to_split_index] + sort_index[0] + 1)  
        #            #split_pos_list.insert(array_to_split_index + 1, split_pos_list[array_to_split_index] + sort_index[0] + 1)
        #        else:
        #            split_pos_list.insert(array_to_split_index + 1, split_pos_list[array_to_split_index] + 1)
        #        mean_list, stdev_list, mean_ratio_list = profile_partition(split_pos_list, dsp_eff_list)    
        #        cur_n_array = len(mean_list)                
        
        #def take_n_array(elem):
        #    return elem["n_arrays"]
        #partition_candidates.sort(key=take_n_array, reverse=True)
        def take_throughput(elem):
            return elem["throughput"]
        partition_candidates.sort(key=take_throughput, reverse=True)
        logger.info(f"Partition candidates:\n{pprint.pformat(partition_candidates, indent=2)}")
        init_partition_candidates = [i for i in range(min(params["n_partition_candidates"], len(partition_candidates)))]
        #pprint.pprint(partition_candidates)        
        #exit(0)

        '''        
        # Internal testing
        partition_candidates = []
        partition = []
        if self.search_config["workload"] == "vgg16":            
            partition.append([0])
            partition.append([1])
            partition.append([2])
            partition.append([3])
            partition.append([4])
            partition.append(list(range(5, len(self.workloads))))            
        elif self.search_config["workload"] == "resnet50":
            partition = []
            partition.append([0])
            partition.append(list(range(1, 10)))  # 2-10
            partition.append(list(range(10, 23))) # 11-23
            partition.append(list(range(23, 40)))  # 24-40
            partition.append(list(range(40, len(self.workloads)))) # 41-end                    
        elif self.search_config["workload"] == "mobilenetv2":
            partition = []
            partition.append([0])            
            partition.append(list(range(1, 2)))
            partition.append(list(range(2, 3)))        
            partition.append(list(range(3, 4)))
            partition.append(list(range(4, 8)))
            partition.append(list(range(8, 14)))        
            partition.append(list(range(14, 22)))
            partition.append(list(range(22, 28)))                
            partition.append(list(range(28, len(self.workloads))))            
        init_partition_candidates = [0]
        partition_candidates.append({
            "idx": len(partition_candidates),
            "partition": partition,            
            "hash": hash_partition(partition),
            "n_arrays": len(partition)
            })
        '''
        
        design_idx_list = [4, 5, 6, 8]
        design_list = [self.designs[i] for i in design_idx_list]

        # Collect the init tasks
        init_tasks = []
        for i in range(len(design_list)):
            job_list = []
            local_silent = silent
            if silent == 0:
                local_silent = 1 if self.search_config["n_worker"] > 1 else 0

            for repeat in range(params["non_fuse_repeat"]):
                search_tasks = []
                for workload in self.workloads:
                    search_task = SingleTask(design_list[i], workload, self.cst)
                    search_tasks.append(search_task)
                for t in search_tasks:
                    for job in job_list:
                        if job['job_hash'] == f'{str(t)}_{repeat}':
                            # Avoid duplicate task
                            continue
                    job_list.append(
                        {'job_hash': f'{str(t)}_{repeat}', 'func': self.tune, \
                         'args': [t, None, local_silent, 0]})

            pool = utils.MyExecutor(self.search_config['n_worker'])
            results = pool.exec(job_list)
            for r in results:
                if results[r].valid:
                    init_tasks.append(results[r])

        # Local search
        search_tasks = []
        for workload in self.workloads:
            search_task = SingleTask(design_list[0], workload, self.cst)
            search_tasks.append(search_task)
        if search_task_configs:
            search_tasks[0].configs = search_task_configs
        search_task = MultiTask(design_list, search_tasks, self.cst, split=1)
        meta = {'partition_candidates': partition_candidates,
                'design_idx_list': design_idx_list,
                'init_partition_candidates': init_partition_candidates,
                "batch_size": self.search_config["batch_size"],
                "use_uram_all": self.search_config["use_uram_all"]}

        '''
        # For internal testing
        import pickle
        #pickle.dump(meta, open('tmp/meta', 'wb'))
        #pickle.dump(init_tasks, open('tmp/init_tasks', 'wb'))
        #exit(0)
        meta = pickle.load(open('tmp/meta', 'rb'))
        init_tasks = pickle.load(open('tmp/init_tasks', 'rb'))
        meta['init_partition_candidates'] = [5]
        design_list = [self.designs[i] for i in meta['design_idx_list']]
        search_tasks = []
        for workload in self.workloads:
            search_task = SingleTask(design_list[0], workload, self.cst)
            search_tasks.append(search_task)
        if search_task_configs:
            search_tasks[0].configs = search_task_configs
        search_task = MultiTask(design_list, search_tasks, self.cst, split=1)
        '''

        search_record = self.tune(search_task, init_tasks, silent=silent, meta=meta)

        best_record.update(search_record)

        return best_record

    def search_fusion_multi_acc_customized2(self, design_idx=-1, search_task_configs=None, silent=0):
        """ This function searches the best multi-array configuration.
        It will periodically schedule the layers onto different systolic arrays.
        """
        best_record = utils.SearchRecord().reset()

        params = {
            "non_fuse_repeat": 1, # Run the single-array search for multiple times to stablelize the results
            "n_designs": 4, # Only select the top-k designs for consideration
            "n_partition_candidates": 3, # Only consider the top-k partitioning candidates
            "n_array_max": self.search_config["max_n_array"] # At most #arrays are supported
        }

        import logging
        logger = logging.getLogger('AutoSA-Tuner')

        design_list = self.designs
        if design_idx != -1:
            # Only search a certain design
            design_idx_list = [design_idx]
        else:
            design_idx_list = list(range(len(self.designs)))
                        
        # Single array search        
        design_history = []
        single_array_record = utils.SearchRecord().reset()
        search_task_configs = {}
        #for i in range(len(self.workloads)):
        #    search_task_configs[i] = {'fix_param': [['r', 1]]}
        for i in design_idx_list:
            local_record = utils.SearchRecord().reset()
            for repeat in range(params["non_fuse_repeat"]):
                local_record.update(\
                    self.search_non_fusion_single_acc_customized1(design_idx=i, silent=silent, one_gen=True))
                    #search_task_configs=search_task_configs))
            design_history.append({"idx": i, "record": local_record})
            single_array_record.update(local_record)
        single_array_record.throughput = 1 / single_array_record.latency                

        # For internal testing
        import pickle
        pickle.dump(design_history, open(f'tmp/design_history_{self.search_config["workload"]}', 'wb'))
        pickle.dump(single_array_record, open(f'tmp/single_array_record_{self.search_config["workload"]}', 'wb'))
        #design_history = pickle.load(open(f'tmp/design_history_{self.search_config["workload"]}', 'rb'))
        #single_array_record = pickle.load(open(f'tmp/single_array_record_{self.search_config["workload"]}', 'rb'))        

        '''
        # For the scalability issue, we will only select the top-4 designs
        # as the candidate dataflows for further exploration.
        def take_record_latency(elem):
            return elem["record"].latency
        design_history.sort(key=take_record_latency)
        design_history = design_history[:min(params["n_designs"], len(design_history))]
        design_idx_list = [h["idx"] for h in design_history]                
        logger.info(f"Selected design idx: {design_idx_list}")
        design_list = [self.designs[i] for i in design_idx_list]
        '''

        # Try all different #array combinations and rank based on the total ideal latency        
        def hash_partition(partition):
            ret = ""
            for p in partition:
                ret += "|"
                ret += ''.join(str(p))
                ret += "|"
            return ret

        partition_candidates = []          
        for n_array in range(2, min(len(self.workloads), params["n_array_max"]) + 1):
            partition = [[] for i in range(n_array)]
            for i in range(len(self.workloads)):
                array_idx = i % n_array
                partition[array_idx].append(i)
            layer_sols = single_array_record.task_sols
            dsp_eff_list = [sol["DSP_eff"] for sol in layer_sols]
            op_list = [sol["ops"] for sol in layer_sols]
            total_ops = np.sum(op_list)
            throughput_list = []
            for i in range(n_array):
                dsp_eff_list_cur = [dsp_eff_list[p] for p in partition[i]]
                dsp_eff_cur = np.mean(dsp_eff_list_cur)                
                op_list_cur = [op_list[p] for p in partition[i]]
                t_cur = np.sum(op_list_cur) / total_ops * dsp_eff_cur
                throughput_list.append(t_cur)
            record_latency = []
            for i in range(n_array):
                op_list_cur = [op_list[p] for p in partition[i]]
                array_latency_cur = [op_cur / throughput_list[i] for op_cur in op_list_cur]
                record_latency.append(array_latency_cur)

            design_latency = 0
            max_round = 0
            for p in partition:
                max_round = max(max_round, len(p))
            for round in range(max_round):
                array_latency = [record_latency[0][round] * self.search_config["batch_size"]]
                setup_latency = [0]
                for array_idx in range(1, n_array):
                    if round >= len(partition[array_idx]):
                        break
                    setup = record_latency[array_idx - 1][round] * 0.2
                    setup_latency.append(setup)                    
                    array_latency.append(max(record_latency[array_idx][round] * self.search_config["batch_size"], array_latency[array_idx - 1]))
                design_latency += (sum(setup_latency) + array_latency[-1])                    
            design_throughput = 1 / design_latency * self.search_config["batch_size"]
            if len(partition) > params["n_array_max"]:
                continue
            partition_candidates.append({
                "idx": len(partition_candidates),
                "partition": partition,
                "hash": hash_partition(partition),
                "throughput": design_throughput,
                "n_arrays": len(partition)
            })                

        def take_throughput(elem):
            return elem["throughput"]
        partition_candidates.sort(key=take_throughput, reverse=True)
        logger.info(f"Partition candidates:\n{pprint.pformat(partition_candidates, indent=2)}")
        init_partition_candidates = [i for i in range(min(params["n_partition_candidates"], len(partition_candidates)))]

        design_idx_list = [4, 5, 6, 8]
        design_list = [self.designs[i] for i in design_idx_list]

        # Collect the init tasks
        init_tasks = []
        for i in range(len(design_list)):
            job_list = []
            local_silent = silent
            if silent == 0:
                local_silent = 1 if self.search_config["n_worker"] > 1 else 0

            for repeat in range(params["non_fuse_repeat"]):
                search_tasks = []
                for workload in self.workloads:
                    search_task = SingleTask(design_list[i], workload, self.cst)
                    search_tasks.append(search_task)
                for t in search_tasks:
                    for job in job_list:
                        if job['job_hash'] == f'{str(t)}_{repeat}':
                            # Avoid duplicate task
                            continue
                    job_list.append(
                        {'job_hash': f'{str(t)}_{repeat}', 'func': self.tune, \
                         'args': [t, None, local_silent, 0]})

            pool = utils.MyExecutor(self.search_config['n_worker'])
            results = pool.exec(job_list)
            for r in results:
                if results[r].valid:
                    init_tasks.append(results[r])

        # Local search
        search_tasks = []
        for workload in self.workloads:
            search_task = SingleTask(design_list[0], workload, self.cst)
            search_tasks.append(search_task)
        if search_task_configs:
            search_tasks[0].configs = search_task_configs
        search_task = MultiTask(design_list, search_tasks, self.cst, split=1)
        meta = {'partition_candidates': partition_candidates,                
                'design_idx_list': design_idx_list,
                'init_partition_candidates': init_partition_candidates,                
                "batch_size": self.search_config["batch_size"]}
        search_record = self.tune(search_task, init_tasks, silent=silent, meta=meta)

        best_record.update(search_record)

        return best_record
