#include "pch.h"
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")

static SOCKET clientSocket = INVALID_SOCKET;
static std::mutex sendMutex;
static std::mutex consoleInputMutex;

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

extern "C" __declspec(dllexport) BSTR GetConsoleInput(const WCHAR* prompt)
{
    std::wstring input;
    
    std::wcout << prompt;
    std::getline(std::wcin, input);
    
    return SysAllocString(input.c_str());
}

extern "C" __declspec(dllexport) bool EstablishConnection(const WCHAR* serverIp, int port)
{
    if (!InitializeWinsock())
        return false;

    clientSocket = CreateAndConnectSocket(serverIp, port);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to connect to server persistent connection" << std::endl;
        return false;
    }
    return true;
}

extern "C" __declspec(dllexport) bool SendCommand(const WCHAR* command)
{
    if (clientSocket == INVALID_SOCKET)
        return false;

    std::wstring commandWStr(command);
    std::string commandStr(commandWStr.begin(), commandWStr.end());

    std::lock_guard<std::mutex> lk(sendMutex);
    if (!SendData(clientSocket, commandStr))
    {
        std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
        return false;
    }
    return true;
}

extern "C" __declspec(dllexport) void ReceiveRoutine()
{
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Persistent socket not established." << std::endl;
        return;
    }

    while (true)
    {
        std::string message = ReceiveData(clientSocket);
        if (message.empty())
        {
            std::cerr << "Failed to receive message or connection closed." << std::endl;
            break;
        }

        std::istringstream iss(message);
        std::string command;
        iss >> command;

        if (command == "fo")
        {
            std::string senderName, filename;
            size_t fileSize = 0;
            iss >> senderName >> filename >> fileSize;

            std::string offerPrompt = std::format("Client {} is offering file '{}' ({} bytes). Accept (y/n)? ", senderName, filename, fileSize);
            std::wstring offerPromptW(offerPrompt.begin(), offerPrompt.end());

            WCHAR* userResponseW = GetConsoleInput(offerPromptW.c_str());
            std::wstring userResponseWStr(userResponseW);

            delete[] userResponseW;
            std::string userResponse(userResponseWStr.begin(), userResponseWStr.end());

            if (!SendData(clientSocket, userResponse))
            {
                std::cerr << "Failed to send file response." << std::endl;
            }

            if (userResponse == "y")
            {
                std::cout << "Receiving file '" << filename << "' ..." << std::endl;
                if (!WriteFileFromStream(filename, clientSocket))
                {
                    std::cerr << "Failed to receive file." << std::endl;
                }
                else
                {
                    std::cout << "File '" << filename << "' successfully received." << std::endl;
                }
            }
            else
            {
                std::cout << "File transfer declined." << std::endl;
            }
        }
        else
        {
            std::cout << "Message: " << message << std::endl;
        }
    }
    closesocket(clientSocket);
    WSACleanup();
}

extern "C" __declspec(dllexport) bool SendFile(const WCHAR* filename)
{
    std::wstring fileWStr(filename);
    std::string fileStr(fileWStr.begin(), fileWStr.end());

    return SendFileToStream(fileStr, clientSocket);
}