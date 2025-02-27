#include <mutex>
#include <queue>
#include <chrono>
#include <thread>
#include <unordered_map>
#include "CommunicationLib.cpp"

#pragma comment(lib, "CommunicationLib.lib")  

std::unordered_map<std::string, int> commandStatistics;
std::mutex statsMutex;

static std::unordered_map<SOCKET, int> clientRooms;
static std::mutex roomMutex;

static std::unordered_map<int, std::queue<std::string>> roomMessageQueues;
static std::unordered_map<int, std::vector<SOCKET>> roomClients;

static const unsigned int TIME_OUT_MS = 30000; // 30s

static void BroadcastMessage(const std::string& message, SOCKET senderSocket)
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
			SendData(client, message);
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
			if (!SendData(client, offerMsg))
				return;

			std::string response = ReceiveData(client, TIME_OUT_MS);
			if (response == "TIME_OUT")
			{
				std::cout << "Client " << senderClientName << " timed out on file: " << displayFileName << std::endl;
				return;
			}

			if (response == "y")
			{
				if (!SendFileToStream(fullFilePath, client))
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

	std::string joinMsg = std::format("CLIENT {} JOINED ROOM {}\n", clientName, roomID);
	BroadcastMessage(joinMsg, clientSocket);
}

static void HandleClient(SOCKET clientSocket, const std::filesystem::path& serverFiles)
{
	try
	{
		while (true)
		{
			std::string message = ReceiveData(clientSocket);
			if (message.empty())
			{
				std::cerr << "Receive failed or connection closed. Error: " << WSAGetLastError() << std::endl;
				break;
			}
			std::cout << "Received command: " << message << std::endl;
			std::istringstream iss(message);
			std::string command, clientName;
			iss >> command >> clientName;

			std::filesystem::path clientFolder = serverFiles / clientName;
			std::filesystem::create_directory(clientFolder);

			if (command == "j")
			{
				int roomID;
				iss >> roomID;
				JoinRoom(clientSocket, roomID, clientName);
				SendData(clientSocket, "OK");
			}
			else if (command == "f")
			{
				std::string filename;
				iss >> filename;
				std::filesystem::path filePath = clientFolder / filename;
				if (!WriteFileFromStream(filePath.string(), clientSocket))
				{
					std::cerr << "Failed to receive file: " << filename << std::endl;
					SendData(clientSocket, "ERROR");
					continue;
				}
				std::cout << "File download completed" << std::endl;
				SendData(clientSocket, "OK");

				size_t fileSize = std::filesystem::file_size(filePath);
				BroadcastFile(filePath.string(), filename, fileSize, clientSocket, clientName);
			}
			else if (command == "m")
			{
				std::string text;
				std::getline(iss, text);
				while (!text.empty() && isspace(text.front()))
					text.erase(text.begin());
				std::string broadcastMsg = std::format("CLIENT {}: {}", clientName, text);
				std::cout << "Broadcasting message: " << broadcastMsg << std::endl;
				BroadcastMessage(broadcastMsg, clientSocket);
			}
			else
			{
				std::string response = "Unknown command.";
				SendData(clientSocket, response);
			}
		}
	}
	catch (const std::exception& ex)
	{
		std::cerr << "Exception in HandleClient: " << ex.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "Unknown exception in HandleClient." << std::endl;
	}
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

	   std::thread clientThread(HandleClient, clientSocket, serverFiles);
	   clientThread.detach();
   }
}