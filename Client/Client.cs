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
            if (clientSocket is { Available: > 0 })
            {
                ReceiveData();
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
        if (command.StartsWith("q", StringComparison.OrdinalIgnoreCase))
        {
            isQuitting = true;
            return;
        }

        if (command.StartsWith('j') || command.StartsWith('m') || command.StartsWith('f'))
        {
            // add client name to command (j <roomID>, m <message>, f <filename>)
            string[] parts = command.Split(' ', 2, StringSplitOptions.TrimEntries);
            if (parts.Length == 2)
            {
                command = $"{parts[0]} {clientName} {parts[1]}";
            }
        }

        if (!SendString(command))
        {
            Console.WriteLine("Error sending command.");
        }

        if (command.StartsWith("f "))
        {
            // "f <clientName> <filename>"
            string[] parts = command.Split(' ', 3, StringSplitOptions.TrimEntries);
            if (parts.Length == 3)
            {
                if (!SendFile(parts[2]))
                    Console.WriteLine("Error sending file data.");
            }
        }
    }

    private static void ReceiveData()
    {
        try
        {
            byte[] lengthBuffer = new byte[sizeof(ulong)];
            int received = clientSocket!.Receive(lengthBuffer);
            if (received == 0)
            {
                Console.WriteLine("Connection closed.");
                isQuitting = true;
                return;
            }

            if (received != sizeof(ulong))
            {
                Console.WriteLine("Incomplete length prefix received.");
                return;
            }

            ulong msgLength = BitConverter.ToUInt64(lengthBuffer, 0);
            byte[] buffer = new byte[msgLength];

            int totalReceived = 0;
            while (totalReceived < (int)msgLength)
            {
                int bytesRead = clientSocket!.Receive(buffer, totalReceived, (int)msgLength - totalReceived, SocketFlags.None);
                if (bytesRead == 0)
                {
                    Console.WriteLine("Connection closed during receive.");
                    isQuitting = true;
                    return;
                }
                totalReceived += bytesRead;
            }

            string message = Encoding.UTF8.GetString(buffer);
            if (message.StartsWith("fo "))
            {
                // fo <senderClient> <filename> <fileSize>
                string[] parts = message.Split(' ', 4, StringSplitOptions.TrimEntries);
                if (parts.Length == 4)
                {
                    Console.WriteLine($"Client {parts[1]} is offering file '{parts[2]}' ({parts[3]} bytes).");
                    Console.Write("Accept (y/n)?\n> ");
                    string response = Console.ReadLine() ?? "n";

                    SendString(response);
                    if (response.Equals("y", StringComparison.OrdinalIgnoreCase))
                    {
                        Console.WriteLine($"Receiving file '{parts[2]}'...");
                        if (!ReceiveFile(parts[2]))
                            Console.WriteLine("Error receiving file data.");
                    }
                    else
                    {
                        Console.WriteLine("File transfer declined.");
                    }
                }
            }
            else
            {
                Console.WriteLine($"Received message: {message}");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error receiving data: {ex.Message}");
            isQuitting = true;
        }
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

    private static bool SendString(string command)
    {
        try
        {
            byte[] data = Encoding.UTF8.GetBytes(command);
            byte[] lengthPrefix = BitConverter.GetBytes((ulong)data.Length);
            clientSocket!.Send(lengthPrefix);
            clientSocket.Send(data);
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error sending data: {ex.Message}");
            return false;
        }
    }

    private static bool SendFile(string filename)
    {
        try
        {
            if (!File.Exists(filename))
            {
                Console.WriteLine("File not found.");
                return false;
            }

            byte[] fileBytes = File.ReadAllBytes(filename);
            byte[] lengthPrefix = BitConverter.GetBytes((ulong)fileBytes.Length);

            clientSocket!.Send(lengthPrefix);

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
                Console.WriteLine("Incomplete length prefix received for file.");
                return false;
            }

            ulong fileLength = BitConverter.ToUInt64(lengthBuffer, 0);
            byte[] fileBuffer = new byte[fileLength];

            int totalReceived = 0;
            while (totalReceived < (int)fileLength)
            {
                int bytesRead = clientSocket!.Receive(fileBuffer, totalReceived, (int)fileLength - totalReceived, SocketFlags.None);
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