using System.Runtime.InteropServices;

namespace Client;

class Client
{
    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void HandleClientCommunication(string serverIp, int port, string command);

    static void Main()
    {
        string serverIp = "127.0.0.1";
        int port = 12345;
        string command;

        do
        {
            command = GetCommand();
            HandleClientCommunication(serverIp, port, command);

        } while (command != "QUIT");
    }

    static string GetCommand()
    {
        while (true)
        {
            Console.WriteLine("Enter command (GET <filename>, LIST, PUT <filename>, DELETE <filename>, INFO <filename>, QUIT):");
            string input = Console.ReadLine() ?? "null";
            string[] parts = input.Split(' ', 2, StringSplitOptions.TrimEntries);

            string inputCommand = parts[0].ToUpper();
            if (parts.Length == 1 && (inputCommand == "LIST" || inputCommand == "QUIT"))
                return $"{inputCommand}";

            else if (parts.Length == 2 && (inputCommand == "GET" || inputCommand == "PUT" || inputCommand == "DELETE" || inputCommand == "INFO"))
                return $"{inputCommand} {parts[1]}";

            else
                Console.WriteLine("Invalid command. Please try again.");
        }
    }
}