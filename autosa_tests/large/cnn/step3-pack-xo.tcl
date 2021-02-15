open_project kernel0
open_solution solution
export_design -rtl verilog -format ip_catalog -xo kernel0.xo

close_project
puts "Pack XO successfully"
exit
