C {sky130_fd_prnfet_01v8.sym} 200 -50 0 0 {name=M1 L=0.15 W=1 nf=1}
C {sky130_fd_prpfet_01v8.sym} 200 50 0 0 {name=M2 L=0.15 W=2 nf=1}
C {devicesvdd.sym} 200 120 0 0 {name=VDD}
C {devicesgnd.sym} 200 -120 0 0 {name=GND}
C {devicesipin.sym} 80 0 0 0 {name=A}
C {devicesopin.sym} 320 0 0 0 {name=Y}

N 80 0 200 0 {lab=A}
N 200 0 320 0 {lab=Y}
N 200 80 200 120 {lab=VDD}
N 200 -80 200 -120 {lab=GND}