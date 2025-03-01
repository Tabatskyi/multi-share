using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Client;

internal class Client
{
    private static readonly string serverIp = "127.0.0.1";
    private static readonly int port = 12345;
    private static Socket? clientSocket;
    private static string clientName = "Unknown";
    private static bool isQuitting = false;

    private static FileStream? fileStream = null;
    private static long expectedFileSize = 0;
    private static long receivedFileSize = 0;
    private static string currentFileName = "";

    private enum Command : byte
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
    }

    static void Main()
    {
        if (!EstablishConnection(serverIp, port))
        {
            Console.WriteLine("Could not establish persistent connection to server.");
            return;
        }

        Console.Write("Enter your client name: ");
        clientName = Console.ReadLine() ?? "Unknown";

        Thread receiveThread = new(ReceiveData)
        {
            IsBackground = true
        };
        receiveThread.Start();

        while (!isQuitting)
        {
            if (Console.KeyAvailable)
            {
                string command = Console.ReadLine()?.Trim() ?? string.Empty;
                if (!string.IsNullOrEmpty(command))
                    HandleCommand(command);
            }
        }

        clientSocket?.Close();
    }

    private static void HandleCommand(string command)
    {
        // quit
        if (command.StartsWith("q", StringComparison.OrdinalIgnoreCase))
        {
            isQuitting = true;
            return;
        }

        // j <roomID>
        else if (command.StartsWith('j'))
        {
            var parts = command.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 2)
            {
                string payloadString = $"{clientName} {parts[1]}";
                byte[] payload = Encoding.UTF8.GetBytes(payloadString);
                SendData(Command.JoinRoom, payload);
            }
            return;
        }

        // m <message>
        else if (command.StartsWith('m'))
        {
            string payloadString = $"{clientName} {command[1..].Trim()}";
            byte[] payload = Encoding.UTF8.GetBytes(payloadString);
            SendData(Command.MessageText, payload);
            return;
        }

        // f <filename> 
        else if (command.StartsWith('f'))
        {
            var parts = command.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 2 && File.Exists(parts[1]))
            {
                string filename = parts[1];
                long fileSize = new FileInfo(filename).Length;

                SendData(Command.FileSize, Encoding.UTF8.GetBytes($"{clientName} {filename} {fileSize}"));
                SendData(Command.FileChunk, File.ReadAllBytes(filename));

                string payloadString = $"fo {clientName} {filename} {fileSize}";
                byte[] payload = Encoding.UTF8.GetBytes(payloadString);
                SendData(Command.FileOffer, payload);
            }
            else
            {
                Console.WriteLine("File path invalid.");
            }
            return;
        }

        else if (command.StartsWith('y') || command.StartsWith('n'))
            return;

        Console.WriteLine("Unknown command. Use j/m/f/q.");
    }

    private static bool EstablishConnection(string serverIp, int port)
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

    // communication protocol:
    // 4 bytes: payload length (network order)
    // 1 byte:  command
    // n bytes: payload
    private static bool SendData(Command command, byte[] payload)
    {
        try
        {
            int payloadLenNetwork = IPAddress.HostToNetworkOrder(payload.Length);
            byte[] buffer = new byte[4 + 1 + payload.Length];

            Array.Copy(BitConverter.GetBytes(payloadLenNetwork), 0, buffer, 0, 4);
            buffer[4] = (byte)command;
            Array.Copy(payload, 0, buffer, 5, payload.Length);

            int totalSent = 0;
            while (totalSent < buffer.Length)
            {
                int sent = clientSocket!.Send(buffer, totalSent, buffer.Length - totalSent, SocketFlags.None);
                if (sent <= 0)
                    return false;
                totalSent += sent;
            }
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error sending multiplexed data: {ex.Message}");
            return false;
        }
    }

    private static void ReceiveData()
    {
        try
        {
            while (clientSocket is { Connected: true } && !isQuitting)
            {
                byte[] lenBytes = new byte[4];
                int totalRead = 0;
                while (totalRead < 4)
                {
                    int read = clientSocket.Receive(lenBytes, totalRead, 4 - totalRead, SocketFlags.None);
                    if (read <= 0)
                    {
                        Console.WriteLine("Server closed connection.");
                        isQuitting = true;
                        return;
                    }
                    totalRead += read;
                }

                int networkOrderSize = BitConverter.ToInt32(lenBytes, 0);
                int payloadSize = IPAddress.NetworkToHostOrder(networkOrderSize);

                byte[] cmdByte = new byte[1];
                totalRead = 0;
                while (totalRead < 1)
                {
                    int read = clientSocket.Receive(cmdByte, totalRead, 1 - totalRead, SocketFlags.None);
                    if (read <= 0)
                    {
                        Console.WriteLine("Server closed connection.");
                        isQuitting = true;
                        return;
                    }
                    totalRead += read;
                }
                byte command = cmdByte[0];

                byte[] payload = new byte[payloadSize];
                totalRead = 0;
                while (totalRead < payloadSize)
                {
                    int read = clientSocket.Receive(payload, totalRead, payloadSize - totalRead, SocketFlags.None);
                    if (read <= 0)
                    {
                        Console.WriteLine("Server closed connection during payload read.");
                        isQuitting = true;
                        return;
                    }
                    totalRead += read;
                }

                HandleIncomingMessage(command, payload);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error in ReceiveData: {ex.Message}");
            isQuitting = true;
        }
    }

    private static void HandleIncomingMessage(byte command, byte[] payload)
    {
        switch ((Command)command)
        {
            case Command.JoinRoomResponse:
            case Command.MessageTextResponse:
                Console.WriteLine(Encoding.UTF8.GetString(payload));
                break;

            case Command.FileOffer:
            {
                string message = Encoding.UTF8.GetString(payload);
                if (message.StartsWith("fo ")) // fo <senderClient> <filename> <fileSize>
                {
                    string[] parts = message.Split(' ', 4, StringSplitOptions.TrimEntries);
                    if (parts.Length == 4)
                    {
                        Console.WriteLine($"Client {parts[1]} is offering file '{parts[2]}' ({parts[3]} bytes).");
                        Console.Write("Accept (y/n)? ");

                        string response = Console.ReadLine() ?? "n";
                        if (!response.Equals("y", StringComparison.OrdinalIgnoreCase))
                        {
                            Console.WriteLine("File declined.");
                            SendData(Command.FileOfferResponse, Encoding.UTF8.GetBytes("n"));
                            return;
                        }

                        Console.WriteLine($"Accepted. Waiting for file '{parts[2]}'...");
                        SendData(Command.FileOfferResponse, Encoding.UTF8.GetBytes("y"));
                    }
                }
                break;
            }

            case Command.FileSize:
            {
                // <filename> <filesize>
                string fileInfo = Encoding.UTF8.GetString(payload);
                string[] parts = fileInfo.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length == 2 && long.TryParse(parts[1], out long fileSize))
                {
                    currentFileName = parts[0];
                    expectedFileSize = fileSize;
                    receivedFileSize = 0;
                    try
                    {
                        fileStream = new FileStream(currentFileName, FileMode.Create, FileAccess.Write);
                        Console.WriteLine($"Receiving file '{currentFileName}' ({expectedFileSize} bytes)...");
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Cannot open file for writing: {ex.Message}");
                    }
                }
                else Console.WriteLine("Invalid file size message.");
                break;
            }

            case Command.FileChunk:
            {
                if (fileStream != null)
                {
                    fileStream.Write(payload, 0, payload.Length);
                    receivedFileSize += payload.Length;
                    if (receivedFileSize >= expectedFileSize)
                    {
                        fileStream.Close();
                        fileStream = null;
                        Console.WriteLine($"File '{currentFileName}' received complete ({receivedFileSize} bytes).");
                    }
                }
                else
                {
                    Console.WriteLine("Received a file chunk with no active transfer.");
                }
                break;
            }

            default:
            {
                string text = Encoding.UTF8.GetString(payload);
                Console.WriteLine($"Received message: {text}");
                break;
            }
        }
    }
}