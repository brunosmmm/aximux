# ip
set ip_name axi_mux

set proj_files [list \
                    "axi_mux.v" \
                    "axislave.v" \
                   ]

create_project $ip_name . -force
set proj_dir [get_property directory [current_project]]
set proj_name [get_projects $ip_name]

set origin_dir "."

#add files
set proj_fileset [get_filesets sources_1]
add_files -norecurse -scan_for_includes -fileset $proj_fileset $proj_files
set_property "top" "$ip_name" $proj_fileset

#set properties
ipx::package_project -root_dir .

set_property vendor {esl} [ipx::current_core]
set_property library {user} [ipx::current_core]
set_property taxonomy {{/AXI_Infrastructure}} [ipx::current_core]
set_property vendor_display_name {BMorais} [ipx::current_core]
set_property display_name {AXI Signal Multiplexer} [ipx::current_core]
set_property description {Multiplex signals controlled by AXI interface} [ipx::current_core]

set_property supported_families \
    {{zynq}       {Production} \
         {qzynq}      {Production} \
         {azynq}      {Production}} \
    [ipx::current_core]

set_property driver_value 0 [ipx::get_ports -filter "direction==in" -of_objects [ipx::current_core]]

ipx::save_core [ipx::current_core]
