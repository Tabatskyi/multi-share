using System.Runtime.InteropServices;

namespace Client;

internal class Client
{
    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern bool EstablishConnection(string serverIp, int port);

    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern bool SendCommand(string command);

    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern bool SendFile(string filename);

    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.BStr)]
    public static extern string GetConsoleInput([MarshalAs(UnmanagedType.BStr)] string prompt);

    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl)]
    public static extern void ReceiveRoutine();

    private static readonly string serverIp = "127.0.0.1";
    private static readonly int port = 12345;

    static string clientName = "Unknown";

    static void Main()
    {
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
}