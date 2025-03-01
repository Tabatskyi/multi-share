#include <mutex>
#include <queue>
#include <chrono>
#include <future>
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")  

static std::mutex responseMutex;
static std::unordered_map<SOCKET, std::promise<std::string>> responsePromises;

static std::unordered_map<SOCKET, int> clientRooms;
static std::mutex roomMutex;

static std::unordered_map<int, std::queue<std::string>> roomMessageQueues;
static std::unordered_map<int, std::vector<SOCKET>> roomClients;

static const unsigned int TIME_OUT_MS = 30000; // 30s

static void BroadcastMessage(const std::string& message, SOCKET senderSocket, Command command = Command::MessageTextResponse)
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
			SendData(client, command, message.c_str(), (uint32_t)message.size());
		}
	}
}

static bool BroadcastFile(const std::string& fullFilePath, const std::string& displayFileName, size_t fileSize, SOCKET senderSocket, const std::string& senderClientName)
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
	}

	std::vector<std::thread> threads;
	threads.reserve(clientsInRoom.size());

	for (SOCKET client : clientsInRoom)
	{
		if (client == senderSocket)
			continue;

		threads.emplace_back([&fullFilePath, &displayFileName, fileSize, client, &senderClientName]()
		{
			std::string offerMsg = std::format("fo {} {} {}", senderClientName, displayFileName, fileSize);
			std::cout << "Offering file: " << displayFileName << " to client " << client << std::endl;
			if (!SendData(client, Command::FileOffer, offerMsg.c_str(), (uint32_t)offerMsg.size()))
				return;

			std::promise<std::string> promise;
			auto futureResponse = promise.get_future();
			{
				std::lock_guard<std::mutex> lock(responseMutex);
				responsePromises[client] = std::move(promise);
			}

			if (futureResponse.wait_for(std::chrono::milliseconds(TIME_OUT_MS)) == std::future_status::timeout)
			{
				std::cerr << "Timeout waiting for response from client " << client << "\n";
				std::lock_guard<std::mutex> lock(responseMutex);
				responsePromises.erase(client);
				return;
			}

			std::string response = futureResponse.get();
			std::cout << "Client " << client << " response: " << response << std::endl;

			{
				std::lock_guard<std::mutex> lock(responseMutex);
				responsePromises.erase(client);
			}

			if (response == "y")
			{
				if (!SendFileToStream(fullFilePath, displayFileName, client))
				{
					std::cerr << "File transfer failed for client " << client << std::endl;
				}
			}
			else
			{
				std::cout << "Client " << client << " rejected file: " << displayFileName << std::endl;
			}
		});
	}

	for (std::thread& thread : threads)
		thread.join();

	return true;
}


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

	std::string joinMsg = std::format("CLIENT {} JOINED ROOM {}", clientName, roomID);
	BroadcastMessage(joinMsg, clientSocket);
}

static void HandleMessage(unsigned char command, const std::string& payload, SOCKET clientSocket)
{
	std::istringstream dataStream(std::string(payload.begin(), payload.end()));

	switch ((Command)command)
	{
	case Command::JoinRoom:
	{
		int roomID;
		std::string clientName;
		dataStream >> clientName >> roomID;
		if (!clientName.empty())
		{
			JoinRoom(clientSocket, roomID, clientName);
			const char* msg = "Joined room successfully.";
			SendData(clientSocket, Command::JoinRoomResponse, msg, (uint32_t)strlen(msg));
		}
	}
	break;

	case Command::MessageText:
	{
		std::string clientName;
		std::string text;
		dataStream >> clientName >> text;

		if (!text.empty())
		{
			std::string broadcastMsg = std::format("CLIENT {}: {}", clientName, text);
			BroadcastMessage(broadcastMsg, clientSocket);
		}
	}
	break;

	case Command::FileOffer:
	{
		std::string command;
		dataStream >> command;
		std::string senderName, filename;
		size_t fileSize = 0;
		dataStream >> senderName >> filename >> fileSize;
		std::string fullFilePath = std::format("ServerFiles/{}/{}", senderName, filename);

		bool success = BroadcastFile(fullFilePath, filename, fileSize, clientSocket, senderName);
		if (success)
		{
			const char* completeMsg = "File transfer complete to all clients.";
			SendData(clientSocket, Command::MessageTextResponse, completeMsg, (uint32_t)strlen(completeMsg));
		}
	}
	break;

	case Command::FileSize:
	{
		std::string filename;
		std::string clientName;
		size_t fileSize = 0;

		dataStream >> clientName >> filename >> fileSize;
		std::string fullFilePath = std::format("ServerFiles/{}/{}", clientName, filename);

		FileTransferContext ctx;
		ctx.ofs.open(fullFilePath, std::ios::binary);
		if (!ctx.ofs)
		{
			std::cerr << "Failed to open file for writing: " << fullFilePath << std::endl;
			break;
		}
		ctx.expectedSize = fileSize;
		ctx.received = 0;
		fileTransfers[clientSocket] = std::move(ctx);
	}
	break;

	case Command::FileChunk:
	{
		auto it = fileTransfers.find(clientSocket);
		if (it != fileTransfers.end())
		{
			FileTransferContext& ctx = it->second;
			ctx.ofs.write(payload.data(), payload.size());
			ctx.received += payload.size();
			if (ctx.received >= ctx.expectedSize)
			{
				ctx.ofs.close();
				std::cout << "File received complete (" << ctx.received << " bytes)" << std::endl;
				fileTransfers.erase(it);
			}
		}
		else
		{
			std::cerr << "Received a file chunk with no transfer context" << std::endl;
		}
	}
	break;

	case Command::FileOfferResponse:
	{
		std::string response = payload;
		{
			std::lock_guard<std::mutex> lock(responseMutex);
			auto it = responsePromises.find(clientSocket);
			if (it != responsePromises.end())
			{
				it->second.set_value(response);
			}
		}
	}
	break;

	default:
	{
		const char* unknown = "Unknown command.";
		SendData(clientSocket, Command::Unknown, unknown, (uint32_t)strlen(unknown));
	}
	break;
	}
}

static void HandleClient(SOCKET clientSocket)
{
	try
	{
		while (true)
		{
			Message message;
			if (!ReceiveMessage(clientSocket, message))
			{
				std::cerr << "Failed to receive complete message or connection closed.\n";
				break;
			}
			HandleMessage(message.command, message.payload, clientSocket);
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception in HandleClient: " << e.what() << std::endl;
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