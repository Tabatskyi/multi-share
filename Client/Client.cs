using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Client;

internal record FileOffer(string Sender, string Filename, string FileSize);

internal class Client
{
    private static readonly string serverIp = "127.0.0.1";
    private static readonly int port = 12345;
    private static Socket clientSocket;
    private static string clientName = "Unknown";

    private static readonly ConcurrentQueue<FileOffer> fileOffers = new();

    private static readonly BlockingCollection<string> promptQueue = [];
    private static readonly BlockingCollection<string> responseQueue = [];
    private static Thread? inputThread;

    static void Main()
    {
        inputThread = new Thread(ConsoleInputLoop) { IsBackground = true };
        inputThread.Start();

        clientName = GetConsoleInput("Enter your client name:\n> ");

        if (!EstablishConnection(serverIp, port))
        {
            Console.WriteLine("Could not establish persistent connection to server.");
            return;
        }

        Thread receiverThread = new(ReceiveRoutine);
        receiverThread.Start();

        string command;
        do
        {
            command = GetCommand();

            if (command.StartsWith('j') || command.StartsWith('m') || command.StartsWith('f'))
            {
                string[] parts = command.Split(' ', 2, StringSplitOptions.TrimEntries);
                if (parts.Length == 2)
                    command = $"{parts[0]} {clientName} {parts[1]}";
            }

            if (!SendCommand(command))
                Console.WriteLine("Error sending command.");

            if (command.StartsWith("f "))
            {
                string[] parts = command.Split(' ', 3, StringSplitOptions.TrimEntries);
                if (parts.Length == 3)
                {
                    if (!SendFile(parts[2]))
                        Console.WriteLine("Error sending file data.");
                }
            }

        } while (!command.Contains('q'));
    }

    private static void ConsoleInputLoop()
    {
        while (true)
        {
            string prompt = promptQueue.Take();
            Console.Write(prompt);
            string input = Console.ReadLine() ?? string.Empty;
            responseQueue.Add(input);
        }
    }

    static string GetConsoleInput(string prompt)
    {
        promptQueue.Add(prompt);      
        return responseQueue.Take();  
    }

    static void ProcessPendingFileOffers()
    {
        while (fileOffers.TryDequeue(out var offer))
        {
            Console.WriteLine($"Client {offer.Sender} is offering file '{offer.Filename}' ({offer.FileSize} bytes).");
            string response = GetConsoleInput("Accept (y/n)?\n> ");

            byte[] responseBytes = Encoding.UTF8.GetBytes(response);
            byte[] responseLength = BitConverter.GetBytes((ulong)responseBytes.Length);
            clientSocket.Send(responseLength);
            clientSocket.Send(responseBytes);

            if (response.Equals("y", StringComparison.CurrentCultureIgnoreCase))
            {
                Console.WriteLine($"Receiving file '{offer.Filename}'...");
                if (!ReceiveFile(offer.Filename))
                    Console.WriteLine("Error receiving file data.");
            }
            else
            {
                Console.WriteLine("File transfer declined.");
            }
        }
    }

