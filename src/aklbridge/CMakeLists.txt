###############################################################################
#C++ compilation
add_executable(aklbridge
	#AseqSCPIServer.cpp
	#WaveformServerThread.cpp
	main.cpp
)

###############################################################################
#Linker settings
target_link_libraries(aklbridge
	xptools
	log
	scpi-server-tools)

