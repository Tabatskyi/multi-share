#include "pch.h"
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")

static BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

static SOCKET CreateAndConnectSocket(PCWSTR serverIp, int port)
{
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    InetPton(AF_INET, serverIp, &serverAddr.sin_addr);

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Connect failed with error: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        return INVALID_SOCKET;
    }

    return clientSocket;
}

static void Cleanup(SOCKET clientSocket)
{
    closesocket(clientSocket);
}

extern "C" __declspec(dllexport) void HandleOutcomingClientCommunication(const WCHAR* serverIp, int port, const WCHAR* message)
{
    if (!InitializeWinsock())
        return;

    SOCKET clientSocket = CreateAndConnectSocket(serverIp, port);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to connect to server" << std::endl;
        WSACleanup();
        return;
    }

    std::wstring messageWStr(message);
    std::string messageStr(messageWStr.begin(), messageWStr.end());

    if (!SendData(clientSocket, messageStr))
    {
        std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
        Cleanup(clientSocket);
        WSACleanup();
        return;
    }

    std::istringstream iss(messageStr);
    std::string command, clientName, filename;
    int roomID;
    iss >> command >> clientName >> roomID >> filename;

    if (command == "j")
    {
        std::string joinMessage = std::format("CLIENT {} JOINED ROOM {}", clientName, roomID);
        if (SendData(clientSocket, joinMessage))
        {
            std::cout << joinMessage << std::endl;
        }
        else
        {
            std::cerr << "Failed to send join message" << std::endl;
        }
    }
    else if (command == "sm")
    {
        std::string chatMessage;
        std::getline(iss, chatMessage);
        std::string fullMessage = std::format("CLIENT {}: {}", clientName, chatMessage);
        if (SendData(clientSocket, fullMessage))
        {
            std::cout << fullMessage << std::endl;
        }
        else
        {
            std::cerr << "Failed to send message" << std::endl;
        }
    }
    else if (command == "sf")
    {
        
        if (SendFileToStream(filename, clientSocket))
        {
            std::cout << "File '" << filename << "' sent" << std::endl;
            if (CheckResponse(clientSocket))
                std::cout << "File delivered" << std::endl;
        }
        else
        {
            std::cerr << "Failed to deliver file to server" << std::endl;
        }
    }
    else if (command == "q")
    {
        std::cout << "Quitting the server" << std::endl;
    }
    else
        std::cerr << "Invalid command" << std::endl;

    Cleanup(clientSocket);
}

extern "C" __declspec(dllexport) void HandleIncomingClientCommunication(const WCHAR* bindIp, int port)
{
    if (!InitializeWinsock())
        return;

    SOCKET listenSocket = CreateAndBindSocket(bindIp, port);
    if (listenSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create listening socket." << std::endl;
        WSACleanup();
        return;
    }

    if (Listen(listenSocket) != 0)
    {
        return;
    }

    SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return;
    }
    closesocket(listenSocket);

    while (true)
    {
        std::string message = ReceiveData(clientSocket);
        if (message.empty())
        {
            std::cerr << "Failed to receive message or connection closed." << std::endl;
            break;
        }
        std::istringstream iss(message);
        std::string command, clientName, filename;
        iss >> command >> clientName >> filename;
        if (command == "fo")
        {
            size_t fileSize;
            iss >> fileSize;
            std::string offerMsg = std::format("CLIENT {} wants to send {} file (%zu bytes). Accept (y/n)?", clientName, filename, fileSize);
            std::cout << offerMsg << std::endl;
            std::string response;
            std::cin >> response;
            if (response == "y")
            {
                SendData(clientSocket, "y");
                if (!WriteFileFromStream(filename, clientSocket))
                {
                    std::cerr << "Failed to receive file" << std::endl;
                }
                else
                {
                    std::cout << "File '" << filename << "' received" << std::endl;
                }
            }
            else
            {
                SendData(clientSocket, "n");
                std::cout << "File transfer rejected" << std::endl;
            }
        }
        else if (command == "m")
        {
            std::string chatMessage;
            std::getline(iss, chatMessage);
            std::cout << "CLIENT " << clientName << ": " << chatMessage << std::endl;
        }
        else
        {
            std::cerr << "Unknown command received: " << command << std::endl;
        }
    }
    closesocket(clientSocket);
    WSACleanup();
}