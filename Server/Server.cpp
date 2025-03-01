#include <mutex>
#include <queue>
#include <chrono>
#include <thread>
#include <unordered_map>
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")  

static std::unordered_map<SOCKET, int> clientRooms;
static std::mutex roomMutex;

static std::unordered_map<int, std::queue<std::string>> roomMessageQueues;
static std::unordered_map<int, std::vector<SOCKET>> roomClients;

static const unsigned int TIME_OUT_MS = 30000; // 30s

static bool SendData(SOCKET clientSocket, unsigned char command, const void* payload, uint32_t payloadSize)
{
	uint32_t msgLen = htonl(payloadSize);
	std::vector<char> buffer(sizeof(msgLen) + 1 + payloadSize);

	std::memcpy(buffer.data(), &msgLen, sizeof(msgLen));
	buffer[sizeof(msgLen)] = static_cast<char>(command);

	if (payloadSize > 0)
	{
		std::memcpy(buffer.data() + sizeof(msgLen) + 1,	payload, payloadSize);
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

static void BroadcastMessage(const std::string& message, SOCKET senderSocket, int code = 0x20)
{
	int roomID = 0;
	std::vector<SOCKET> clientsInRoom;
	{
		std::lock_guard<std::mutex> lk(roomMutex);
		if (clientRooms.find(senderSocket) != clientRooms.end())
		{
			roomID = clientRooms[senderSocket];
		}
		clientsInRoom = roomClients[roomID];
		roomMessageQueues[roomID].push(message);
	}

	for (SOCKET client : clientsInRoom)
	{
		if (client != senderSocket)
		{
			SendData(client, code, message.c_str(), (uint32_t)message.size());
		}
	}
}

//static bool BroadcastFile(const std::string& fullFilePath, const std::string& displayFileName, size_t fileSize, SOCKET senderSocket, const std::string& senderClientName)
//{
//	int roomID = 0;
//	std::vector<SOCKET> clientsInRoom;
//	{
//		std::lock_guard<std::mutex> lk(roomMutex);
//		if (clientRooms.find(senderSocket) != clientRooms.end())
//		{
//			roomID = clientRooms[senderSocket];
//		}
//		clientsInRoom = roomClients[roomID];
//	}
//
//	std::vector<std::thread> threads;
//	threads.reserve(clientsInRoom.size());
//
//	for (SOCKET client : clientsInRoom)
//	{
//		if (client == senderSocket)
//			continue;
//
//		threads.emplace_back([&fullFilePath, &displayFileName, fileSize, client, &senderClientName]()
//		{
//			std::string offerMsg = std::format("fo {} {} {}", senderClientName, displayFileName, fileSize);
//			std::cout << "Offering file: " << displayFileName << " to client " << client << std::endl;
//			if (!SendData(client, offerMsg))
//				return;
//
//			std::string response = ReceiveData(client, TIME_OUT_MS);
//			if (response == "TIME_OUT")
//			{
//				std::cout << "Client " << senderClientName << " timed out on file: " << displayFileName << std::endl;
//				return;
//			}
//
//			if (response == "y")
//			{
//				if (!SendFileToStream(fullFilePath, client))
//				{
//					std::cerr << "File transfer failed for client " << client << std::endl;
//				}
//			}
//			else
//			{
//				std::cout << "Client " << client << " rejected file: " << displayFileName << std::endl;
//			}
//		});
//	}
//
//	for (std::thread& thread : threads)
//		thread.join();
//
//	return true;
//}

static void JoinRoom(SOCKET clientSocket, int roomID, std::string clientName)
{
	{
		std::lock_guard<std::mutex> lk(roomMutex);
		if (clientRooms.find(clientSocket) != clientRooms.end())
		{
			int oldRoom = clientRooms[clientSocket];
			std::vector<SOCKET>& clientsVec = roomClients[oldRoom];
			clientsVec.erase(std::remove(clientsVec.begin(), clientsVec.end(), clientSocket), clientsVec.end());
		}
		clientRooms[clientSocket] = roomID;
		roomClients[roomID].push_back(clientSocket);
	} 

	std::string joinMsg = std::format("CLIENT {} JOINED ROOM {}\n", clientName, roomID);
	BroadcastMessage(joinMsg, clientSocket);
}

static void HandleMessage(unsigned char command, const std::vector<char>& payload, SOCKET clientSocket)
{
	std::istringstream dataStream(std::string(payload.begin(), payload.end()));

	switch (command)
	{
	case 0x01: // join room
	{
		int roomID;
		std::string clientName;
		dataStream >> roomID >> clientName;
		if (!clientName.empty())
		{
			JoinRoom(clientSocket, roomID, clientName);
			const char* msg = "Joined room successfully.";
			SendData(clientSocket, 0x10, msg, (uint32_t)strlen(msg));
			BroadcastMessage(std::format("CLIENT {} JOINED ROOM {}\n", clientName, roomID), clientSocket);
		}
	}
	break;

	case 0x02: // broadcast text
	{
		std::string text((std::istreambuf_iterator<char>(dataStream)), std::istreambuf_iterator<char>());

		if (!text.empty())
		{
			std::string broadcastMsg = std::format("CLIENT: {}", text);
			BroadcastMessage(broadcastMsg, clientSocket);
		}
	}
	break;

	case 0x03: // file offer or file chunk
	{
		std::string command;
		dataStream >> command;
		if (command == "fo")
		{
			std::string senderName, filename;
			size_t fileSize = 0;
			dataStream >> senderName >> filename >> fileSize;
			BroadcastMessage(std::format("CLIENT {} OFFERS FILE {} ({} bytes). ACCEPT (y/n)?", senderName, filename, fileSize), clientSocket, 0x30);
		}
		else if (command == "fc")
		{
			std::string filename;
			size_t fileSize = 0;
			dataStream >> filename >> fileSize;
			std::string fullFilePath = std::format("ServerFiles/{}", filename);
			if (!WriteFileFromStream(fullFilePath, clientSocket))
			{
				std::cerr << "Failed to write file: " << filename << std::endl;
			}
		}
	}
	break;

	default:
	{
		const char* unknown = "Unknown command.";
		SendData(clientSocket, 0xFF, unknown, (uint32_t)strlen(unknown));
	}
	break;
	}
}

static void HandleClient(SOCKET clientSocket)
{
	std::vector<char> recvBuffer;
	const size_t headerSize = sizeof(uint32_t) + 1; // length + command
	try
	{
		while (true)
		{
			char temp[1024];
			int bytesRead = recv(clientSocket, temp, sizeof(temp), 0);
			if (bytesRead <= 0)
			{
				std::cerr << "Connection closed or recv error.\n";
				break;
			}
			recvBuffer.insert(recvBuffer.end(), temp, temp + bytesRead);

			while (recvBuffer.size() >= headerSize)
			{
				uint32_t msgLen = 0;
				std::memcpy(&msgLen, recvBuffer.data(), sizeof(uint32_t));

				msgLen = ntohl(msgLen);

				if (recvBuffer.size() < (headerSize + msgLen))
					break; 

				unsigned char command = recvBuffer[sizeof(uint32_t)];

				std::vector<char> payload(msgLen);
				std::memcpy(payload.data(),
					recvBuffer.data() + headerSize,
					msgLen);

				recvBuffer.erase(
					recvBuffer.begin(),
					recvBuffer.begin() + headerSize + msgLen
				);

				HandleMessage(command, payload, clientSocket);
			}
		}
	}
	catch (...)
	{
		std::cerr << "Unknown exception in HandleClient.\n";
	}
	closesocket(clientSocket);
}

int main()  
{  
	if (!InitializeWinsock())
		return 1;

   int port = 12345;  
   SOCKET serverSocket = CreateAndBindSocket(port);

   if (serverSocket == INVALID_SOCKET)
       return 1;

   if (Listen(serverSocket) != 0)
	   return 1;

   std::filesystem::path serverPath = std::filesystem::current_path();
   std::filesystem::path serverFiles = serverPath / "ServerFiles";
   std::filesystem::create_directory(serverFiles);

   std::cout << "Server listening on port " << port << std::endl;  

   while (true) 
   {
	   SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
	   if (clientSocket == INVALID_SOCKET) 
	   {
		   continue;
	   }

	   std::thread clientThread(HandleClient, clientSocket);
	   clientThread.detach();
   }
}