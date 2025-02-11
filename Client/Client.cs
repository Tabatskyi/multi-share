using System.Runtime.InteropServices;

namespace Client;

class Client
{
    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void HandleClientCommunication(string serverIp, int port, string command);

    static string clientName = "Unknown";

    static void Main()
    {
        Console.Write("Enter your client name:\n> ");
        clientName = Console.ReadLine() ?? clientName;

        string serverIp = "127.0.0.1";
        int port = 12345;
        string command;

        do
        {
            command = GetCommand();
            try
            {
                HandleClientCommunication(serverIp, port, command);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
            }

        } while (!command.Contains("QUIT"));
    }

    static string GetCommand()
    {
        while (true)
        {
            Console.Write("Enter command (GET <filename>, LIST, PUT <filename>, DELETE <filename>, INFO <filename>, QUIT):\n> ");
            string input = Console.ReadLine() ?? "null";
            string[] parts = input.Split(' ', 2, StringSplitOptions.TrimEntries);

            string inputCommand = parts[0].ToUpper();
            if (parts.Length == 1 && (inputCommand == "LIST" || inputCommand == "QUIT"))
                return $"{inputCommand} {clientName}";

            else if (parts.Length == 2 && (inputCommand == "GET" || inputCommand == "PUT" || inputCommand == "DELETE" || inputCommand == "INFO"))
                return $"{inputCommand} {clientName} {parts[1]}";

            else
                Console.WriteLine("Invalid command. Please try again.");
        }
    }
}