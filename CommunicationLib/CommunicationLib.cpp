#include "pch.h"
#include "framework.h"

#pragma comment(lib, "ws2_32.lib")  

// Initialize Winsock
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

// Send data
static bool SendData(SOCKET socket, const std::string& message)
{
    size_t messageSize = static_cast<size_t>(message.size());
    if (send(socket, reinterpret_cast<char*>(&messageSize), sizeof(messageSize), 0) == SOCKET_ERROR)
    {
        std::cerr << "Failed to send message size: " << WSAGetLastError() << std::endl;
        return false;
    }

    size_t totalSent = 0;
    const char* dataPtr = message.c_str();

    while (totalSent < message.size())
    {
        int bytesSent = send(socket, dataPtr + totalSent, static_cast<int>(message.size() - totalSent), 0);
        if (bytesSent == SOCKET_ERROR)
        {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            return false;
        }
        totalSent += bytesSent;
    }
    return true;
}

// Receive data
static std::string ReceiveData(SOCKET socket)
{
    size_t messageSize = 0;
    int bytesReceived = recv(socket, reinterpret_cast<char*>(&messageSize), sizeof(messageSize), 0);
    if (bytesReceived != sizeof(messageSize))
    {
        std::cerr << "Failed to receive message size." << std::endl;
        return "";
    }

    std::string receivedData;
    size_t totalReceived = 0;
    std::vector<char> buffer(1024);

    while (totalReceived < messageSize)
    {
        int bytesToReceive = static_cast<int>(min(buffer.size(), messageSize - totalReceived));
        bytesReceived = recv(socket, buffer.data(), bytesToReceive, 0);

        if (bytesReceived == SOCKET_ERROR)
        {
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
            return "";
        }
        else if (bytesReceived == 0)
        {
            std::cerr << "Connection closed unexpectedly." << std::endl;
            return "";
        }
        receivedData.append(buffer.data(), bytesReceived);
        totalReceived += bytesReceived;
    }

    return receivedData;
}

static bool CheckResponse(SOCKET socket)
{
	std::string responce = ReceiveData(socket);
    std::cout << "Received responce: " << responce << std::endl;
    if (responce.compare("OK") != 0)
    {
        std::cerr << "Failed to receive responce from server" << std::endl;
        return false;
    }
	return true;
}

static bool SendFileToStream(const std::string& filename, SOCKET socket)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        SendData(socket, "ERROR");
        return false;
    }

    std::string response = "OK";
    if (SendData(socket, response))
    {
        std::cout << "Sending file: " << filename << std::endl;
    }
    else
    {
        std::cerr << "Failed to send response" << std::endl;
        file.close();
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t fileSizeValue = static_cast<size_t>(fileSize);
    if (send(socket, reinterpret_cast<char*>(&fileSizeValue), sizeof(fileSizeValue), 0) == SOCKET_ERROR)
    {
        std::cerr << "Failed to send file size: " << WSAGetLastError() << std::endl;
        file.close();
        return false;
    }

    char buffer[1024];
    while (fileSize > 0)
    {
        std::streamsize bytesToRead = min(static_cast<std::streamsize>(sizeof(buffer)), fileSize);
        if (!file.read(buffer, bytesToRead))
        {
            std::cerr << "Failed to read from file: " << filename << std::endl;
            file.close();
            return false;
        }

        size_t totalSent = 0;
        while (totalSent < bytesToRead)
        {
            int bytesSent = send(socket, buffer + totalSent, static_cast<int>(bytesToRead - totalSent), 0);
            if (bytesSent == SOCKET_ERROR)
            {
                std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
                file.close();
                return false;
            }
            totalSent += bytesSent;
        }
        fileSize -= bytesToRead;
    }

    file.close();
    return true;
}

static bool WriteFileFromStream(const std::string& filename, SOCKET socket)
{
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        SendData(socket, "ERROR");
        return false;
    }

    if (!CheckResponse(socket))
    {
        std::cerr << "Failed to receive response" << std::endl;
        file.close();
        return false;
    }

    size_t fileSize = 0;
    int bytesReceived = recv(socket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0);
    if (bytesReceived != sizeof(fileSize))
    {
        std::cerr << "Failed to receive file size." << std::endl;
        file.close();
        return false;
    }

    size_t totalReceived = 0;
    char buffer[1024];

    while (totalReceived < fileSize)
    {
        int bytesToReceive = static_cast<int>(min(sizeof(buffer), fileSize - totalReceived));
        bytesReceived = recv(socket, buffer, bytesToReceive, 0);
        if (bytesReceived == SOCKET_ERROR)
        {
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
            file.close();
            return false;
        }
        else if (bytesReceived == 0)
        {
            std::cerr << "Connection closed unexpectedly." << std::endl;
            file.close();
            return false;
        }
        file.write(buffer, bytesReceived);
        totalReceived += bytesReceived;
    }

    std::cout << "File '" << filename << "' received (" << totalReceived << " bytes)" << std::endl;
    file.close();
    return true;
}