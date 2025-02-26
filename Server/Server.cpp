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

static void JoinRoom(SOCKET clientSocket, int roomID, std::string clientName)
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

	std::string joinMsg = std::format("CLIENT {} JOINED ROOM {}\n", clientName, roomID);
	roomMessageQueues[roomID].push(joinMsg);
	std::cout << joinMsg;
}

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

static bool BroadcastFile(const std::string& filename, size_t fileSize, SOCKET senderSocket)
{
	int roomID = 0;
	{
		std::lock_guard<std::mutex> lk(roomMutex);
		if (clientRooms.find(senderSocket) != clientRooms.end())
			roomID = clientRooms[senderSocket];
	}

	std::vector<std::thread> threads;
	threads.reserve(roomClients[roomID].size());

	for (SOCKET client : roomClients[roomID])
	{
		if (client == senderSocket)
			continue;

		threads.emplace_back([&filename, fileSize, client]()
		{
			std::string offerMsg = std::format("FILE_OFFER {} {}", filename, fileSize);
			if (!SendData(client, offerMsg))
				return;

			std::string response = ReceiveData(client, TIME_OUT_MS);
			if (response == "TIME_OUT")
			{
				std::cout << "Client " << client << " timed out on file: " << filename << std::endl;
				return;
			}

			if (response == "y")
			{
				if (!SendFileToStream(filename, client))
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

	for (auto& th : threads)
		th.join();

	return true;
}

static void HandleClient(SOCKET clientSocket, const std::filesystem::path& serverFiles)
{
	std::string message = ReceiveData(clientSocket);
	if (!message.empty())
	{
		std::cout << "Received command: " << message << std::endl;
		std::istringstream iss(message);
		std::string command, clientName, filename;
		iss >> command >> clientName >> filename;

		{
			std::lock_guard<std::mutex> lock(statsMutex);
			commandStatistics[command]++;
		}

		std::filesystem::path clientFolder = serverFiles / clientName;
		std::filesystem::create_directory(clientFolder);

		if (command == "j")
		{
			int roomID;
			iss >> roomID;
			JoinRoom(clientSocket, roomID, clientName);
			SendData(clientSocket, "OK");
		}
		else if (command == "sf")
		{
			std::string filePath = (clientFolder / filename).string();
			if (!WriteFileFromStream(filePath, clientSocket))
			{
				std::cerr << "Failed to receive file: " << filename << std::endl;
				SendData(clientSocket, "ERROR");
				return;
			}

			std::cout << "File download completed" << std::endl;
			SendData(clientSocket, "OK");
			
		}
		else if (command == "q")
		{
			std::cout << "Server shutting down." << std::endl;
			closesocket(clientSocket);
			WSACleanup();
			exit(0);
		}
		else if (command == "sm") 
		{
			std::cout << "Broadcasting message:" << message << std::endl;
			BroadcastMessage(message, clientSocket);
		}
		else
		{
			std::string response = "Unknown command.";
		}
	}
	else
	{
		std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
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