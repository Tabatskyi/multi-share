#include "pch.h"
#include "framework.h"

#pragma comment(lib, "ws2_32.lib")  

// Initialize Winsock
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

// Send data
static bool SendData(SOCKET socket, const std::string& message)
{
	if (send(socket, message.c_str(), static_cast<int>(message.size() * sizeof(char)), 0) == SOCKET_ERROR)
	{
		std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
		return false;
	}
	return true;
}

// Receive data
static int ReceiveData(SOCKET socket, std::vector<char>& buffer)
{
	return recv(socket, buffer.data(), sizeof(buffer), 0);
}

static bool CheckResponce(SOCKET socket)
{
    std::vector<char> responceBuffer(1024);
    int responceBytes = ReceiveData(socket, responceBuffer);
    std::string responce(responceBuffer.data(), responceBytes);
    std::cout << "Received responce: " << responce << std::endl;
    if (responceBytes <= 0 || responce.compare("OK") != 0)
    {
        std::cerr << "Failed to receive responce from server" << std::endl;
        return false;
    }
	return true;
}

static bool WriteFileFromStream(std::string filename, SOCKET socket)
{
    std::ofstream file(filename, std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        std::string response = "ERROR";
        send(socket, response.c_str(), static_cast<int>(response.size()), 0);
		return false;
    }

    std::string response = "OK";
    if (SendData(socket, response))
    {
        int totalBytesReceived = 0;
        std::vector<char> fileBuffer(1024);
        int bytesReceived = 0;
        while ((bytesReceived = ReceiveData(socket, fileBuffer)) > 0)
        {
            file.write(fileBuffer.data(), bytesReceived);
            totalBytesReceived += bytesReceived;
        }

        if (bytesReceived == SOCKET_ERROR)
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;

        std::cout << "File '" << filename << "' received (" << totalBytesReceived << " bytes)" << std::endl;
		file.close();
		return true;
    }
	return false;
}

static bool SendFileToStream(std::string filename, SOCKET socket)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        std::string response = "ERROR";
        send(socket, response.c_str(), static_cast<int>(response.size()), 0);
        return false;
    }
	if (CheckResponce(socket))
    {
        std::vector<char> fileBuffer(1024);
        while (file.read(fileBuffer.data(), fileBuffer.size()) || file.gcount() > 0)
        {
            std::streamsize bytesToSend = file.gcount();
            if (send(socket, fileBuffer.data(), static_cast<int>(bytesToSend), 0) == SOCKET_ERROR)
            {
                std::cerr << "File data send failed with error: " << WSAGetLastError() << std::endl;
                file.close();
                return false;
            }
        }
        file.close();
        return true;
    }
    return false;
}