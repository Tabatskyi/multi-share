#include <iostream>
#include <WinSock2.h>
#include <fstream>
#include <vector>

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
    std::vector<char> buffer(1024);
    int bytesReceived = recv(clientSocket, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
    if (bytesReceived > 0)
    {
        std::string command(buffer.data(), bytesReceived);
        std::cout << "Received command: " << command << std::endl;

        if (command.compare(0, 4, "PUT ") == 0)
        {
            std::string filename = command.substr(4);

            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open())
            {
                std::cerr << "Failed to open file for writing: " << filename << std::endl;
                std::string response = "ERROR";
                send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);
            }
            else
            {
                std::string response = "OK";
                send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);

                int totalBytesReceived = 0;
                std::vector<char> fileBuffer(1024);
                while ((bytesReceived = recv(clientSocket, fileBuffer.data(), static_cast<int>(fileBuffer.size()), 0)) > 0)
                {
                    file.write(fileBuffer.data(), bytesReceived);
                    totalBytesReceived += bytesReceived;
                }

                if (bytesReceived == SOCKET_ERROR)
                    std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;

                file.close();
                std::cout << "File '" << filename << "' received (" << totalBytesReceived << " bytes)" << std::endl;
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

    // Clean up
    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();

    return 0;
}