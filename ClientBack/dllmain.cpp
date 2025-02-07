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

extern "C" __declspec(dllexport) void HandleClientCommunication(const WCHAR* serverIp, int port, const WCHAR* message)
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
    iss >> command >> clientName >> filename;

	if (command == "PUT")
    {
        if (SendFileToStream(filename, clientSocket))
        {
            std::cout << "File '" << filename << "' sent" << std::endl;
            if (CheckResponse(clientSocket))
                std::cout << "File delivered" << std::endl;
        }
        else
        {
            std::cerr << "Failed to deliver file" << std::endl;
        }
    }
	else if (command == "GET")
    {
		if (!WriteFileFromStream(filename, clientSocket))
        { 
			std::cerr << "Failed to receive file" << std::endl;
			Cleanup(clientSocket);
			return;
		}

		SendData(clientSocket, "OK");
    }
	else if (command == "QUIT")
	{
		std::cout << "Quitting the server" << std::endl;
	}
	else if (command == "LIST")
    {
		std::vector<char> buffer(1024);
        std::string fileList = ReceiveData(clientSocket);

        if (fileList.size() > 0)
            std::cout << "Received files list:\n" << fileList;
		else
			std::cerr << "Failed to receive files list" << std::endl;
	}
    else if (command == "DELETE")
    {
		if (CheckResponse(clientSocket))
		    std::cout << "File deleted" << std::endl;
    }
	else if (command == "INFO")
	{
        std::string fileInfo = ReceiveData(clientSocket);
		if (fileInfo.size() > 0)
			std::cout << "Received file info:\n" << fileInfo << std::endl;
        else
			std::cerr << "Failed to receive file info" << std::endl;
	}
	else
        std::cerr << "Invalid command" << std::endl;

    Cleanup(clientSocket);
}