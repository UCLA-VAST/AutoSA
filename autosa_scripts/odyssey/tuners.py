import json
import numpy as np
import xgboost as xgb
import random
import sys
import shutil
import copy
import pprint
from bayes_opt import BayesianOptimization
import itertools
import csv
from scipy import optimize
import math
import time
from datetime import datetime
from collections import deque

import utils
from solver import off_chip_solver
from search_task import MultiTask, SingleTask

#import opentuner
#from opentuner import ConfigurationManipulator
#from opentuner import IntegerParameter
#from opentuner import MeasurementInterface
#from opentuner import Result
#from opentuner.search.manipulator import PowerOfTwoParameter
#
#from RL_utils import RLAgent, RLEnv

class Constraint(object):
    def __init__(self, cst_path):
        with open(cst_path) as f:
            data = json.load(f)
        # Update the constraints
        self.hw_cst = {}
        for res in data:
            self.hw_cst[res] = data[res]["total"] * data[res]["ratio"]
            self.hw_cst[f'{res}_total'] = data[res]["total"]

    def __repr__(self):
        ret = ""
        ret += f"b{int(self.hw_cst['BRAM18K'])}"
        ret += f"d{int(self.hw_cst['DSP'])}"
        ret += f"u{int(self.hw_cst['URAM'])}"
        return ret

class Tuner(object):
    def __init__(self, search_task, cst, search_obj, max_epoch, max_time, n_worker=1, silent=0, max=1):
        self.search_task = search_task
        self.cst = cst
        self.search_obj = search_obj
        self.max_epoch = max_epoch
        self.max_time = max_time
        self.max = max
        if self.max == 1:
            self.best_reward = 0
        else:
            self.best_reward = float('inf')
        self.best_reward_meta = None
        self.best_rewards = []
        self.best_rewards_time = []
        self.best_sol = None
        self.best_sol_cst = None
        self.last_update_epoch = -1
        self.best_search_record = utils.SearchRecord().reset()
        self.converge_time = 0
        self.silent = silent
        self.sub_task_silent = silent
        self.n_worker = n_worker
        # If multi-processing, silent the sub tasks
        if n_worker > 1:
            self.sub_task_silent = 1

    def log(self, str, force=0):
        """ If force is set to 1, we will print the log info regardless of the silence argument.
        """
        if not self.silent or force:
            import logging
            logger = logging.getLogger('AutoSA-Tuner')
            logger.info(str)
            sys.stdout.flush()

    def overuse_constraint(self, used_cst):
        if not used_cst:
            # If constraint doesn't exist, return True to exclude this design
            return True

        if used_cst['BRAM18K'] > self.cst.hw_cst['BRAM18K']:
            return True
        if used_cst['DSP'] > self.cst.hw_cst['DSP']:
            return True
        if used_cst['URAM'] > self.cst.hw_cst['URAM']:
            return True

        return False

def exhaustive_search(search_task, cst, search_obj, max_epochs, max_time, n_worker=1, silent=0, time_out=-1, pruning=0, profiling=0):
    if profiling:
        repeat_num = 3
    else:
        repeat_num = 1

    tuner_params = {
        "pruning": pruning,
        "DSP_thres": [0.95, 1.0]
    }

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = ExhaustiveTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            config_str = "_exhaustive"
            if pruning:
                config_str += "_pruning"

            config_str += f"_{search_task.design.name}"
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record

class ExhaustiveTuner(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name
        self.params_history = []

    def search(self):
        """ This tuner only works for GEMM (kernel3) """
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        def filter_non_power_of_two(x):
            if np.log2(x) != int(np.log2(x)):
                return True
            return False

        #print(self.cst.hw_cst["DSP"])

        i, j, k = self.search_task.workload["params"]["i"], self.search_task.workload["params"]["j"], self.search_task.workload["params"]["k"]
        if not self.params["pruning"]:
            for i_t1 in range(1, i + 1):
                for j_t1 in range(1, j + 1):
                    for k_t1 in range(1, k + 1):
                        for i_t2 in utils.get_divisors(int(i_t1), None):
                            for j_t2 in utils.get_divisors(int(j_t1), None):
                                for k_t2 in utils.get_divisors(int(min(k_t1,8)), filter_non_power_of_two):
                                    latency_factors = 1
                                    latency_factors *= i_t2
                                    latency_factors *= j_t2
                                    simd_factor = k_t2
                                    if latency_factors >= 8 * simd_factor:
                                    	continue
                                    params = {
                                        "i": i, "j": j, "k": k,
                                        "i_t1": i_t1, "j_t1": j_t1, "k_t1": k_t1,
                                        "i_t2": i_t2, "j_t2": j_t2, "k_t2": k_t2,
                                    }
                                    task_params = self.search_task.adjust_params(task_params)
                                    reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
                                    if self.overuse_constraint(used_constraint):
                                        reward = 0
                                    if reward > self.best_reward:
                                        self.best_reward = reward
                                        self.best_reward_meta = reward_meta
                                        self.best_sol_cst = used_constraint
                                        self.best_sol = task_params
                                        self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                                        #self.last_update_epoch = self.epoch
                                        #self.counter.update_counter('converge_time')
                                        self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)
                                    #self.best_rewards.append(self.best_reward)
                                    #self.counter.update_counter('time')
                                    #self.best_rewards_time.append(self.counter.get_counter('time'))

                                    #if self.stop_criteria == "epoch" and self.epoch > self.max_epoch:
                                    #    break
                                    #if self.stop_criteria == "time":
                                    #    self.counter.update_counter('time')
                                    #    if self.counter.get_counter('time') > self.max_time:
                                    #        break
        else:
            #for i_t1 in range(1, i + 1):
            #for i_t1 in range(int(i/6), int(i/2)):
            for i_t1 in range(200, 270):
                if i_t1 % 2 != 0:
                    continue
                #for j_t1 in range(1, j + 1):
                #for j_t1 in range(int(j/6), int(j/2)):
                for j_t1 in range(200, 270):
                    if j_t1 % 2 != 0:
                        continue
                    #for k_t1 in range(4, int(k/8)):
                    for k_t1 in range(16, 64):
                        if k_t1 % 2 != 0:
                            continue
                        for i_t2 in utils.get_divisors(int(i_t1), None):
                            if i_t2 % 2 != 0:
                                continue
                            for j_t2 in utils.get_divisors(int(j_t1), None):
                                if j_t2 % 2 != 0:
                                    continue
                                if (i_t1 / i_t2) * (j_t1 / j_t2) < 200:
                                    continue
                                if (i_t1 / i_t2) * (j_t1 / j_t2) > 240:
                                    continue
                                if 8 not in utils.get_divisors(int(min(k_t1,8))):
                                    continue
                                #if 4 not in utils.get_divisors(int(min(k_t1,8))):
                                #    continue
                                for k_t2 in [8]:
                                #for k_t2 in utils.get_divisors(int(min(k_t1,8)), filter_non_power_of_two):
                                    latency_factors = 1
                                    latency_factors *= i_t2
                                    latency_factors *= j_t2
                                    simd_factor = k_t2
                                    if latency_factors < 8 * simd_factor:
                                    	continue

                                    dsp_usage = (i_t1 / i_t2) * (j_t1 / j_t2) * k_t2 * 5
                                    if dsp_usage / self.cst.hw_cst["DSP"] < self.params["DSP_thres"][0] or \
                                       dsp_usage / self.cst.hw_cst["DSP"] > self.params["DSP_thres"][1]:
                                        continue

                                    task_params = {
                                        "i": i, "j": j, "k": k,
                                        "i_t1": i_t1, "j_t1": j_t1, "k_t1": k_t1,
                                        "i_t2": i_t2, "j_t2": j_t2, "k_t2": k_t2,
                                    }
                                    task_params = self.search_task.adjust_params(task_params)
                                    reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
                                    if self.overuse_constraint(used_constraint):
                                        reward = 0
                                    if reward > self.best_reward:
                                        self.best_reward = reward
                                        self.best_reward_meta = reward_meta
                                        self.best_sol_cst = used_constraint
                                        self.best_sol = task_params
                                        self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                                        self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)

def random_search(search_task, cst, search_obj, max_epochs, max_time, n_worker=1, silent=0, time_out=-1, pruning=0, profiling=0):
    if profiling:
        repeat_num = 3
    else:
        repeat_num = 1

    tuner_params = {
        "pruning": pruning,
        "DSP_thres": [0.6, 1.0]
    }

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = RandomTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            config_str = "_random"
            if pruning:
                config_str += "_pruning"
            
            config_str += f"_{search_task.design.name}"
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record

class RandomTuner(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name
        self.params_history = []

    def generate_random_sample(self):
        """ Generate a random sample from the design space.
        We bookkeeping all the searched params to avoid duplicated search.
        """
        duplicate = True
        cnt = 0
        task_params = None
        while duplicate:
            task_params = self.search_task.generate_random_sample()
            # Serialize the params
            params_hash = ""
            for k, v in task_params.items():
                params_hash += str(v)
            if params_hash not in self.params_history:
                duplicate = False
                self.params_history.append(params_hash)
            cnt += 1
            if cnt > 20:
                break

        return task_params

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        while True:            
            task_params = None
            if self.params["pruning"]:
                while True:
                    task_params = self.generate_random_sample()
                    if not task_params:
                        break
                    self.epoch += 1
                    self.best_rewards.append(self.best_reward)
                    self.counter.update_counter('time')
                    self.best_rewards_time.append(self.counter.get_counter('time'))

                    task_params = self.search_task.adjust_params(task_params)
                    task_params = self.search_task.design.infer_params(task_params)
                    dsp_usage = task_params["i_t1"] / task_params["i_t2"] * task_params["j_t1"] / task_params["j_t2"] * task_params["k_t2"] * 5
                    if task_params["k_t2"] == 8 and \
                       dsp_usage / self.cst.hw_cst["DSP"] >= self.params["DSP_thres"][0] and \
                       dsp_usage / self.cst.hw_cst["DSP"] <= self.params["DSP_thres"][1]:
                       break
                    '''
                    resource, _ = self.search_task.design.est_resource(task_params)
                    # Estimate the resource
                    if resource["DSP"] / self.cst.hw_cst["DSP"] >= self.params["DSP_thres"][0] and \
                       resource["DSP"] / self.cst.hw_cst["DSP"] <= self.params["DSP_thres"][1]:
                        break
                    '''
            else:
                task_params = self.generate_random_sample()
                self.epoch += 1
                self.best_rewards.append(self.best_reward)
                self.counter.update_counter('time')
                self.best_rewards_time.append(self.counter.get_counter('time'))
            if not task_params:
                # Design space is exhausted
                break
            task_params = self.search_task.adjust_params(task_params)
            reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
            if self.overuse_constraint(used_constraint):
                reward = 0
            if reward > self.best_reward:
                self.best_reward = reward
                self.best_reward_meta = reward_meta
                self.best_sol_cst = used_constraint
                self.best_sol = task_params
                self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                self.last_update_epoch = self.epoch
                self.counter.update_counter('converge_time')
                self.converge_time = self.counter.get_counter('converge_time')
                self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)            

            if self.stop_criteria == "epoch" and self.epoch > self.max_epoch:
                break
            if self.stop_criteria == "time":
                self.counter.update_counter('time')
                if self.counter.get_counter('time') > self.max_time:
                    break

        return

def annealing_search(search_task, cst, search_obj, max_epochs, max_time, n_worker=1, silent=0, time_out=-1, profiling=0):
    if profiling:
        repeat_num = 3
    else:
        repeat_num = 1

    tuner_params = {
        "T": 200,
        "stepsize": 16,
        "mutation_probability": 1.0,
        "epsilon": 0.1,
        "mutation_probs": [0.2, 0.8, 0],
        "max_latency": search_task.compute_ops()*10
    }

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = AnnealingTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            config_str = "_annealing"

            config_str += f"_{search_task.design.name}"
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record

class AnnealingTuner(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name

    def update(self, args):
        """ Optimization function
        """
        if (np.any(np.isnan(args))) or (np.any(np.isneginf(args))) or (np.any(np.isposinf(args))) or (np.any(args[:] == 0)):
            return self.params["max_latency"]
            #return float("inf")

        task_params = {}
        for p, param in self.search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = args[self.param_idx_map[param["name"]]]
        for p, param in self.search_task.design.params_config["external"].items():
            task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
        #print(args)
        #print(task_params)
        task_params = self.search_task.adjust_params(task_params)
        reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
        # SA minimizes the opt target
        if reward == 0:
            reward = self.params["max_latency"]
            #reward = float("inf")
        else:
            reward = 1 / reward
        if self.overuse_constraint(used_constraint):
            reward = self.params["max_latency"]
            #reward = float("inf")

        return reward

    def bound_check(self, f_new, x_new, f_old, x_old):
        """ Check if the parameters are legal.
        """
        self.epoch += 1
        self.best_rewards.append(self.best_reward)
        self.counter.update_counter('time')
        self.best_rewards_time.append(self.counter.get_counter('time'))

        task_params = {}
        for p, param in self.search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = x_new[self.param_idx_map[param["name"]]]
        for p, param in self.search_task.design.params_config["external"].items():
            task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
        task_params = self.search_task.adjust_params(task_params)
        task_params = self.search_task.design.infer_params(task_params)
        if task_params:
            status = self.search_task.design.bound_check(task_params)
            #print("bound_check: ", task_params, status)
            return status
        else:
            return False

    def print_minimal(self, x, f, accepted):
        """ Update the rewards when a local minimal is found.
        """
        task_params = {}
        for p, param in self.search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = x[self.param_idx_map[param["name"]]]
        for p, param in self.search_task.design.params_config["external"].items():
            task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
        task_params = self.search_task.adjust_params(task_params)
        reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
        if self.overuse_constraint(used_constraint):
            reward = 0
        if reward > self.best_reward:
            self.best_reward = reward
            self.best_reward_meta = reward_meta
            self.best_sol_cst = used_constraint
            self.best_sol = task_params
            self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
            self.last_update_epoch = self.epoch
            self.counter.update_counter('converge_time')
            self.converge_time = self.counter.get_counter('converge_time')
            self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)        

    def take_step(self, x):
        """ Step-taking routine.
        Note: Only for gemm.
        """
        '''
        s = self.params["stepsize"]
        x[0:3] += np.random.uniform(-max(1,s), max(1,s), 3)
        x[3:5] += np.random.uniform(-max(1,int(.5*s)), max(1,int(.5*s)), 2)
        x[5] += np.random.uniform(-max(1,int(.25*s)), max(1,int(.25*s)))
        x = np.array([int(a) if int(a) > 0 else 1 for a in x])
        '''
        # Reuse the genetic search mutation method
        if random.random() < self.params["mutation_probability"]:
            if random.random() < self.params["epsilon"]:
                task_params = self.search_task.generate_random_sample()
                for i in range(len(x)):
                    x[i] = task_params[self.idx_param_map[i]]
            else:
                idv = x
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                for p, param in self.search_task.design.params_config["external"].items():
                    task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
                # Build the chains
                # [{"params": [p0, p3, p7], "factors": [ceil(p0/p3), p3/p7, p7]}, {}]
                split_chains = []
                for p, param in self.search_task.design.params_config["external"].items():
                    chain = {"params": [param["name"]], "factors": []}
                    cur_param = param
                    while "split_by" in cur_param:
                        if "divisors" in self.search_task.design.params_config["tunable"][cur_param["split_by"]] \
                            and cur_param["name"] in self.search_task.design.params_config["tunable"][cur_param["split_by"]]["divisors"]:
                            div = 1
                        else:
                            div = 0
                        chain["params"].append(cur_param["split_by"])
                        if div:
                            factor = np.ceil(task_params[cur_param["name"]] / task_params[cur_param["split_by"]])
                        else:
                            factor = task_params[cur_param["name"]] / task_params[cur_param["split_by"]]
                        chain["factors"].append(max(1, int(factor)))
                        cur_param = self.search_task.design.params_config["tunable"][cur_param["split_by"]]
                    chain["factors"].append(max(1, int(task_params[cur_param["name"]])))
                    split_chains.append(chain)

                # Mutation
                for chain in split_chains:
                    if len(chain["factors"]) <= 1:
                        continue
                    if 'fix_param' in self.search_task.configs:
                        # Avoid mutating the fixed parameters
                        for fix_p in self.search_task.configs['fix_param']:
                            if fix_p[0] == chain['params'][0]:
                                continue
                    src_idx, dst_idx = random.sample(range(0, len(chain["factors"])), 2)
                    #mutation_policy_probs = [0.2, 0, 0.8] #
                    mutation_policy_probs = self.params["mutation_probs"]
                    mutation_policy_probs = np.cumsum(mutation_policy_probs)
                    #print(mutation_policy_probs)
                    select_prob = random.random()
                    if select_prob < mutation_policy_probs[0]:
                        # Random
                        if chain["factors"][dst_idx] == 1:
                            continue
                        """
                        inc_stride = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))
                        dec_stride = max(1, int(chain["factors"][dst_idx] - chain["factors"][src_idx] * chain["factors"][dst_idx] / (chain["factors"][src_idx] + inc_stride)))
                        chain["factors"][src_idx] += inc_stride
                        chain["factors"][dst_idx] -= dec_stride
                        chain["factors"][dst_idx] = max(1, chain["factors"][dst_idx])
                        """
                        src = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))
                        dst = max(1, math.ceil(chain["factors"][src_idx] * chain["factors"][dst_idx] / src))                        
                        chain["factors"][src_idx] = src
                        chain["factors"][dst_idx] = dst                    
                    elif select_prob < mutation_policy_probs[2]:
                        # Factorization
                        factor = chain["factors"][src_idx]
                        if factor == 1:
                            continue
                        divs = utils.factorization(factor)
                        div = random.choice(divs)
                        chain["factors"][src_idx] /= div
                        chain["factors"][dst_idx] *= div
                    else:
                        # Random
                        chain["factors"][src_idx] = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))

                # Revert to the params
                # [{"params": [p0, p3, p7], "factors": [ceil(p0/p3), p3/p7, p7]}, {}]
                for chain in split_chains:
                    factor = chain["factors"][-1]
                    param = chain["params"][-1]
                    if param in self.param_idx_map:
                        x[self.param_idx_map[param]] = factor
                    for idx in range(len(chain["factors"]) - 2, -1, -1):
                        param = chain["params"][idx]
                        factor *= chain["factors"][idx]
                        if param in self.param_idx_map:
                            x[self.param_idx_map[param]] = factor

        return x

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        # Init guess
        init_reward = 0
        init_params = None
        for i in range(5):
            task_params = self.search_task.generate_random_sample()
            task_params = self.search_task.adjust_params(task_params)
            reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
            if self.overuse_constraint(used_constraint):
                reward = 0
            if reward > init_reward:
                init_reward = reward
                init_params = task_params

        param_arr = []
        for p, param in self.search_task.design.params_config["tunable"].items():
            param_arr.append(task_params[param["name"]])
        x0 = np.array(param_arr)
        # Search
        optimize.basinhopping(self.update, x0, niter=self.max_epoch, \
                accept_test=self.bound_check,
                stepsize=self.params['stepsize'],
                T=self.params['T'], callback=self.print_minimal,
                take_step=self.take_step)

        return

def bayesian_search(search_task, cst, search_obj, max_epochs, max_time, n_worker=1, silent=0, time_out=-1, profiling=0):
    if profiling:
        repeat_num = 3
    else:
        repeat_num = 1

    tuner_params = {        
        "init_points": 10,
        "mutation_probability": 1.0,
        "epsilon": 0.1,
        "mutation_probs": [0.2, 0.8, 0],
        "max_latency": search_task.compute_ops()*10
    }

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = BayesianTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            config_str = "_bayesian"

            config_str += f"_{search_task.design.name}"
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record

