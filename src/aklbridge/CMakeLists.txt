###############################################################################
#C++ compilation
add_executable(aklbridge
	GPIOSCPIServer.cpp
	ILA8b10bSCPIServer.cpp
	ILASCPIServer.cpp
	VIOSCPIServer.cpp
	main.cpp
)

###############################################################################
#Linker settings
target_link_libraries(aklbridge
	xptools
	log
	scpi-server-tools)

