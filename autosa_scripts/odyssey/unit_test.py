
import copy
from search_task import SingleTask
from design import Design
import json
from tuners import Constraint

class Workload(object):
    def __init__(self, params):
        self.params = params

    def __repr__(self):
        return f"{self.params}"

class SearchTask(object):
    def __init__(self, workload):
        self.workload = workload

    def __repr__(self):
        return str(self.workload)

def est_mm_performance():
    params = {
        "i": 1024, "i_t1": 129, "i_t2": 3,
        "j": 1024, "j_t1": 130, "j_t2": 13,
        "k": 1024, "k_t1": 64, "k_t2": 4,
        "p9": 16, "p10": 16, "p11": 4, "p12": 4 # A, B, None, C
    }

    # comp
    #params = {
    #    "i": 1024, "i_t1": 520, "i_t2": 26,
    #    "j": 1024, "j_t1": 520, "j_t2": 26,
    #    "k": 1024, "k_t1": 320, "k_t2": 4,
    #    "p9": 16, "p10": 16, "p11": 4, "p12": 4 # A, B, None, C
    #}

    # comm
    #params = {
    #    "i": 1024, "i_t1": 1024, "i_t2": 128,
    #    "j": 1024, "j_t1": 1024, "j_t2": 128,
    #    "k": 1024, "k_t1": 320, "k_t2": 4,
    #    "p9": 16, "p10": 16, "p11": 4, "p12": 4 # A, B, None, C
    #}

    # comm-comp
    #params = {
    #    "i": 1024, "i_t1": 1024, "i_t2": 64,
    #    "j": 1024, "j_t1": 1024, "j_t2": 64,
    #    "k": 1024, "k_t1": 320, "k_t2": 4,
    #    "p9": 16, "p10": 16, "p11": 4, "p12": 4 # A, B, None, C
    #}

    workload = {
        "name": "gemm",
        "tags": ["gemm"],
        "params": {
            "i": 1024, "j": 1024, "k": 1024
        }
    }

    cst = Constraint("cst/hw_cst.json")

    design_dir = "designs"
    kernel_name = "kernel3_2"
    with open(f"designs/{kernel_name}.json", "r") as json_f:
        desp = json.load(json_f)
    design = Design(kernel_name)
    design.register(desp, f"designs/register/{kernel_name}.py")

    search_task = SingleTask(design, workload, cst)
    reward, resource, meta = search_task.evaluate(params)
    print(1 / reward)
    print(resource)
    print(meta)

if __name__ == "__main__":
    est_mm_performance()
