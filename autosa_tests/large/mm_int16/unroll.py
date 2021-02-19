import math

# Modify the parameters here
UNROLL_FACTOR = 32
DATA_T = 'unsigned short'

# Generate the code
data_type = DATA_T
level = int(math.log2(UNROLL_FACTOR))
for layer in range(level - 1, -1, -1):
    pair = int(math.pow(2, layer))
    for i in range(pair):
        # data_t tmp_[layer]_[pair] = tmp_[layer+1]_[pair*2]_[pair*2+1]
        if layer == level - 1:
            print(f'{data_type} mul_{layer}_{i}_0 = local_A[0][{i*2}] * local_B[0][{i*2}];')
            print(f'{data_type} add_{layer}_{i} = mul_{layer}_{i}_0 + local_A[0][{i*2+1}] * local_B[0][{i*2+1}];')
        else:
            print(f'{data_type} add_{layer}_{i} = add_{layer+1}_{i*2} + add_{layer+1}_{i*2+1};')
print('local_C[c7][c6] += add_0_0;')