class BayesianTuner(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name

    def black_box_function(self, i_t1, j_t1, k_t1, i_t2, j_t2, k_t2):        
        task_params = {
            "i_t1": int(i_t1), "j_t1": int(j_t1), "k_t1": int(k_t1),
            "i_t2": int(i_t2), "j_t2": int(j_t2), "k_t2": int(k_t2)
        }        

        #task_params = {}
        #for p, param in self.search_task.design.params_config["tunable"].items():
        #    task_params[param["name"]] = x_new[self.param_idx_map[param["name"]]]
        for p, param in self.search_task.design.params_config["external"].items():
            task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
        task_params = self.search_task.adjust_params(task_params)
        task_params = self.search_task.design.infer_params(task_params)
        if task_params:
            status = self.search_task.design.bound_check(task_params)            
            if not status:
                return 0
        else:
            return 0

        reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)        
        if self.overuse_constraint(used_constraint):
            return 0
        if reward > self.best_reward:
            self.best_reward = reward
            self.best_reward_meta = reward_meta
            self.best_sol_cst = used_constraint
            self.best_sol = task_params
            self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
            self.last_update_epoch = self.epoch
            self.counter.update_counter('converge_time')
            self.converge_time = self.counter.get_counter('converge_time')
            self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)        
        self.best_rewards.append(self.best_reward)
        self.counter.update_counter('time')
        self.best_rewards_time.append(self.counter.get_counter('time'))
        self.epoch += 1

        return reward

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        init_points = self.params["init_points"]
        # Only test for mm task
        pbounds = {'i_t1': (1, self.search_task.workload["params"]["i"]), 'j_t1': (1, self.search_task.workload["params"]["j"]), 'k_t1': (1, self.search_task.workload["params"]["k"]),\
                   'i_t2': (1, self.search_task.workload["params"]["i"]), 'j_t2': (1, self.search_task.workload["params"]["j"]), 'k_t2': (1, min(256 // self.search_task.dw, 64, self.search_task.workload["params"]["k"]))}
        
        optimizer = BayesianOptimization(
            f=self.black_box_function,
            pbounds=pbounds,
            #verbose=1,
            random_state=1,
        )

        optimizer.maximize(
            init_points=init_points,
            n_iter=self.max_epoch - init_points,
        )

        return

'''
def opentuner_search(search_task, cst, search_obj, max_epochs, max_time, solver=1, fixed_params=None, n_worker=1, silent=0, time_out=-1, profiling=0, args=None):
    if profiling:
        repeat_num = 3
    else:
        repeat_num = 1

    tuner_params = {
        "args": args
    }

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = OpenTunerInterface(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            config_str = "_opentuner"
            
            config_str += f"_{search_task.design.name}"
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record

class OpenTunerInterface(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name

    def init_args(self, args):
        args.bail_threshold = 500
        args.database = None
        args.display_frequency = None                
        args.generate_bandit_technique = False
        args.label = None
        args.list_techniques = False
        args.machine_class = None
        args.no_dups = True
        args.parallel_compile = False
        args.parallelism = 4
        args.pipelining = 0
        args.print_params = False
        args.print_search_space_size = False
        args.quiet = True
        args.results_log = None
        args.results_log_details = None
        args.seed_configuration = []
        if self.stop_criteria == "time":
            args.stop_after = self.max_time
        else:
            args.stop_after = None
        args.technique = None
        args.test_limit = 5000

        return args

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1
    
        opentuner_args = self.init_args(self.params["args"])
        opentuner = OpenTunerInstance(opentuner_args, self)
        opentuner.main(opentuner_args, self)

        return

class OpenTunerInstance(MeasurementInterface):
    def __init__(self, args, tuner):
        super().__init__(args)
        self.tuner = tuner

    def manipulator(self):
        """
        Define the search space by creating a
        ConfigurationManipulator
        """
        manipulator = ConfigurationManipulator()
        tuner = self.tuner

        manipulator.add_parameter(
            IntegerParameter('i_t1', 1, tuner.search_task.workload["params"]["i"]))
        manipulator.add_parameter(
            IntegerParameter('j_t1', 1, tuner.search_task.workload["params"]["j"]))
        manipulator.add_parameter(
            IntegerParameter('k_t1', 1, tuner.search_task.workload["params"]["k"]))
        manipulator.add_parameter(
            IntegerParameter('i_t2', 1, tuner.search_task.workload["params"]["i"]))
        manipulator.add_parameter(
            IntegerParameter('j_t2', 1, tuner.search_task.workload["params"]["j"]))
        manipulator.add_parameter(
            PowerOfTwoParameter('k_t2', 1, min(256 // tuner.search_task.dw, 64, tuner.search_task.workload["params"]["k"])))

        return manipulator

    def run(self, desired_result, input, limit):
        """
        Compile and run a given configuration then
        return performance
        """
        cfg = desired_result.configuration.data
        tuner = self.tuner

        x = [int(cfg['i_t1']), int(cfg['j_t1']), int(cfg['k_t1']),\
             int(cfg['i_t2']), int(cfg['j_t2']), int(cfg['k_t2'])]
        
        task_params = {}
        for p, param in tuner.search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = x[tuner.param_idx_map[param["name"]]]
        for p, param in tuner.search_task.design.params_config["external"].items():
            task_params[param["name"]] = tuner.search_task.workload["params"][param["name"]]
        task_params = tuner.search_task.adjust_params(task_params)
        task_params = tuner.search_task.design.infer_params(task_params)
        if task_params:
            status = tuner.search_task.design.bound_check(task_params)            
            if not status:
                return Result(state='ERROR', time=float('inf'))
        else:
            return Result(state='ERROR', time=float('inf'))

        reward, used_constraint, reward_meta = tuner.search_task.evaluate(task_params, tuner.search_obj)
        if tuner.overuse_constraint(used_constraint):
            return Result(state='ERROR', time=float('inf'))
        result = Result(time=1/reward)
        if reward > tuner.best_reward:
            tuner.best_reward = reward
            tuner.best_reward_meta = reward_meta
            tuner.best_sol_cst = used_constraint
            tuner.best_sol = task_params
            tuner.log(f'Epoch {tuner.epoch}: new best reward: {tuner.best_reward} ({1/tuner.best_reward:.0f})')
            tuner.last_update_epoch = tuner.epoch
            tuner.counter.update_counter('converge_time')
            tuner.converge_time = tuner.counter.get_counter('converge_time')
            tuner.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(tuner)
        tuner.best_rewards.append(tuner.best_reward)
        tuner.counter.update_counter('time')
        tuner.best_rewards_time.append(tuner.counter.get_counter('time'))
        tuner.epoch += 1

        return result

def RL_search(search_task, cst, search_obj, max_epochs, max_time, n_worker=1, silent=0, time_out=-1, profiling=0):
    if profiling:
        repeat_num = 3
    else:
        repeat_num = 1

    tuner_params = {                
        "eps": 0.0,
        "temperature": 1,
        "batch": 200
    }

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = RLTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            config_str = "_RL"
            
            config_str += f"_{search_task.design.name}"
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record    

class RLTuner(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name

        self.agent = None
        self.env = None

    def policy_gradient(self, n_episodes=100000, max_t=1000, print_every=10, eps=0, temperature=1):
        """
        n_episodes: number of training episodes
        print_every: maximal number of episodes to keep the record
        """
        best_score = -2**20
        scores_window = deque(maxlen=print_every)
        scores = []
        has_succeed_history = False
        for i_episode in range(n_episodes):
            # Adjust learning rate
            if i_episode % 100 == 0 and has_succeed_history:
                eps /= 1.2
                temperature /= 1.01
                temperature = max(temperature, 1)
                self.agent.adjust_lr(ratio=0.8, min_lr=1e-6)
            
            score = 0
            state, infos = self.env.reset()
            # Max number of attempts in one episode
            for t in range(max_t):
                # Generate one action
                action, log_prob = self.agent.act(state, infos, eps, temperature)
                # Get rewards from the env
                next_state, reward, done, infos, sig, impt = self.env.step(action)
                # Update the agent
                self.agent.step(state, action, log_prob, reward, next_state, done, sig, impt, infos)
                state = next_state
                score += infos["reward_raw"]
                if done:
                    break
            
            scores.append(score)
            if infos["succeed"]:
                has_succeed_history = True
                if score > self.best_reward:
                    self.best_reward = score
                    self.best_reward_meta = infos["reward_meta"]
                    self.best_sol_cst = infos["cst"]
                    self.best_sol = infos["sol"]
                    self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                    self.last_update_epoch = self.epoch
                    self.counter.update_counter('converge_time')
                    self.converge_time = self.counter.get_counter('converge_time')
                    self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)                
            self.best_rewards.append(self.best_reward)
            self.counter.update_counter('time')
            self.best_rewards_time.append(self.counter.get_counter('time'))
            self.epoch += 1

        return scores

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        # Dimension of the problem space (i, j, k)
        dim_size = 3
        # Dimension of the action vector (i_t1, j_t1, k_t1, i_t2, j_t2, k_t2)
        n_action_steps = 6
        # Level of each action step
        action_size = max(self.search_task.workload["params"]["i"], 
                          self.search_task.workload["params"]["j"], 
                          self.search_task.workload["params"]["k"])
        # Initialize agent and environment 
        self.agent = RLAgent(dim_size=dim_size, n_action_steps=n_action_steps, action_size=action_size, seed=random.randint(0, 2**63), batch=self.params['batch'])
        self.env = RLEnv(self.search_task, self.cst, self.param_idx_map, self.idx_param_map, self.search_obj,
                         dim_size=dim_size, n_action_steps=n_action_steps, action_size=action_size)                
        state = self.env.reset()
        self.agent.reset()

        scores = self.policy_gradient(n_episodes=self.max_epoch, eps=self.params['eps'], temperature=self.params['temperature'])

        return
'''

def genetic_search(search_task, cst, search_obj, max_epochs, max_time, solver=1, fixed_params=None, n_worker=1, silent=0, time_out=-1, profiling=0):
    """ Genetic search
    If solver is enabled, we will first call IPOPT solver to generate the initial params to
    kick off the genetic search.
    """
    if profiling:
        solver = 1        
        repeat_num = 3
    else:
        repeat_num = 1

    init_params = None
    #solver = 0
    if solver == 1:
        # Call IPOPT solver
        init_params = off_chip_solver(search_task, cst, fixed_params, save=1)
        #init_params = off_chip_solver(search_task, cst, fixed_params)
    #print(search_task)
    #print(init_params)
    
    if init_params:
        # Modify it to divisors
        param_idx_map = {}
        idx_param_map = {}
        idx = 0
        for p, param in search_task.design.params_config["tunable"].items():
            param_idx_map[param["name"]] = idx
            idx_param_map[idx] = param["name"]
            idx += 1
        import bisect
        task_params = {}
        for p, param in search_task.design.params_config["tunable"].items():
            task_params[param["name"]] = init_params[param_idx_map[param["name"]]]
        for p, param in search_task.design.params_config["external"].items():
            task_params[param["name"]] = search_task.workload["params"][param["name"]]
        # Fix the first-level
        #for p, param in search_task.design.params_config["external"].items():
        #    split_by_param = param["split_by"]
        #    choices = utils.get_divisors(int(task_params[p]), None)
        #    idx = bisect.bisect(choices, task_params[split_by_param])
        #    if idx >= len(choices):
        #        idx -= 1
        #    if idx > 1:
        #        if abs(choices[idx - 1] - task_params[split_by_param]) < abs(choices[idx] - task_params[split_by_param]):
        #            idx -= 1
        #    task_params[split_by_param] = choices[idx]

        ## Fix the first-level: make them multiple of 4 (for solver analysis)
        #for p, param in search_task.design.params_config["external"].items():
        #    split_by_param = param["split_by"]
        #    if split_by_param.startswith("k"):
        #        task_params[split_by_param] = int(task_params[split_by_param] / 16) * 16            

        # Fix the first-level: make them multiple of 2
        for p, param in search_task.design.params_config["external"].items():
            split_by_param = param["split_by"]            
            task_params[split_by_param] = int(task_params[split_by_param] / 2) * 2

        # Fix the second-level    
        def filter_non_power_of_two(x):
            if np.log2(x) != int(np.log2(x)):
                return True
            return False
        for p, param in search_task.design.params_config["tunable"].items():        
            if "divisors" in param:
                if "tags" in param and "power_of_two" in param["tags"]:
                    choices = utils.get_divisors(int(task_params[param["divisors"][0]]), filter_non_power_of_two)
                else:
                    choices = utils.get_divisors(int(task_params[param["divisors"][0]]), None)                
                idx = bisect.bisect(choices, task_params[p])
                if idx >= len(choices):
                    idx -= 1
                if idx > 1:
                    if abs(choices[idx - 1] - task_params[p]) < abs(choices[idx] - task_params[p]):
                        idx -= 1
                task_params[p] = choices[idx]
        init_params = []
        for p, param in search_task.design.params_config["tunable"].items():
            init_params.append(task_params[param["name"]])
        #print(init_params)
        #exit(0)        
    
    # comm
    #init_params = [1024, 1024, 256, 128, 128, 4] # [1024, 1024, 320, 128, 128, 4]
    # -comp
    #init_params = [512, 512, 256, 32, 32, 4] # [520, 520, 320, 26, 26, 4]
    # comm-comp
    #init_params = [1024, 1024, 256, 64, 64, 4] # [1024, 1024, 320, 64, 64, 4]
    # imperfect pruning
    #init_params = [512, 1024, 8, 512, 512, 8]

    mutation_probs_list = [
        [0, 1, 0],
        [0.2, 0.8, 0],
        [0.4, 0.6, 0],
        [0.6, 0.4, 0],
        [0.8, 0.2, 0],
        [1, 0, 0],
        #[0, 0.8, 0.2],
        #[0, 0.6, 0.4],
        #[0, 0.4, 0.6],
        #[0, 0.2, 0.8],
        #[0, 0, 1]
    ]

    tuner_params = {
        "population_size": 200,\
        "mutation_probability": 0.5,\
        "parents_ratio": 0.3,\
        "epsilon": 0.1,\
        #"epsilon": 0,\
        "ancestor": init_params,\
        "fixed_params": fixed_params,\
        "time_out": time_out,
        "mutation_probs": mutation_probs_list[1]        
        #"mutation_probs": mutation_probs_list[0]
    }

    #print(tuner_params)

    best_record = utils.SearchRecord().reset()
    for repeat in range(repeat_num):
        tuner = GeneticTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
        tuner.search()

        search_record = tuner.best_search_record
        best_record.update(search_record)

        if profiling:
            # Mutation methods
            #config_str = ""
            #for p in tuner_params["mutation_probs"]:
            #    config_str += "_"
            #    config_str += str(p)

            # Solver
            #config_str = "_comm_div_comp"
            #config_str = "_no_solver"
            #config_str = "_comm"
            #config_str = "_comp"
            config_str = "_comm_minus_comp"

            # Hardware Model
            #config_str = "_baseline"
            #config_str = "_divisor_only"
            #config_str = "_simplified_model"

            # Search Method
            #config_str = "_genetic"            
            #config_str += f"_{search_task.design.name}"

            # Dataflow
            #config_str = f"_{search_task.workload['name']}_{search_obj}_{search_task.design.name}"
            
            config_str += f"_r{repeat}"
            with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
                fieldnames = ['epoch', 'reward', 'time']
                writer = csv.DictWriter(f, fieldnames=fieldnames)
                writer.writeheader()
                for epoch in range(len(tuner.best_rewards)):
                    writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return best_record

class GeneticTuner(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.param_idx_map = {} # Maps parameter name to its index in the sample
        self.idx_param_map = {} # Maps the index to the parameter name

    def select_parents(self, population, fitness, num_parents):
        """ Select "num_parents" parents with the highest fitness score.
        """
        fitness_idx_sorted = np.argsort(-fitness)
        parents = population[fitness_idx_sorted[:num_parents]][:]
        return parents

    def crossover(self, pool, num_children):
        """ Perform single-point crossover.
        """
        children = np.empty((num_children, len(self.search_task.design.params_config["tunable"])))
        # Build the parameter dependecy chain
        param_deps = {} # ["param": "dependent_param (multiple of this parameter)"]
        param_cnt = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            if "divisors" in param:
                param_deps[param["name"]] = param["divisors"][0]
                param_cnt += 2
        if param_cnt != len(self.search_task.design.params_config["tunable"]):
            raise RuntimeError("Not all tuning parameters can be handled by crossover")
        for i in range(num_children):
            parents_idx = [i % pool.shape[0], np.random.randint(0, pool.shape[0])]
            for param in param_deps:
                idx = np.random.randint(0, 2)
                children[i][self.param_idx_map[param]] = pool[parents_idx[idx]][self.param_idx_map[param]]
                children[i][self.param_idx_map[param_deps[param]]] = pool[parents_idx[idx]][self.param_idx_map[param_deps[param]]]

        return children

    def mutation(self, pool):
        """ Perform mutation
        """
        for p_idx in range(pool.shape[0]):
            if random.random() < self.params["mutation_probability"]:
                if random.random() < self.params["epsilon"]:
                    task_params = self.search_task.generate_random_sample()
                    for i in range(pool.shape[1]):
                        pool[p_idx][i] = task_params[self.idx_param_map[i]]
                else:
                    idv = pool[p_idx][:]
                    task_params = {}
                    for p, param in self.search_task.design.params_config["tunable"].items():
                        task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                    for p, param in self.search_task.design.params_config["external"].items():
                        task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
                    # Build the chains
                    # [{"params": [p0, p3, p7], "factors": [ceil(p0/p3), p3/p7, p7]}, {}]
                    split_chains = []
                    for p, param in self.search_task.design.params_config["external"].items():
                        chain = {"params": [param["name"]], "factors": []}
                        cur_param = param
                        while "split_by" in cur_param:
                            if "divisors" in self.search_task.design.params_config["tunable"][cur_param["split_by"]] \
                                and cur_param["name"] in self.search_task.design.params_config["tunable"][cur_param["split_by"]]["divisors"]:
                                div = 1
                            else:
                                div = 0
                            chain["params"].append(cur_param["split_by"])
                            if div:
                                factor = np.ceil(task_params[cur_param["name"]] / task_params[cur_param["split_by"]])
                            else:
                                factor = task_params[cur_param["name"]] / task_params[cur_param["split_by"]]
                            chain["factors"].append(max(1, int(factor)))
                            cur_param = self.search_task.design.params_config["tunable"][cur_param["split_by"]]
                        chain["factors"].append(max(1, int(task_params[cur_param["name"]])))
                        split_chains.append(chain)

                    # Mutation
                    for chain in split_chains:
                        if len(chain["factors"]) <= 1:
                            continue
                        if 'fix_param' in self.search_task.configs:
                            # Avoid mutating the fixed parameters
                            for fix_p in self.search_task.configs['fix_param']:
                                if fix_p[0] == chain['params'][0]:
                                    continue
                        src_idx, dst_idx = random.sample(range(0, len(chain["factors"])), 2)                        
                        #src_idx, dst_idx = random.sample(range(1, len(chain["factors"])), 2)
                        mutation_policy_probs = self.params["mutation_probs"]
                        mutation_policy_probs = np.cumsum(mutation_policy_probs)
                        #print(mutation_policy_probs)
                        select_prob = random.random()
                        if select_prob < mutation_policy_probs[0]:
                            # Random
                            if chain["factors"][dst_idx] == 1:
                                continue
                            """
                            inc_stride = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))
                            dec_stride = max(1, int(chain["factors"][dst_idx] - chain["factors"][src_idx] * chain["factors"][dst_idx] / (chain["factors"][src_idx] + inc_stride)))
                            chain["factors"][src_idx] += inc_stride
                            chain["factors"][dst_idx] -= dec_stride
                            chain["factors"][dst_idx] = max(1, chain["factors"][dst_idx])
                            """
                            #src = chain["factors"][src_idx] + max(1, int(chain["factors"][src_idx] * random.random() * 1.0))
                            src = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))
                            dst = max(1, math.ceil(chain["factors"][src_idx] * chain["factors"][dst_idx] / src))
                            chain["factors"][src_idx] = src
                            chain["factors"][dst_idx] = dst                        
                        elif select_prob < mutation_policy_probs[1]:
                            # Factorization
                            factor = chain["factors"][src_idx]
                            if factor == 1:
                                continue
                            divs = utils.factorization(factor)
                            div = random.choice(divs)
                            chain["factors"][src_idx] /= div
                            chain["factors"][dst_idx] *= div
                        else:
                            # Random (single)
                            chain["factors"][src_idx] = max(1, int(chain["factors"][src_idx] * random.random() * 1.0))

                    # Revert to the params
                    # [{"params": [p0, p3, p7], "factors": [ceil(p0/p3), p3/p7, p7]}, {}]
                    for chain in split_chains:
                        factor = chain["factors"][-1]
                        param = chain["params"][-1]
                        if param in self.param_idx_map:
                            pool[p_idx][self.param_idx_map[param]] = factor
                        for idx in range(len(chain["factors"]) - 2, -1, -1):
                            param = chain["params"][idx]
                            factor *= chain["factors"][idx]
                            if param in self.param_idx_map:
                                pool[p_idx][self.param_idx_map[param]] = factor

        return pool

    def search(self):
        """ Search the design space using genetic algorithms.

        The algorithm is configured by several parameters.
        @ population_size: the number of trial solutions in each epoch.
        @ mutation_probability: the chance of each gene in each individual solution
        to be replaced by a random value.
        @ crossover_probability: the chance of an existed solution to pass its genome
        to new trial solutions.
        @ parents_ratio: the ratio of population filled by the members of the previous
        generation.
        """
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0
        # Internal testing
        #local_reward = 0

        # Init the stats
        num_pop = int(self.params["population_size"])
        num_gen = int(self.max_epoch // num_pop)
        num_parents = int(num_pop * self.params["parents_ratio"])
        self.log(f'Number of generations: {num_gen}')
        self.log(f'Number of population: {num_pop}')
        self.log(f'Number of parents: {num_parents}')

        # Init the population
        population = np.empty((num_pop, len(self.search_task.design.params_config["tunable"])), dtype=int)
        if "ancestor" in self.params and self.params["ancestor"] != None:
            # Initialize the population with the ancestor
            ancestor = self.params["ancestor"]
            task_params = {}
            idx = 0
            for p, param in self.search_task.design.params_config["external"].items():
                task_params[param["split_by"]] = ancestor[idx]
                idx += 1
            # Note: We assume only up to two-level tiling
            for p, param in self.search_task.design.params_config["external"].items():
                task_params[self.search_task.design.params_config["tunable"][param["split_by"]]["split_by"]] = ancestor[idx]
                idx += 1
            #print(task_params)
            task_params = self.search_task.adjust_params(task_params)
            #print(task_params)
            param_arr = []
            for p, param in self.search_task.design.params_config["tunable"].items():
                param_arr.append(task_params[param["name"]])
            for i in range(num_pop):
                population[i] = np.array(param_arr, dtype=int)
        else:
            # Initialize the population randomly
            pop_cnt = 0
            while pop_cnt < num_pop:
                task_params = self.search_task.generate_random_sample()
                param_arr = []
                for p, param in self.search_task.design.params_config["tunable"].items():
                    param_arr.append(task_params[param["name"]])
                population[pop_cnt] = np.array(param_arr, dtype=int)
                pop_cnt += 1
        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        fitness = np.empty(num_pop, dtype=float)

        terminate = False
        while True:
            if self.epoch > 0:
                # Select the parents
                parents = self.select_parents(population, fitness, num_parents)
                if parents.shape[0] == 0:
                    break
                # Crossover
                children = self.crossover(parents, num_pop - parents.shape[0])
                # Mutation
                children = self.mutation(children)
                # Compose the new generation
                population[0:parents.shape[0], :] = parents
                population[parents.shape[0]:, :] = children

            # Update the fitness
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                for p, param in self.search_task.design.params_config["external"].items():
                    task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
                task_params = self.search_task.adjust_params(task_params)
                reward, used_constraint, reward_meta = self.search_task.evaluate(task_params, self.search_obj)
                #print(reward, used_constraint)
                #pprint.pprint(reward_meta)
                #print(task_params)
                #exit(0)
                if self.overuse_constraint(used_constraint):
                    reward = 0
                # Internal testing
                #reward_old = reward
                #if reward:
                #    latency_tmp = 0
                #    for lat in reward_meta["latency"]["latency_main"]:
                #        latency_tmp = max(latency_tmp, reward_meta["latency"]["latency_main"][lat])
                #    reward = 1 / latency_tmp

                fitness[i] = reward
                # Update the record
                if reward > self.best_reward:
                    self.best_reward = reward
                    self.best_reward_meta = reward_meta
                    self.best_sol_cst = used_constraint
                    self.best_sol = task_params
                    self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.3f})')
                    self.last_update_epoch = self.epoch
                    self.counter.update_counter('converge_time')
                    self.converge_time = self.counter.get_counter('converge_time')
                    self.best_search_record = utils.SearchRecord().extract_from_tuner_single_acc(self)
                    #print(self.best_search_record)
                    #exit(0)
                self.best_rewards.append(self.best_reward)
                self.counter.update_counter('time')
                self.best_rewards_time.append(self.counter.get_counter('time'))

                # Internal testing
                #if reward_old > local_reward:
                #    local_reward = reward_old
                #self.best_rewards.append(local_reward)

                self.epoch += 1
                self.counter.update_counter('time')
                if self.params['time_out'] > 0:
                    if self.counter.get_counter('time') - self.counter.get_counter('converge_time'):
                        # If the results are not improved after certain period of time, timeout
                        terminate = True
            if self.stop_criteria == "epoch" and self.epoch > self.max_epoch:
                break
            if self.stop_criteria == "time":
                self.counter.update_counter('time')
                if self.counter.get_counter('time') > self.max_time:
                    break
            if terminate:
                break

        return

def non_fuse_genetic_search(search_task, init_tasks, cst, search_obj, max_epochs, max_time, \
                            n_worker=1, silent=0, population_size=20, policy=0, meta=None):
    """ This function finds the best array architecture for a list of tasks.
    Init_tasks include the search records for each single task.
    Policy 0: Allocate the init population based on the achieved throughput of each task.
    Policy 1: Allocate the init population uniformly.
    """
    import logging
    logger = logging.getLogger('AutoSA-Tuner')
    if silent == 0:
        logger.info("Performing cross layer non-fusion genetic search...")

    # Internal use for profiling the init population
    #logger.info('Init tasks')
    #policy = 1
    #for task in init_tasks:
    #    logger.info(f'{task.to_str()}')

    #import pickle    
    #pickle.dump(init_tasks, open(f'tmp/{search_task.design.name}_init_tasks', 'wb'))
    #init_tasks = pickle.load(open(f'tmp/{search_task.design.name}_init_tasks', 'rb'))

    # Extract the init popluation allocation information
    init_pop_record = []
    for record in init_tasks:
        task_hash = record.task_sols[0]['hash']
        init_pop_record.append({
            'latency': record.latency,
            'ops': record.task_sols[0]['ops'],
            'params': record.task_sols[0]['sol'],
            'flops': record.task_sols[0]['ops'] / record.latency
        })

    best_latency = utils.compute_tasks_latency(search_task.tasks, init_tasks)
    if silent == 0:
        logger.info(f'Cross-layer non-fusion ideal latency: {best_latency}')

    if policy == 0:
        # Sort the records by flops and prune the ones with low throughput.
        # The heuristic here is that the arch solution with higher throughput
        # can potentially deliver the best performance for the entire network.
        thres = 0.5
        def takeFLOPS(elem):
            return elem['flops']
        init_pop_record.sort(key=takeFLOPS, reverse=True)
        prune_idx = len(init_pop_record)
        prune_flops = init_pop_record[0]['flops'] * thres
        for i in range(len(init_pop_record)):
            if init_pop_record[i]['flops'] < prune_flops:
                prune_idx = i
                break
        init_pop_record = init_pop_record[:prune_idx]
    elif policy == 1:
        random.shuffle(init_pop_record)    

    tuner_params = {
        "population_size": max(population_size, len(init_pop_record)),
        "mutation_probability": 0.7,
        "parents_ratio": 0.3,
        "hw_parents_ratio": 0.1, # Maintain the best parents found by the hw models
        "epsilon": 0.05,
        "mutation_probs": [0.2, 0.8, 0],
        "policy": policy,
        "init_pop": init_pop_record,
        "unit_max_epoch": 0,
        "unit_max_time": max_time,
        "best_reward": 1 / best_latency,
        "best_reward_thres": 0.95, # Terminate if the reward is within xx% compared to the best reward
        "use_ml_model": 1,
        "model_gens": meta["xgb_params"]["n_gens"], # Switch to real estimates after every x gens
        "prune_params": {
            "reward_thres": 10, # Prune parents that is x worse than the best
            "xgb_n_turns": population_size, # Use XGBoost model after x epochs
            "xgb_thres": meta["xgb_params"]["thres"], # Prune designs below x of the ideal reward
            "xgb_thres_adjust": meta["xgb_params"]["thres_adjust"] # Adjust the updated threshold by x
        },
        "one_gen": meta["one_gen"] if meta else False # Only explore for one generation
    }    

    if max_epochs > 0:
        pass
    else:
        max_time *= (len(search_task.tasks) * tuner_params["population_size"] * 3)
        max_time = min(max_time, 180) # 3 min at most

    # Uncomment below if profiling the cost model
    #tuner_params["best_reward_thres"] = 1
    #tuner_params["prune_params"]["xbg_thres"] = 0
    #tuner_params["policy"] = 2
    #tuner_params["one_gen"] = 0
    #max_time = 1800 # 30min

    # Uncomment below if comparing methods
    #tuner_params["best_reward_thres"] = 2
    #tuner_params["use_ml_model"] = 1
    #max_time = 180 # 3min

    tuner = MultiWorkloadArrayGeneticTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
    tuner.search()

    # Uncomment below if profiling the cost model
    #np.savetxt('tmp/cost_model_samples.csv', tuner.bst_data['data'], delimiter=',')

    search_record = tuner.best_search_record
    # Internal use for method comparison
    #config_str= "thrpt_init"    
    #if tuner_params["use_ml_model"]:
    #    config_str += "_ml_"
    #    #config_str += f"{meta['xgb_params']['n_gens']}_{meta['xgb_params']['thres']}_{meta['xgb_params']['thres_adjust']}"
    #else:
    #    config_str += "_no_ml_"
    #config_str += f"{search_task.design.name}"        

    #with open(f"tmp/tuning_rewards_{config_str}.csv", "w", newline='') as f:
    #    fieldnames = ['epoch', 'reward', 'time']
    #    writer = csv.DictWriter(f, fieldnames=fieldnames)
    #    writer.writeheader()
    #    for epoch in range(len(tuner.best_rewards)):
    #        writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return search_record

class MultiWorkloadArrayGeneticTuner(GeneticTuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, params, n_worker=n_worker, silent=silent)
        self.search_cache = {} # Avoid search duplicate sample
        self.bst_data = {'num': 0, 'valid': 0, 'data': None} # Boost tree information
        self.bst = None # Boost tree
        self.gen = 0
        self.best_hw_sols = []

    def xgboost_add_sample(self, sol, cst, reward):
        """ Add the training sample into the training set.
        """
        feature = []
        for p, param in self.search_task.design.params_config['tunable'].items():
            feature.append(sol[param['name']])
        for dim in cst['dims']:
            feature.append(dim)
        feature.append(cst['SIMD'])
        feature.append(cst['resource']['BRAM18K'])
        feature.append(cst['resource']['DSP'])
        for arr in cst['data_pack']:
            for dp in cst['data_pack'][arr]:
                feature.append(dp)
        feature.append(reward)
        if self.bst_data['num'] == 0:
            self.bst_data['data'] = np.array([feature])
        else:
            self.bst_data['data'] = np.append(
                self.bst_data['data'],
                np.array([feature]), axis=0
            )

        self.bst_data['num'] += 1

    def xgboost_train(self):
        """ Train the XGBoost model.
        """
        if self.bst_data['num'] == 0:
            return

        # Build the training set
        data = self.bst_data['data'][:, :self.bst_data['data'].shape[1] - 1]
        label = self.bst_data['data'][:, self.bst_data['data'].shape[1] - 1].flatten()
        if len(label) == 0:
            return

        dtrain = xgb.DMatrix(data, label=label)
        param = {'objective':'reg:squarederror', 'nthread': 1}
        num_round = 10
        self.bst = xgb.train(param, dtrain, num_round)

        # Disable it when profiling the cost model
        if self.bst_data['num'] >= self.params['prune_params']['xgb_n_turns']:
            self.bst_data['valid'] = 1

    def xgboost_predict(self, sol, cst):
        preds = None
        if self.bst:
            feature = []
            for p, param in self.search_task.design.params_config['tunable'].items():
                feature.append(sol[param['name']])
            for dim in cst['dims']:
                feature.append(dim)
            feature.append(cst['SIMD'])
            feature.append(cst['resource']['BRAM18K'])
            feature.append(cst['resource']['DSP'])
            for arr in cst['data_pack']:
                for dp in cst['data_pack'][arr]:
                    feature.append(dp)

            data = np.array([feature])
            dtest = xgb.DMatrix(data)
            preds = self.bst.predict(dtest)[0]

        return preds

    def xgboost_prune(self, sol, cst):
        """ Prune the solution by XGBoost model
        """
        pred = self.xgboost_predict(sol, cst)
        if pred and self.bst_data['valid'] == 1:
            if pred < self.params['prune_params']['xgb_thres']:
                return True
        return False

    def select_parents(self, population, fitness, num_parents, num_hw_parents):
        """ Select "num_parents" parents with the highest fitness score.
        If num_hw_parents > 0, enlist the best hw solutions
        """
        fitness_idx_sorted = np.argsort(-fitness)
        parents = population[fitness_idx_sorted[:num_parents]][:]

        sorted_fitness = fitness[fitness_idx_sorted[:num_parents]]
        # Remove illegal parents
        cut_idx = 0
        while cut_idx < len(parents) and sorted_fitness[cut_idx] > 0:
            cut_idx += 1
        parents = parents[:cut_idx][:]

        # Remove parents with low performance
        cut_idx = 0
        while cut_idx < len(parents) and \
              sorted_fitness[cut_idx] > sorted_fitness[0] / self.params['prune_params']['reward_thres']:
            cut_idx += 1
        parents = parents[:cut_idx][:]

        # Remove redundant parents
        cur_idx = 1
        if parents.shape[0] > 1:
            while cur_idx < parents.shape[0]:
                if np.array_equal(parents[cur_idx], parents[cur_idx - 1]):
                    parents = np.delete(parents, (cur_idx), axis=0)
                else:
                    cur_idx += 1

        if num_hw_parents > 0:
            num_hw_parents = min(num_hw_parents, len(self.best_hw_sols))
            hw_parents = np.zeros((num_hw_parents, parents.shape[1]))
            for i in range(num_hw_parents):
                hw_parents[i] = self.best_hw_sols[-1 - i]["idv"]
            #print(hw_parents)
            #print(parents)
            cur_idx = 0
            while cur_idx < hw_parents.shape[0]:
                redundant = False
                for i in range(parents.shape[0]):
                    if np.array_equal(parents[i], hw_parents[cur_idx]):
                        redundant = True
                        break
                if redundant:
                    hw_parents = np.delete(hw_parents, (cur_idx), axis=0)
                else:
                    cur_idx += 1
            parents = np.concatenate((hw_parents, parents))
            parents = parents[:num_parents][:]

        return parents

    def init_population(self, num_pop):
        population = np.empty((num_pop, len(self.search_task.design.params_config["tunable"])), dtype=int)
        if self.params["policy"] in [0, 1]:
            for i in range(num_pop):
                sol = self.params["init_pop"][i % len(self.params["init_pop"])]["params"]
                param_arr = []
                for p, param in self.search_task.design.params_config["tunable"].items():
                    param_arr.append(sol[param["name"]])
                population[i] = np.array(param_arr, dtype=int)
        else:
            raise RuntimeError("Unknown policy number.")

        return population

    def hash_params(self, sol):
        """ Hash the sample to string.
        """
        hash_str = ""
        for k, v in sol.items():
            hash_str += f'{k}{v}'
        return hash_str

    def search_design(self, arch_sol, use_model=0, bst=None):
        """ Search the optimal task configuration in the fixed array.
        """
        network_search_record = utils.SearchRecord(self.max).reset()
        # Update the hardware constraints
        search_task = copy.deepcopy(self.search_task)
        arch_cst = search_task.compute_arch_cst(arch_sol)
        search_task.set_arch_cst(arch_cst)
        search_task.set_arch_sol(arch_sol)

        job_list = []
        for task in search_task.tasks:
            job_list.append({
                'job_hash': str(task), 'func': genetic_search,
                'args': [task, self.cst, self.search_obj, self.params["unit_max_epoch"], self.params["unit_max_time"], 1, None, 1, self.sub_task_silent]
            })
        pool = utils.MyExecutor(max(int(self.n_worker/2), 2))
        results = pool.exec(job_list)
        for task in search_task.tasks:
            layer_record = results[str(task)]
            network_search_record = network_search_record.append(layer_record)

        network_search_record.cst = copy.deepcopy(arch_cst["resource"])

        return network_search_record

    def search(self):
        """ Search the design space using genetic algorithms.

        The algorithm is configured by several parameters.
        @ population_size: the number of trial solutions in each epoch.
        @ mutation_probability: the chance of each gene in each individual solution
        to be replaced by a random value.
        @ crossover_probability: the chance of an existed solution to pass its genome
        to new trial solutions.
        @ parents_ratio: the ratio of population filled by the members of the previous
        generation.
        """
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        # Init the stats
        num_pop = int(self.params["population_size"])
        num_gen = int(self.max_epoch // num_pop)
        num_parents = int(num_pop * self.params["parents_ratio"])
        if self.params["use_ml_model"]:
            num_hw_parents = int(num_pop * self.params["hw_parents_ratio"])
        else:
            num_hw_parents = 0
        self.log(f'Number of generations: {num_gen}')
        self.log(f'Number of population: {num_pop}')
        self.log(f'Number of parents: {num_parents}')
        self.log(f'Number of hw parents: {num_hw_parents}')

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        # Init the population
        population = self.init_population(num_pop)
        fitness = np.empty(num_pop, dtype=float)

        terminate = False
        while True:
            # Update the fitness
            use_model = self.params["use_ml_model"] and self.bst_data['valid'] and (self.gen % self.params['model_gens'] != 0)
            if self.epoch > 0:
                if use_model:
                    num_pop = int(self.params["population_size"]) * 4
                    population = np.resize(population, (num_pop, population.shape[1]))
                    fitness = np.resize(fitness, (num_pop))
                    num_parents = int(num_pop * self.params["parents_ratio"])
                else:
                    num_pop = int(self.params["population_size"])
                    population = np.resize(population, (num_pop, population.shape[1]))
                    fitness = np.resize(fitness, (num_pop))
                    num_parents = int(num_pop * self.params["parents_ratio"])
                if self.params["use_ml_model"] and not use_model and self.bst_data['valid']:
                    num_hw_parents = int(num_pop * self.params["hw_parents_ratio"])
                else:
                    num_hw_parents = 0

                # Select the parents
                parents = self.select_parents(population, fitness, num_parents, num_hw_parents)
                if parents.shape[0] == 0:
                    break
                # Crossover
                children = self.crossover(parents, num_pop - parents.shape[0])
                # Mutation
                children = self.mutation(children)
                # Compose the new generation
                population[0:parents.shape[0], :] = parents
                population[parents.shape[0]:, :] = children
                #if use_model:
                #    print("parents:")
                #    print(parents)
                #    print("children:")
                #    print(children)

            job_list = []
            results = {}
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                idv_hash = self.hash_params(task_params)
                for p, param in self.search_task.design.params_config["external"].items():
                    task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
                # Note: XGBoost model has compatibility problem with multi-processing.
                search_task = copy.deepcopy(self.search_task)
                # Compute the architecture features
                arch_cst = search_task.compute_arch_cst(task_params)
                if not use_model:
                    if idv_hash in self.search_cache:
                        continue
                    else:
                        search_record = utils.SearchRecord(self.max).reset()
                        if arch_cst:
                            if not self.xgboost_prune(task_params, arch_cst):
                                self.search_cache[idv_hash] = {'status': 'submit', 'value': None}
                                job_list.append({
                                    'job_hash': idv_hash,
                                    'func': self.search_design,
                                    'args': [task_params, use_model, copy.deepcopy(self.bst)]})
                            else:
                                results[idv_hash] = search_record
                        else:
                            results[idv_hash] = search_record
                else:
                    reward = 0
                    if arch_cst:
                        reward = self.xgboost_predict(task_params, arch_cst)
                    results[idv_hash] = reward

            if len(job_list) > 0:
                pool = utils.MyExecutor(self.n_worker)
                pool_results = pool.exec(job_list)
                for result in pool_results:
                    results[result] = pool_results[result]

            # Update the tuner results
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                idv_hash = self.hash_params(task_params)
                if use_model:
                    fitness[i] = results[idv_hash]
                else:
                    if idv_hash in self.search_cache and self.search_cache[idv_hash]['status'] == 'done':
                        fitness[i] = self.search_cache[idv_hash]['value']
                        continue
                    search_record = results[idv_hash]
                    if self.overuse_constraint(search_record.cst) or search_record.valid == 0:
                        search_record.reward = 0
                    #self.log(f'{search_record}')
                    if search_record.reward > 0:
                        if self.search_task.max_latency == -1 or \
                           (self.search_task.max_latency != -1 and (self.best_reward < 1 / self.search_task.max_latency)):
                           if search_record.reward > self.best_reward:
                                self.best_reward = search_record.reward
                                self.best_reward_meta = search_record.reward_meta
                                self.best_sol_cst = search_record.cst
                                self.best_sol = {"arch_sol": search_record.arch_sol, \
                                                 "task_sols": search_record.task_sols}
                                self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                                self.last_update_epoch = self.epoch
                                self.counter.update_counter('converge_time')
                                self.best_search_record = search_record
                                self.best_hw_sols.append({"idv": population[i], "reward": search_record.reward})
                                if self.best_reward >= self.params["best_reward"] * self.params["best_reward_thres"]:
                                    terminate = True
                        else:
                            # If max_latency is set, when the best search records
                            # fall less than the max_latency, the tuner will only
                            # update the records that use fewer memory resources.
                            if search_record.cst['BRAM18K'] < self.best_search_record.cst['BRAM18K']:
                                self.best_reward = search_record.reward
                                self.best_reward_meta = search_record.reward_meta
                                self.best_sol_cst = search_record.cst
                                self.best_sol = {"arch_sol": search_record.arch_sol, \
                                                 "task_sols": search_record.task_sols}
                                self.log(f'Epoch {self.epoch}: new best reward (less BRAM): {self.best_reward} ({1/self.best_reward:.0f})')
                                self.last_update_epoch = self.epoch
                                self.counter.update_counter('converge_time')
                                self.best_search_record = search_record
                                self.best_hw_sols.append({"idv": population[i], "reward": search_record.reward})
                                if self.best_reward >= self.params["best_reward"] * self.params["best_reward_thres"]:
                                    terminate = True

                    self.best_rewards.append(self.best_reward)
                    self.counter.update_counter('time')
                    self.best_rewards_time.append(self.counter.get_counter('time'))
                    fitness[i] = search_record.reward / self.params['best_reward']
                    self.search_cache[idv_hash] = {'status': 'done', 'value': fitness[i]}
                    if terminate:
                        break
                self.epoch += 1

            #if use_model:
            #    print("fitness")
            #    print(fitness)

            if self.params["one_gen"]:
                break

            if self.search_task.max_latency != -1 and self.best_search_record.latency < self.search_task.max_latency:
                break

            # Add training samples
            if not use_model and self.params["use_ml_model"]:
                for result in results:
                    search_record = results[result]
                    if self.params["best_reward"] and search_record.valid:
                        arch_cst = self.search_task.compute_arch_cst(search_record.arch_sol)
                        if search_record.reward > 0:
                            self.xgboost_add_sample(search_record.arch_sol, arch_cst, search_record.reward / self.params['best_reward'])
                        else:
                            self.xgboost_add_sample(search_record.arch_sol, arch_cst, 0)

            # Train the cost model
            if not use_model and self.params["use_ml_model"]:
                self.xgboost_train()
                # Adjust the cost model threshold dynamically
                if self.best_search_record.valid:
                    arch_sol = self.best_search_record.arch_sol
                    arch_cst = self.search_task.compute_arch_cst(arch_sol)
                    pred = self.xgboost_predict(arch_sol, arch_cst)
                    self.params['prune_params']['xgb_thres'] = pred * self.params['prune_params']['xgb_thres_adjust']
                    self.log(f'Updated XGB pruning thres: {self.params["prune_params"]["xgb_thres"]}')

            self.gen += 1
            # Uncomment it if profiling the cost model
            #print(self.bst_data['num'])

            if self.stop_criteria == "epoch" and self.epoch > self.max_epoch:
                break
            if self.stop_criteria == "time":
                self.counter.update_counter('time')
                if self.counter.get_counter('time') > self.max_time:
                    break
            if terminate:
                break

        return

def all_fuse_genetic_search(search_task, init_tasks, cst, search_obj, max_epochs, max_time, \
                            n_worker=1, silent=0, population_size=20, policy=0, explorer=None):
    """ This function finds the best array architecture for a list of tasks.
    Init_tasks include the search records for each single task.
    All the tasks are fused.
    Policy 0: We search the best config to minimize the latency of the last task, and
    use it as the array config to search for the best config for the rest of the layers.
    Then, we perform several epochs of genetic search on top of the arch config.
    """
    import logging
    logger = logging.getLogger('AutoSA-Tuner')
    if silent == 0:
        logger.info("Performing cross layer all-fusion genetic search...")

    # If init_tasks are provided, use them as the initial population,
    # otherwise, the architecture is fixed. Use the fixed arch sol instead.
    init_pop_record = []
    best_latency = None
    if search_task.fixed == 1:
        init_pop_record.append({
            'latency': -1, 'ops': -1, 'params': search_task.arch_sol
        })
        # Try to search for the last layer under the fixed constraints and add it
        # as the candidate sample
        last_task = copy.deepcopy(search_task.tasks[-1])
        last_task.fuse = 1
        last_task.last_fuse = 1
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
        local_silent = silent
        if silent == 0:
            local_silent = 1 if n_worker > 1 else 0
        job_list = []
        for repeat in range(3):
            job_list.append({'job_hash': f'{str(last_task)}_{repeat}', 'func': explorer.tune, \
                             'args': [last_task, None, local_silent, 0]})
        pool = utils.MyExecutor(n_worker)
        results = pool.exec(job_list)
        for r in results:
            if results[r].valid:
                init_pop_record.append({
                    'latency': -1, "ops": -1, 'params': results[r].task_sols[0]['sol']
                })
    else:
        for record in init_tasks:
            task_hash = record.task_sols[0]['hash']
            init_pop_record.append({
                'latency': record.latency,
                'ops': record.task_sols[0]['ops'],
                'params': record.task_sols[0]['sol'],
                'flops': record.task_sols[0]['ops'] / record.latency
            })

        best_latency = utils.compute_tasks_latency(search_task.tasks, init_tasks)
        if silent == 0:
            logger.info(f'Cross-layer all-fusion ideal latency: {best_latency}')

    tuner_params = {
        "population_size": max(population_size, len(init_pop_record)),
        "mutation_probability": 1.0,
        "parents_ratio": 0.2,
        "epsilon": 0.1,
        "policy": policy,
        "init_pop": init_pop_record,
        "unit_max_epoch": 0,
        "unit_max_time": max_time,
        "arch_fixed": search_task.fixed,
        "best_reward": 1 / best_latency if best_latency else None,
        "best_reward_thres": 0.95, # Terminate if the reward is within xx% compared to the best reward
        "model_gens": 10, # Switch to real estimates after every x gens
        "prune_params": {
            "reward_thres": 10, # Prune parents that is x worse than the best
            "xgb_n_turns": population_size / 2, # Use XGBoost model after x epochs
            "xgb_thres": 0.5, # Prune designs below x of the ideal reward
            "xgb_thres_adjust": 0.8 # Adjust the updated threshold by x
        }
    }

    if max_epochs > 0:
        pass
    else:
        max_time *= (len(search_task.tasks) * tuner_params["population_size"] * 3)
        #if tuner_params["arch_fixed"] == 1:
        #    max_time = min(max_time, 60) # 60 seconds at most
        #else:
        #    max_time = min(max_time, 120) # 120 seconds at most
        max_time = min(max_time, 120) # 120 seconds at most

    tuner = AllFuseGeneticTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
    tuner.search()

    search_record = tuner.best_search_record

    return search_record

class AllFuseGeneticTuner(MultiWorkloadArrayGeneticTuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, params, n_worker=n_worker, silent=silent)

    def init_population(self, num_pop):
        population = np.empty((num_pop, len(self.search_task.design.params_config["tunable"])), dtype=int)
        # Allocate uniformly
        for i in range(num_pop):
            sol = self.params["init_pop"][i % len(self.params["init_pop"])]["params"]
            param_arr = []
            for p, param in self.search_task.design.params_config["tunable"].items():
                param_arr.append(sol[param["name"]])
            population[i] = np.array(param_arr, dtype=int)

        return population

    def update_task_configs(self, tasks):
        """ Update the fusion task configurations.
        """
        for task_idx in range(len(tasks)):
            task = tasks[task_idx]
            task.fuse = 1
            if task_idx == len(tasks) - 1:
                task.last_fuse = 1
            if task_idx == 0:
                if task.use_uram == 0:
                    task.configs['cin_read_mode'] = 1 # load one time
                else:
                    task.configs['cin_read_mode'] = 0 # load in ping-pong fashion
            else:
                if task.use_uram == 0:
                    task.configs['cin_read_mode'] = 2 # load from on-chip BRAM buffers
                else:
                    task.configs['cin_read_mode'] = 3 # load from on-chip URAM buffers
            if task_idx == len(tasks) - 1:
                task.configs['cout_write_mode'] = 0 # write to off-chip memory
            else:
                task.configs['cout_write_mode'] = 1 # write to on-chip buffer
            if task_idx == len(tasks) - 1:
                task.set_aux_func('update_cin_latency', 'update_cin_latency_last')
                if task.use_uram == 0:
                    task.set_aux_func('update_cin_buf', 'update_cin_buf_bram_last')
                else:
                    task.set_aux_func('update_cin_buf', 'update_cin_buf_uram_last')
            else:
                task.set_aux_func('update_cin_latency', 'update_cin_latency')
                if task.use_uram == 0:
                    task.set_aux_func('update_cin_buf', 'update_cin_buf_bram')
                else:
                    task.set_aux_func('update_cin_buf', 'update_cin_buf_uram')

    def update_fused_task_dims(self, last_sol, last_task, cur_task, partial):
        """ Given the solution of the latter layer, update the workload dimensions of the
        current layer.
        For fused CNN, we have the or_t and oc_t from the latter layer.
        We will estimate the or_t' and oc_t' of the former layer by
        or_t' = or_t + k - 1
        oc_t' = oc_t + k - 1
        """
        if partial == 1:
            or_t = min(last_sol['r_t1'], last_task.workload['params']['r'])
            oc_t = min(last_sol['c_t1'], last_task.workload['params']['c'])
        else:
            or_t = last_task.workload['params']['r']
            oc_t = last_task.workload['params']['c']

        for tag in cur_task.workload['tags']:
            if tag.startswith('maxpool'):
                stride = int(tag.split('_')[-1])
                or_t *= stride
                oc_t *= stride
        k = cur_task.workload['params']['p']
        or_t_prev = or_t + k - 1
        oc_t_prev = oc_t + k - 1
        cur_task.workload['params']['r'] = or_t_prev
        cur_task.workload['params']['c'] = oc_t_prev

        return cur_task

    def est_latency(self, layer_stats, search_task, mode=0):
        """ Estimate the overall latency of the fused tasks.
        If mode is 1, the last task r/c are set to 1.
        """
        one_pass_latency = 0
        for task_id in range(len(search_task.tasks)):
            task = search_task.tasks[task_id]
            nxt_task_id = (task_id + 1) % len(search_task.tasks)
            if task_id == len(search_task.tasks) - 1:
                one_pass_latency += layer_stats[task_id].reward_meta['latency']['latency_main'] / \
                                    np.ceil(task.workload['params']['r'] / layer_stats[task_id].task_sols[0]['sol']['r_t1']) / \
                                    np.ceil(task.workload['params']['c'] / layer_stats[task_id].task_sols[0]['sol']['c_t1']) + \
                                    max(layer_stats[nxt_task_id].reward_meta['latency']['latency_prologue'], layer_stats[task_id].reward_meta['latency']['latency_epilogue'])
            else:
                one_pass_latency += layer_stats[task_id].reward_meta['latency']['latency_main'] + \
                                    max(layer_stats[nxt_task_id].reward_meta['latency']['latency_prologue'],
                                        layer_stats[task_id].reward_meta['latency']['latency_epilogue'])
        last_task = search_task.tasks[-1]
        if mode == 1:
            # Revert back
            last_task.workload["params"]['r'] = last_task.workload["params"]['old_r']
            last_task.workload["params"]['c'] = last_task.workload["params"]['old_c']

        total_latency = np.ceil(last_task.workload['params']['r'] / layer_stats[-1].task_sols[0]['sol']['r_t1']) * \
                        np.ceil(last_task.workload['params']['c'] / layer_stats[-1].task_sols[0]['sol']['c_t1']) * \
                        one_pass_latency
        total_latency += layer_stats[0].reward_meta['latency']['latency_prologue']

        return total_latency

    def est_off_chip_trans(self, layer_stats, search_task, mode=0):
        """ Compute the total off-chip transactions.
        """
        total_trans = 0
        one_pass_trans = 0
        for task_id in range(len(search_task.tasks) - 1):
            task = search_task.tasks[task_id]
            layer_stat = layer_stats[task_id]
            sol = layer_stat.task_sols[0]['sol']
            if task_id == 0:
                # Read cin, weights off-chip, write cout on-chip
                one_pass_trans += np.ceil(task.workload['params']['i'] / sol['i_t1']) * \
                                  np.ceil(task.workload['params']['o'] / sol['o_t1']) * \
                                  np.ceil(task.workload['params']['r'] / sol['r_t1']) * \
                                  np.ceil(task.workload['params']['c'] / sol['c_t1']) * \
                                  (sol['i_t1'] * sol['r_t1'] * sol['c_t1'] + sol['i_t1'] * sol['o_t1'] * task.workload['params']['p'] * task.workload['params']['q'])
            else:
                # Read cin on-chip, weights off-chip, write cout on-chip
                one_pass_trans += np.ceil(task.workload['params']['i'] / sol['i_t1']) * \
                                  np.ceil(task.workload['params']['o'] / sol['o_t1']) * \
                                  np.ceil(task.workload['params']['r'] / sol['r_t1']) * \
                                  np.ceil(task.workload['params']['c'] / sol['c_t1']) * \
                                  (sol['i_t1'] * sol['o_t1'] * task.workload['params']['p'] * task.workload['params']['q'])
        last_task = search_task.tasks[-1]
        if mode == 1:
            # Revert back
            last_task.workload["params"]['r'] = last_task.workload["params"]['old_r']
            last_task.workload["params"]['c'] = last_task.workload["params"]['old_c']

        total_trans = np.ceil(last_task.workload["params"]['r'] / sol['r_t1']) * \
                      np.ceil(last_task.workload["params"]['c'] / sol['c_t1']) * one_pass_trans
        # Last task, read cin on-chip, weights off-chip, write cout off-chip
        sol = layer_stats[-1].task_sols[0]['sol']
        total_trans += np.ceil(last_task.workload['params']['i'] / sol['i_t1']) * \
                       np.ceil(last_task.workload['params']['o'] / sol['o_t1']) * \
                       np.ceil(last_task.workload['params']['r'] / sol['r_t1']) * \
                       np.ceil(last_task.workload['params']['c'] / sol['c_t1']) * \
                       (sol['i_t1'] * sol['o_t1'] * task.workload['params']['p'] * task.workload['params']['q']) + \
                       np.ceil(last_task.workload['params']['o'] / sol['o_t1']) * \
                       np.ceil(last_task.workload['params']['r'] / sol['r_t1']) * \
                       np.ceil(last_task.workload['params']['c'] / sol['c_t1']) * \
                       sol['o_t1'] * sol['r_t1'] * sol['c_t1']

        return total_trans

    def search_fixed_design(self, last_layer_sol, use_model=0, bst=None):
        """ This function takes a fixed array and the solution of the last layer,
        searches the config of the rest of the layers.
        """
        network_search_record = utils.SearchRecord(self.max).reset()
        # Update the hardware constraints
        search_task = copy.deepcopy(self.search_task)
        # Update the task configs
        self.update_task_configs(search_task.tasks)

        # Update the workload parameters
        for p in search_task.tasks[-1].workload["params"]:
            last_layer_sol[p] = search_task.tasks[-1].workload["params"][p]

        last_sol = last_layer_sol
        last_task = search_task.tasks[-1]

        succeed = True
        layer_stats = []
        total_ops = 0
        for task in search_task.tasks:
            total_ops += task.compute_ops()
        # Build the record of the last layer
        reward, used_constraint, reward_meta = last_task.evaluate(last_layer_sol, self.search_obj)

        if self.overuse_constraint(used_constraint):
            reward = 0
            return network_search_record
        record = utils.SearchRecord(self.max).reset()
        record.valid = 1
        record.metric = self.search_obj
        record.cst = used_constraint
        record.reward = reward
        record.reward_meta = reward_meta
        record.latency = 1 / reward
        record.ops = last_task.compute_ops()
        record.task_names = [last_task.workload["name"]]
        record.arch_sol = last_task.arch_sol
        record.task_sols = [{
            "name": last_task.workload["name"],
            "hash": str(last_task),
            "ops": last_task.compute_ops(),
            "sol": last_layer_sol,
            "latency": record.latency,
            "DSP_eff": 0,
            #"reward_meta": reward_meta,
            "BW": 0
        }]
        record.records = None
        layer_stats.append(record)
        network_search_record = network_search_record.append(record)

        for task_idx in range(len(search_task.tasks) - 2, -1, -1):
            task = search_task.tasks[task_idx]
            # Update the task desp
            task = self.update_fused_task_dims(last_sol, last_task, task, 1 if task_idx == len(search_task.tasks) - 2 else 0)
            search_record = genetic_search(task, self.cst, self.search_obj, self.params["unit_max_epoch"], self.params["unit_max_time"], 1, None, 1, self.sub_task_silent)
            if search_record.valid == 0:
                succeed = False
                break
            last_sol = search_record.task_sols[0]['sol']
            last_task = task
            network_search_record = network_search_record.append(search_record)
            # Update the resource constraints
            if task.use_uram == 0:
                if search_record.cst["BRAM18K"] > network_search_record.cst["BRAM18K"]:
                    network_search_record.cst = search_record.cst
            else:
                if search_record.cst["URAM"] > network_search_record.cst["URAM"]:
                    network_search_record.cst = search_record.cst
            layer_stats.insert(0, search_record)

        network_search_record.fuse = 1
        if succeed:
            total_latency = self.est_latency(layer_stats, search_task)
            network_search_record.reward = 1 / total_latency
            network_search_record.latency = total_latency
        else:
            network_search_record.valid = 0

        return network_search_record

    def search_design1(self, arch_sol, use_model=0, bst=None):
        """ This function searches from the last layer, and uses the
        solution from the latter layer to allocate the fusion task of the previous layer.
        It tends to allocate large tiles for the latter layers, which may
        lead to large tiles for the early layers, resulting in no solution.
        """
        network_search_record = utils.SearchRecord(self.max).reset()
        # Update the hardware constraints
        search_task = copy.deepcopy(self.search_task)
        arch_cst = search_task.compute_arch_cst(arch_sol)
        search_task.set_arch_cst(arch_cst)
        search_task.set_arch_sol(arch_sol)

        last_sol = None
        last_task = None
        succeed = True
        layer_stats = []
        total_ops = 0
        for task in search_task.tasks:
            total_ops += task.compute_ops()

        # Update the task configs
        self.update_task_configs(search_task.tasks)

        for task_idx in range(len(search_task.tasks) - 1, -1, -1):
            task = search_task.tasks[task_idx]
            if task_idx < len(search_task.tasks) - 1:
                # Update the task desp
                task = self.update_fused_task_dims(last_sol, last_task, task, 1 if task_idx == len(search_task.tasks) - 2 else 0)
            search_record = genetic_search(task, self.cst, self.search_obj, self.params["unit_max_epoch"], self.params["unit_max_time"], 1, None, 1, self.sub_task_silent)
            if search_record.valid == 0:
                succeed = False
                break
            last_sol = search_record.task_sols[0]['sol']
            last_task = task
            network_search_record = network_search_record.append(search_record)
            # Update the resource constraints
            if task.use_uram == 0:
                if search_record.cst["BRAM18K"] > network_search_record.cst["BRAM18K"]:
                    network_search_record.cst = search_record.cst
            else:
                if search_record.cst["URAM"] > network_search_record.cst["URAM"]:
                    network_search_record.cst = search_record.cst
            layer_stats.insert(0, search_record)

        network_search_record.fuse = 1
        if succeed:
            total_latency = self.est_latency(layer_stats, search_task)
            total_off_chip_trans = self.est_off_chip_trans(layer_stats, search_task)
            network_search_record.reward = 1 / total_latency
            network_search_record.latency = total_latency
            network_search_record.ctc = total_ops / (total_off_chip_trans * search_task.dw)
        else:
            network_search_record.valid = 0

        return network_search_record

    def search_design2(self, arch_sol, use_model=0, bst=None):
        """ This function searches from the last layer, and uses the
        solution from the latter layer to allocate the fusion task of the previous layer.
        The tile size of the last layer is fixed to 1x1.
        """
        network_search_record = utils.SearchRecord(self.max).reset()
        # Update the hardware constraints
        search_task = copy.deepcopy(self.search_task)
        arch_cst = search_task.compute_arch_cst(arch_sol)
        search_task.set_arch_cst(arch_cst)
        search_task.set_arch_sol(arch_sol)

        last_sol = None
        last_task = None
        succeed = True
        layer_stats = []
        total_ops = 0
        for task in search_task.tasks:
            total_ops += task.compute_ops()

        # Update the task configs
        self.update_task_configs(search_task.tasks)

        for task_idx in range(len(search_task.tasks) - 1, -1, -1):
            task = search_task.tasks[task_idx]
            if task_idx == len(search_task.tasks) - 1:
                # Fix the r/c to 1
                task.workload["params"]['old_r'] = task.workload["params"]['r']
                task.workload["params"]['old_c'] = task.workload["params"]['c']
                task.workload["params"]['r'] = 1
                task.workload["params"]['c'] = 1
            else:
                # Update the task desp
                task = self.update_fused_task_dims(last_sol, last_task, task, 1 if task_idx == len(search_task.tasks) - 2 else 0)
            search_record = genetic_search(task, self.cst, self.search_obj, self.params["unit_max_epoch"], self.params["unit_max_time"], 1, None, 1, self.sub_task_silent)
            if search_record.valid == 0:
                succeed = False
                break
            last_sol = search_record.task_sols[0]['sol']
            last_task = task
            network_search_record = network_search_record.append(search_record)
            # Update the resource constraints
            if task.use_uram == 0:
                if search_record.cst["BRAM18K"] > network_search_record.cst["BRAM18K"]:
                    network_search_record.cst = search_record.cst
            else:
                if search_record.cst["URAM"] > network_search_record.cst["URAM"]:
                    network_search_record.cst = search_record.cst
            layer_stats.insert(0, search_record)

        network_search_record.fuse = 1
        if succeed:
            total_latency = self.est_latency(layer_stats, search_task, mode=1)
            total_off_chip_trans = self.est_off_chip_trans(layer_stats, search_task, mode=1)
            network_search_record.reward = 1 / total_latency
            network_search_record.latency = total_latency
            network_search_record.ctc = total_ops / (total_off_chip_trans * search_task.dw)
        else:
            network_search_record.valid = 0

        return network_search_record

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        # Init the stats
        num_pop = int(self.params["population_size"])
        num_gen = int(self.max_epoch // num_pop)
        num_parents = int(num_pop * self.params["parents_ratio"])
        self.log(f'Number of generations: {num_gen}')
        self.log(f'Number of population: {num_pop}')
        self.log(f'Number of parents: {num_parents}')

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        # Init the population
        population = self.init_population(num_pop)
        fitness = np.empty(num_pop, dtype=float)

        terminate = False
        while True:
            if self.epoch > 0:
                # Select the parents
                parents = self.select_parents(population, fitness, num_parents)
                if parents.shape[0] == 0:
                    break
                # Crossover
                children = self.crossover(parents, num_pop - parents.shape[0])
                # Mutation
                children = self.mutation(children)
                # Compose the new generation
                population[0:parents.shape[0], :] = parents
                population[parents.shape[0]:, :] = children

            # Update the fitness
            use_model = self.bst_data['valid'] and (self.gen % self.params['model_gens'] != 0)
            job_list = []
            results = {}
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                idv_hash = self.hash_params(task_params)
                for p, param in self.search_task.design.params_config["external"].items():
                    task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
                # Note: XGBoost model has compatibility problem with multi-processing.
                search_task = copy.deepcopy(self.search_task)
                # Compute the architecture features
                arch_cst = search_task.compute_arch_cst(task_params)
                if not use_model:
                    if idv_hash in self.search_cache:
                        continue
                    else:
                        search_record = utils.SearchRecord(self.max).reset()
                        if arch_cst:
                            if not self.xgboost_prune(task_params, arch_cst):
                                self.search_cache[idv_hash] = {'status': 'submit', 'value': None}
                                if self.params["arch_fixed"] == 0:
                                    job_list.append({
                                        'job_hash': idv_hash,
                                        'func': self.search_design1 if self.params['policy'] == 0 else self.search_design2,
                                        'args': [task_params, use_model, copy.deepcopy(self.bst)]})
                                else:
                                    job_list.append({
                                        'job_hash': idv_hash,
                                        'func': self.search_fixed_design,
                                        'args': [task_params, use_model, copy.deepcopy(self.bst)]})
                            else:
                                results[idv_hash] = search_record
                        else:
                            results[idv_hash] = search_record
                else:
                    reward = 0
                    if arch_cst:
                        reward = self.xgboost_predict(task_params, arch_cst)[0]
                    results[idv_hash] = reward

            if len(job_list) > 0:
                pool = utils.MyExecutor(self.n_worker)
                pool_results = pool.exec(job_list)
                for result in pool_results:
                    results[result] = pool_results[result]

            # Update the tuner results
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                idv_hash = self.hash_params(task_params)
                if use_model:
                    fitness[i] = results[idv_hash]
                else:
                    if idv_hash in self.search_cache and self.search_cache[idv_hash]['status'] == 'done':
                        fitness[i] = self.search_cache[idv_hash]['value']
                        continue
                    search_record = results[idv_hash]
                    if search_record.valid == 0 or self.overuse_constraint(search_record.cst):
                        search_record.reward = 0
                    if search_record.reward > 0:
                        if search_record.reward > self.best_reward:
                            self.best_reward = search_record.reward
                            self.best_reward_meta = search_record.reward_meta
                            self.best_sol_cst = search_record.cst
                            self.best_sol = {"arch_sol": search_record.arch_sol, \
                                             "task_sols": search_record.task_sols}
                            self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                            self.last_update_epoch = self.epoch
                            self.counter.update_counter('converge_time')
                            self.best_search_record = search_record
                            if self.params["arch_fixed"] == 1:
                                if not self.params["best_reward"]:
                                    self.params["best_reward"] = search_record.reward
                            else:
                                if self.best_reward >= self.params["best_reward"] * self.params["best_reward_thres"]:
                                    terminate = True

                    self.best_rewards.append(self.best_reward)
                    if not self.params["best_reward"]:
                        if search_record.reward == 0:
                            fitness[i] = 0
                        else:
                            raise RuntimeError("Best reward is not set.")
                    else:
                        fitness[i] = search_record.reward / self.params['best_reward']
                    self.search_cache[idv_hash] = {'status': 'done', 'value': fitness[i]}
                    if terminate:
                        break
                self.epoch += 1

            # Add training samples
            if not use_model:
                for result in results:
                    search_record = results[result]
                    if self.params["best_reward"] and search_record.valid:
                        arch_cst = self.search_task.compute_arch_cst(search_record.arch_sol)
                        if search_record.reward > 0:
                            self.xgboost_add_sample(search_record.arch_sol, arch_cst, search_record.reward / self.params['best_reward'])
                        else:
                            self.xgboost_add_sample(search_record.arch_sol, arch_cst, 0)

            # Train the cost model
            if not use_model:
                self.xgboost_train()
                # Adjust the cost model threshold dynamically
                if self.best_search_record.valid:
                    arch_sol = self.best_search_record.arch_sol
                    arch_cst = self.search_task.compute_arch_cst(arch_sol)
                    pred = self.xgboost_predict(arch_sol, arch_cst)
                    self.params['prune_params']['xgb_thres'] = pred * self.params['prune_params']['xgb_thres_adjust']
                    self.log(f'Updated XGB pruning thres: {self.params["prune_params"]["xgb_thres"]}')

            self.gen += 1

            #exit(0)
            if self.stop_criteria == "epoch" and self.epoch > self.max_epoch:
                break
            if self.stop_criteria == "time":
                self.counter.update_counter('time')
                if self.counter.get_counter('time') > self.max_time:
                    break
            if terminate:
                break

        return

def fuse_genetic_search(search_task, init_tasks, cst, search_obj, max_epochs, max_time, \
                        n_worker=1, silent=0, population_size=20, policy=0, meta=None, explorer=None):
    """ This function finds the best fused array architecture for a list of tasks.
    Init_tasks include the search records for each single task.
    """
    import logging
    logger = logging.getLogger('AutoSA-Tuner')
    if silent == 0:
        logger.info("Performing cross layer partial-fusion genetic search...")

    best_latency = utils.compute_tasks_latency(search_task.tasks, init_tasks)
    if silent == 0:
        logger.info(f'Cross-layer partial-fusion ideal latency: {best_latency}')

    thres = 0.5
    def takeFLOPS(elem):
        return elem['flops']
    multi_task_records = []
    single_task_records = []
    for record in init_tasks:
        if record.valid == 0:
            continue
        if len(record.task_sols) > 1:
            multi_task_records.append(record)
        else:
            single_task_records.append(record)
    init_pop_record = []
    for record in single_task_records:
        if record.valid == 0:
            continue
        init_pop_record.append({
            'latency': record.latency,
            'ops': record.task_sols[0]['ops'],
            'params': record.task_sols[0]['sol'],
            'flops': record.task_sols[0]['ops'] / record.latency
        })
    init_pop_record.sort(key=takeFLOPS, reverse=True)
    prune_idx = len(init_pop_record)
    prune_flops = init_pop_record[0]['flops'] * thres
    for i in range(len(init_pop_record)):
        if init_pop_record[i]['flops'] < prune_flops:
            prune_idx = i
            break
    init_pop_record = init_pop_record[:prune_idx]

    for record in multi_task_records:
        init_pop_record.insert(0, {
            'latency': record.latency,
            'ops': 0,
            'params': record.arch_sol
        })

    tuner_params = {
        "population_size": max(population_size, len(init_pop_record)),
        "mutation_probability": 1.0,
        "parents_ratio": 0.2,
        "epsilon": 0.1,
        "policy": policy,
        "init_pop": init_pop_record,
        "unit_max_epoch": 0,
        "unit_max_time": max_time,
        "explorer": explorer,
        "best_reward": 1 / best_latency if best_latency else None,
        "best_reward_thres": 0.95, # Terminate if the reward is within xx% compared to the best reward
        "model_gens": 10, # Switch to real estimates after every x gens
        "prune_params": {
            "reward_thres": 10, # Prune parents that is x worse than the best
            "xgb_n_turns": population_size / 2, # Use XGBoost model after x epochs
            "xgb_thres": 0.5, # Prune designs below x of the ideal reward
            "xgb_thres_adjust": 0.8 # Adjust the updated threshold by x
        }
    }

    if meta:
        tuner_params["fusion_candidates"] = meta["fusion_candidates"]

    if max_epochs > 0:
        pass
    else:
        max_time *= (len(search_task.tasks) * tuner_params["population_size"] * 3)
        max_time = min(max_time, 600) # 600 seconds at most

    tuner = FuseGeneticTuner(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
    tuner.search()

    search_record = tuner.best_search_record

    return search_record

class FuseDPTuner(object):
    def __init__(self, config, tasks, cst, n_worker=1):
        self.config = config
        self.tasks = tasks
        self.cst = cst
        self.n_worker = n_worker

    def hash_dp_task(self, tasks):
        ret = ""
        for task in tasks:
            ret += str(task)
        return ret

    def DP(self, cur_tasks, cut_idx):
        num_tasks = len(cur_tasks)
        search_record = utils.SearchRecord().reset()

        if num_tasks == 1:
            new_task = copy.deepcopy(cur_tasks[0])
            new_task.set_arch_cst(copy.deepcopy(self.config['arch_cst']))
            new_task.set_arch_sol(new_task.arch_sol)
            new_task.fuse = 0
            if str(new_task) in self.config['search_jobs'] and self.config['search_jobs'][str(new_task)]['done'] == 1:
                search_record = self.config['search_jobs'][str(new_task)]['search_record'].dup()
                # Correct the task names since cache is used
                search_record.task_names = [cur_tasks[0].workload["name"]]
                search_record.exec_model = [cur_tasks[0].workload["name"]]
                search_record.records = None
            else:
                # Submit the task
                self.config['search_jobs'][str(new_task)] = {'search_task': new_task, 'done': 0}
        elif cut_idx == num_tasks:
            task_names = []
            exec_model = []
            for task in cur_tasks:
                task_names.append(task.workload["name"])
                exec_model.append(task.workload["name"])
            task_names_str = ''.join(task_names)
            if "fusion_candidates" in self.config.keys():
                # Only fuse the promising candidates
                if task_names_str not in self.config['fusion_candidates']:
                    return search_record
            cur_tasks = copy.deepcopy(cur_tasks)
            new_task = MultiTask(cur_tasks[0].design, cur_tasks, self.cst, fuse=2, use_uram=self.config['explorer'].search_config["use_uram"])
            new_task.set_arch_cst(copy.deepcopy(self.config['arch_cst']))
            new_task.set_arch_sol(cur_tasks[0].arch_sol)
            if str(new_task) in self.config['search_jobs'] and self.config['search_jobs'][str(new_task)]['done'] == 1:
                search_record = self.config['search_jobs'][str(new_task)]['search_record'].dup()
                # Correct the task names since cache is used
                search_record.task_names = task_names
                search_record.exec_model = exec_model
            else:
                self.config['search_jobs'][str(new_task)] = {'search_task': new_task, 'done': 0}
        else:
            for cut_idx in range(1, num_tasks + 1):
                # Front
                front = cur_tasks[:cut_idx]
                front_hash = self.hash_dp_task(front)
                if front_hash in self.config['DP_tasks']:
                    search_record_front = self.config['DP_tasks'][front_hash].dup()
                    # Update the task names
                    task_names = []
                    for task in front:
                        task_names.append(task.workload["name"])
                    search_record_front.task_names = task_names
                else:
                    search_record_front = self.DP(front, cut_idx)
                    self.config['DP_tasks'][front_hash] = search_record_front

                if (cut_idx < num_tasks) and (self.mode == "submit" or \
                   (self.mode == "aggregate" and search_record_front.valid == 1)):
                    # Back
                    back = cur_tasks[cut_idx:]
                    back_hash = self.hash_dp_task(back)
                    if back_hash in self.config['DP_tasks']:
                        search_record_back = self.config['DP_tasks'][back_hash].dup()
                        # Update the task names
                        task_names = []
                        for task in back:
                            task_names.append(task.workload["name"])
                        search_record_back.task_names = task_names
                    else:
                        search_record_back = self.DP(back, cut_idx)
                        self.config['DP_tasks'][back_hash] = search_record_back

                    local_search_record = utils.SearchRecord().reset().merge(search_record_front, search_record_back)
                else:
                    local_search_record = search_record_front

                # Update the task names
                task_names = []
                for task in cur_tasks:
                    task_names.append(task.workload["name"])
                local_search_record.task_names = task_names
                search_record.update(local_search_record)

        return search_record

    def exec(self):
        job_list = []
        for job in self.config['search_jobs']:
            explorer = copy.deepcopy(self.config['explorer'])
            # Reduce the maximal forked processes
            explorer.search_config['n_worker'] = max(int(self.n_worker / 2), 2)
            job_list.append(
                {'job_hash': job, 'func': explorer.tune,
                 'args': [self.config['search_jobs'][job]['search_task'], None, 1, 0]}
            )
        pool = utils.MyExecutor(max(int(self.n_worker / 2), 2))
        results = pool.exec(job_list)

        for job in self.config['search_jobs']:
            self.config['search_jobs'][job]['done'] = 1
            self.config['search_jobs'][job]['search_record'] = results[job]

    def search(self):
        # Submit all DP tasks
        self.mode = "submit"
        self.DP(self.tasks, -1)
        # Execute tasks
        self.exec()
        self.config['DP_tasks'] = {}
        # Collect the results
        self.mode = "aggregate"
        search_record = self.DP(self.tasks, -1)

        return search_record

class FuseGeneticTuner(MultiWorkloadArrayGeneticTuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, params, n_worker=n_worker, silent=silent)

    def init_population(self, num_pop):
        population = np.empty((num_pop, len(self.search_task.design.params_config["tunable"])), dtype=int)
        # Allocate uniformly
        for i in range(num_pop):
            sol = self.params["init_pop"][i % len(self.params["init_pop"])]["params"]
            param_arr = []
            for p, param in self.search_task.design.params_config["tunable"].items():
                param_arr.append(sol[param["name"]])
            population[i] = np.array(param_arr, dtype=int)

        return population

    def search_design(self, arch_sol, use_model=0, bst=None):
        network_search_record = utils.SearchRecord(self.max).reset()
        # Update the hardware constraints
        search_task = copy.deepcopy(self.search_task)
        arch_cst = search_task.compute_arch_cst(arch_sol)
        search_task.set_arch_cst(arch_cst)
        search_task.set_arch_sol(arch_sol)

        # Dynamic programming
        dp_config = {
            "explorer": self.params["explorer"],
            "arch_cst": arch_cst,
            "DP_tasks": {},
            "search_jobs": {}
        }
        if "fusion_candidates" in self.params:
            dp_config["fusion_candidates"] = self.params["fusion_candidates"]

        DP_tuner = FuseDPTuner(dp_config, search_task.tasks, self.cst, self.n_worker)
        network_search_record.update(DP_tuner.search())

        return network_search_record

    def search(self):
        self.counter.init_counter('time')
        self.counter.init_counter('converge_time')
        self.epoch = 0

        # Init the stats
        num_pop = int(self.params["population_size"])
        num_gen = int(self.max_epoch // num_pop)
        num_parents = int(num_pop * self.params["parents_ratio"])
        self.log(f'Number of generations: {num_gen}')
        self.log(f'Number of population: {num_pop}')
        self.log(f'Number of parents: {num_parents}')

        idx = 0
        for p, param in self.search_task.design.params_config["tunable"].items():
            self.param_idx_map[param["name"]] = idx
            self.idx_param_map[idx] = param["name"]
            idx += 1

        # Init the population
        population = self.init_population(num_pop)
        fitness = np.empty(num_pop, dtype=float)

        terminate = False
        while True:
            if self.epoch > 0:
                # Select the parents
                parents = self.select_parents(population, fitness, num_parents)
                if parents.shape[0] == 0:
                    break
                # Crossover
                children = self.crossover(parents, num_pop - parents.shape[0])
                # Mutation
                children = self.mutation(children)
                # Compose the new generation
                population[0:parents.shape[0], :] = parents
                population[parents.shape[0]:, :] = children

            # Update the fitness
            use_model = self.bst_data['valid'] and (self.gen % self.params['model_gens'] != 0)
            job_list = []
            results = {}
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                idv_hash = self.hash_params(task_params)
                for p, param in self.search_task.design.params_config["external"].items():
                    task_params[param["name"]] = self.search_task.workload["params"][param["name"]]
                # Note: XGBoost model has compatibility problem with multi-processing.
                search_task = copy.deepcopy(self.search_task)
                # Compute the architecture features
                arch_cst = search_task.compute_arch_cst(task_params)
                if not use_model:
                    if idv_hash in self.search_cache:
                        continue
                    else:
                        search_record = utils.SearchRecord(self.max).reset()
                        if arch_cst:
                            if not self.xgboost_prune(task_params, arch_cst):
                                self.search_cache[idv_hash] = {'status': 'submit', 'value': None}
                                job_list.append({
                                    'job_hash': idv_hash,
                                    'func': self.search_design,
                                    'args': [task_params, use_model, copy.deepcopy(self.bst)]})
                            else:
                                results[idv_hash] = search_record
                        else:
                            results[idv_hash] = search_record
                else:
                    reward = 0
                    if arch_cst:
                        reward = self.xgboost_predict(task_params, arch_cst)[0]
                    results[idv_hash] = reward

            if len(job_list) > 0:
                pool = utils.MyExecutor(max(int(self.n_worker / 2), 2))
                pool_results = pool.exec(job_list)
                for result in pool_results:
                    results[result] = pool_results[result]

            # Update the tuner results
            for i in range(num_pop):
                idv = population[i]
                task_params = {}
                for p, param in self.search_task.design.params_config["tunable"].items():
                    task_params[param["name"]] = idv[self.param_idx_map[param["name"]]]
                idv_hash = self.hash_params(task_params)
                if use_model:
                    fitness[i] = results[idv_hash]
                else:
                    if idv_hash in self.search_cache and self.search_cache[idv_hash]['status'] == 'done':
                        fitness[i] = self.search_cache[idv_hash]['value']
                        continue
                    search_record = results[idv_hash]
                    if self.overuse_constraint(search_record.cst) or search_record.valid == 0:
                        search_record.reward = 0
                    if search_record.reward > 0:
                        if search_record.reward > self.best_reward:
                            self.best_reward = search_record.reward
                            self.best_reward_meta = search_record.reward_meta
                            self.best_sol_cst = search_record.cst
                            self.best_sol = {"arch_sol": search_record.arch_sol, \
                                             "task_sols": search_record.task_sols}
                            self.log(f'Epoch {self.epoch}: new best reward: {self.best_reward} ({1/self.best_reward:.0f})')
                            self.last_update_epoch = self.epoch
                            self.counter.update_counter('converge_time')
                            # Update the DSP eff
                            search_record.dsp_eff = self.search_task.compute_dsp_eff(search_record.latency, search_record.cst["DSP"])
                            self.best_search_record = search_record
                            if self.best_reward >= self.params["best_reward"] * self.params["best_reward_thres"]:
                                terminate = True

                    self.best_rewards.append(self.best_reward)
                    fitness[i] = search_record.reward / self.params['best_reward']
                    self.search_cache[idv_hash] = {'status': 'done', 'value': fitness[i]}
                self.epoch += 1

            # Add training samples
            if not use_model:
                for result in results:
                    search_record = results[result]
                    if self.params["best_reward"] and search_record.valid:
                        arch_cst = self.search_task.compute_arch_cst(search_record.arch_sol)
                        if search_record.reward > 0:
                            self.xgboost_add_sample(search_record.arch_sol, arch_cst, search_record.reward / self.params['best_reward'])
                        else:
                            self.xgboost_add_sample(search_record.arch_sol, arch_cst, 0)

            # Train the cost model
            if not use_model:
                self.xgboost_train()
                # Adjust the cost model threshold dynamically
                if self.best_search_record.valid:
                    arch_sol = self.best_search_record.arch_sol
                    arch_cst = self.search_task.compute_arch_cst(arch_sol)
                    pred = self.xgboost_predict(arch_sol, arch_cst)
                    self.params['prune_params']['xgb_thres'] = pred * self.params['prune_params']['xgb_thres_adjust']
                    self.log(f'Updated XGB pruning thres: {self.params["prune_params"]["xgb_thres"]}')

            self.gen += 1

            if self.stop_criteria == "epoch" and self.epoch > self.max_epoch:
                break
            if self.stop_criteria == "time":
                self.counter.update_counter('time')
                if self.counter.get_counter('time') > self.max_time:
                    break
            if terminate:
                break

        return

def multi_acc_search1(search_task, init_tasks, cst, search_obj, max_epochs, max_time, \
                      n_worker=1, silent=0, population_size=20, policy=0, meta=None, explorer=None, profiling=0):
    """ This function finds the best multi-array architecture for a list of tasks.
    """
    import logging
    logger = logging.getLogger('AutoSA-Tuner')
    if silent == 0:
        logger.info("Performing cross layer multi-accelerator genetic search...")

    best_latency = utils.compute_tasks_latency(search_task.tasks, init_tasks)
    if silent == 0:
        logger.info(f'Cross-layer multi-accelerator ideal latency: {best_latency}')

    partition_candidates = meta["partition_candidates"]

    tuner_params = {
        "explorer": explorer,
        "probe_points": meta["init_partition_candidates"],
        "best_reward": 1 / best_latency if best_latency else None,
        "partition_candidates": partition_candidates,
        "batch_size": meta["batch_size"],
        "use_uram_all": meta["use_uram_all"],
        "dsp_eff_thres": 0.85, # If the DSP eff is greater than this thres, no fine-tuning is required.
        "latency_stdev_thres": 0.03,
        "reward_stdev_thres": 0.025,
        "max_trial": 3 # Terminate fine-tuning after more than x trials
    }
    if meta:
        tuner_params["design_idx_list"] = meta['design_idx_list']

    if max_epochs > 0:
        pass
    else:
        max_time = 3600 # 60 minutes at most

    tuner = MultiAccTuner1(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
    tuner.search()

    search_record = tuner.best_search_record

    # For internal testing
    now = datetime.now()
    config_str = f"_{explorer.search_config['workload']}_multi1"
    with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
        fieldnames = ['epoch', 'reward', 'time']
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        #for epoch in range(len(tuner.best_rewards)):
        for epoch in range(len(tuner.bayopt_best_rewards)):
            writer.writerow({'epoch': epoch, 'reward': tuner.bayopt_best_rewards[epoch], 'time': tuner.bayopt_best_rewards_time[epoch]})

    return search_record

class MultiAccTuner1(Tuner):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, n_worker=n_worker, silent=silent)
        self.params = params
        self.epoch = 0
        if max_epoch > 0:
            self.stop_criteria = "epoch"
            self.max_epoch = max_epoch
        else:
            self.stop_criteria = "time"
            self.max_time = max_time
        self.counter = utils.PerfCounter()
        self.bayopt_epoch = 0
        self.bayopt_best_rewards = []
        self.bayopt_best_rewards_time = []

        self.search_cache = {} # Store searc records
        self.search_cache_cst = {}
        self.bay_search_log = {} # Bayesian search log

    def resource_alloc(self, partition):
        """ Allocate initial DSP/BRAM limit.
        The highest throughput is achieved when each array has a similar latency.
        At the ideal case,
        ops1/#DSP1 = ops2/#DSP2 = ...
        The initial DSP is then allocated based on the #ops of each array.
        #DSPi = opsi/ops_total * #DSP_total
        """
        DSP_total = self.cst.hw_cst['DSP']
        BRAM_total = self.cst.hw_cst['BRAM18K']
        array_ops = []
        for p in partition:
            cur_ops = 0
            for idx in p:
                cur_ops += self.search_task.tasks[idx].compute_ops()
            array_ops.append(cur_ops)

        total_ops = sum(array_ops)
        DSP_alloc = [int(n / total_ops * DSP_total) for n in array_ops]

        if len(partition) == 1:
            step = 1
        else:
            step = pow(2, int(np.log2(len(partition))) + 1)
        BRAM18K_alloc = [self.cst.hw_cst['BRAM18K'] / step for n in array_ops]
        #BRAM18K_alloc = [self.cst.hw_cst['BRAM18K'] for n in array_ops]

        return {"DSP": DSP_alloc, "BRAM18K": BRAM18K_alloc, "state": 0}

    def est_URAM(self, records):
        URAM_total = 0
        for i in range(len(records)):
            record = records[i]
            URAM_total += record.cst["URAM"]

        return URAM_total

    def est_mem(self, partition, records, verbose=0):
        """ Estimate the total BRAM18K usage.
        BRAM18K is consumed by two parts: arrays and streaming buffers in-between.
        For two adjacent arrays, suppose their tiling factors as:
        [tr1, tc1, to1, ti1] and [tr2, tc2, to2, ti2]
        Compute the tiling factors such that:
        tr' = c0 * tr1
        tc' = c1 * tc1
        (c0 - 1) * tr1 < tr2 + k - 1 <= c0 * tr1
        (c1 - 1) * tc1 < tc2 + k - 1 <= c1 * tc1
        Streaming buffers are allocated to hold at least:
        tr' * tc' * o1(i2) * 2
        such that when the second array is using the first block of (tr2 + k - 1) * ... * i2,
        the first array will continue to fill the rest of the buffer for the next round.
        If verbose is set to 1, return the detailed resource usage of each array and streaming buffer.
        """
        array_bufs = []
        stream_bufs = []
        BRAM18K_total = 0
        URAM_total = 0
        # array bufs
        for i in range(len(records)):
            record = records[i]
            BRAM18K_total += record.cst["BRAM18K"]
            array_bufs.append(record.cst["BRAM18K"])

        # array bufs
        if self.params["use_uram_all"]:
            for i in range(len(records)):
                if len(partition[i]) == 1:
                    continue
                # Allocate URAMs to store the intermediate results for multi-task array
                URAM_tmp = 0
                for layer_idx in partition[i]:
                    o = self.search_task.tasks[layer_idx].workload['params']['o']
                    r = self.search_task.tasks[layer_idx].workload['params']['r']
                    c = self.search_task.tasks[layer_idx].workload['params']['c']
                    data_pack = records[i].task_sols[0]['sol']['i_t2']
                    ele_num = o * r * c
                    URAM_tmp = max(URAM_tmp, 2 * np.ceil(self.search_task.dw * data_pack * 8 / 72) * np.ceil(ele_num / data_pack / 4096))
                URAM_total += URAM_tmp

        # streaming bufs
        for i in range(1, len(records)):
            array1 = records[i - 1]
            array2 = records[i]
            # Streaming buffers are only inserted between single-task array.
            if len(array1.task_names) > 1 or len(array2.task_names) > 1:
                continue
            layer1_idx = partition[i - 1][0]
            layer2_idx = partition[i][0]
            # Extract parameters of array 1
            o1 = self.search_task.tasks[layer1_idx].workload['params']['o']
            tr1 = min(array1.task_sols[0]['sol']['r_t1'], self.search_task.tasks[layer1_idx].workload['params']['r'])
            tc1 = min(array1.task_sols[0]['sol']['c_t1'], self.search_task.tasks[layer1_idx].workload['params']['c'])
            for tag in self.search_task.tasks[layer1_idx].workload['tags']:
                if tag.startswith('maxpool'):
                    stride = int(tag.split('_')[-1])
                    tr1 /= stride
                    tc1 /= stride
            tr1 = max(int(tr1), 1)
            tc1 = max(int(tc1), 1)
            # Extract parameters of array 2
            tr2 = min(array2.task_sols[0]['sol']['r_t1'], self.search_task.tasks[layer2_idx].workload['params']['r'])
            tc2 = min(array2.task_sols[0]['sol']['c_t1'], self.search_task.tasks[layer2_idx].workload['params']['c'])
            k = self.search_task.tasks[layer2_idx].workload['params']['p']
            data_pack = array2.task_sols[0]['sol']['i_t2']
            # Compute the BRAM size
            c0 = np.ceil((tr2 + k - 1) / tr1)
            c1 = np.ceil((tc2 + k - 1) / tc1)
            array1_params = self.search_task.tasks[layer1_idx].workload["params"]
            array2_params = self.search_task.tasks[layer2_idx].workload["params"]
            trp = min(c0 * tr1, array1_params['r'])
            tcp = min(c1 * tc1, array1_params['c'])
            #ele_num = trp * tcp * o1 * 2
            ele_num = min(trp * array1_params['c'] * o1, tcp * array1_params['r'] * o1)
            #buffer = np.ceil(self.search_task.dw * data_pack * 8 / 36) * np.ceil(ele_num / data_pack / 512)
            buffer = np.ceil(self.search_task.dw * data_pack * 8 / 72) * np.ceil(ele_num / data_pack / 4096)
            stream_bufs.append(buffer)
            #BRAM18K_total += buffer
            URAM_total += buffer

        if verbose == 0:
            return {"BRAM18K": BRAM18K_total, "URAM": URAM_total}, None            
        else:
            return {"BRAM18K": BRAM18K_total, "URAM": URAM_total}, {"array_bufs": array_bufs, "stream_bufs": stream_bufs}            

    def overuse_resource(self, partition, records):
        for record in records:
            if record.valid == 0:
                return True
        #BRAM18K = self.est_BRAM18K(partition, records)
        mem, meta = self.est_mem(partition, records)
        #URAM = self.est_URAM(records)
        DSP = 0
        for record in records:
            DSP += record.cst["DSP"]
        BRAM18K = mem["BRAM18K"]
        URAM = mem["URAM"]
        if BRAM18K > self.cst.hw_cst["BRAM18K"]:
            return True
        if URAM > self.cst.hw_cst["URAM"]:
            return True
        if DSP > self.cst.hw_cst["DSP"]:
            return True

        return False

    def est_resource(self, partition, records):
        #BRAM18K = self.est_BRAM18K(partition, records)
        #URAM = self.est_URAM(records)
        mem, meta = self.est_mem(partition, records)
        DSP = 0
        for record in records:
            DSP += record.cst["DSP"]

        return {"DSP": DSP, "BRAM18K": mem["BRAM18K"], "URAM": mem["URAM"]}

    def est_latency(self, partition, records, in_place=0, adjust=0, verbose=0):
        """ Compute the latency of the design.
        Single-task arrays are adjusted to start as long as the first batch of data
        are ready in the streaming buffer.
        Multi-task array will wait until the previous array finishes.
        Any arrays following the multi-task array will also wait for the previous array to complete.
        If in_place is set to 1, records latency will be updated.
        If adjust is set to 1, we will consider the possible stall between arrays.
        """
        array_latency = [records[0].latency * self.params["batch_size"]]
        setup_latency = [0]
        record_latency = [r.latency for r in records]

        # Update array and setup latency
        for i in range(1, len(records)):
            array1 = records[i - 1]
            array2 = records[i]
            if len(array1.task_names) > 1 or len(array2.task_names) > 1:
                # One of the arrays is a multi-task array
                # Start only if the previous one finishes
                setup = array_latency[-1]
                setup_latency.append(setup)

                array_latency.append(max(record_latency[i] * self.params["batch_size"], array_latency[i - 1]))
            else:
                # Both arrays are single-task arrays
                # Start as long as the first block of data is ready
                layer1_idx = partition[i - 1][0]
                layer2_idx = partition[i][0]
                # Extract parameters of array 1
                o1 = self.search_task.tasks[layer1_idx].workload['params']['o']
                tr1 = min(array1.task_sols[0]['sol']['r_t1'], self.search_task.tasks[layer1_idx].workload['params']['r'])
                tc1 = min(array1.task_sols[0]['sol']['c_t1'], self.search_task.tasks[layer1_idx].workload['params']['c'])
                tr1_post = tr1
                tc1_post = tc1
                for tag in self.search_task.tasks[layer1_idx].workload['tags']:
                    if tag.startswith('maxpool'):
                        stride = int(tag.split('_')[-1])
                        tr1_post /= stride
                        tc1_post /= stride
                tr1_post = max(int(tr1_post), 1)
                tc1_post = max(int(tc1_post), 1)
                # Extract parameters of array 2
                tr2 = min(array2.task_sols[0]['sol']['r_t1'], self.search_task.tasks[layer2_idx].workload['params']['r'])
                tc2 = min(array2.task_sols[0]['sol']['c_t1'], self.search_task.tasks[layer2_idx].workload['params']['c'])
                k = self.search_task.tasks[layer2_idx].workload['params']['p']
                data_pack = array2.task_sols[0]['sol']['i_t2']

                c0 = np.ceil((tr2 + k - 1) / tr1_post)
                c1 = np.ceil((tc2 + k - 1) / tc1_post)
                array1_params = self.search_task.tasks[layer1_idx].workload["params"]
                array2_params = self.search_task.tasks[layer2_idx].workload["params"]
                trp = min(c0 * tr1, array1_params['r'])
                tcp = min(c1 * tc1, array1_params['c'])
                # Setup latency
                #setup = record_latency[i - 1] / (np.ceil(array1_params['r'] / trp) * np.ceil(array1_params['c'] / tcp))
                if trp > tcp:
                    setup = record_latency[i - 1] / np.ceil(array1_params['c'] / tcp)
                else:
                    setup = record_latency[i - 1] / np.ceil(array1_params['r'] / trp)
                setup_latency.append(setup)

                # Adjust the array latency
                if adjust:
                    raise RuntimeError("Array latency adjust for multi-array is not implemented.")
                    '''
                    # Consider the fine-grained produce-consume relationship
                    n_fill_rounds = np.ceil((min(2 * tr2 + k - 1, array1_params['r'] + k - 1) - c0 * tr1_post) / tr1_post) * c1
                    fill_latency = array_latency[-1] / (np.ceil(array1_params['r'] / tr1 * np.ceil(array1_params['c'] / tc1))) * n_fill_rounds
                    consume_latency = record_latency[i] / (np.ceil(array2_params['r'] / tr2 * np.ceil(array2_params['c'] / tc2)))
                    adjusted_latency = max(fill_latency, consume_latency) * np.ceil(array2_params['r'] / tr2) * np.ceil(array2_params['c'] / tc2)
                    record_latency[i] = adjusted_latency
                    array_latency.append(adjusted_latency)
                    '''
                else:
                    # Simply compute the max
                    array_latency.append(max(record_latency[i] * self.params["batch_size"], array_latency[i - 1]))

        if in_place:
            # Update the array latency
            for i in range(len(records)):
                records[i].latency = array_latency[i]

        # Compute the latency
        design_latency = 0
        for lat in setup_latency:
            design_latency += lat
        design_latency += array_latency[-1]

        # Throughput
        max_latency = 0
        for latency in array_latency:
            if latency > max_latency:
                max_latency = latency
        throughput = 1 / max_latency * self.params["batch_size"]

        return design_latency, throughput, None

    def est_dsp_eff(self, throughput, cst):
        total_ops = 0
        for task in self.search_task.tasks:
            total_ops += task.compute_ops()
        # Note: Only works for FP32
        dsp_eff = throughput / (cst["DSP"] / 5 * 2 / total_ops)

        return dsp_eff

    def evaluate(self, partition, records, verbose=0):
        latency, throughput, meta = self.est_latency(partition, records, verbose=verbose)
        #latency, throughput = self.est_latency(partition, records)
        resource = self.est_resource(partition, records)
        return latency, resource, throughput, meta

    def is_finetune_required(self, records, dsp_eff):
        """ Check if finetuning is required.
        """
        # If DSP efficiency is higher than the thres, stop
        if dsp_eff >= self.params["dsp_eff_thres"]:
            return False

        return True

    def resource_alloc_adjust(self, partition, resource_alloc, records, overuse_mem):
        """ Adjust the resource allocation.
        State 0: Try to allocate all the available resource to the bottleneck design.
        If the resource allocation leads to memory overuse, reduce the resource allocated
        to the bottleneck design graduualy until a legal one is found.
        Switch to state 0.5 afterwards.
        If the first attempt leads to a legal design while the bottleneck design remains
        the bottleneck, switch to state 1.

        State 0.5 (deprecated): Intermediate state. Simply try to increase the resource allocation.
        If succeeds, switch back to state 0, otherwise, switch to state 1.
        This state is set considering the instability of the search results, i.e.,
        re-run the searching for more arrays might lead to a feasible solution.

        State 1: Borrow resource from fastest designs to the bottleneck design.
        We keep a cache to store all the past records for different arrays with
        different resource allocation. This cache is prioritized when selecting
        the reduced resource allocation.
        In the case when no such option is found in the search log, simply reducing the
        resource usage by a fixed amount.

        "records" is the best feasible array records found so far.
        "overuse_mem" indicates if the last attempt leads to memory overutilization.
        """
        # Calculate the available resource
        available_dsp = self.cst.hw_cst["DSP"]
        available_bram = self.cst.hw_cst["BRAM18K"]
        if resource_alloc["state"] in [0, 0,5]:
            available_dsp -= resource_alloc['init']['DSP_total']
            available_bram -= resource_alloc['init']['BRAM18K_total']
        else:
            resource = self.est_resource(partition, records)
            available_dsp -= resource['DSP']
            available_bram -= resource['BRAM18K']

        slow_idx_list = resource_alloc["slow_idx"]
        fast_idx_list = resource_alloc["fast_idx"]

        inc_dsp = 0
        inc_bram = 0
        dec_dsp = 0
        dec_bram = 0

        # State transition
        if resource_alloc["state"] == 0:
            if resource_alloc["decrease"][0] == 1 and not overuse_mem:
                #resource_alloc["state"] = 0.5 # Stale state for one more attempt
                resource_alloc["state"] = 1
            if resource_alloc["n_adjust"][0] == 1 and not overuse_mem:
                # Allocate all the available resource is insufficent
                resource_alloc["state"] = 1
        elif resource_alloc["state"] == 0.5:
            if resource_alloc["decrease"][0] == 0 and not overuse_mem:
                resource_alloc["state"] = 0
            else:
                resource_alloc["state"] = 1

        if resource_alloc["state"] in [0, 0.5]:
            if resource_alloc["n_adjust"][0] > 1:
                if overuse_mem == 0:
                    # Increase the lower bound
                    resource_alloc["step"][0][0] = sum(resource_alloc["step"][0]) / 2
                else:
                    # Decrease faster
                    resource_alloc["step"][0][1] = sum(resource_alloc["step"][0]) / 4

            if resource_alloc["n_adjust"][0] == 0:
                # At the first attempt, allocate all the available resource to the bottleneck design
                ratio = resource_alloc["step"][0][1]
            else:
                ratio = sum(resource_alloc["step"][0]) / 2
            inc_dsp = available_dsp
            inc_bram = int(available_bram * ratio)
            resource_alloc["DSP"][slow_idx_list[0]] = resource_alloc['init']['DSP'][slow_idx_list[0]] + inc_dsp
            resource_alloc["BRAM18K"][slow_idx_list[0]] = resource_alloc['init']['BRAM18K'][slow_idx_list[0]] + inc_bram

            if resource_alloc["n_adjust"][0] > 0:
                if inc_bram > resource_alloc["history"][0]:
                    resource_alloc["decrease"][0] = 0
                else:
                    resource_alloc["decrease"][0] = 1
            resource_alloc["history"][0] = inc_bram
            if inc_bram == 0:
                resource_alloc["state"] = 1

        if resource_alloc["state"] == 1:
            # Calculate the available resource
            available_dsp = self.cst.hw_cst["DSP"]
            available_bram = self.cst.hw_cst["BRAM18K"]
            resource = self.est_resource(partition, records)
            available_dsp -= resource['DSP']
            available_bram -= resource['BRAM18K']

        if resource_alloc["state"] == 1:
            cur_adjust_thres = len(fast_idx_list) * 2
            update_idx_select = {}
            while True:
                total_adjust_num = 0 # Number of successfully adjusted arrays
                if cur_adjust_thres == 0:
                    break
                for idx in fast_idx_list:
                    history = self.search_cache[idx]
                    def take_latency(record):
                        return record.latency
                    history.sort(key=take_latency)
                    # Compute the latency upper bound to adjust this array
                    ub_latency = (records[slow_idx_list[0]].latency - records[idx].latency) / (cur_adjust_thres + 1) + records[idx].latency
                    #print("adjust ub latency: ", ub_latency)

                    # Decrease the memory allocation for this design to increase array latency up to ub_latency
                    min_mem = records[idx].cst["BRAM18K"]
                    update_idx = -1
                    for history_idx in range(len(history)):
                        r = history[history_idx]
                        if r.latency > records[slow_idx_list[0]].latency:
                            break
                        if r.latency >= records[idx].latency and r.latency <= ub_latency:
                            if r.cst["BRAM18K"] < min_mem:
                                min_mem = r.cst["BRAM18K"]
                                update_idx = history_idx
                    if update_idx != -1:
                        total_adjust_num += 1
                    update_idx_select[idx] = update_idx
                if total_adjust_num < min(len(fast_idx_list), 2):
                    # Adjust at least two arrays each time
                    # If not enough candidate arrays are found, try to loose the upper bound
                    cur_adjust_thres -= 1
                else:
                    break
            for idx in fast_idx_list:
                history = self.search_cache[idx]
                def take_latency(record):
                    return record.latency
                history.sort(key=take_latency)
                dec_bram_single = 0
                dec_dsp_single = 0
                if update_idx_select[idx] != -1:
                    #print("cur, selected records constraints: ", records[idx].cst["BRAM18K"], r.cst["BRAM18K"])
                    #print("cur, selected records latency: ", records[idx].latency, r.latency)
                    r = history[update_idx_select[idx]]
                    dec_bram_single = (records[idx].cst["BRAM18K"] - r.cst["BRAM18K"])
                    dec_dsp_single = (records[idx].cst["DSP"] - r.cst["DSP"])
                    resource_alloc["DSP"][idx] = r.cst["DSP"]
                    resource_alloc["BRAM18K"][idx] = r.cst["BRAM18K"]
                dec_bram += dec_bram_single
                dec_dsp += dec_dsp_single
            if dec_bram == 0:
                # No available records found in the search cache.
                # We will force fast designs to spare resource to the bottleneck design.
                dec_dsp = 0
                for idx in fast_idx_list:
                    limit_ratio = min((1 - records[idx].latency / records[slow_idx_list[0]].latency) / 8, resource_alloc["step"][1][0])
                    dec_bram_single = records[idx].cst["BRAM18K"] * limit_ratio
                    dec_dsp_single = records[idx].cst["DSP"] * limit_ratio / 2
                    resource_alloc["DSP"][idx] = records[idx].cst["DSP"] - dec_dsp_single
                    resource_alloc["BRAM18K"][idx] = records[idx].cst["BRAM18K"] - dec_bram_single
                    dec_bram += dec_bram_single
                    dec_dsp += dec_dsp_single

            resource_alloc["DSP"][slow_idx_list[0]] = records[slow_idx_list[0]].cst['DSP'] + (dec_dsp + available_dsp)
            resource_alloc["BRAM18K"][slow_idx_list[0]] = records[slow_idx_list[0]].cst['BRAM18K'] + (dec_bram + available_bram)
            if resource_alloc["n_adjust"][1] > 0:
                if resource_alloc["BRAM18K"][slow_idx_list[0]] > resource_alloc["history"][1]:
                    resource_alloc["decrease"][1] = 0
                else:
                    resource_alloc["decrease"][1] = 1
            resource_alloc["history"][1] = resource_alloc["BRAM18K"][slow_idx_list[0]]
            if resource_alloc["decrease"][1] == 1 and not overuse_mem:
                # Stop further tuning
                return False

        resource_alloc["n_adjust"][math.floor(resource_alloc["state"])] += 1
        # Only try at most 3 times for each state
        if resource_alloc["n_adjust"][0] > 3 or resource_alloc["n_adjust"][1] > 3:
            return False

        return True

    def update_bottleneck_idx(self, records):
        """ Return the slowest/fastest design index.
        Select up to len(records) - 1 fast designs.
        Select 1 slow design.
        """
        slow = {'latency': 0, 'idx': []}
        fast = {'latency': float("inf"), 'idx': []}
        for i in range(len(records)):
            record = records[i]
            if record.latency < fast['latency']:
                fast['latency'] = record.latency
                fast['idx'] = [i]
            if record.latency > slow['latency']:
                slow['latency'] = record.latency
                slow['idx'] = [i]

        list_len = len(records) - 1
        for i in range(len(records)):
            if i in fast['idx']:
                continue
            record = records[i]
            if abs((record.latency - fast['latency']) / fast['latency']) < 0.05 and len(fast['idx']) < list_len:
                fast['idx'].append(i)
        if len(fast["idx"]) == 1 and list_len > 1:
            # Add one more into the list
            fast_val = float("inf")
            idx = -1
            for i in range(len(records)):
                record = records[i]
                if i == fast['idx'][0]:
                    continue
                if record.latency < fast_val:
                    fast_val = record.latency
                    idx = i
            fast['idx'].append(idx)

        list_len = 1
        for i in range(len(records)):
            if i in slow['idx']:
                continue
            record = records[i]
            if abs((record.latency - slow['latency']) / slow['latency']) < 0.02 and len(slow['idx']) < list_len:
                slow['idx'].append(i)

        return slow["idx"], fast["idx"]

    def find_legal_config(self, partition, resource_alloc, old_records=None, adjust_func=None, fine_tune=0, skip_search=1):
        """ Find a legal configuration given the resource allocation.
        If "skip_search" is set to 1, only re-search the designs in the slow/fast idx list.
        If "fine_tune" is set to 1, we will adjust the resource allocation using
        "adjust_func" until the bottleneck array changes or there is no valid resource
        allocation found.
        Otherwise, the current function will gradually reduce the BRAM allocation until a
        valid design is found.
        """
        legal_records = old_records
        best_throughput = 0
        is_first = True
        n_arrays = len(partition)
        # Maintain a list of several best designs for each array
        history = [[] for i in range(n_arrays)]
        history_thres = 2
        if n_arrays > 10:
            # Avoid storing too many configs
            history_thres = 1

        single_task_arrays = []
        multi_task_arrays = []
        for i in range(n_arrays):
            if len(partition[i]) == 1:
                single_task_arrays.append(i)
            else:
                multi_task_arrays.append(i)

        while True:
            # For internal testing
            #pprint.pprint(resource_alloc)
            records = []
            skip_idx = []
            job_list = []
            tasks = []
            # single task arrays
            for i in single_task_arrays:
                # Update the history
                history_tmp = []
                for record in history[i]:
                    if record.cst["BRAM18K"] <= resource_alloc["BRAM18K"][i] and \
                       record.cst["DSP"] <= resource_alloc["DSP"][i]:
                       history_tmp.append(record)
                if legal_records and is_first:
                    if legal_records[i].cst["BRAM18K"] <= resource_alloc["BRAM18K"][i] and \
                       legal_records[i].cst["DSP"] <= resource_alloc["DSP"][i]:
                       history_tmp.append(legal_records[i])
                       self.search_cache[i].append(legal_records[i])
                history[i] = history_tmp
                if skip_search == 1:
                    if (i not in resource_alloc["slow_idx"]) and (i not in resource_alloc["fast_idx"]) and len(history[i]) > 0:
                        skip_idx.append(i)
                        continue
                # Submit the search job
                explorer_tmp = copy.deepcopy(self.params["explorer"])
                # Update the constraints
                explorer_tmp.cst.hw_cst["DSP"] = resource_alloc["DSP"][i]
                explorer_tmp.cst.hw_cst["BRAM18K"] = resource_alloc["BRAM18K"][i]
                array_tasks = []
                for design_idx in self.params["design_idx_list"]:
                    search_task = SingleTask(explorer_tmp.designs[design_idx], self.search_task.tasks[partition[i][0]].workload, explorer_tmp.cst)
                    # Update the task configs
                    if i == 0:
                        # Load from DRAM
                        search_task.configs["cin_read_mode"] = 0
                    elif (i > 0 and len(partition[i - 1]) > 1):
                        if self.params["use_uram_all"]:
                            search_task.configs["cin_read_mode"] = 2
                        else:
                            search_task.configs["cin_read_mode"] = 0
                    else:
                        # Access on-chip streaming buffers
                        search_task.configs["cin_read_mode"] = 2
                    if i == len(partition) - 1:
                        # Write to DRAM
                        search_task.configs["cout_write_mode"] = 0
                    elif (i < len(partition) - 1 and len(partition[i + 1]) > 1):
                        if self.params["use_uram_all"]:
                            search_task.configs["cout_write_mode"] = 1
                        else:
                            search_task.configs["cout_write_mode"] = 0
                    else:
                        search_task.configs["cout_write_mode"] = 1
                    # Run it for multiple times
                    for repeat in range(1):
                        job_list.append(
                            {
                                "job_hash": f"{str(search_task)}_{repeat}",
                                "func": explorer_tmp.tune,
                                "args": [search_task, None, self.sub_task_silent, 1]
                            })
                    array_tasks.append(search_task)
                tasks.append(array_tasks)

            pool = utils.MyExecutor(self.n_worker)
            results = pool.exec(job_list)

            idx = 0
            for i in single_task_arrays:
                if i in skip_idx:
                    continue
                history_local = history[i]
                array_tasks = tasks[idx]
                for task in array_tasks:
                    for result in results:
                        if result.startswith(str(task)):
                            record = results[result]
                            if record.valid:
                                record.arch_sol = record.task_sols[0]
                                history_local.append(record)
                                self.search_cache[i].append(record)
                if len(history_local) == 0:
                    return legal_records
                def take_latency(record):
                    return record.latency
                history_local.sort(key=take_latency)
                # Only take up to 2 designs for scalability issues
                history_local = history_local[:min(len(history_local), history_thres)]
                history[i] = history_local
                idx += 1            

            # multi-task array
            for i in multi_task_arrays:
                # Update the history
                history_tmp = []
                for record in history[i]:
                    if record.cst["BRAM18K"] <= resource_alloc["BRAM18K"][i] and \
                       record.cst["DSP"] <= resource_alloc["DSP"][i]:
                       history_tmp.append(record)
                if legal_records and is_first:
                    if legal_records[i].cst["BRAM18K"] <= resource_alloc["BRAM18K"][i] and \
                       legal_records[i].cst["DSP"] <= resource_alloc["DSP"][i]:
                        history_tmp.append(legal_records[i])
                        self.search_cache[i].append(legal_records[i])
                history[i] = history_tmp
                if skip_search == 1:
                    if (i not in resource_alloc["slow_idx"]) and (i not in resource_alloc["fast_idx"]) and len(history[i]) > 0:
                        continue
                explorer_tmp = copy.deepcopy(self.params["explorer"])
                # Update the constraints
                explorer_tmp.cst.hw_cst["DSP"] = resource_alloc["DSP"][i]
                explorer_tmp.cst.hw_cst["BRAM18K"] = resource_alloc["BRAM18K"][i]
                early_stop = -1
                if self.params["use_uram_all"]:
                    search_task_configs = {}
                    for task_idx in range(len(partition[i])):
                        search_task_configs[task_idx] = {'cin_read_mode': 2, 'cout_write_mode': 1}
                    if i == 0:
                        search_task_configs[0]["cin_read_mode"] = 0
                    if i == n_arrays - 1:
                        search_task_configs[len(partition[i]) - 1]["cout_write_mode"] = 0
                else:
                    search_task_configs = None
                job_list = []
                for design_idx in self.params["design_idx_list"]:
                    # Parallel version
                    job_list.append(
                        {
                            "job_hash": f"{design_idx}",
                            "func": explorer_tmp.search_non_fusion_single_acc_customized1,
                            "args": [design_idx, search_task_configs, -1, self.sub_task_silent, partition[i], None, True]
                        })
                    # Sequential version
                    #search_record = explorer_tmp.search_non_fusion_single_acc_customized1(\
                    #    design_idx=design_idx, silent=self.sub_task_silent, \
                    #    workload_idx=partition[i], early_stop=early_stop, one_gen=True)
                    #if search_record.valid:
                    #    early_stop = search_record.latency
                    #    history[i].append(search_record)
                    #    self.search_cache[i].append(search_record)
                pool = utils.MyExecutor(max(int(self.n_worker/8), 4))
                results = pool.exec(job_list)
                for design_idx in self.params["design_idx_list"]:
                    search_record = results[f"{design_idx}"]
                    if search_record.valid:
                        history[i].append(search_record)
                        self.search_cache[i].append(search_record)
                def take_latency(record):
                    return record.latency
                history[i].sort(key=take_latency)
                history[i] = history[i][:min(len(history[i]), history_thres)]            

            # Find the array combination that satisfies the memory usage
            choices_tmp = [list(range(len(h))) for h in history]
            choices = list(itertools.product(*choices_tmp))
            max_bram_tmp = 0
            min_bram_tmp = float("inf")
            best_throughput_tmp = 0
            for choice in choices:
                records_tmp = []
                for i in range(n_arrays):
                    records_tmp.append(history[i][choice[i]])
                latency, throughput, _ = self.est_latency(partition, records_tmp)                
                if not self.overuse_resource(partition, records_tmp):
                    if throughput > best_throughput_tmp:
                        records = records_tmp
                        best_throughput_tmp = throughput

            # Search for several designs with fewer resource for tuning
            if records and fine_tune == 1:
                # single-task array
                max_attempt = 3
                n_attempt_list = [max_attempt for i in range(n_arrays)]
                for i in multi_task_arrays:
                    n_attempt_list[i] = 0
                last_record = [None for i in range(n_arrays)]
                while any(y > 0 for y in n_attempt_list):
                    job_list = []
                    skip_idx = []
                    tasks = []
                    for i in single_task_arrays:
                        if i not in resource_alloc["fast_idx"]:
                            skip_idx.append(i)
                            n_attempt_list[i] = 0
                            continue
                        if int(resource_alloc["BRAM18K"][i]) in self.search_cache_cst[i]:
                            # Candidate search has been done for this one before
                            skip_idx.append(i)
                            n_attempt_list[i] = 0
                            continue
                        array_tasks = []
                        unit_dec_bram = 4 # Decrease by 4 each time
                        if last_record[i]:
                            dec_bram = records[i].cst["BRAM18K"]- last_record[i].cst["BRAM18K"] + unit_dec_bram
                        else:
                            dec_bram = unit_dec_bram
                        slow_idx_list = resource_alloc["slow_idx"]
                        fast_idx_list = resource_alloc["fast_idx"]
                        ub_latency = (records[slow_idx_list[0]].latency - records[i].latency) / (len(fast_idx_list) + 1) + records[i].latency
                        n_attempt = n_attempt_list[i]
                        if n_attempt == max_attempt:
                            self.search_cache_cst[i].append(int(resource_alloc["BRAM18K"][i]))
                        if n_attempt > 0:
                            explorer_tmp = copy.deepcopy(self.params["explorer"])
                            explorer_tmp.cst.hw_cst["DSP"] = resource_alloc["DSP"][i]
                            explorer_tmp.cst.hw_cst["BRAM18K"] = records[i].cst["BRAM18K"] - dec_bram
                            for design_idx in self.params["design_idx_list"]:
                                design = explorer_tmp.designs[design_idx]
                                if design.name == records[i].design:
                                    cur_design_idx = design_idx
                            search_record = None
                            for r_c in self.search_cache[i]:
                                if r_c.cst["BRAM18K"] == explorer_tmp.cst.hw_cst["BRAM18K"] and \
                                   r_c.cst["DSP"] == explorer_tmp.cst.hw_cst["DSP"] and \
                                   r_c.design == explorer_tmp.designs[cur_design_idx].name:
                                    search_record = r_c
                                    last_record[i] = search_record
                                    break
                            if not search_record:
                                search_task = SingleTask(explorer_tmp.designs[cur_design_idx], self.search_task.tasks[partition[i][0]].workload, explorer_tmp.cst)
                                # Update the task configs
                                if i == 0:
                                    # Load from DRAM
                                    search_task.configs["cin_read_mode"] = 0
                                elif (i > 0 and len(partition[i - 1]) > 1):
                                    if self.params["use_uram_all"]:
                                        search_task.configs["cin_read_mode"] = 2
                                    else:
                                        search_task.configs["cin_read_mode"] = 0
                                else:
                                    # Access on-chip streaming buffers
                                    search_task.configs["cin_read_mode"] = 2
                                if i == len(partition) - 1:
                                    # Write to DRAM
                                    search_task.configs["cout_write_mode"] = 0
                                elif (i < len(partition) - 1 and len(partition[i + 1]) > 1):
                                    if self.params["use_uram_all"]:
                                        search_task.configs["cout_write_mode"] = 1
                                    else:
                                        search_task.configs["cout_write_mode"] = 0
                                else:
                                    search_task.configs["cout_write_mode"] = 1
                                for repeat in range(1):
                                    job_list.append(
                                    {
                                        "job_hash": f"{str(search_task)}_{repeat}",
                                        "func": explorer_tmp.tune,
                                        "args": [search_task, None, self.sub_task_silent, 0]
                                    })
                                array_tasks.append(search_task)
                        tasks.append(array_tasks)

                    pool = utils.MyExecutor(self.n_worker)
                    results = pool.exec(job_list)

                    idx = 0
                    for i in single_task_arrays:
                        if i in skip_idx:
                            continue
                        array_tasks = tasks[idx]
                        no_valid_record = True
                        for task in array_tasks:
                            for result in results:
                                if result.startswith(str(task)):
                                    record = results[result]
                                    if record.valid:
                                        record.arch_sol = record.task_sols[0]
                                        self.search_cache[i].append(record)
                                        last_record[i] = record
                                        no_valid_record = False

                        idx += 1
                        if no_valid_record:
                            n_attempt_list[i] = 0
                        else:
                            n_attempt_list[i] -= 1

                # multi-task array
                for i in multi_task_arrays:
                    if i not in resource_alloc["fast_idx"]:
                        continue
                    if int(resource_alloc["BRAM18K"][i]) in self.search_cache_cst[i]:
                        continue
                    unit_dec_bram = 16 # Start with 16
                    dec_bram = unit_dec_bram
                    slow_idx_list = resource_alloc["slow_idx"]
                    fast_idx_list = resource_alloc["fast_idx"]
                    ub_latency = (records[slow_idx_list[0]].latency - records[i].latency) / (len(fast_idx_list) + 1) + records[i].latency
                    n_attempt = 2 # Search two designs for multi-task array
                    while n_attempt > 0:
                        explorer_tmp = copy.deepcopy(self.params["explorer"])
                        explorer_tmp.cst.hw_cst["DSP"] = resource_alloc["DSP"][i]
                        explorer_tmp.cst.hw_cst["BRAM18K"] = records[i].cst["BRAM18K"] - dec_bram
                        for design_idx in self.params["design_idx_list"]:
                            design = explorer_tmp.designs[design_idx]
                            if design.name == records[i].design:
                                cur_design_idx = design_idx
                        search_record = None
                        for r_c in self.search_cache[i]:
                            if r_c.cst["BRAM18K"] == explorer_tmp.cst.hw_cst["BRAM18K"] and \
                               r_c.cst["DSP"] == explorer_tmp.cst.hw_cst["DSP"] and \
                               r_c.design == explorer_tmp.designs[cur_design_idx].name:
                                search_record = r_c
                                break
                        if not search_record:
                            if self.params["use_uram_all"]:
                                search_task_configs = {}
                                for task_idx in range(len(partition[i])):
                                    search_task_configs[task_idx] = {'cin_read_mode': 2, 'cout_write_mode': 1}
                                if i == 0:
                                    search_task_configs[0]["cin_read_mode"] = 0
                                if i == n_arrays - 1:
                                    search_task_configs[len(partition[i]) - 1]["cout_write_mode"] = 0
                            else:
                                search_task_configs = None
                            search_record = explorer_tmp.search_non_fusion_single_acc_customized1(\
                                design_idx=cur_design_idx, search_task_configs=search_task_configs, \
                                silent=self.sub_task_silent, \
                                workload_idx=partition[i], one_gen=True)
                            if search_record.valid:
                                self.search_cache[i].append(search_record)
                        if search_record.valid:
                            if n_attempt == 2 and search_record.latency > ub_latency:
                                unit_dec_bram = 4
                                dec_bram = unit_dec_bram
                            else:
                                dec_bram = records[i].cst["BRAM18K"]- search_record.cst["BRAM18K"] + unit_dec_bram
                        else:
                            break
                        n_attempt -= 1
                    self.search_cache_cst[i].append(int(resource_alloc["BRAM18K"][i]))

            is_first = False
            if fine_tune:
                skip_search = 1
                if len(records) == 0:
                    if not adjust_func(partition, resource_alloc, legal_records, 1):
                        break
                else:
                    if best_throughput_tmp > best_throughput:
                        legal_records = copy.deepcopy(records)
                        best_throughput = best_throughput_tmp

                    old_slow_idx = resource_alloc["slow_idx"][0]
                    old_slow_record_latency = resource_alloc["array_latency"][old_slow_idx]
                    slow, fast = self.update_bottleneck_idx(records)
                    resource_alloc["slow_idx"] = slow
                    resource_alloc["fast_idx"] = fast
                    resource_alloc["array_latency"] = [record.latency for record in records]
                    
                    # For internal testing
                    #print("****************** Tuning ******************")
                    #latency_list = [r.latency for r in records]
                    #dsp_list = [r.cst["DSP"] for r in records]
                    #bram_list = [r.cst["BRAM18K"] for r in records]
                    #dsp_eff_list = [r.dsp_eff for r in records]
                    #print("max latency: ", 1 / best_throughput_tmp)
                    #print("latency list: ", latency_list)
                    #print("bram list: ", bram_list)
                    #print("dsp list: ", dsp_list)
                    #print("dsp eff: ", dsp_eff_list)
                    #print("****************** Tuning ******************")

                    if resource_alloc["slow_idx"][0] == old_slow_idx:
                        if records[i].latency <= old_slow_record_latency:
                            break
                        if not adjust_func(partition, resource_alloc, records, 0):
                            break
                    else:
                        break
            else:
                if len(records) == 0:
                    resource_alloc["BRAM18K"] = [n / 2 for n in resource_alloc["BRAM18K"]]
                    #resource_alloc["DSP"] = [n / 2 for n in resource_alloc["DSP"]]
                else:
                    legal_records = records
                    break

        return legal_records

    def search_design(self, partition_idx):
        partition_idx = int(partition_idx)
        if partition_idx in self.bay_search_log:
            return self.bay_search_log[partition_idx]
        #print(len(self.params['partition_candidates']))
        #print(partition_idx)
        self.log(f"Partition {partition_idx}: {self.params['partition_candidates'][partition_idx]['partition']}")
        rewards_window = []
        self.counter.init_counter('local_time')
        local_best_reward = 0
        partition = self.params['partition_candidates'][partition_idx]['partition']
        n_arrays = len(partition)
         # Store all the search records for each array
        for i in range(n_arrays):
            self.search_cache[i] = []
        # Store the resource constraint used for each search to avoid redundant search
        for i in range(n_arrays):
            self.search_cache_cst[i] = []

        # Initialize resource allocation
        resource_alloc = self.resource_alloc(partition)

        # Find a legal config
        records = self.find_legal_config(partition, resource_alloc, skip_search=0)
        if records:
            self.local_epoch = 0
            self.last_update_epoch = 0
            last_slow_idx = -1
            while True:
                latency, used_constraints, throughput, meta = self.evaluate(partition, records)
                dsp_eff = self.est_dsp_eff(throughput, used_constraints)
                reward = throughput
                search_record = utils.SearchRecord().extract_from_tuner_multi_acc(records, reward, latency, used_constraints, throughput, dsp_eff, partition=partition)
                # Update global reward
                if reward > self.best_reward:
                    self.best_reward = reward
                    self.best_search_record = search_record
                    self.log(f'Global Epoch {self.epoch} - Partition {partition_idx} - #Array {n_arrays}: new global best reward: {self.best_reward} (latency: {latency:.0f}, throughput: {throughput}, DSP eff: {dsp_eff:.2f}, BRAM: {used_constraints["BRAM18K"]:.2f}, DSP: {used_constraints["DSP"]:.2f}, URAM: {used_constraints["URAM"]:.2f}, BW: {search_record.bw:.2f})')
                self.best_rewards.append(self.best_reward)
                self.counter.update_counter('time')
                self.best_rewards_time.append(self.counter.get_counter('time'))
                # Update local reward
                if reward > local_best_reward:
                    local_best_reward = reward
                    self.log(f'Local Epoch {self.local_epoch} - Partition {partition_idx} - #Array {n_arrays}: new local best reward: {self.best_reward} (latency: {latency:.0f}, throughput: {throughput}, DSP eff: {dsp_eff:.2f}, BRAM: {used_constraints["BRAM18K"]:.2f}, DSP: {used_constraints["DSP"]:.2f}, URAM: {used_constraints["URAM"]:.2f}, BW: {search_record.bw:.2f})')
                    self.last_update_epoch = self.local_epoch
                rewards_window.append(reward)

                if len(rewards_window) > self.params["max_trial"]:
                    stdev_percent = np.std(rewards_window[-3:]) / np.mean(rewards_window[-3:])
                    if stdev_percent < self.params["reward_stdev_thres"]:
                        self.log(f'Minimal improvement after {self.params["max_trial"]} rounds, terminated')
                        break
                if self.local_epoch - self.last_update_epoch > self.params["max_trial"]:
                    self.log(f'No improvement after {self.params["max_trial"]} rounds, terminated')
                    break
                # If the tuning time is too long, kill it
                self.counter.update_counter('local_time')
                if self.counter.get_counter("local_time") > self.max_time:
                    self.log('Time out, terminated')
                    break

                # Fine-tuning
                if self.is_finetune_required(records, dsp_eff):
                    # Find fastest/slowest design index
                    slow, fast = self.update_bottleneck_idx(records)
                    # Update resource alloc to reflect the current usage
                    for i in range(len(records)):
                        resource_alloc['DSP'][i] = np.ceil(records[i].cst['DSP'])
                        resource_alloc['BRAM18K'][i] = np.ceil(records[i].cst['BRAM18K'])
                    # Adjust resource alloc
                    resource_alloc["init"] = {"DSP": copy.deepcopy(resource_alloc['DSP']),
                                              "BRAM18K": copy.deepcopy(resource_alloc['BRAM18K']),
                                              "DSP_total": used_constraints['DSP'],
                                              "BRAM18K_total": used_constraints['BRAM18K']}
                    resource_alloc["array_latency"] = [record.latency for record in records]
                    resource_alloc["state"] = 0
                    if slow[0] == last_slow_idx:
                        resource_alloc["state"] = 1
                    resource_alloc["slow_idx"] = slow
                    resource_alloc["fast_idx"] = fast
                    resource_alloc["step"] = [[0, 1], [0.025]] # step for resource adjustment
                    resource_alloc["n_adjust"] = [0, 0] # number of attempts at each state
                    resource_alloc["decrease"] = [-1, -1] # indicate if the allocation of bram decreases in the previous round
                    resource_alloc["history"] = [0, 0] # bram allocation in the last round
                    last_slow_idx = slow[0]
                    if not self.resource_alloc_adjust(partition, resource_alloc, records, 0):
                        self.log('No valid resource allocation found, terminated')
                        break
                    records = self.find_legal_config(partition, resource_alloc, old_records=records, adjust_func=self.resource_alloc_adjust, fine_tune=1, skip_search=0)
                    if not records:
                        self.log('No valid records found, terminated')
                        break
                else:
                    self.log('Fine-tuning not required, terminated')
                    break

                self.epoch += 1
                self.local_epoch += 1

        self.bay_search_log[partition_idx] = local_best_reward
        self.bayopt_epoch += 1
        self.bayopt_best_rewards.append(self.best_reward)
        self.counter.update_counter('time')
        self.bayopt_best_rewards_time.append(self.counter.get_counter('time'))
        return local_best_reward

    def search(self):
        self.n_layers = len(self.search_task.tasks)
        if self.n_layers < 2:
            raise RuntimeError("Multi-acc exploration requires at least two conv layers.")
        self.counter.init_counter('time')
        # Bayesian Tuner
        pbounds = {'partition_idx': (0, len(self.params["partition_candidates"]) - 1)} # Right included

        bay_tuner = BayesianOptimization(
            f=self.search_design,
            pbounds=pbounds,
            random_state=1
        )
        for probe_idx in self.params['probe_points']:
            bay_tuner.probe(
                params=[probe_idx],
                lazy=True
            )
        bay_tuner.maximize(
            init_points=0,
            n_iter=10
        )

def multi_acc_search2(search_task, init_tasks, cst, search_obj, max_epochs, max_time, \
                      n_worker=1, silent=0, population_size=20, policy=0, meta=None, explorer=None, profiling=0):
    """ This function finds the best multi-array architecture for a list of tasks.
    The key difference compared to multi_acc_search2 is that in multi_acc_search2,
    we restrain the resource for each array and search the best config for each one.
    However, in multi_acc_search2, we search the array in sequence, when searching the
    next array, we will take into account the previous one, and penalize the configs
    resulting in longer overall latency (setup + array latency).
    """
    import logging
    logger = logging.getLogger('AutoSA-Tuner')
    if silent == 0:
        logger.info("Performing cross layer multi-accelerator genetic search...")

    best_latency = utils.compute_tasks_latency(search_task.tasks, init_tasks)
    if silent == 0:
        logger.info(f'Cross-layer multi-accelerator ideal latency: {best_latency}')

    partition_candidates = meta["partition_candidates"]

    tuner_params = {
        "explorer": explorer,
        "probe_points": meta["init_partition_candidates"],        
        "best_reward": 1 / best_latency if best_latency else None,
        "partition_candidates": partition_candidates,
        "batch_size": meta["batch_size"],        
        "dsp_eff_thres": 0.85, # If the DSP eff is greater than this thres, no fine-tuning is required.
        "latency_stdev_thres": 0.03,
        "reward_stdev_thres": 0.025,
        "max_trial": 3 # Terminate fine-tuning after more than x trials
    }
    if meta:
        tuner_params["design_idx_list"] = meta['design_idx_list']

    if max_epochs > 0:
        pass
    else:
        max_time = 1800 # 30 minutes at most

    tuner = MultiAccTuner2(search_task, cst, search_obj, max_epochs, max_time, tuner_params, n_worker, silent)
    tuner.search()

    search_record = tuner.best_search_record
    
    now = datetime.now()
    config_str = f"_{explorer.search_config['workload']}_multi2"        
    with open(f'tmp/tuning_rewards{config_str}.csv', "w", newline='') as f:
        fieldnames = ['epoch', 'reward', 'time']
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for epoch in range(len(tuner.best_rewards)):
            writer.writerow({'epoch': epoch, 'reward': tuner.best_rewards[epoch], 'time': tuner.best_rewards_time[epoch]})

    return search_record

class MultiAccTuner2(MultiAccTuner1):
    def __init__(self, search_task, cst, obj, max_epoch, max_time, params, n_worker=1, silent=0):
        super().__init__(search_task, cst, obj, max_epoch, max_time, params, n_worker=n_worker, silent=silent)

    def est_mem(self, partition, records, verbose=0):
        """ Estimate the total memory usage.
        BRAM18K is consumed by two parts: arrays and streaming buffers in-between.
        For two adjacent arrays, suppose their tiling factors as:
        [tr1, tc1, to1, ti1] and [tr2, tc2, to2, ti2]
        Compute the tiling factors such that:
        tr' = c0 * tr1
        tc' = c1 * tc1
        (c0 - 1) * tr1 < tr2 + k - 1 <= c0 * tr1
        (c1 - 1) * tc1 < tc2 + k - 1 <= c1 * tc1
        Streaming buffers are allocated to hold at least:
        tr' * tc' * o1(i2) * 2
        such that when the second array is using the first block of (tr2 + k - 1) * ... * i2,
        the first array will continue to fill the rest of the buffer for the next round.
        If verbose is set to 1, return the detailed resource usage of each array and streaming buffer.

        Streaming buffers are mapped to URAMs for this architecture.
        """
        array_bufs = []
        stream_bufs = [0 for i in range(len(records))]
        BRAM18K_total = 0
        URAM_total = 0
        # array bufs
        for i in range(len(records)):
            record = records[i]
            BRAM18K_total += record.cst["BRAM18K"]
            array_bufs.append(record.cst["BRAM18K"])

        # streaming buffers
        for round in range(len(partition[0])):
            for i in range(1, len(records)):
                if round >= len(partition[i - 1]):
                    continue
                layer1_idx = partition[i - 1][round]
                if round >= len(partition[i]):
                    continue
                layer2_idx = partition[i][round]
                array1 = records[i - 1].task_sols[round]
                array2 = records[i].task_sols[round]
                # Extract parameters of array 1
                o1 = self.search_task.tasks[layer1_idx].workload['params']['o']
                tr1 = min(array1['sol']['r_t1'], self.search_task.tasks[layer1_idx].workload['params']['r'])
                tc1 = min(array1['sol']['c_t1'], self.search_task.tasks[layer1_idx].workload['params']['c'])
                for tag in self.search_task.tasks[layer1_idx].workload['tags']:
                    if tag.startswith('maxpool'):
                        stride = int(tag.split('_')[-1])
                        tr1 /= stride
                        tc1 /= stride
                tr1 = max(int(tr1), 1)
                tc1 = max(int(tc1), 1)
                # Extract parameters of array 2
                tr2 = min(array2['sol']['r_t1'], self.search_task.tasks[layer2_idx].workload['params']['r'])
                tc2 = min(array2['sol']['c_t1'], self.search_task.tasks[layer2_idx].workload['params']['c'])
                k = self.search_task.tasks[layer2_idx].workload['params']['p']
                data_pack = array2['sol']['i_t2']
                # Compute the BRAM size
                c0 = np.ceil((tr2 + k - 1) / tr1)
                c1 = np.ceil((tc2 + k - 1) / tc1)
                array1_params = self.search_task.tasks[layer1_idx].workload["params"]
                array2_params = self.search_task.tasks[layer2_idx].workload["params"]
                trp = min(c0 * tr1, array1_params['r'])
                tcp = min(c1 * tc1, array1_params['c'])
                #ele_num = trp * tcp * o1 * 2
                ele_num = min(trp * array1_params['c'] * o1, tcp * array1_params['r'] * o1)

                #buffer = np.ceil(self.search_task.dw * data_pack * 8 / 36) * np.ceil(ele_num / data_pack / 512)
                buffer = np.ceil(self.search_task.dw * data_pack * 8 / 72) * np.ceil(ele_num / data_pack / 4096)

                #print(array1['sol'])
                #print(array2['sol'])
                #print(c0, c1, tr1, tc1, trp, tcp, o1)
                #print(i, data_pack, ele_num, buffer)
                stream_bufs[i] = max(stream_bufs[i], buffer)

        #BRAM18K_total += np.sum(stream_bufs)
        URAM_total = np.sum(stream_bufs)
        if verbose == 0:
            return {"BRAM18K": BRAM18K_total, "URAM": URAM_total}, None
        else:
            return {"BRAM18K": BRAM18K_total, "URAM": URAM_total}, {"array_bufs": array_bufs, "stream_bufs": stream_bufs}

    #def overuse_resource(self, partition, records):
    #    for record in records:
    #        if record.valid == 0:
    #            return True
    #    mem, meta = self.est_mem(partition, records)
    #    DSP = 0
    #    for record in records:
    #        DSP += record.cst["DSP"]
    #    BRAM18K = mem["BRAM18K"]
    #    URAM = mem["URAM"]
    #    if BRAM18K > self.cst.hw_cst["BRAM18K"]:
    #        return True
    #    if URAM > self.cst.hw_cst["URAM"]:
    #        return True
    #    if DSP > self.cst.hw_cst["DSP"]:
    #        return True
#
    #    return False

    #def est_resource(self, partition, records):
    #    mem, meta = self.est_mem(partition, records)
    #    DSP = 0
    #    for record in records:
    #        DSP += record.cst["DSP"]
#
    #    return {"DSP": DSP, "BRAM18K": mem["BRAM18K"], "URAM": mem["URAM"]}

    def est_latency(self, partition, records, in_place=0, adjust=0, verbose=0):
        """ Compute the latency of the design.
        The execution model is that at each round, each array will execute the layer at the head of
        its partition list. Between arrays, there are streaming buffers that make sure the computation
        gets started as soon as the data are available from the previous array.
        Until all the arrays finish their tasks, we will start the next round.
        If in_place is set to 1, records latency will be updated.
        If adjust is set to 1, we will consider the possible stall between arrays.
        """
        record_latency = []
        for r in records:
            tmp_latency = [s["latency"] for s in r.task_sols]
            record_latency.append(tmp_latency)

        total_latency = [0 for i in range(len(records))]
        for latency in record_latency[0]:
            total_latency[0] += (latency * self.params["batch_size"])
        # Store the setup/array latency of each array at each round
        round_info = []

        design_latency = 0
        for round in range(len(partition[0])):
            setup_latency = [0]
            array_latency = [record_latency[0][round] * self.params["batch_size"]]

            # Update the array and setup latency
            for i in range(1, len(records)):
                if round >= len(partition[i - 1]):
                    continue
                layer1_idx = partition[i - 1][round]
                if round >= len(partition[i]):
                    continue
                layer2_idx = partition[i][round]
                array1 = records[i - 1].task_sols[round]
                array2 = records[i].task_sols[round]
                # Extract the parameters of array 1
                o1 = self.search_task.tasks[layer1_idx].workload['params']['o']
                tr1 = min(array1['sol']['r_t1'], self.search_task.tasks[layer1_idx].workload['params']['r'])
                tc1 = min(array1['sol']['c_t1'], self.search_task.tasks[layer1_idx].workload['params']['c'])
                tr1_post = tr1
                tc1_post = tc1
                for tag in self.search_task.tasks[layer1_idx].workload['tags']:
                    if tag.startswith('maxpool'):
                        stride = int(tag.split('_')[-1])
                        tr1_post /= stride
                        tc1_post /= stride
                tr1_post = max(int(tr1_post), 1)
                tc1_post = max(int(tc1_post), 1)
                # Extract parameters of array 2
                tr2 = min(array2['sol']['r_t1'], self.search_task.tasks[layer2_idx].workload['params']['r'])
                tc2 = min(array2['sol']['c_t1'], self.search_task.tasks[layer2_idx].workload['params']['c'])
                k = self.search_task.tasks[layer2_idx].workload['params']['p']
                data_pack = array2['sol']['i_t2']

                c0 = np.ceil((tr2 + k - 1) / tr1_post)
                c1 = np.ceil((tc2 + k - 1) / tc1_post)
                array1_params = self.search_task.tasks[layer1_idx].workload["params"]
                array2_params = self.search_task.tasks[layer2_idx].workload["params"]
                trp = min(c0 * tr1, array1_params['r'])
                tcp = min(c1 * tc1, array1_params['c'])
                # Set up latency
                #if (array1['sol']['r_t1'] == array2['sol']['r_t1']) and \
                #   (array1['sol']['c_t1'] == array2['sol']['c_t1']):
                #    tri = np.ceil(array2['sol']['i_t1'] / array1['sol']['o_t1']) * array1['sol']['o_t1']
                #    setup = array_latency[-1] / (np.ceil(array1_params['o'] / tri))
                #else:
                #setup = record_latency[i - 1][round] / (np.ceil(array1_params['r'] / trp) * np.ceil(array1_params['c'] / tcp))
                if trp > tcp:
                    setup = record_latency[i - 1][round] / np.ceil(array1_params['c'] / tcp)
                else:
                    setup = record_latency[i - 1][round] / np.ceil(array1_params['r'] / trp)
                #setup = 0
                setup_latency.append(setup)

                # Adjust the array latency
                if adjust:
                    raise RuntimeError("Array latency adjust for multi-array is not implemented.")
                    """
                    # Consider the fine-grained produce-consume relationship
                    n_fill_rounds = np.ceil((min(2 * tr2 + k - 1, array1_params['r'] + k - 1) - c0 * tr1_post) / tr1_post) * c1
                    fill_latency = array_latency[-1] / (np.ceil(array1_params['r'] / tr1 * np.ceil(array1_params['c'] / tc1))) * n_fill_rounds
                    consume_latency = record_latency[i][round] / (np.ceil(array2_params['r'] / tr2 * np.ceil(array2_params['c'] / tc2)))
                    adjusted_latency = max(fill_latency, consume_latency) * np.ceil(array2_params['r'] / tr2) * np.ceil(array2_params['c'] / tc2)
                    record_latency[i][round] = adjusted_latency
                    array_latency.append(adjusted_latency)
                    """
                else:
                    # Simply compute the max
                    array_latency.append(max(record_latency[i][round] * self.params["batch_size"], array_latency[i - 1]))

                for prev_i in range(i + 1):
                    total_latency[i] += setup_latency[prev_i]
                total_latency[i] += array_latency[i]

            local_round_latency = 0
            for lat in setup_latency:
                local_round_latency += lat
            local_round_latency += array_latency[-1]
            design_latency += local_round_latency
            
            total_off_chip_trans = 0
            for i in range(len(records)):
                if round >= len(partition[i]):
                    break
                off_chip_acc_num_meta = records[i].task_sols[round]["reward_meta"]["activity"]["off_chip_acc_num_meta"]
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
                if i in range(1, len(records)):
                    cin_trans = 0
                if i in range(len(records) - 1):
                    cout_trans = 0
                total_off_chip_trans += (cin_trans + w_trans + cout_trans)

            round_info.append({"latency": local_round_latency, "setup": setup_latency, 
                               "total_off_chip_trans": total_off_chip_trans,
                               "array": [], "sol": [], "params": []})
            for i in range(len(records)):
                if round >= len(partition[i]):
                    continue
                round_info[-1]["array"].append(records[i].task_sols[round]["latency"])
                #round_info[-1]["sol"].append(records[i].task_sols[round]["sol"])
                #round_info[-1]["params"].append(self.search_task.tasks[partition[i][round]].workload["params"])

        if in_place:
            for i in range(1, len(records)):
                records[i].latency = total_latency[i]

        # Throughput
        throughput = 1 / design_latency * self.params["batch_size"]

        if verbose == 1:
            return design_latency, throughput, {"total_latency": total_latency, "round_info": round_info}
        else:
            return design_latency, throughput, {"total_latency": total_latency, "round_info": round_info}

    #def evaluate(self, partition, records, verbose=0):
    #    latency, throughput, meta = self.est_latency(partition, records, verbose=verbose)
    #    resource = self.est_resource(partition, records)
    #    return latency, resource, throughput, meta

    def find_legal_config(self, partition, resource_alloc, old_records=None, adjust_func=None, fine_tune=0, skip_search=1):
        legal_records = old_records
        best_throughput = 0
        is_first = True
        n_arrays = len(partition)
        # Maintain a list of several best designs for each array
        history = [[] for i in range(n_arrays)]
        history_thres = 2
        #history_thres = 1
        if n_arrays > 10:
            # Aovid storing too many configs
            history_thres = 1

        while True:
            ## For internal testing
            #print("****************** Allocation ******************")
            #pprint.pprint(resource_alloc)
            #print("****************** Allocation ******************")
            #start  = time.time()

            records = []
            skip_idx = []
            job_list = []
            tasks = []
            for i in range(n_arrays):
                # Update the history
                history_tmp = []
                for record in history[i]:
                    if record.cst["BRAM18K"] <= resource_alloc["BRAM18K"][i] and \
                       record.cst["DSP"] <= resource_alloc["DSP"][i]:
                       history_tmp.append(record)
                if legal_records and is_first:
                    if legal_records[i].cst["BRAM18K"] <= resource_alloc["BRAM18K"][i] and \
                       legal_records[i].cst["DSP"] <= resource_alloc["DSP"][i]:
                       history_tmp.append(legal_records[i])
                       self.search_cache[i].append(legal_records[i])
                history[i] = history_tmp
                if skip_search == 1:
                    if (i in resource_alloc["fast_idx"] and len(history[i]) > 0) or \
                       ((i not in resource_alloc["fast_idx"]) and (i not in resource_alloc["slow_idx"]) and len(history[i]) > 0):
                    #if ((i not in resource_alloc["slow_idx"]) and (i not in resource_alloc["fast_idx"])) or len(history[i]) > 0:
                        #if resource_alloc["state"] == 0:
                        #    if i < min(resource_alloc["slow_idx"]):
                        #        skip_idx.append(i)
                        #        continue
                        #elif resource_alloc["state"] == 1:
                        #    if i < min(resource_alloc["slow_idx"]) and i < min(resource_alloc["fast_idx"]):
                        #        skip_idx.append(i)
                        #        continue

                        skip_idx.append(i)
                        continue

                #print("skipped: ", skip_idx)
                #job_list = []
                for design_idx in self.params["design_idx_list"]:
                    # Submit the search job
                    #local_start = time.time()
                    explorer_tmp = copy.deepcopy(self.params["explorer"])
                    #local_end = time.time()
                    #print("copy time: ", local_end - local_start)
                    # Update the constraints
                    explorer_tmp.cst.hw_cst["DSP"] = resource_alloc["DSP"][i]
                    explorer_tmp.cst.hw_cst["BRAM18K"] = resource_alloc["BRAM18K"][i]
                    early_stop = -1
                    search_task_configs = {}
                    for task_idx in range(len(partition[i])):
                        search_task_configs[task_idx] = {'cin_read_mode': 2, 'cout_write_mode': 1}
                    if i == 0:
                        for task_idx in range(len(partition[i])):
                            # Load from DRAM
                            search_task_configs[task_idx]['cin_read_mode'] = 0
                    if i == n_arrays - 1:
                        for task_idx in range(len(partition[i])):
                            # Write to DRAM
                            search_task_configs[task_idx]['cout_write_mode'] = 0

                    if i > 0 and len(history[i - 1]) > 0:
                        prev_array = {"record": history[i - 1][0], "workloads": partition[i - 1]}
                    else:
                        prev_array = None
                    # Parallel version
                    #print(f"{i}_{design_idx}")
                    job_list.append(
                        {
                            "job_hash": f"{i}_{design_idx}",
                            "func": explorer_tmp.search_non_fusion_single_acc_customized1,
                            "args": [design_idx, search_task_configs, -1, self.sub_task_silent, partition[i], None, True]
                            #"args": [design_idx, search_task_configs, -1, self.sub_task_silent, partition[i], prev_array, True]
                        }
                    )
                    # Sequential version
                    #search_record = explorer_tmp.search_non_fusion_single_acc_customized1(\
                    #    design_idx=design_idx, silent=self.sub_task_silent, \
                    #    workload_idx=partition[i], early_stop=early_stop, \
                    #    search_task_configs=search_task_configs, prev_array=prev_array, one_gen=True)
                    #if search_record.valid:
                    #    early_stop = search_record.latency
                    #    history[i].append(search_record)
                    #    self.search_cache[i].append(search_record)

            pool = utils.MyExecutor(max(int(self.n_worker/8), 8))
            results = pool.exec(job_list)
            for i in range(n_arrays):
                if i in skip_idx:
                    continue
                for design_idx in self.params["design_idx_list"]:
                    search_record = results[f"{i}_{design_idx}"]
                    if search_record.valid:
                        history[i].append(search_record)
                        self.search_cache[i].append(search_record)
                def take_latency(record):
                    return record.latency
                history[i].sort(key=take_latency)
                history[i] = history[i][:min(len(history[i]), history_thres)]

            #end = time.time()
            #print("eval time: ", end - start)
            #start  = time.time()

            # Find the array combination that satisfies the memory usage
            choices_tmp = [list(range(len(h))) for h in history]
            choices = list(itertools.product(*choices_tmp))
            #print("total choices: ", len(choices))
            max_bram_tmp = 0
            min_bram_tmp = float("inf")
            best_throughput_tmp = 0
            for choice in choices:
                records_tmp = []
                for i in range(n_arrays):
                    records_tmp.append(history[i][choice[i]])
                latency, throughput, _ = self.est_latency(partition, records_tmp)
                memory, meta = self.est_mem(partition, records_tmp, verbose=0)
                #print("array_bufs: ", meta["array_bufs"])
                #print("stream_bufs: ", meta["stream_bufs"])
                #if memory > max_bram_tmp:
                #    max_bram_tmp = memory["BRAM18K"]
                #if memory < min_bram_tmp:
                #    min_bram_tmp = memory["BRAM18K"]
                #if memory < self.cst.hw_cst["BRAM18K"]:
                if not self.overuse_resource(partition, records_tmp):
                    if throughput > best_throughput_tmp:
                        records = records_tmp
                        best_throughput_tmp = throughput

            #end = time.time()
            #print(f"select time: {end - start} (avg: {(end - start) / len(choices)})")
            #start  = time.time()

            # Search for several designs with fewer resource for tuning
            if records and fine_tune == 1:
                for i in range(n_arrays):
                    if i not in resource_alloc["fast_idx"]:
                        continue
                    if int(resource_alloc["BRAM18K"][i]) in self.search_cache_cst[i]:
                        continue
                    unit_dec_bram = 16 # Start with 16
                    dec_bram = unit_dec_bram
                    slow_idx_list = resource_alloc["slow_idx"]
                    fast_idx_list = resource_alloc["fast_idx"]
                    ub_latency = (records[slow_idx_list[0]].latency - records[i].latency) / (len(fast_idx_list) + 1) + records[i].latency
                    #print("cache ub latency: ", ub_latency)
                    n_attempt = 2
                    while n_attempt > 0:
                        explorer_tmp = copy.deepcopy(self.params["explorer"])
                        explorer_tmp.cst.hw_cst["DSP"] = resource_alloc["DSP"][i]
                        explorer_tmp.cst.hw_cst["BRAM18K"] = records[i].cst["BRAM18K"] - dec_bram
                        for design_idx in self.params["design_idx_list"]:
                            design = explorer_tmp.designs[design_idx]
                            if design.name == records[i].design:
                                cur_design_idx = design_idx
                        search_record = None
                        for r_c in self.search_cache[i]:
                            if r_c.cst["BRAM18K"] == explorer_tmp.cst.hw_cst["BRAM18K"] and \
                               r_c.cst["DSP"] == explorer_tmp.cst.hw_cst["DSP"] and \
                               r_c.design == explorer_tmp.designs[cur_design_idx].name:
                                search_record = r_c
                                break
                        if not search_record:
                            search_task_configs = {}
                            for task_idx in range(len(partition[i])):
                                search_task_configs[task_idx] = {'cin_read_mode': 2, 'cout_write_mode': 1}
                            if i == 0:
                                for task_idx in range(len(partition[i])):
                                    # Load from DRAM
                                    search_task_configs[task_idx]['cin_read_mode'] = 0
                            if i == n_arrays - 1:
                                for task_idx in range(len(partition[i])):
                                    # Write to DRAM
                                    search_task_configs[task_idx]['cout_write_mode'] = 0
                            if i > 0:
                                prev_array = {"record": records[i - 1], "workloads": partition[i - 1]}
                            else:
                                prev_array = None
                            search_record = explorer_tmp.search_non_fusion_single_acc_customized1(\
                                design_idx=cur_design_idx, silent=self.sub_task_silent, workload_idx=partition[i], \
                                #search_task_configs=search_task_configs, prev_array=prev_array, one_gen=True)
                                search_task_configs=search_task_configs, prev_array=None, one_gen=True)
                            if search_record.valid:
                                self.search_cache[i].append(search_record)
                        if search_record.valid:
                            #print("cache searching: ", search_record.cst["BRAM18K"], search_record.latency)
                            if n_attempt == 2 and search_record.latency > ub_latency:
                                unit_dec_bram = 4
                                dec_bram = unit_dec_bram
                            else:
                                dec_bram = records[i].cst["BRAM18K"]- search_record.cst["BRAM18K"] + unit_dec_bram
                        else:
                            break
                        n_attempt -= 1
                    self.search_cache_cst[i].append(int(resource_alloc["BRAM18K"][i]))

            #end = time.time()
            #print("cache time: ", end - start)

            is_first = False
            if fine_tune:
                skip_search = 1
                if len(records) == 0:
                    if not adjust_func(partition, resource_alloc, legal_records, 1):
                        break
                else:
                    if best_throughput_tmp > best_throughput:
                        legal_records = copy.deepcopy(records)
                        best_throughput = best_throughput_tmp

                    latency_list = [r.latency for r in records]
                    old_slow_idx = resource_alloc["slow_idx"][0]
                    old_slow_record_latency = resource_alloc["array_latency"][old_slow_idx]
                    slow, fast = self.update_bottleneck_idx(records)
                    resource_alloc["slow_idx"] = slow
                    resource_alloc["fast_idx"] = fast
                    resource_alloc["array_latency"] = [record.latency for record in records]

                    ## For internal testing
                    #print("****************** Tuning ******************")
                    #latency, throughput, meta = self.est_latency(partition, records, verbose=1)
                    #print("total_latency: ", meta["total_latency"])
                    #print("latency: ", latency)
                    #print("throughput: ", throughput)
                    #print("round_info: ")
                    #pprint.pprint(meta["round_info"])
                    #memory, meta = self.est_mem(partition, records, verbose=1)
                    #print("memory: ", memory)
                    #print("array_bufs: ", meta["array_bufs"])
                    #print("stream_bufs: ", meta["stream_bufs"])
                    #latency_list = [r.latency for r in records]
                    #dsp_list = [r.cst["DSP"] for r in records]
                    #dsf_eff_list = [r.dsp_eff for r in records]
                    #bram_list = [r.cst["BRAM18K"] for r in records]
                    #kernel_list = [r.design for r in records]
                    #print("latency list: ", latency_list)
                    #print("bram list: ", bram_list)
                    #print("dsp list: ", dsp_list)
                    #print("dsp eff list: ", dsf_eff_list)
                    #print("kernel list: ", kernel_list)
                    #print("****************** Tuning ******************")

                    if resource_alloc["slow_idx"][0] == old_slow_idx:
                        # If the performance is not improved upon the last time, break as well
                        if records[i].latency <= old_slow_record_latency:
                            break
                        if not adjust_func(partition, resource_alloc, records, 0):
                            break
                    else:
                        break
            else:
                if len(records) == 0:
                    resource_alloc["BRAM18K"] = [n / 2 for n in resource_alloc["BRAM18K"]]
                    #resource_alloc["DSP"] = [n / 2 for n in resource_alloc["DSP"]]
                else:
                    legal_records = records
                    break

        return legal_records

    def search_design(self, partition_idx):
        partition_idx = int(partition_idx)
        if partition_idx in self.bay_search_log:
            return self.bay_search_log[partition_idx]
        #n_array = int(n_array)
        #if n_array in self.bay_search_log:
        #    return self.bay_search_log[n_array]
        self.log(f"Partition {partition_idx}: {self.params['partition_candidates'][partition_idx]['partition']}, #Array: {len(self.params['partition_candidates'][partition_idx]['partition'])}")
        #self.log(f"#Array: {n_array}")
        rewards_window = []
        self.counter.init_counter('local_time')
        local_best_reward = 0
        # Build the partition
        partition = self.params['partition_candidates'][partition_idx]['partition']
        n_arrays = len(partition)
        #partition = [[] for i in range(n_array)]
        #for i in range(len(self.search_task.tasks)):
        #    array_idx = i % n_array
        #    partition[array_idx].append(i)
        # Store all the search records for each array
        for i in range(n_arrays):
            self.search_cache[i] = []
        # Store the resource constraint used for each search to avoid redundant search
        for i in range(n_arrays):
            self.search_cache_cst[i] = []

        # Initialize resource allocation
        resource_alloc = self.resource_alloc(partition)

        # Find a legal config
        records = self.find_legal_config(partition, resource_alloc, skip_search=0)
        if records:
            self.local_epoch = 0
            self.last_update_epoch = 0
            last_slow_idx = -1
            while True:
                latency, used_constraints, throughput, meta = self.evaluate(partition, records, verbose=1)
                dsp_eff = self.est_dsp_eff(throughput, used_constraints)                
                reward = throughput
                search_record = utils.SearchRecord().extract_from_tuner_multi_acc(records, reward, latency, used_constraints, throughput, dsp_eff, partition=partition)
                # Update global reward
                if reward > self.best_reward:
                    self.best_reward = reward
                    self.best_search_record = search_record                    
                    self.log(f'Global Epoch {self.epoch} - #Array {n_arrays}: new global best reward: {self.best_reward} (latency: {latency:.0f}, throughput: {throughput}, DSP eff: {dsp_eff:.2f}, BRAM: {used_constraints["BRAM18K"]:.2f}, DSP: {used_constraints["DSP"]:.2f}, URAM: {used_constraints["URAM"]:.2f}, BW: {search_record.bw:.2f})')
                self.best_rewards.append(self.best_reward)
                self.counter.update_counter('time')
                self.best_rewards_time.append(self.counter.get_counter('time'))
                # Update local reward
                if reward > local_best_reward:
                    local_best_reward = reward
                    self.log(f'Local Epoch {self.local_epoch} - #Array {n_arrays}: new local best reward: {self.best_reward} (latency: {latency:.0f}, throughput: {throughput}, DSP eff: {dsp_eff:.2f}, BRAM: {used_constraints["BRAM18K"]:.2f}, DSP: {used_constraints["DSP"]:.2f}, URAM: {used_constraints["URAM"]:.2f}, BW: {search_record.bw:.2f})')
                    self.last_update_epoch = self.local_epoch
                rewards_window.append(reward)

                if len(rewards_window) > self.params["max_trial"]:
                    stdev_percent = np.std(rewards_window[-3:]) / np.mean(rewards_window[-3:])
                    if stdev_percent < self.params["reward_stdev_thres"]:
                        self.log(f'Minimal improvement after {self.params["max_trial"]} rounds, terminated')
                        break
                if self.local_epoch - self.last_update_epoch > self.params["max_trial"]:
                    self.log(f'No improvement after {self.params["max_trial"]} rounds, terminated')
                    break
                # If the tuning time is too long, kill it
                self.counter.update_counter('local_time')
                if self.counter.get_counter("local_time") > self.max_time:
                    self.log('Time out, terminated')
                    break

                # Fine-tuning
                if self.is_finetune_required(records, dsp_eff):
                    # Find fastest/slowest design index
                    slow, fast = self.update_bottleneck_idx(records)
                    # Update resource alloc to reflect the current usage
                    for i in range(len(records)):
                        resource_alloc['DSP'][i] = np.ceil(records[i].cst['DSP'])
                        resource_alloc['BRAM18K'][i] = np.ceil(records[i].cst['BRAM18K'])
                    # Adjust resource alloc
                    resource_alloc["init"] = {"DSP": copy.deepcopy(resource_alloc['DSP']),
                                              "BRAM18K": copy.deepcopy(resource_alloc['BRAM18K']),
                                              "DSP_total": used_constraints['DSP'],
                                              "BRAM18K_total": used_constraints['BRAM18K'],
                                              "URAM_total": used_constraints['URAM'],
                                              }
                    resource_alloc["array_latency"] = [record.latency for record in records]
                    resource_alloc["state"] = 0
                    if slow[0] == last_slow_idx:
                        resource_alloc["state"] = 1
                    resource_alloc["slow_idx"] = slow
                    resource_alloc["fast_idx"] = fast
                    resource_alloc["step"] = [[0, 1], [0.025]] # step for resource adjustment
                    resource_alloc["n_adjust"] = [0, 0] # number of attempts at each state
                    resource_alloc["decrease"] = [-1, -1] # indicate if the allocation of bram decreases in the previous round
                    resource_alloc["history"] = [0, 0] # bram allocation in the last round
                    last_slow_idx = slow[0]
                    if not self.resource_alloc_adjust(partition, resource_alloc, records, 0):
                        self.log('No valid resource allocation found, terminated')
                        break
                    records = self.find_legal_config(partition, resource_alloc, old_records=records, adjust_func=self.resource_alloc_adjust, fine_tune=1, skip_search=0)
                    if not records:
                        self.log('No valid records found, terminated')
                        break
                else:
                    self.log('Fine-tuning not required, terminated')
                    break

                self.epoch += 1
                self.local_epoch += 1

        self.bay_search_log[partition_idx] = local_best_reward
        return local_best_reward

    def search(self):
        self.n_layers = len(self.search_task.tasks)
        if self.n_layers < 2:
            raise RuntimeError("Multi-acc exploration requires at least two conv layers.")
        self.counter.init_counter('time')
        # Bayesian Tuner
        #pbounds = {'n_array': (2, min(self.n_layers, self.params["n_array_max"]))} # Right included
        pbounds = {'partition_idx': (0, len(self.params["partition_candidates"]) - 1)} # Right included

        bay_tuner = BayesianOptimization(
            f=self.search_design,
            pbounds=pbounds,
            random_state=1
        )
        for probe_idx in self.params['probe_points']:
            bay_tuner.probe(
                params=[probe_idx],
                lazy=True
            )
        bay_tuner.maximize(
            init_points=0,
            n_iter=10
        )
