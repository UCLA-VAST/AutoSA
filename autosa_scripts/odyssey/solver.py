from subprocess import Popen, PIPE
import tempfile
import shutil

def off_chip_solver_gemm(search_task, cst, fixed_params=None, save=0):
    """ If any parameter found in fixed_params, this parameter will not be tiled.
    """
    with tempfile.TemporaryDirectory() as tmpdirname:
        # Generate the model file
        with open(f'{tmpdirname}/tmp.mod', 'w') as f:
            for p in ["i", "j", "k"]:
                f.write(f'param {p};\n')            
            f.write('param dsp_bound;\n')
            f.write('param bram_bound;\n')
            f.write('param data_w;\n')
            
            for p in ["i", "j", "k"]:
                f.write(f'var {p}1 integer >= 1, <= {p};\n')
            for p in ["i", "j"]:
                f.write(f'var {p}2 integer >= 1, <= {p};\n')
            for p in ["k"]:
                f.write(f'var {p}2 integer >= 1, <= {32/search_task.dw};\n')
            
            for p in ["i", "j", "k"]:            
                f.write(f'var c{p}1 integer >= 1, <= {p};\n')
                f.write(f'var c{p}2 integer >= 1, <= {p};\n')
            for p in ["k"]:
                f.write(f'var c{p}3 integer >= 1, <= {p};\n')            
            
            f.write('minimize target:\n')
            # off_chip/DSP
            #f.write('\t(i*cj1*k+ci1*j*k+i*j)/\n')
            #if search_task.design.name.startswith("kernel0"):
            #    f.write('\t(ci2*k2);\n\n')
            #elif search_task.design.name.startswith("kernel1"):
            #    f.write('\t(cj2*k2);\n\n')
            #elif search_task.design.name.startswith("kernel2"):
            #    f.write('\t(k1);\n\n')
            #elif search_task.design.name.startswith("kernel3"):
            #    f.write('\t(ci2*cj2*k2);\n\n')
            #elif search_task.design.name.startswith("kernel4"):
            #    f.write('\t(ci2*k1);\n\n')
            #elif search_task.design.name.startswith("kernel5"):
            #    f.write('\t(cj2*k1);\n\n')
            
            # off_chip
            #f.write('\t(i*cj1*k+ci1*j*k+i*j);\n\n')

            # compute
            #f.write('\t-(ci2*cj2*k2);\n\n')

            # off_chip - compute            
            f.write('\t(i*cj1*k+ci1*j*k+i*j)-\n')
            if search_task.design.name.startswith("kernel0"):
                f.write('\t(ci2*k2);\n\n')
            elif search_task.design.name.startswith("kernel1"):
                f.write('\t(cj2*k2);\n\n')
            elif search_task.design.name.startswith("kernel2"):
                f.write('\t(k1);\n\n')
            elif search_task.design.name.startswith("kernel3"):
                f.write('\t(ci2*cj2*k2);\n\n')
            elif search_task.design.name.startswith("kernel4"):
                f.write('\t(ci2*k1);\n\n')
            elif search_task.design.name.startswith("kernel5"):
                f.write('\t(cj2*k1);\n\n')

            if search_task.design.name.startswith("kernel0"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= ci2*1*k2*5 <= dsp_bound;\n\n') # Only works for FP32
            elif search_task.design.name.startswith("kernel1"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cj2*1*k2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel2"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= k1*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel3"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= ci2*cj2*k2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel4"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= ci2*k1*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel5"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cj2*k1*5 <= dsp_bound;\n\n')
            
            f.write('subject to BRAM_cst:\n')
            #f.write('\t0 <= (data_w*i1*k1)/(18*1024)*2+\n')
            f.write('\tceil(data_w/18)*ceil(i1*k1/1024)*2+\n')
            #f.write('\t     (data_w*j1*k1)/(18*1024)*2+\n')
            f.write('\tceil(data_w/18)/ceil(j1*k1/1024)*2+\n')
            #f.write('\t     (data_w*i1*j1)/(18*1024)*2 <= bram_bound;\n\n')
            f.write('\tceil(data_w/18)/ceil(i1*j1/1024)*2 <= bram_bound;\n\n')

            for p in ["i", "j", "k"]:
                f.write(f'subject to c{p}1_cst:\n')
                f.write(f'\t{p} = c{p}1*{p}1;\n\n')
            for p in ["i", "j", "k"]:
                f.write(f'subject to c{p}2_cst:\n')
                f.write(f'\t{p}1 = c{p}2*{p}2;\n\n')
            for p in ["k"]:
                f.write(f'subject to c{p}3_cst:\n')
                f.write(f'\t{p}2 = c{p}3*2;\n\n') # even number

            if search_task.design.name.startswith("kernel0") or \
               search_task.design.name.startswith("kernel1") or \
               search_task.design.name.startswith("kernel3"):             
                f.write('subject to latency_hiding_cst:\n')
                f.write('\ti2*j2 >= 8*k2;\n\n') # Only for FP32
            
        with open(f'{tmpdirname}/tmp.dat', 'w') as f:
            for p in ["i", "j", "k"]:
                f.write(f'param {p} := {search_task.workload["params"][p]};\n')            
            f.write(f'param dsp_bound := {int(cst.hw_cst["DSP"])};\n')
            f.write(f'param bram_bound := {int(cst.hw_cst["BRAM18K"])};\n')
            f.write(f'param data_w := 32;\n') # Only for FP32           

        # Generate the AMPL script
        with open(f'{tmpdirname}/tmp.run', 'w') as f:
            f.write('option solver ipopt;\n')
            f.write('reset;\n')
            f.write('model ./solver/tmp.mod;\n')
            f.write('data ./solver/tmp.dat;\n')
            f.write('solve;\n')
            f.write('display target,i1,j1,k1,i2,j2,k2;\n')
        
        # Call the solver    
        cmd = ["ampl", f"{tmpdirname}/tmp.run"]
        pipe = Popen(cmd, stdout=PIPE, stderr=PIPE)
        text = pipe.communicate()[0].decode('ascii')

        # Collect the results
        text = text.split('\n')
        #print(text)
        opt_dims = [1, 1, 1, 1, 1, 1]
        update = 0
        for line in text:
            if line.startswith("i1 = "):
                opt_dims[0] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("j1 = "):
                opt_dims[1] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("k1 = "):
                opt_dims[2] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("i2 = "):
                opt_dims[3] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("j2 = "):
                opt_dims[4] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("k2 = "):
                opt_dims[5] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
        
        #print(update, opt_dims)
        if update != len(opt_dims):
            # The solver isn't finished correctly.
            opt_dims = None

        if save == 1:
            shutil.copyfile(f'{tmpdirname}/tmp.mod', 'solver/tmp.mod')
            shutil.copyfile(f'{tmpdirname}/tmp.dat', 'solver/tmp.dat')
            shutil.copyfile(f'{tmpdirname}/tmp.run', 'solver/tmp.run')
    
    return opt_dims

def off_chip_solver_conv(search_task, cst, fixed_params=None, save=0):
    """ If any parameter found in fixed_params, this parameter will not be tiled.
    """
    with tempfile.TemporaryDirectory() as tmpdirname:
        # Generate the model file
        with open(f'{tmpdirname}/tmp.mod', 'w') as f:
            for p in ["i", "o", "r", "c", "p", "q"]:
                f.write(f'param {p};\n')            
            f.write('param dsp_bound;\n')
            f.write('param bram_bound;\n')
            f.write('param data_w;\n')
            
            for p in ["i", "o", "r", "c"]:
                f.write(f'var {p}1 integer >= 1, <= {p};\n')            
            for p in ["o", "r", "c"]:
                f.write(f'var {p}2 integer >= 1, <= {p};\n')
            for p in ["i"]:
                f.write(f'var {p}2 integer >= 1, <= {32/search_task.dw};\n')
            
            for p in ["i", "o", "r", "c"]:
                f.write(f'var c{p}1 integer >= 1, <= {p};\n')
                f.write(f'var c{p}2 integer >= 1, <= {p};\n')
            for p in ["i"]:
                f.write(f'var c{p}3 integer >= 1, <= {p};\n')
            
            f.write('minimize target:\n')
            # off_chip/DSP
            # Ignore the padded data
            f.write('\t(i*r*c*co1+i*o*p*q*cr1*cc1+o*r*c*ci1)/\n')                        
            if search_task.design.name.startswith("kernel0"):
                f.write('\t(co2*i2);\n\n')
            elif search_task.design.name.startswith("kernel1"):
                f.write('\t(cr2*i2);\n\n')
            elif search_task.design.name.startswith("kernel2"):
                f.write('\t(cc2*i2);\n\n')
            elif search_task.design.name.startswith("kernel3"):
                f.write('\t(ci2*i2);\n\n')
            elif search_task.design.name.startswith("kernel4"):
                f.write('\t(co2*cr2*i2);\n\n')
            elif search_task.design.name.startswith("kernel5"):
                f.write('\t(co2*cc2*i2);\n\n')                
            elif search_task.design.name.startswith("kernel6"):
                f.write('\t(co2*ci2*i2);\n\n')
            elif search_task.design.name.startswith("kernel7"):
                f.write('\t(cr2*cc2*i2);\n\n')                
            elif search_task.design.name.startswith("kernel8"):
                f.write('\t(cr2*ci2*i2);\n\n')       
            elif search_task.design.name.startswith("kernel9"):
                f.write('\t(cc2*ci2*i2);\n\n')
            else:
                raise RuntimeError(f"Not supported design by the solver: {search_task.design.name}")            

            if search_task.design.name.startswith("kernel0"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= co2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel1"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cr2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel2"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cc2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel3"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= ci2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel4"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= co2*cr2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel5"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= co2*cc2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel6"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= co2*ci2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel7"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cr2*cc2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel8"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cr2*ci2*i2*5 <= dsp_bound;\n\n')
            elif search_task.design.name.startswith("kernel9"):
                f.write('subject to DSP_cst:\n')
                f.write('\t0 <= cc2*ci2*i2*5 <= dsp_bound;\n\n')
            else:
                raise RuntimeError(f"Not supported design by the solver: {search_task.design.name}")
                        
            f.write('subject to BRAM_cst:\n')
            f.write('\t0 <= (data_w*i1*r1*c1)/(18*1024)*2+\n')
            f.write('\t     (data_w*i1*o1*p*q)/(18*1024)*2+\n')                
            f.write('\t     (data_w*o1*r1*c1)/(18*1024)*2 <= bram_bound;\n\n')            

            for p in ["i", "o", "r", "c"]:
                f.write(f'subject to c{p}1_cst:\n')
                f.write('\t{p} = c{p}1*{p}1;\n\n')
            for p in ["i", "o", "r", "c"]:
                f.write(f'subject to c{p}2_cst:\n')
                f.write('\t{p}1 = c{p}2*{p}2;\n\n')                
            for p in ["i"]:
                f.write(f'subject to c{p}3_cst:\n')
                f.write(f'\t{p}2 = c{p}3*2;\n\n') # even number   

            # TODO: Add other dataflows
            if search_task.design.name.startswith("kernel0") or \
               search_task.design.name.startswith("kernel1") or \
               search_task.design.name.startswith("kernel2") or \
               search_task.design.name.startswith("kernel4") or \
               search_task.design.name.startswith("kernel5") or \
               search_task.design.name.startswith("kernel7"):             
                f.write('subject to latency_hiding_cst:\n')
                f.write('\to2*r2*c2 >= 8*i2;\n\n') # Only for FP32
            
        with open(f'{tmpdirname}/tmp.dat', 'w') as f:
            for p in ["i", "o", "r", "c"]:
                f.write(f'param {p} := {search_task.workload["params"][p]};\n')            
            f.write(f'param dsp_bound := {int(cst.hw_cst["DSP"])};\n')
            f.write(f'param bram_bound := {int(cst.hw_cst["BRAM18K"])};\n')
            f.write(f'param data_w := 32;\n') # Only for FP32           

        # Generate the AMPL script
        with open(f'{tmpdirname}/tmp.run', 'w') as f:
            f.write('option solver ipopt;\n')
            f.write('reset;\n')
            f.write('model ./solver/tmp.mod;\n')
            f.write('data ./solver/tmp.dat;\n')
            f.write('solve;\n')
            f.write('display target,i1,o1,r1,c1,i2,o2,r2,c2;\n')
        
        # Call the solver    
        cmd = ["ampl", f"{tmpdirname}/tmp.run"]
        pipe = Popen(cmd, stdout=PIPE, stderr=PIPE)
        text = pipe.communicate()[0].decode('ascii')

        # Collect the results
        text = text.split('\n')
        #print(text)
        opt_dims = [1, 1, 1, 1, 1, 1, 1, 1]
        update = 0
        for line in text:
            if line.startswith("i1 = "):
                opt_dims[0] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("o1 = "):
                opt_dims[1] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("r1 = "):
                opt_dims[2] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("c1 = "):
                opt_dims[3] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("i2 = "):
                opt_dims[4] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("o2 = "):
                opt_dims[5] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("r2 = "):
                opt_dims[5] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
            if line.startswith("c2 = "):
                opt_dims[5] = int(float(line.split('=')[-1].strip()) + 0.5)
                update += 1
                
        if update != len(opt_dims):
            # The solver isn't finished correctly.
            opt_dims = None

        if save == 1:
            shutil.copyfile(f'{tmpdirname}/tmp.mod', 'solver/tmp.mod')
            shutil.copyfile(f'{tmpdirname}/tmp.dat', 'solver/tmp.dat')
            shutil.copyfile(f'{tmpdirname}/tmp.run', 'solver/tmp.run')
    
    return opt_dims    

def off_chip_solver(search_task, cst, fixed_params=None, save=0):
    """ Run the solver to minimize the off-chip data communication.
    """
    if "gemm" in search_task.workload["tags"]:
        return off_chip_solver_gemm(search_task, cst, fixed_params, save)
    elif "conv" in search_task.workload["tags"]:
        return off_chip_solver_conv(search_task, cst, fixed_params, save)
    else:
        RuntimeError(f"Not supported task: {search_task.workload['name']}")