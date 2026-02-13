extends CircuitSimulator




func _ready():
	if initialize_ngspice():
		print("ngspice ready!")
	else:
		print("Failed to initialize ngspice")


	if not load_netlist("D:/code/cse115b/Circuit-Visualization/project/circuits/vdiv.cir"):
		push_error("Failed to load netlist into ngspice")
		return

	run_simulation()

	var vout = get_voltage("out")
	print("V(out) =", vout)
