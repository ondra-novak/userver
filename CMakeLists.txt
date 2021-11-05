cmake_minimum_required(VERSION 2.8) 

set(userver_sources 	netaddr.cpp 
	socket.cpp 
	stream.cpp 
	socket_server.cpp 
	http_server.cpp
	http_client.cpp
	dispatcher.cpp
	dispatcher_epoll.cpp
	async_provider.cpp
	query_parser.cpp
	dgramsocket.cpp
	filedesc.cpp
	init.cpp
	static_webserver.cpp
	openapi.cpp
	websockets_parser.cpp
)

if(NOT DEFINED USERVER_NO_SSL)
	set(userver_sources ${userver_sources}	
		ssl.cpp 
		ssl_socket.cpp
		ssl_exception.cpp)
endif()
	

add_library (userver ${userver_sources}) 

