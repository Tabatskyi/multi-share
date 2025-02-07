#include <iostream>   
#include <fstream>  
#include <vector>  
#include <chrono>
#include <filesystem>
#include <thread>
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")  

// Server configuration  
static SOCKET CreateAndBindSocket(int port)
{
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    // Bind the socket  
    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

	return serverSocket;
}

// Listen for incoming connections 
static int Listen(SOCKET serverSocket)
{ 
	if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}
	return 0;
}

static std::string ListFiles(std::filesystem::path path)
{
	std::string fileList;
    for (const auto& entry : std::filesystem::directory_iterator(path))
		fileList += entry.path().filename().string() + "\n";
	return fileList;
}

static void HandleClient(SOCKET clientSocket, const std::filesystem::path& serverFiles)
{
	std::string message = ReceiveData(clientSocket);
	if (!message.empty())
	{
		std::cout << "Received command: " << message << std::endl;
		std::istringstream iss(message);
		std::string command, clientName, filename;
		iss >> command >> clientName >> filename;
		std::filesystem::path clientFolder = serverFiles / clientName;
		std::filesystem::create_directory(clientFolder);

		if (command == "PUT")
		{
			std::string filePath = (clientFolder / filename).string();
			if (WriteFileFromStream(filePath, clientSocket))
			{
				std::cout << "File download completed" << std::endl;
				SendData(clientSocket, "OK");
			}
		}
		else if (command == "GET")
		{
			std::string filePath = (clientFolder / filename).string();
			if (SendFileToStream(filePath, clientSocket))
				std::cout << "File upload completed" << std::endl;

			if (CheckResponse(clientSocket))
				std::cout << "File delivered" << std::endl;
		}
		else if (command == "QUIT")
		{
			std::cout << "Server shutting down." << std::endl;
			closesocket(clientSocket);
			WSACleanup();
			exit(0);
		}
		else if (command == "LIST")
		{
			std::string fileList = ListFiles(clientFolder);
			SendData(clientSocket, fileList);
		}
		else if (command == "DELETE")
		{
			std::string filePath = (clientFolder / filename).string();
			if (std::filesystem::remove(filePath))
			{
				std::cout << "File '" << filePath << "' deleted" << std::endl;
				SendData(clientSocket, "OK");
			}
			else
			{
				std::string response = std::format("ERROR: Failed to delete file: '{}'", filePath);
				std::cerr << response << std::endl;
				SendData(clientSocket, response);
			}
		}
		else if (command == "INFO")
		{
			std::string file = (clientFolder / filename).string();
			if (std::filesystem::exists(file))
			{
				long long fileSize = std::filesystem::file_size(file);
				std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(file);
				std::string fileInfo = std::format("Size: {} bytes\nLast modified: {}", fileSize, lastWriteTime);
				SendData(clientSocket, fileInfo);
			}
			else
			{
				std::cerr << "File '" << file << "' not found" << std::endl;
				std::string response = "File not found";
				SendData(clientSocket, response);
			}
		}
		else
		{
			std::string response = "Unknown command.";
		}
	}
	else
	{
		std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
	}
}

int main()  
{  
	if (!InitializeWinsock())
		return 1;

   int port = 12345;  
   SOCKET serverSocket = CreateAndBindSocket(port);

   if (serverSocket == INVALID_SOCKET)
       return 1;

   if (Listen(serverSocket) != 0)
	   return 1;

   std::filesystem::path serverPath = std::filesystem::current_path();
   std::filesystem::path serverFiles = serverPath / "ServerFiles";
   std::filesystem::create_directory(serverFiles);

   std::cout << "Server listening on port " << port << std::endl;  

   while (true) 
   {
	   SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
	   if (clientSocket == INVALID_SOCKET) 
	   {
		   std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
		   continue;
	   }

	   std::thread clientThread(HandleClient, clientSocket, serverFiles);
	   clientThread.detach();
   }
}