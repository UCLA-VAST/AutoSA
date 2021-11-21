import json

#with open('workload/vgg16.json') as f:
#with open('workload/resnet50.json') as f:
with open('workload/mobilenetv2.json') as f:
    data = json.load(f)

for layer in data["workloads"]:
    i, o, r, c, p, q = layer["params"]["i"], layer["params"]["o"], layer["params"]["r"], \
                       layer["params"]["c"], layer["params"]["p"], layer["params"]["q"]
    gemm_i = o
    gemm_j = r * c
    gemm_k = i * p * q
    layer["params"] = {"i": gemm_i, "j": gemm_j, "k": gemm_k}
    layer["tags"] = ["gemm"]


#with open("workload/vgg16_img2col.json", "w") as f:
#with open("workload/resnet50_img2col.json", "w") as f:
with open("workload/mobilenetv2_img2col.json", "w") as f:
    json.dump(data, f, indent=2)