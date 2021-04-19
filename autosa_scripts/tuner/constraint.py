import json

class Constraint(object):
    def __init__(self, cst_path):
        with open(cst_path) as f:
            data = json.load(f)
        self.hw_cst = {}
        for res in data:
            self.hw_cst[res] = data[res]["total"] * data[res]["ratio"]        
            self.hw_cst[f'{res}_total'] = data[res]["total"]

    def __repr__(self):
        ret = ""
        ret += f"b{int(self.hw_cst['BRAM18K'])}"
        ret += f"d{int(self.hw_cst['DSP'])}"
        return ret    