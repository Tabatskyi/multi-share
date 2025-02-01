using System.Runtime.InteropServices;

namespace Client;

class Program
{
    static void Main()
    {
        string serverIp = "127.0.0.1";
        int port = 12345;
        string command = "Hello";

        ClientBackInterop.HandleClientCommunication(serverIp, port, command);
    }
}

static class ClientBackInterop
{
    [DllImport("ClientBack.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    public static extern void HandleClientCommunication(string serverIp, int port, string command);
}
