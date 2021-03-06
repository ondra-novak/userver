cmake_minimum_required(VERSION 2.8) 

add_library (userver 
	netaddr.cpp 
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
	init.cpp
#	ssl.cpp
#	ssl_socket.cpp
#	ssl_exception.cpp
	static_webserver.cpp
)

