cmake_minimum_required(VERSION 2.8) 

add_library (userver 
	netaddr.cpp 
	socket.cpp 
	stream.cpp 
	socket_server.cpp 
	http_server.cpp
	dispatcher.cpp
	async_provider.cpp
	query_parser.cpp
	dgramsocket.cpp
)

