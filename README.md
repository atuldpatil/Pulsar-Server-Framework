# Pulsar Server Framework
Pulsar Server Framework is a framework to develop your heavy duty server in C++ where client-to-client(s) communication is needed.

## What is Pulsar Server Framework?

Pulsar Server Framework (PSF) is a framework to develop TCP based application server in C++ (UDP is not supported as of now). PSF encapsulates physical servers underneath, OS specific details and many other core technical stuff related to networking and provides simple API needed for client-to-client(s) communication.

## Features at glance:

### Scalable: 
PSF based server can be easily scaled up. When users base increases, becomes too large and existing server(s) starts exhausting out of resources, adding another server is as simple as just running another PSF based server application on another machine, that’s it! Let the next users then start connecting to new server and PSF will take care of the rest.
 
### Protocol flexibility for applications:
Application can define its own protocol (Protocol details are described later in this document)
 
### Capability of sending broadcasts:
Using PSF API to broadcast, server can send a communication buffer to as many users as needed, at a time.
 
### Capacity planning as per configured by application:
Your server application can configure communication buffer size, number of threads and other parameters

### Provides detailed server statistics:
As your server keeps running PSF keeps providing detailed server statistics ongoing periodically and consistently. This way you can monitor details of various server parameters.

### Easy logging:
PSF provides inbuilt mechanism to log information, warnings, errors and exceptions

### Easy and hardware independent API:
PSF based application can access client handle which is independent of server instance. Various APIs provided by PSF accepts/provides this handle to represent the client. Application doesn't need to bother which physical server the client is connected to.

## Protocol details:

The framework uses minimal basic protocol to transfer plain buffer of bytes (of given size) from/to a client or multiple clients at a time. Server application developed using this framework, can use this bytes buffer to transfer serialised form of their own define protocol. PSF's version based request processing also enables application to work with multiple protocols (or versions of same protocol) at a time. Essentially the framework takes care of buffering, basic validations and calling appropriate application defined request processor based on protocol version.

PSF uses a simple inbuilt MAI (Acronym of Message And Information) protocol:

   **First three bytes: Preamble:** Letters "MAI"
  
   **Next two bytes:** Protocol version
  
   **Next four bytes:** Size of actual request/response (Excluding these 9 bytes of header)
  
   **Remaining bytes:** Actual request/response

Your client/server application can form request/response using any format of structured data (XML, Protobuf, Json, Avro, Thrift, Messagepack etc) and then serialise it to send those bytes across network using MAI protocol.

## Scenarios where PSF can be useful:

Consider you have multiple servers, each having hundreds or thousands of users connected. There could be following scenario(s):

 1. A user connected to a particular server wants to communicate with
    user(s) connected to other servers
    
 2. A server wants to send communication to a user(s) connected to
    another server
    
 3. You have peer to peer communication app and want to start with
    single server and seamlessly add more server(s) as and when needed
    in future as your user base grows

If you are developing a server and likely to come across such scenarios, then Pulsar Server Framework might come handy for you. Pulsar framework library is highly tuned to develop easily scalable, high-performance heavy duty server. Especially, where peer to peer communication is needed.  For example gaming, messaging or for any other purpose.

## Example:

Let’s say thousands of users are connected to multiple servers and a user wants to send a message to multiple other users (broadcasting or multicasting). 

In such scenarios your server (developed using PSF) need not have to bother about which user is connected to which server. PSF will provide you a handle representing the user (regardless which server user is connected to). Your application can store the handle (for e.g. in database). Now in case of scenario given above, in your server all you need to do is just call a method provided by PSF to send the message to multiple users with handles (of recipients connected to different servers) as parameter and PSF will take care to route the message to appropriate server and finally deliver it to connected users.
 

## Specific example of messenger:

[![How PSF can be used][1]][1] 

As we can see here, all your users are connected to different servers. Now let's say User 3 wants to send a message to User 7.


1. On Server1, your application will receive (via framework) a request as sent by User 3. Based on your protocol it will decode the request.

2. After decoding the request, application finds that the request is a message to send to user 7 (connected to other server)

3. The application then will fetch handle to User 7 from DB (which your application on Server2 has already stored when User7 connected)

4. Application will then simply call `SendResponse` API of framework with the obtained handle and the message as parameters

5. Framework, after realising the response is for user connected to another server, will internally forward the response to intended server

6. On Server2, your application's framework component will receive the response and will send it straight to User 7

## Flow inside framework:

