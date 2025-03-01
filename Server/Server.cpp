#include <mutex>
#include <queue>
#include <chrono>
#include <future>
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")  

static std::mutex responseMutex;
static std::unordered_map<SOCKET, std::promise<std::string>> responsePromises;

static std::mutex roomMutex;
static std::unordered_map<SOCKET, int> clientRooms;

static std::unordered_map<int, std::vector<SOCKET>> roomClients;
static std::unordered_map<int, std::queue<std::string>> roomMessageQueues;

static std::unordered_map<SOCKET, FileTransferState> fileTransfers;

static const unsigned int TIME_OUT_MS = 30000; // 30s

static void BroadcastMessage(const std::string& message, SOCKET senderSocket, Command command = Command::MessageTextResponse)
{
	int roomID = 0;
	std::vector<SOCKET> clientsInRoom;
	{
		std::lock_guard<std::mutex> lock(roomMutex);
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
			SendData(client, command, message.c_str());
		}
	}
}

static bool BroadcastFile(const std::string& filepath, const std::string& filename, size_t fileSize, SOCKET senderSocket, const std::string& senderName)
{
	int roomID = 0;
	std::vector<SOCKET> clientsInRoom;
	{
		std::lock_guard<std::mutex> lock(roomMutex);
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

		threads.emplace_back([&filepath, &filename, fileSize, client, &senderName]()
		{
			std::string offerMsg = std::format("fo {} {} {}", senderName, filename, fileSize);
			std::cout << "Offering file: " << filename << " to client " << client << std::endl;
			if (!SendData(client, Command::FileOffer, offerMsg.c_str()))
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
				if (!SendFile(filepath, filename, client))
				{
					std::cerr << "File transfer failed for client " << client << std::endl;
				}
			}
			else
			{
				std::cout << "Client " << client << " rejected file: " << filename << std::endl;
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
		std::lock_guard<std::mutex> lock(roomMutex);
		if (clientRooms.find(clientSocket) != clientRooms.end())
		{
			int oldRoom = clientRooms[clientSocket];
			std::vector<SOCKET>& clientsVec = roomClients[oldRoom];
			clientsVec.erase(std::remove(clientsVec.begin(), clientsVec.end(), clientSocket), clientsVec.end());
		}
		clientRooms[clientSocket] = roomID;
		roomClients[roomID].push_back(clientSocket);
	} 

	BroadcastMessage(std::format("CLIENT {} JOINED ROOM {}", clientName, roomID), clientSocket);
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
			SendData(clientSocket, Command::JoinRoomResponse, "Joined room successfully.");
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
			BroadcastMessage(std::format("CLIENT {}: {}", clientName, text), clientSocket);
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

		if (BroadcastFile(fullFilePath, filename, fileSize, clientSocket, senderName))
		{
			SendData(clientSocket, Command::MessageTextResponse, "File transfer complete to all clients.");
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
		std::filesystem::create_directory("ServerFiles/" + clientName);

		FileTransferState state;
		state.stream.open(fullFilePath, std::ios::binary);
		if (!state.stream)
		{
			std::cerr << "Failed to open file for writing: " << fullFilePath << std::endl;
			break;
		}
		state.expectedSize = fileSize;
		state.received = 0;
		fileTransfers[clientSocket] = std::move(state);
	}
	break;

	case Command::FileChunk:
	{
		auto it = fileTransfers.find(clientSocket);
		if (it != fileTransfers.end())
		{
			FileTransferState& state = it->second;
			state.stream.write(payload.data(), payload.size());
			state.received += payload.size();
			if (state.received >= state.expectedSize)
			{
				state.stream.close();
				std::cout << "File received complete (" << state.received << " bytes)" << std::endl;
				fileTransfers.erase(it);
			}
		}
		else
		{
			std::cerr << "Received a file chunk with no transfer state" << std::endl;
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
		SendData(clientSocket, Command::Unknown, "Unknown command.");
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