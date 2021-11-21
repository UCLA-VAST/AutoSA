import csv
import json

#network = "resnet50"
network = "mobilenetv2"
with open(f"../workload/{network}.json", "r") as f:
    network_data = json.load(f)
layer_idx = 1
for layer in network_data["workloads"]:
    data = {}
    data["workloads"] = [layer]
    with open(f"../workload/{network}_{layer_idx}.json", "w") as f:
        json.dump(data, f, indent=4)
    layer_idx += 1