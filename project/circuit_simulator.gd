extends CircuitSimulator


func _ready():
	if !initialize_ngspice():
		push_error("ngspice init failed")
		return

	var report = test_spice_pipeline() # or test_spice_pipeline("/absolute/output/dir")
	print("passed: ", report.get("passed", false))
	print("checks: ", report.get("checks", {}))
	print("errors: ", report.get("errors", []))
	print("normalized:\n", report.get("normalized_netlist", ""))
