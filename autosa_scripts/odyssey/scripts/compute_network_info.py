import csv
import json

csv_columns = ["Layer", "Name", "i", "o", "r", "c", "p", "q", "ops", "parallelism", "ai", "parallelism_norm", "ai_norm",
               "throughput_free", "dsp_eff_free", "kernel", "latency_fixed", "dsp_eff_fixed", "throughput", "throughput_norm"]
dict_data = []
with open("../workload/resnet50.json", "r") as f:
    network_data = json.load(f)
#for layer in network_data["workloads"]:
parallelism_min = float("inf")
ai_min = float("inf")
for idx in range(len(network_data["workloads"])):
    layer = network_data["workloads"][idx]
    i, o, r, c, p, q = layer["params"]["i"], layer["params"]["o"], layer["params"]["r"], layer["params"]["c"], \
                       layer["params"]["p"], layer["params"]["q"]
    dict_data.append({
        'Layer': idx + 1,
        'Name': layer["name"],
        'i': i, 'o': o, 'r': r, 'c': c, 'p': p, 'q': q,
        "ops": i*o*r*c*p*q, "parallelism": o*r*c, "ai": i*o*r*c*p*q/(i*(r+p-1)*(c+q-1)+o*r*c+i*o*p*q)
    })
    parallelism_min = min(parallelism_min, dict_data[-1]["parallelism"])
    ai_min = min(ai_min, dict_data[-1]["ai"])
# normalize
for data in dict_data:
    data["parallelism_norm"] = data["parallelism"] / parallelism_min
    data["ai_norm"] = data["ai"] / ai_min

# load the tuning log
log_file = "/home/jaywang/AutoSA_Tuner/refactor2/outdir/O_latency-W_resnet50-C_f0ma0p0mam0u0-T_2021-07-02-11:52:32.005352/tuning.log"
throughput_min = float("inf")
with open(log_file, "r") as f:
    lines = f.readlines()
    total_layer = 0
    for line_idx in range(len(lines)):
        line = lines[line_idx]    
        if line.find("DSP_eff") != -1:
            dsp_eff = float(line.strip().split(":")[-1].strip(","))
            dict_data[total_layer]["dsp_eff_fixed"] = dsp_eff
            latency = float(lines[line_idx + 2].strip().split(":")[-1].strip(","))
            dict_data[total_layer]["latency_fixed"] = latency
            dict_data[total_layer]["throughput"] = dict_data[total_layer]["ops"] / dict_data[total_layer]["latency_fixed"]
            throughput_min = min(throughput_min, dict_data[total_layer]["throughput"])
            total_layer += 1
            if total_layer >= len(dict_data):
                break

# normalize
for data in dict_data:
    data["throughput_norm"] = data["throughput"] / throughput_min

with open("../tmp/resnet_info.csv", "w") as csvfile:
    write = csv.DictWriter(csvfile, fieldnames=csv_columns)
    write.writeheader()
    for data in dict_data:
        write.writerow(data)