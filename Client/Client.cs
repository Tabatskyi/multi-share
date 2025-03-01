using System.Net;
using System.Net.Sockets;
using System.Text;

namespace Client;

internal record FileOffer(string Sender, string Filename, string FileSize);

internal class Client
{
    private static readonly string serverIp = "127.0.0.1";
    private static readonly int port = 12345;
    private static Socket? clientSocket;
    private static string clientName = "Unknown";
    private static bool isQuitting = false;

    static void Main()
    {
        if (!EstablishConnection(serverIp, port))
        {
            Console.WriteLine("Could not establish persistent connection to server.");
            return;
        }

        Console.Write("Enter your client name:\n> ");
        clientName = Console.ReadLine() ?? "Unknown";

        while (!isQuitting)
        {
            if (clientSocket is { Connected: true, Available: > 0 })
            {
                MultiplexReceive();
            }

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
        if (command.StartsWith('j'))
        {
            var parts = command.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 2)
            {
                string payloadString = $"{parts[1]} {clientName}";
                byte[] payload = Encoding.UTF8.GetBytes(payloadString);
                SendMultiplexed(0x01, payload);
            }
            return;
        }

        // m <message>
        if (command.StartsWith('m'))
        {
            string text = command[1..].Trim();
            byte[] payload = Encoding.UTF8.GetBytes(text);
            SendMultiplexed(0x02, payload);
            return;
        }

        // f <filename>
        if (command.StartsWith('f'))
        {
            var parts = command.Split(' ', 2, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 2 && File.Exists(parts[1]))
            {
                string filename = parts[1];
                long fileSize = new FileInfo(filename).Length;
                string payloadString = $"fo {clientName} {filename} {fileSize}";
                byte[] payload = Encoding.UTF8.GetBytes(payloadString);
                SendMultiplexed(0x03, payload);
            }
            else
            {
                Console.WriteLine("File path invalid.");
            }
            return;
        }

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

    private static bool SendMultiplexed(byte command, byte[] payload)
    {
        try
        {
            int payloadLenNetwork = IPAddress.HostToNetworkOrder(payload.Length);
            byte[] buffer = new byte[4 + 1 + payload.Length];

            Array.Copy(BitConverter.GetBytes(payloadLenNetwork), 0, buffer, 0, 4);
            buffer[4] = command;
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

    private static void MultiplexReceive()
    {
        try
        {
            while (clientSocket is { Connected: true } && !isQuitting)
            {
                if (clientSocket.Available < 5)
                    return; 

                byte[] lenBytes = new byte[4];
                int readLen = clientSocket.Receive(lenBytes, 0, 4, SocketFlags.None);
                if (readLen < 4)
                    return;

                int networkOrderSize = BitConverter.ToInt32(lenBytes, 0);
                int payloadSize = IPAddress.NetworkToHostOrder(networkOrderSize);

                byte[] cmdByte = new byte[1];
                int readCmd = clientSocket.Receive(cmdByte, 0, 1, SocketFlags.None);
                if (readCmd < 1)
                    return;

                byte command = cmdByte[0];

                byte[] payload = new byte[payloadSize];
                int totalReceived = 0;
                while (totalReceived < payloadSize)
                {
                    int chunk = clientSocket.Receive(payload, totalReceived, payloadSize - totalReceived, SocketFlags.None);
                    if (chunk <= 0)
                    {
                        Console.WriteLine("Server closed connection.");
                        isQuitting = true;
                        return;
                    }
                    totalReceived += chunk;
                }

                HandleIncomingMessage(command, payload);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error in MultiplexReceive: {ex.Message}");
            isQuitting = true;
        }
    }

    private static void HandleIncomingMessage(byte command, byte[] payload)
    {
        switch (command)
        {
            case 0x10: // successful join
            case 0x20: // server prompt/message
                Console.WriteLine(Encoding.UTF8.GetString(payload));
                break;

            case 0x03: // file offer
                {
                    string message = Encoding.UTF8.GetString(payload);
                    
                    if (message.StartsWith("fo ")) // fo <senderClient> <filename> <fileSize>
                    {
                        string[] parts = message.Split(' ', 4, StringSplitOptions.TrimEntries);
                        if (parts.Length == 4)
                        {
                            Console.WriteLine($"Client {parts[1]} is offering file '{parts[2]}' ({parts[3]} bytes).");
                            Console.Write("Accept (y/n)?\n> ");
                            string response = Console.ReadLine() ?? "n";

                            if (!response.Equals("y", StringComparison.OrdinalIgnoreCase))
                            {
                                Console.WriteLine("File declined.");
                                return;
                            }

                            Console.WriteLine($"Receiving file '{parts[2]}'...");
                            if (!ReceiveFile(parts[2]))
                                Console.WriteLine("Error receiving file data.");
                        }
                    }
                    break;
                }

            default:
                string text = Encoding.UTF8.GetString(payload);
                Console.WriteLine($"Received message: {text}");
                break;
        }
    }

    private static bool ReceiveFile(string filename)
    {
        try
        {
             byte[] lengthBuffer = new byte[sizeof(ulong)];
            int received = clientSocket!.Receive(lengthBuffer);
            if (received == 0)
            {
                Console.WriteLine("Connection closed.");
                return false;
            }
            if (received != sizeof(ulong))
            {
                Console.WriteLine("Incomplete file length prefix received.");
                return false;
            }

            ulong fileLength = BitConverter.ToUInt64(lengthBuffer, 0);
            byte[] fileBuffer = new byte[fileLength];

            int totalReceived = 0;
            while (totalReceived < (int)fileLength)
            {
                int bytesRead = clientSocket.Receive(fileBuffer, totalReceived, (int)fileLength - totalReceived, SocketFlags.None);
                if (bytesRead == 0)
                {
                    Console.WriteLine("Connection closed during file reception.");
                    return false;
                }
                totalReceived += bytesRead;
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