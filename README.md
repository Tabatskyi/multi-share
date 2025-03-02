# PCSP Assignment 6 chat application

## 1. General System Description
In general, architecture is the same as in previous assignments except that the Client is now written completely on C#. 
It may be an impulsive decision, and I may sometimes regret doing so, but it’s too late for rollback. 
Client implementation involves one thread for taking user input and sending messages and another for taking incoming connections. 
Furthermore, I have changed the protocol a bit. 
When dealing with file offer confirmation std::promise and std::future come in handy because the get() method of std::future will block the thread until the std::promise sets the value. 
This is exactly how confirmation behaviour should work. 
## 2. Application Protocol Description
protocol now follows this scheme: <br>
4-bit length of payload <br>
1-bit command (e. g. 0x02 for messaging) <br>
n-bit payload (content varies depending on the command and who sends it) <br>
available user commands:<br>
j roomID - join to room <br>
m any text - message to room <br>
f filename - send file to room <br>
l - leave the room <br>
q - quit the application <br>
## 3. Screenshots of Different Use Cases
- Join Room
- Exit Room
- Client Force Close
- Send File
- Accept File
- Decline File
- Change Room

## 4. Explanation of Different Use Cases
![JoinRoomUML](resources/JoinRoom.drawio.svg)
![MessageUML](resources/Message.drawio.svg)

## 5. Appendices
https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/sockets/socket-services <br>
https://www.geeksforgeeks.org/socket-programming-in-c-sharp/ <br>
https://stackoverflow.com/questions/5840148/how-can-i-get-a-files-size-in-c <br>
https://stackoverflow.com/questions/11004273/what-is-stdpromise <br>
https://www.geeksforgeeks.org/std-promise-in-cpp/ <br>
https://youtu.be/t7CUti_7d7c?si=Zg2IScpG_yGiD1-Q <br>