    static string GetCommand()
    {
        while (true)
        {
            string input = GetConsoleInput("Enter command (j <roomID>, m <message>, f <filename>, q):\n> ");
            string[] parts = input.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length >= 1)
            {
                string cmd = parts[0].ToLower();
                if ((cmd == "j" || cmd == "m" || cmd == "f") && parts.Length == 2)
                    return input;
                else if (cmd == "q")
                    return input;
            }
            Console.WriteLine("Invalid command. Please try again.");
        }
    }

    static bool EstablishConnection(string serverIp, int port)
    {
        try
        {
            clientSocket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            clientSocket.Connect(new IPEndPoint(IPAddress.Parse(serverIp), port));
            clientSocket.NoDelay = true;
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to connect to server: {ex.Message}");
            return false;
        }
    }

    static bool SendCommand(string command)
    {
        try
        {
            byte[] commandBytes = Encoding.UTF8.GetBytes(command);
            byte[] lengthPrefix = BitConverter.GetBytes((ulong)commandBytes.Length);
            clientSocket.Send(lengthPrefix);
            clientSocket.Send(commandBytes);
            Console.WriteLine($"Sent command: {command}");
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error sending command: {ex.Message}");
            return false;
        }
    }

    static void ReceiveRoutine()
    {
        try
        {
            while (true)
            {
                byte[] lengthBuffer = new byte[sizeof(ulong)];
                int received = clientSocket.Receive(lengthBuffer);
                if (received == 0)
                {
                    Console.WriteLine("Connection closed.");
                    break;
                }
                if (received != sizeof(ulong))
                {
                    Console.WriteLine("Incomplete length prefix received.");
                    continue;
                }
                ulong messageLength = BitConverter.ToUInt64(lengthBuffer, 0);
                byte[] buffer = new byte[messageLength];
                int totalReceived = 0;
                while (totalReceived < (int)messageLength)
                {
                    int bytesReceived = clientSocket.Receive(buffer, totalReceived, (int)messageLength - totalReceived, SocketFlags.None);
                    if (bytesReceived == 0)
                    {
                        Console.WriteLine("Connection closed during message reception.");
                        return;
                    }
                    totalReceived += bytesReceived;
                }
                string message = Encoding.UTF8.GetString(buffer);

                if (message.StartsWith("fo "))
                {
                    string[] parts = message.Split(' ', 4, StringSplitOptions.TrimEntries);
                    if (parts.Length >= 4)
                    {
                        fileOffers.Enqueue(new FileOffer(parts[1], parts[2], parts[3]));
                        ProcessPendingFileOffers(); 
                    }
                    else
                    {
                        Console.WriteLine("Malformed file offer received.");
                    }
                }
                else
                {
                    Console.WriteLine($"Received message: {message}");
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error receiving data: {ex.Message}");
        }
    }

    static bool SendFile(string filename)
    {
        try
        {
            if (!File.Exists(filename))
            {
                Console.WriteLine("File not found.");
                return false;
            }

            byte[] fileBytes = File.ReadAllBytes(filename);
            ulong fileLength = (ulong)fileBytes.Length;
            byte[] lengthPrefix = BitConverter.GetBytes(fileLength);
            clientSocket.Send(lengthPrefix);

            int totalSent = 0;
            while (totalSent < fileBytes.Length)
            {
                int sent = clientSocket.Send(fileBytes, totalSent, fileBytes.Length - totalSent, SocketFlags.None);
                if (sent == 0)
                {
                    Console.WriteLine("Connection closed during file sending.");
                    return false;
                }
                totalSent += sent;
            }
            Console.WriteLine($"File '{filename}' sent ({totalSent} bytes).");
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error sending file: {ex.Message}");
            return false;
        }
    }

    static bool ReceiveFile(string filename)
    {
        try
        {
            byte[] lengthBuffer = new byte[sizeof(ulong)];
            int received = clientSocket.Receive(lengthBuffer);
            if (received == 0)
            {
                Console.WriteLine("Connection closed.");
                return false;
            }
            if (received != sizeof(ulong))
            {
                Console.WriteLine("Incomplete length prefix received for file.");
                return false;
            }
            ulong fileLength = BitConverter.ToUInt64(lengthBuffer, 0);
            byte[] fileBuffer = new byte[fileLength];
            int totalReceived = 0;
            while (totalReceived < (int)fileLength)
            {
                int bytesReceived = clientSocket.Receive(fileBuffer, totalReceived, (int)fileLength - totalReceived, SocketFlags.None);
                if (bytesReceived == 0)
                {
                    Console.WriteLine("Connection closed during file reception.");
                    return false;
                }
                totalReceived += bytesReceived;
            }
            File.WriteAllBytes(filename, fileBuffer);
            Console.WriteLine($"File '{filename}' received ({totalReceived} bytes).");
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error receiving file: {ex.Message}");
            return false;
        }
    }
}