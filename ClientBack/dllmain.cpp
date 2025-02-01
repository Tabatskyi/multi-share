// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

// Linking the library needed for network communication
#pragma comment(lib, "ws2_32.lib")

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

static int InitializeWinsock()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
    return 0;
}

static SOCKET CreateAndConnectSocket(PCWSTR serverIp, int port)
{
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
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
        WSACleanup();
        return INVALID_SOCKET;
    }

    return clientSocket;
}

static int SendData(SOCKET clientSocket, const std::wstring& message)
{
    std::string narrowMessage(message.begin(), message.end());
    return send(clientSocket, narrowMessage.c_str(), static_cast<int>(narrowMessage.size()), 0);
}

static int ReceiveData(SOCKET clientSocket, char* buffer, int bufferSize)
{
    memset(buffer, 0, bufferSize);
    return recv(clientSocket, buffer, bufferSize, 0);
}

static void Cleanup(SOCKET clientSocket)
{
    closesocket(clientSocket);
    WSACleanup();
}

extern "C" __declspec(dllexport) void HandleClientCommunication(const WCHAR* serverIp, int port, const WCHAR* command)
{
    if (InitializeWinsock() != 0)
        return;

    SOCKET clientSocket = CreateAndConnectSocket(serverIp, port);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to connect to server" << std::endl;
        return;
    }
    std::cout << "Connected to server" << std::endl;

	std::wstring commandStr(command);

    if (commandStr.compare(0, 4, L"PUT ") == 0)
    {
        std::wstring filename = commandStr.substr(4);

        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            std::wcerr << "Failed to open file: " << filename << std::endl;
            Cleanup(clientSocket);
            return;
        }

		std::string narrowCommand(commandStr.begin(), commandStr.end());
        if (send(clientSocket, narrowCommand.c_str(), static_cast<int>(narrowCommand.size() * sizeof(char)), 0) == SOCKET_ERROR)
        {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            file.close();
            Cleanup(clientSocket);
            return;
        }

		std::vector<char> responceBuffer(1024);
        int responceBytes = recv(clientSocket, responceBuffer.data(), sizeof(responceBuffer), 0);
		std::string responce(responceBuffer.data(), responceBytes);
		std::cout << "Received responce: " << responce << std::endl;
		if (responceBytes <= 0 || responce.compare("OK") != 0)
        {
            std::cerr << "Failed to receive responce from server" << std::endl;
            file.close();
            Cleanup(clientSocket);
            return;
        }

        // Send the file content
        std::vector<char> fileBuffer(1024);
        while (file.read(fileBuffer.data(), fileBuffer.size()) || file.gcount() > 0)
        {
            std::streamsize bytesToSend = file.gcount();
            if (send(clientSocket, fileBuffer.data(), static_cast<int>(bytesToSend), 0) == SOCKET_ERROR)
            {
                std::cerr << "File data send failed with error: " << WSAGetLastError() << std::endl;
                file.close();
                Cleanup(clientSocket);
                return;
            }
        }
        file.close();
        std::cout << "File upload completed" << std::endl;

    }
    else
    {
        if (SendData(clientSocket, command) == SOCKET_ERROR)
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
    }

    Cleanup(clientSocket);
}