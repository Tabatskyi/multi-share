#include <iostream>
#include <WinSock2.h>
// Linking the library needed for network communication
#pragma comment(lib, "ws2_32.lib")
int main()
{
	// Initialize Winsock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cerr << "WSAStartup failed" << std::endl;
		return 1;
	}
	// Server configuration
	int port = 12345;
	SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == INVALID_SOCKET)
	{
		std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(port);

	// Bind the socket
	if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}
	// Listen for incoming connections
	if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}
	std::cout << "Server listening on port " << port << std::endl;

	// Accept a client connection
	SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
	if (clientSocket == INVALID_SOCKET)
	{
		std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}
	// Receive data from the client
	char buffer[1024];
	memset(buffer, 0, 1024);
	int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
	if (bytesReceived > 0)
	{
		std::cout << "Received data: " << buffer << std::endl;
		// Send a response back to the client
		const char* response = "Hello, client! This is the server.";
		send(clientSocket, response, (int)strlen(response), 0);
	}
	// Clean up
	closesocket(clientSocket);
	closesocket(serverSocket);
	WSACleanup();
	return 0;
}