import json
import random
import numpy as np
import bisect
from sympy import *

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
        return self.design.random_sampling(task_params)        

    def evaluate(self, params, metric="latency"):        
        if metric == "latency":
            params = self.design.infer_params(params)                        
            if params:
                if not self.design.bound_check(params):
                    return 0, None
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