[![Event loop and request processing][4]][4]

Below is how we can describe flow of actions as they happen inside framework

1.	PSF receives request sent by connected client

2.	PSF creates request object and queues it in requests queue

3.	Request processing thread of PSF picks up the request object and calls your application’s request processor (derived from `RequestProcessor` class of PSF) to process the request object

4.	Your server application’s request processor processes the request and creates response object and calls PSF’s method to store the response

5.	Based on where the response receiving client is connected, PSF stores the response either:
a.	In response queue of locally connected client  (if response is intended for client connected to same server where application has processed the request) OR
b.	In response queue of peer server (if response is intended for client connected to peer server) 

6.	Framework sends responses from queues (of clients as well as of peers servers)

7.	In case of (b) in the point 5 above, at receiving end peer server (again developed using PSF) receives response and queues it to response queue of intended client (this is internally taken care by PSF and your application need not have to bother about it)

## Performance:
PSF has been tested tuned well for high performance. Data structures are used by keeping performance in mind. Locks have been used very efficiently. PSF supports inbuilt class `Profiler` which you can use to check performance 

## Class diagram of PSF:
[![Pulsar Server Framework class diagram][2]][2]
## Underlying Technology:

The Pulsar framework has been built around a tiny network library [LIBUV][3]  (which is also used by Google's Node.js servers). The core technology is asynchronous I/O based on event loops. That makes it really fast and reliable.

## API Documentation:

PSF provides methods of class `RequestProcessor` as API for your application. Documentation of these methods is very much in the header file itself (RequestProcessor.h) of the framework. Header file of every class has also its own documentation. Source has lots of relevant information through inline comments.

## Develop your server using PSF:

Using Pulsar Framework is quick and easy. Starting server is just as simple as shown below:

		// Instantiate ConnectionManager
		ConnectionsManager* pConnectionsManager = NULL;

		try
		{
			pConnectionsManager = new ConnectionsManager;
		}
		catch(std::bad_alloc&)
		{
			std::cout << "\nMemory exception in ConnectionsManager.";
		}

		// Start server
		int RetVal = pConnectionsManager->StartServer (IPAddress, IPv4Port, true);

		if (RetVal)
			std::cout << "\nERROR Starting server. Code " << RetVal << "(" << pConnectionsManager->GetErrorDescription(RetVal) << ")";

		delete pConnectionsManager;

		std::cout << "\nMAIN: Server shutdown completed. Press a key to exit...\n\n";


Developer has to derive couple of or three classes provided by framework to write their own server application. Framework API has been provided via public methods of these classes. For example to process requests from clients, derive your request processing class from `RequestProcessor` as under :

    class RequestProcessor_v1 : public RequestProcessor
    {
        /* Code here specific to your request processor class*/
    
    	public:
    		/* Constructor of derived processor
    		*/
    		RequestProcessor_v1(USHORT version);
    
    		/* Destructor of derived processor
    		*/
    		virtual ~RequestProcessor_v1(); 
    
    		/* Framework calls this when client disconnects
    		*/
    		virtual void ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData);
    
    		/* Framework calls this when client send server a request 
    		*/
    		virtual BOOL ProcessRequest ();
    };

Here we can see the framework abstracts actual server hardware/instances behind it and provides your application a unique `ClientHandle` representing client, regardless which physical machine/instance it is actually connected to. Thus, for example when your server application calls `SendResponse` method of `RequestProcessor` class with given `ClientHandle`, framework takes care of routing the message to appropriate actual physical machine/instance.

In other words, if your server hardware is running out of resources (because of increased user connections or any other reason), all you need to launch just another instance of your server application on another server and framework will take care of the rest.

## Sample Server and Client:

Along with the PSF code is provided source code of sample server developed using PSF and its client. This would be the best place for you to start with if you want to develop your server using PSF.

## Limitations:
1.	Currently the source code is written and built in Microsoft Visual Studio 2012. However, not much OS specific calls have been used and hopefully with little efforts you should be able to make it to build on Linux and other operating systems

2.	As mentioned before, in the current code only TCP protocol is supported. UDP is not yet supported. Therefore although the framework is very well tuned for performance, it still may not be useful for UDP based streaming purpose.


  [1]: https://i.stack.imgur.com/29xpb.jpg
  [2]: https://i.stack.imgur.com/tUcOr.jpg
  [3]: https://github.com/libuv/libuv
  [4]: https://i.imgur.com/XGbBvnI.jpg
