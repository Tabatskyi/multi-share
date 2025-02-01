// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <iostream>
#include <WinSock2.h>
#include <Ws2tcpip.h>

// Linking the library needed for network communication
#pragma comment(lib, "ws2_32.lib")

BOOL APIENTRY DllMain(HMODULE hModule,
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

int InitializeWinsock()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
    return 0;
}

SOCKET CreateAndConnectSocket(PCWSTR serverIp, int port)
{
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr;
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

// https://stackoverflow.com/questions/3019977/convert-wchar-t-to-char
size_t to_narrow(const wchar_t* src, char* dest, size_t dest_len)
{
    size_t i;
    wchar_t code;

    i = 0;

    while (src[i] != '\0' && i < (dest_len - 1)) {
        code = src[i];
        if (code < 128)
            dest[i] = char(code);
        else 
        {
            dest[i] = '?';
            if (code >= 0xD800 && code <= 0xDBFF)
                // lead surrogate, skip the next code unit, which is the trail
                i++;
        }
        i++;
    }

    dest[i] = '\0';

    return i - 1;
}

int SendData(SOCKET clientSocket, const WCHAR* message)
{
    char buffer[1024];
    to_narrow(message, buffer, sizeof(buffer));
    return send(clientSocket, buffer, (int)strlen(buffer), 0);
}

int ReceiveData(SOCKET clientSocket, char* buffer, int bufferSize)
{
    memset(buffer, 0, bufferSize);
    return recv(clientSocket, buffer, bufferSize, 0);
}

void Cleanup(SOCKET clientSocket)
{
    closesocket(clientSocket);
    WSACleanup();
}

extern "C" __declspec(dllexport) void HandleClientCommunication(const WCHAR* serverIp, int port, const WCHAR* command)
{
    if (InitializeWinsock() != 0)
        return;
	std::cout << "Connecting to " << serverIp << ":" << port << std::endl;
    SOCKET clientSocket = CreateAndConnectSocket(serverIp, port);
    if (clientSocket == INVALID_SOCKET)
    {
		std::cerr << "Failed to connect to server" << std::endl;
        return;
    }
	std::cout << "Connected to server" << std::endl;
    int result = SendData(clientSocket, command);
    if (result == SOCKET_ERROR)
        std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;

    char buffer[1024];
    int bytesReceived = ReceiveData(clientSocket, buffer, sizeof(buffer));
    if (bytesReceived > 0)
        std::cout << "Received: " << buffer << std::endl;

    Cleanup(clientSocket);
}


