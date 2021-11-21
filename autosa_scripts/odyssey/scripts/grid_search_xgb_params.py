import os
import subprocess
import re
import pprint

'''
for model_gens in [5, 10, 20, 50]:
    for xgb_thres in [0.2, 0.4, 0.6, 0.8]:
        #for xgb_thres_adjust in [0.2, 0.4, 0.6, 0.8]:
        # data1
        #for xgb_thres_adjust in [0.2, 0.4]: 
        # data2
        for xgb_thres_adjust in [0.6, 0.8]:
            # Call the python command
            cmd = f"python main.py --workload=vgg16 --stop-after-time=10 --use-db=0 --n-worker=32 --design-idx=4 --xgb-n-gens={model_gens} --xgb-thres={xgb_thres} --xgb-thres-adjust={xgb_thres_adjust}"
            #os.system(f"python main.py --workload=vgg16 --stop-after-time=10 --use-db=0 --n-worker=32 --design-idx=4 --xgb-n-gens={model_gens} --xgb-thres={xgb_thres} --xgb-thres-adjust={xgb_thres_adjust}")
            #print(cmd)
            process = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE)
            output, error = process.communicate()
'''

# Collect the best
basepath = "./outdir/"
prjs = os.listdir(basepath)
prjs.sort()
#print(prjs)

results = []
prj_idx = 0
for model_gens in [5, 10, 20, 50]:
    for xgb_thres in [0.2, 0.4, 0.6, 0.8]:        
        for xgb_thres_adjust in [0.6, 0.8]:
            with open(f"./outdir/{prjs[prj_idx]}/tuning.log") as f:
                lines = f.readlines()
                rewards = []
                for line in lines:
                    if line.find("new best reward") != -1:                        
                        epoch = re.search(r"Epoch (.+?):", line).group(1)
                        latency = re.search(r"\((.+?)\)", line).group(1)
                        rewards.append({"epoch": int(epoch), "latency": float(latency)})
                results.append({"configs": [model_gens, xgb_thres, xgb_thres_adjust], "rewards": rewards, "prj": prjs[prj_idx]})
            prj_idx += 1

# Sort the results
def takeBestReward(elem):
    return elem["rewards"][-1]["latency"]
results.sort(key=takeBestReward)
pprint.pprint(results)