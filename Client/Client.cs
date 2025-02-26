using System.Runtime.InteropServices;

namespace Client;

class Client
{
    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void HandleOutcomingClientCommunication(string serverIp, int port, string command);

    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void HandleIncomingClientCommunication(string serverIp, int port);

    static string clientName = "Unknown";

    static void Main()
    {
        Console.Write("Enter your client name:\n> ");
        clientName = Console.ReadLine() ?? clientName;

        string serverIp = "127.0.0.1";
        int port = 12345;
        string command;

        Thread incomingThread = new(() => HandleIncomingClientCommunication(serverIp, port));
        incomingThread.Start();

        do
        {
            command = GetCommand();
            try
            {
                HandleOutcomingClientCommunication(serverIp, port, command);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
            }

        } while (!command.Contains('q'));
    }

    static string GetCommand()
    {
        while (true)
        {
            Console.Write("Enter command (j <roomID>, m <message>, sf <filename>, q):\n> ");
            string input = Console.ReadLine() ?? "null";
            string[] parts = input.Split(' ', 2, StringSplitOptions.TrimEntries);

            string inputCommand = parts[0].ToLower();
            if (parts.Length == 2 && (inputCommand == "j" || inputCommand == "sm" || inputCommand == "sf"))
            {
                return $"{inputCommand} {clientName} {parts[1]}";
            }
            else if (inputCommand == "q")
            {
                return $"{inputCommand} {clientName}";
            }
            else
            {
                Console.WriteLine("Invalid command. Please try again.");
            }
        }
    }
}