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

static void Cleanup(SOCKET clientSocket)
{
    closesocket(clientSocket);
    WSACleanup();
}


extern "C" __declspec(dllexport) void HandleClientCommunication(const WCHAR* serverIp, int port, const WCHAR* command)
{
    if (!InitializeWinsock())
        return;

    SOCKET clientSocket = CreateAndConnectSocket(serverIp, port);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to connect to server" << std::endl;
        return;
    }

	std::wstring commandWStr(command);
    std::string commandStr(commandWStr.begin(), commandWStr.end());

    if (commandStr.compare(0, 4, "PUT ") == 0)
    {
		if (!SendData(clientSocket, commandStr))
			std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;

        std::string filename = commandStr.substr(4);
		if (SendFileToStream(filename, clientSocket))
			std::cout << "File '" << filename << "' sent" << std::endl;
    }
    else if (commandStr.compare(0, 4, "GET ") == 0)
    {
		if (!SendData(clientSocket, commandStr))
			std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;

        std::string filename = commandStr.substr(4);
		if (!WriteFileFromStream(filename, clientSocket))
			std::cerr << "Failed to receive file" << std::endl;
    }
	else if (commandStr.compare(0, 5, "QUIT") == 0)
	{
		if (!SendData(clientSocket, commandStr))
			std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;

		std::cout << "Quitting the server" << std::endl;
	}
	else if (commandStr.compare(0, 4, "LIST") == 0)
    {
		if (!SendData(clientSocket, commandStr))
			std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;

		std::vector<char> buffer(1024);
        std::string fileList = ReceiveData(clientSocket);
        if (fileList.size() > 0)
        {
            std::cout << "Received file list: " << fileList << std::endl;
        }
	}
    else if (commandStr.compare(0, 6, "DELETE") == 0)
    {
		if (!SendData(clientSocket, commandStr))
			std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
		if (CheckResponse(clientSocket))
		    std::cout << "File deleted" << std::endl;
    }
	else if (commandStr.compare(0, 4, "INFO") == 0)
	{
		if (!SendData(clientSocket, commandStr))
			std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;

        std::string fileInfo = ReceiveData(clientSocket);
		if (fileInfo.size() > 0)
		{
			std::cout << "Received file info:\n" << fileInfo << std::endl;
		}
	}
	else
	{
		std::cerr << "Invalid command" << std::endl;
	}

    Cleanup(clientSocket);
}