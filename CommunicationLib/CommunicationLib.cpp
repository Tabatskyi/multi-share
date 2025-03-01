#include "pch.h"
#include "framework.h"

#pragma comment(lib, "ws2_32.lib")  

static const size_t BUFFER_SIZE_BYTES = 1024;

static enum class Command : unsigned char
{
	Unknown = 0xFF,
	JoinRoom = 0x01,
	MessageText = 0x02,
	FileOffer = 0x03,
	FileSize = 0x04,
	FileChunk = 0x05,
	JoinRoomResponse = 0x10,
	MessageTextResponse = 0x20,
	FileOfferResponse = 0x30,
};

struct FileTransferState
{
    std::ofstream stream;
    size_t expectedSize = 0;
    size_t received = 0;
};

struct Message
{
    unsigned char command;
    std::string payload;
};

static bool InitializeWinsock()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		std::cerr << "WSAStartup failed" << std::endl;
		return false;
	}
	return true;
}

static SOCKET CreateAndBindSocket(int port, PCWSTR bindIp = nullptr)
{
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET)
    {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(port);

    if (bindIp != nullptr && wcslen(bindIp) > 0)
    {
        if (InetPton(AF_INET, bindIp, &bindAddr.sin_addr) != 1)
        {
            std::cerr << "Invalid bind IP address." << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return INVALID_SOCKET;
        }
    }
    else
    {
        bindAddr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    return listenSocket;
}

static bool SendData(SOCKET clientSocket, const Command command, const void* payload, uint32_t payloadSize = 0)
{
	if (payloadSize == 0)
	{
		payloadSize = static_cast<uint32_t>(strlen(static_cast<const char*>(payload)));
	}

	std::cout << "Sending size: " << payloadSize << std::endl;
    uint32_t msgLen = htonl(payloadSize);
    std::vector<char> buffer(sizeof(msgLen) + 1 + payloadSize);

    std::memcpy(buffer.data(), &msgLen, sizeof(msgLen));
    buffer[sizeof(msgLen)] = static_cast<char>(command);

    if (payloadSize > 0)
    {
        std::memcpy(buffer.data() + sizeof(msgLen) + 1, payload, payloadSize);
    }

    int totalSent = 0;
    while (totalSent < static_cast<int>(buffer.size()))
    {
        int sent = send(clientSocket, buffer.data() + totalSent, static_cast<int>(buffer.size()) - totalSent, 0);
        if (sent <= 0)
            return false;
        totalSent += sent;
    }
    return true;
}

static bool ReceiveMessage(SOCKET socket, Message& message)
{
    const size_t headerSize = sizeof(uint32_t) + 1; 
    char header[5]{};
    int totalHeaderRead = 0;
    int bytesRead = 0;

    while (totalHeaderRead < static_cast<int>(headerSize)) 
    {
        bytesRead = recv(socket, header + totalHeaderRead, headerSize - totalHeaderRead, 0);
        if (bytesRead <= 0)
            return false;

        totalHeaderRead += bytesRead;
    }

    uint32_t networkPayloadSize = 0;
    std::memcpy(&networkPayloadSize, header, sizeof(uint32_t));

    uint32_t payloadSize = ntohl(networkPayloadSize);
    message.command = header[sizeof(uint32_t)];

    message.payload.resize(payloadSize);
    int totalPayloadRead = 0;
    while (totalPayloadRead < static_cast<int>(payloadSize)) 
    {
        bytesRead = recv(socket, message.payload.data() + totalPayloadRead, payloadSize - totalPayloadRead, 0);
        if (bytesRead <= 0)
            return false; 

        totalPayloadRead += bytesRead;
    }
    return true;
}

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

static bool SendFile(const std::string& filepath, const std::string& filename, SOCKET socket)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "ERROR: Failed to open file: " << filepath << std::endl;
        return false;
    }

    std::filesystem::path path(filepath);
    uint32_t fileSizeValue = static_cast<uint32_t>(std::filesystem::file_size(path));

    std::string payload = std::format("{} {}", filename, fileSizeValue);
    if (!SendData(socket, Command::FileSize, payload.c_str()))
    {
        std::cerr << "Failed to send file size message." << std::endl;
        file.close();
        return false;
    }

    std::vector<char> buffer(BUFFER_SIZE_BYTES);
    while (fileSizeValue > 0)
    {
        std::streamsize bytesToRead = min(static_cast<std::streamsize>(buffer.size()), static_cast<std::streamsize>(fileSizeValue));
        if (!file.read(buffer.data(), bytesToRead))
        {
            std::cerr << "Failed to read from file: " << filepath << std::endl;
            file.close();
            return false;
        }
        std::cout << std::format("Sending: {} {}", (unsigned int)Command::FileChunk, bytesToRead) << std::endl;
        if (!SendData(socket, Command::FileChunk, buffer.data(), static_cast<uint32_t>(bytesToRead)))
        {
            std::cerr << "Failed to send file chunk." << std::endl;
            file.close();
            return false;
        }
        fileSizeValue -= bytesToRead;
        std::cout << "Bytes remaining: " << fileSizeValue << std::endl;
    }

    file.close();
    return true;
}