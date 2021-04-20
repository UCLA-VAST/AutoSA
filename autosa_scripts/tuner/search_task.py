import json
import random
import numpy as np
import bisect

import utils

class SearchTask(object):
    def __init__(self, design, task):
        self.design = design
        self.task = task        

    def adjust_params(self, params):
        """ Adjust the parameters based on its contraints.
        """
        def filter_non_power_of_two(x):
            if np.log2(x) != int(np.log2(x)):
                return True
            return False
        
        # Making all factors to be even numbers to have more divisors
        for param in self.design.params_config["tunable"]:
            params[param] = int(np.ceil(params[param] / 2) * 2)        
        
        # Making all divisor factors to be divisors of the dependent variable
        for param in self.design.params_config["tunable"]:
            if "divisors" in param:
                if "tags" in param and "power_of_two" in param["tags"]:
                    choices = utils.get_divisors(params[param["divisors"][0]], filter_non_power_of_two)
                else:
                    choices = utils.get_divisors(params[param["divisors"][0]], None)
                idx = bisect.bisect(choices, params[param])
                if idx >= len(choices):
                    idx -= 1
                if idx > 1:
                    if abs(choices[idx - 1] - params[param]) < abs(choices[idx] - params[param]):
                        idx -= 1
                params[param] = choices[idx]

        return params

    def generate_random_sample(self):
        """ Generate a random sample in the design space.
        """
        task_params = {}
        for param in self.task["params"]:
            task_params[param] = self.task["params"][param]
        while True:        
            params_to_process = []
            for param in self.design.params_config["tunable"]:
                params_to_process.append(self.design.params_config["tunable"][param])
            while len(params_to_process) > 0:
                for param in params_to_process:
                    if "divisors" not in param:
                        bounds = param["bounds"]
                        if not bounds[0].isnumeric():
                            raise RuntimeError(f"Unrecoganized loop bound {bounds[0]}")
                        sample = random.randint(int(bounds[0]), int(task_params[bounds[1]]))
                        task_params[param["name"]] = sample
                        params_to_process.remove(param)
                    if "divisors" in param and param["divisors"] not in params_to_process:
                        bounds = param["bounds"]
                        if not bounds[0].isnumeric():
                            raise RuntimeError(f"Unrecoganized loop bound {bounds[0]}")
                        def filter_non_power_of_two(x):
                            if np.log2(x) != int(np.log2(x)):
                                return True
                            return False
                        if "tags" in param and "power_of_two" in param["tags"]:
                            sample = random.sample(utils.get_divisors(int(task_params[bounds[1]]), filter_non_power_of_two), 1)[-1]
                        else:
                            sample = random.sample(utils.get_divisors(int(task_params[bounds[1]]), filter_non_power_of_two), 1)[-1]
                        task_params[param["name"]] = sample
                        params_to_process.remove(param)
            # Latency hiding
            if self.design.desp["memory"]["PE"]["buf_size"].isnumeric() and int(self.design.desp["memory"]["PE"]["buf_size"]) == 1:
                break
            else:
                latency_factors = 1
                for p in self.design.params_config["tunable"]:
                    param = self.design.params_config["tunable"][p]
                    if param["attr"] == "latency_tiling_factor":
                        latency_factors *= task_params[param["name"]]
                    if param["attr"] == "SIMD_tiling_factor":
                        simd_factor = task_params[param["name"]]
                data_type = self.design.desp["memory"]["PE"]["ele_type"]
                if data_type == "float":
                    if latency_factors > 8 * simd_factor:
                        break
                else:
                    raise RuntimeError(f"Unsupported data type in random sample generation: {data_type}")        

        return task_params

    def evaluate(self, params, metric="latency"):
        if metric == "latency":
            params = self.design.infer_params(params)
            if params:
                latency = self.design.est_latency(params)
                resource = self.design.est_resource(params)
                if latency:
                    return 1 / latency, resource
                else:
                    return 0, None
            else:
                return 0, None
        else:                        
            raise RuntimeError(f"Not supported metric: {metric}")