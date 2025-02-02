#include <iostream>   
#include <fstream>  
#include <vector>  
#include <chrono>
#include <filesystem>
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
       std::vector<char> buffer(1024);  
	   int bytesReceived = ReceiveData(clientSocket, buffer);
       if (bytesReceived > 0)  
       {  
           std::string command(buffer.data(), bytesReceived);  
           std::cout << "Received command: " << command << std::endl;  

           if (command.compare(0, 4, "PUT ") == 0)  
           {  
			   std::string filePath = serverFiles.string() + "\\" + command.substr(4);
			   if (WriteFileFromStream(filePath, clientSocket))
				   std::cout << "File download completed" << std::endl;
           }
           else if (command.compare(0, 4, "GET ") == 0)
           {
			   std::string filePath = serverFiles.string() + "\\" + command.substr(4);
			   if (SendFileToStream(filePath, clientSocket))
				   std::cout << "File upload completed" << std::endl;
           }
           else if (command.compare(0, 5, "QUIT") == 0)  
           {  
               std::cout << "Server shutting down." << std::endl;
               // Clean up server socket  
               closesocket(clientSocket);  
               WSACleanup();
			   return 0;
           }  
		   else if (command.compare(0, 4, "LIST") == 0)
		   {
			   std::string fileList = ListFiles(serverFiles);
			   SendData(clientSocket, fileList);
		   }
		   else if (command.compare(0, 7, "DELETE ") == 0)
		   {
			   std::string filePath = serverFiles.string() + "\\" + command.substr(7);
               if (std::filesystem::remove(filePath))
               {
                   std::cout << "File '" << filePath << "' deleted" << std::endl;
				   std::string response = "OK";
				   SendData(clientSocket, response);
               }
               else
               {
                   std::cerr << "Failed to delete file '" << filePath << "'" << std::endl;
				   std::string response = "ERROR";
				   SendData(clientSocket, response);
               }
		   }
		   else if (command.compare(0, 5, "INFO ") == 0)
		   {
               std::filesystem::path file = serverFiles / command.substr(5);
			   if (std::filesystem::exists(file))
			   {
				   long long fileSize = std::filesystem::file_size(file);
				   std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(file);
				   std::cout << std::format("File: {}\nSize: {} bytes\nLast modified: {}", file.filename().string(), fileSize, lastWriteTime) << std::endl;
				   std::string fileInfo = std::format("Size: {}b\nLast modified: {}", fileSize, lastWriteTime);
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
               send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);  
           }  
       }  
       else  
       {  
           std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;  
       }  

       closesocket(clientSocket);  
   }  
}