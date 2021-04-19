import json

class SearchTask(object):
    def __init__(self, design, task):
        self.design = design
        self.task = task        

    def generate_random_sample(self):
        """ Generate a random sample in the design space.
        """
        pass

    def evaluate(self, params, metric="latency"):
        if metric == "latency":
            latency = self.design.est_latency(params)
            if latency:
                return 1 / latency
            else:
                return 0
        else:                        
		    raise RuntimeError(f"Not supported metric: {metric}")