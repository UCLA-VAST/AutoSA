import argparse

def run(input_f, output_f, batch):
    new_lines = []
    with open(input_f, 'r') as f:
        lines = f.readlines()
    inside_module = False
    inside_inner_module = False
    var_decl = 0
    add_loop = False
    #for line in lines:
    for line in lines:
        if line == '}\n' and add_loop:
            if inside_module and not inside_inner_module:
                new_lines.append(f'  }}\n')
        new_lines.append(line)
        if line.find('Module Definition') != -1:
            inside_module = not inside_module
            if not inside_module:
                inside_inner_module = False
                var_decl = 0
                add_loop = False
        if inside_module:
            if line.find('intra_trans(') != -1 or \
               line.find('inter_trans(') != -1 or \
               line.find('inter_trans_boundary(') != -1:
               inside_inner_module = True
        if inside_module and not inside_inner_module:
            if line.find('Variable Declaration') != -1:
                var_decl += 1
            if var_decl == 2:
                # Insert the batch loop here            
                new_lines.append(f'  for (int bn = 0; bn < {batch}; bn++) {{\n')                
                add_loop = True
                var_decl = 0

    with open(output_f, 'w') as f:
        f.writelines(new_lines)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Add batch loops into the code")
    parser.add_argument('-i', required=True, help='intput kernel file')
    parser.add_argument('-b', required=True, help='batch num')
    parser.add_argument('-o', required=True, help='output kernel file')

    args = parser.parse_args()
    run(args.i, args.o, args.b)