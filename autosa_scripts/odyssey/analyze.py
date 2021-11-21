import seaborn as sns
import matplotlib.pyplot as plt
import numpy as np
import csv
import pandas as pd
import os
import re
import scipy

folder = "resnet50_array24"

design_info = {}
with open(f"{folder}/history.log") as f:
    lines = f.readlines()
    design_idx = 0
    design_lines = []
    start_end = []
    for line_idx in range(len(lines)):
        line = lines[line_idx]
        if line.find(f"<record{design_idx}><begin>") != -1:
            start_end.append(line_idx)
        if line.find("arch sol") != -1:
            start_end.append(line_idx)
        if line.find(f"<record{design_idx}><end>") != -1:
            start_end.append(line_idx)
            design_lines.append(start_end)
            start_end = []
            design_idx += 1    
    layer_infos = []
    layer_info = {}
    for design_idx in range(len(design_lines)):
        for line_idx in range(design_lines[design_idx][0], design_lines[design_idx][1]):
            line = lines[line_idx]            
            if line.find("latency") != -1 and 'latency' not in layer_info:
                layer_info["latency"] = float(line.split(":")[-1].strip().strip(','))                
            if line.find("DSP efficiency") != -1:
                layer_info["DSP_eff"] = float(line.split(":")[-1].strip().strip(','))
            if line.find("CTC(FLOP/byte)") != -1:
                layer_info["CTC"] = float(line.split(":")[-1].strip().strip(','))
            if line.find("design") != -1:
                layer_info["design"] = line.split(":")[-1].strip().strip(',')
                dataflow_idx = layer_info["design"][6:]                
                layer_infos.append(layer_info)
                layer_info = {}    
    design_info["array_infos"] = layer_infos

    # Extract the last array
    layer_infos = []
    layer_info = {}
    #print(design_lines[-1][1], design_lines[-1][2])
    for line_idx in range(design_lines[-1][1], design_lines[-1][2]):
        line = lines[line_idx]
        if line.find("\'sol\':") != -1:
            layer_infos.append(layer_info)
            layer_info = {}
        if line.find("\'latency\':") != -1 and 'latency' not in layer_info:
            layer_info["latency"] = float(line.split(":")[-1].strip().strip(','))            
        if line.find("CTC") != -1:
            layer_info["CTC"] = float(line.split(":")[-1].strip().strip(','))
        if line.find("DSP_eff") != -1:
            layer_info["DSP_eff"] = float(line.split(":")[-1].strip().strip(','))            
    design_info["last_array_info"] = layer_infos

# Plot
dict_data = {"Latency": [], "DSP Eff": [], "CTC": [], "Layer": []}
layer_idx = 0
for idx in range(len(design_info["array_infos"]) - 1):
    layer_info = design_info["array_infos"][idx]
    dict_data["Latency"].append(layer_info["latency"])
    dict_data["DSP Eff"].append(layer_info["DSP_eff"])
    dict_data["CTC"].append(layer_info["CTC"])
    dict_data["Layer"].append(layer_idx + 1)
    layer_idx += 1
for idx in range(len(design_info["last_array_info"])):
    layer_info = design_info["last_array_info"][idx]
    #print(layer_info)
    dict_data["Latency"].append(layer_info["latency"])
    dict_data["DSP Eff"].append(layer_info["DSP_eff"])
    dict_data["CTC"].append(layer_info["CTC"])
    dict_data["Layer"].append(layer_idx + 1)
    layer_idx += 1
print("max CTC: ", max(dict_data["CTC"]))
print("max latency: ", max(dict_data["Latency"]))

df = pd.DataFrame.from_dict(dict_data)
sns.set_theme()
sns.set(rc={'figure.figsize':(20,5)})

'''
g = sns.lineplot(
    data=df,
    x="Layer", y="Latency", markers=True
)
g.set(xticks=df.Layer.values)
plt.xlabel("Layer")
plt.ylabel("Latency")
g.figure.savefig("network_latency_cmp")

g = sns.lineplot(
    data=df,
    x="Layer", y="DSP Eff", markers=True
)
g.set(xticks=df.Layer.values)
plt.xlabel("Layer")
plt.ylabel("DSP Eff")
plt.ylim(0, 1.1)
g.figure.savefig("network_dsp_eff_cmp")
'''

g = sns.lineplot(
    data=df,
    x="Layer", y="CTC", markers=True
)
g.set(xticks=df.Layer.values)
plt.xlabel("Layer")
plt.ylabel("CTC")
plt.ylim(0, 250)
g.figure.savefig("network_ctc_cmp")
