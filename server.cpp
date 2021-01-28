//#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <map>
#include <process.h>
#include <sstream>
#include <atlrx.h>
#include <signal.h>
//#include <utility>
using namespace std;
#include "zoop.h"

stringstream ss_recv;

HANDLE socks_lock;
HANDLE ss_lock;
HANDLE output_lock;

void monitor_net(void*);
int send_command(const cmd_struct&);
void handle_client(void*);
int parse_command(string&, cmd_struct&);
void display_help();
bool regex_match(string cmd_str, string regex, stringstream *ss, int ss_size);

map<index_t, SOCKET> client_socks;
map<index_t, SOCKET>::iterator cs_itr;
bool diewhendone = false;
bool mainrun;
int sendall_delay = 0;

int main(int argc, char *argv[])
{
	string command;
	int status;
	cmd_struct cmd;
	SOCKET s;
	char input[MAXBYTES];
	int ret_val, i;

   //Data structures for initializing Winsock.
   WSADATA wsData;
   WORD    wVer = MAKEWORD(2,2);
	
	//(security attributes, initialy owned?, name)
	socks_lock = CreateMutex( NULL, FALSE, NULL); 
	ss_lock = CreateMutex( NULL, FALSE, NULL); 
	output_lock = CreateMutex( NULL, FALSE, NULL); 

   //Begin: Init Winsock2
   status = WSAStartup(wVer,&wsData);
   if (status != NO_ERROR)
     return -1;
	
   //Create the socket.
   s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
   if (s == INVALID_SOCKET) {
     cout	<< "Failed(here) to create socket: " 
         	<< errstr << endl;
	  exit(1);
   }

	//listen for new clients in a seperate thread
	_beginthread(&monitor_net, 0, NULL);

	mainrun = true;
	while(mainrun)
	{
		cout << "> ";
		memset(input, 0, MAXBYTES);
		//cin.ignore(UINT_MAX);
		cin.sync();
		cin.getline(input, MAXBYTES);
		if(cin.fail())
		{
			cout << "Command too long!" << endl; 
			cin.clear();
			cin.ignore(UINT_MAX);
			continue;
		}
		//cout << input  << endl;
		command = input;
		if(cmp_nocase(command, "quit") == 0)
			mainrun = false;
		else
		{
			ret_val = parse_command(command, cmd);
			if(ret_val < 0)
				cout << "Bad command" << endl;
			else if (ret_val == 0)
				send_command(cmd);
		}	
	}

   WSACleanup();
}

void monitor_net(void* args)
{
   //Data structures for setting up communications.
	SOCKET s, newsock;
   SOCKADDR    sa;
   struct hostent    *he;
	char data[MAXBYTES];
	memset(data, 0, MAXBYTES);

   struct sockaddr_in sa_in;

   int            ilen;
   ULONG        icmd;
   ULONG        ulLen;

   //Miscellaneous variables
   int status;
	bool run;

   //Create the socket.
   s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
   if (s == INVALID_SOCKET) {
     cout	<< "Failed(thread) to create socket: " 
         	<< errstr << endl;
     return;
   }

   //Disable non-blocking IO
   icmd = 0;   
   status = ioctlsocket(s,FIONBIO,&icmd);

   //Bind the socket
   memset(&sa_in,0,sizeof(sa_in));
   sa_in.sin_family=AF_INET;
	sa_in.sin_port=htons(SERVPORT);
	sa_in.sin_addr.s_addr=htonl(INADDR_ANY);
   status = bind(s,(SOCKADDR *)&sa_in,sizeof(sa_in));

   //Note that we need to convert the port to the local 
   //host byte order.
   ilen = sizeof(sa_in);
   status = getsockname(s,(struct sockaddr *)&sa_in,&ilen);
   if (status == NO_ERROR) {
     ss_recv << "Server: Bound to port "
		  		 << ntohs(sa_in.sin_port) << endl;
   }

   //Listen for connections. SOMAXCONN tells the provider to queue
   //a "reasonable" number of connections.
   status = listen(s,SOMAXCONN);
   if (status != NO_ERROR) {
     ss_recv << "Failed to set socket listening: "
          	 << errstr << endl;
     return;
   }

	run = true;
	while(run)
	{
			
		//Block waiting for a connection. 
		ilen = sizeof(sa_in);
		newsock = accept(s,(struct sockaddr*)&sa_in,&ilen);
		if (newsock == INVALID_SOCKET) 
		{
			if(WSAGetLastError() == WSAEINTR)
			{
				//cout << "Error: WSAEINTR" << endl;
				return;
			}
			cout << "Failed(accept) to create socket: " 
					<< errstr << endl;
			cout << "Listener is dying..." << endl;
			run = false;
			continue;
		}
		cout << "Connection from " << inet_ntoa(sa_in.sin_addr)
				<< ", Port " << ntohs(sa_in.sin_port) << endl;

		//insert the new socket into client_socks indexed by <ip,port>
		WaitForSingleObject(socks_lock, INFINITE);
		client_socks[make_pair(sa_in.sin_addr.s_addr, sa_in.sin_port)] = newsock;
		ReleaseMutex(socks_lock);
		
		cout << "Server: client connecting.  Creating thread." << endl;
		_beginthread(&handle_client, 0, &newsock);
	}

   return;
}

void handle_client(void *args)
{
	SOCKET s = *((SOCKET*)args);
	char data[MAXBYTES];
	
	int status = 1;
	int total_bytes = 0;
	int ilen;
	struct sockaddr_in sa;
	ilen = sizeof(sa);
	string address;
	int port;
	fd_set recvset;
	struct timeval timeout;
	bool wait_for_ping = false;
	cmd_struct cmd;
	
   memset(&sa,0,sizeof(sa));
	memset(&data,0,MAXBYTES);
	
	//find out the ip and port of the client we are handling
	getpeername(s, (struct sockaddr*) &sa, &ilen);
	address = inet_ntoa(sa.sin_addr);
	port = ntohs(sa.sin_port);

	while((status > 0) && (status != SOCKET_ERROR))
	{
		status = recv(s, data, MAXBYTES, 0);
		if(status < 0)
		{
			//the user has typed 'quit'
			if(WSAGetLastError() == WSAEINTR)
				return;
			
			cout	<< "recv failed: " << errstr << endl;
		}
		else if(status > 0)
		{
			data[status] = '\0';
			cout << "<" << address << ", " << port << ">\n" << data << endl;
		}
	}
	cout << "Connection closed" << endl;
	//remove the socket from client_socks
	WaitForSingleObject(socks_lock, INFINITE);
	cs_itr = client_socks.find(make_pair(sa.sin_addr.s_addr, sa.sin_port));
	if(cs_itr != client_socks.end())
		client_socks.erase(cs_itr);
	else
		cout	<< "Couldnt find " << "<" 
				<< address << ", " << port << ">" << endl;

	closesocket(s);

	// are we supposed to die when all clients are disconnected?
	if (client_socks.empty() && diewhendone)
	{
		ReleaseMutex(socks_lock);
		mainrun = false;
		raise(SIGINT);
	}

	ReleaseMutex(socks_lock);
}
	
int parse_command(string &cmd_str, cmd_struct &cmd)
{

	if(cmd_str == "")
		return 1;
	else if(cmp_nocase(cmd_str, "?") == 0)
	{
		int count = 0;
		struct in_addr ia;
		//lets print out all the connected clients
		cout << "---===Connected Clients===---" << endl;
		WaitForSingleObject(socks_lock, INFINITE);
		if(client_socks.empty())
		{
			cout << "None" << endl;
			ReleaseMutex(socks_lock);
			return 1;
		}
		cs_itr = client_socks.begin();
		while(cs_itr != client_socks.end())
		{
			ia.s_addr = cs_itr->first.first;
			cout	<< count++ << ")\t"
					<< "IP " << inet_ntoa(ia)
					<< "\t\t Port " << ntohs(cs_itr->first.second) << endl;
			++cs_itr;
		}
		ReleaseMutex(socks_lock);
		return 1;
	}
	else if(cmp_nocase(cmd_str, "help") == 0)
	{
		display_help();
		return 1;
	}
	else if(cmp_nocase(cmd_str, "diewhendone") == 0)
	{
		WaitForSingleObject(socks_lock, INFINITE);
		if (client_socks.empty())
			cout << "Error: There are no connected clients." << endl;
		else
			diewhendone = true;
		ReleaseMutex(socks_lock);
		return 1;
	}
	else
	{
		stringstream ss[4];
		ptrdiff_t len;
		REParseError status;
		CAtlRegExp<> re;
		CAtlREMatchContext<> mc;
		const CAtlREMatchContext<>::RECHAR *start=0, *end=0;
		
		//regular expression
		//command at prompt should be of the form:
		//setalldelay milliseconds
		string regex("{setalldelay}\\b+{\\d+}\\b*$");
		if (regex_match(cmd_str, regex, ss, 4))
		{
			//fill the cmd struct
			ss[1] >> sendall_delay; 

			return 1;
		}
		
		//regular expression
		//command at prompt should be of the form:
		//command ip_address port
		regex = "{\\w}\\b+{all}\\b*$";
		if (regex_match(cmd_str, regex, ss, 4))
		{
			//fill the cmd struct
			cmd.command = ss[0].str();
			cmd.parameters = "()";
			cmd.address = ss[1].str();
			cmd.port = -1;

			return 0;
		}
		
		//regular expression
		//command at prompt should be of the form:
		//command ip_address port
		regex = "{\\w}\\b+{\\z\\.\\z\\.\\z\\.\\z}\\b+{\\z}\\b*$";
		if (regex_match(cmd_str, regex, ss, 4))
		{
			//fill the cmd struct
			cmd.command = ss[0].str();
			cmd.parameters = "()";
			cmd.address = ss[1].str();
			ss[2] >> cmd.port;

			return 0;
		}
		
		//regular expression
		//command at prompt should be of the form:
		//command client_number
		regex = "{\\w}\\b+{\\z}\\b*$";
		if (regex_match(cmd_str, regex, ss, 4))
		{
			//fill the cmd struct
			cmd.command = ss[0].str();
			cmd.parameters = "()";
			cmd.address = "ordinal";
			ss[1] >> cmd.port; // is the ordinal number

			return 0;
		}
		
		//regular expression
		//command at prompt should be of the form:
		//command (parameters) all
		//status = re.Parse("{\\w}\\b+{\\q}\\b+{all}\\b*");
		regex = "{\\w}\\b*{\\(.*\\)}\\b+{all}\\b*$";
		if (regex_match(cmd_str, regex, ss, 4))
		{
			//fill the cmd struct
			cmd.command = ss[0].str();
			cmd.parameters = ss[1].str();
			cmd.address = ss[2].str();
			cmd.port = -1;

			return 0;
		}
	
		//regular expression2
		//command at prompt should be of the form:
		//command (parameters) ip_address port
		//see the above comments
		regex = "{\\w}\\b*{\\(.*\\)}\\b+{\\z\\.\\z\\.\\z\\.\\z}\\b+{\\z}\\b*$";
		if (regex_match(cmd_str, regex, ss, 4))
		{
			cmd.command = ss[0].str();
			cmd.parameters = ss[1].str();
			cmd.address = ss[2].str();
			ss[3] >> cmd.port;
			return 0;
		}


		//regular expression
		//command at prompt should be of the form:
		//command (parameters) client_number
		regex = "{\\w}\\b*{\\(.*\\)}\\b+{\\z}\\b*$";
		if (regex_match(cmd_str, regex, ss, 4))
		{
			//fill the cmd struct
			cmd.command = ss[0].str();
			cmd.parameters = ss[1].str();
			cmd.address = "ordinal";
			ss[2] >> cmd.port; // is the ordinal number

			return 0;
		}

		// No Match.  Bad Command.
		return -1;
	}

	// should never get here
	return 0;
}

int send_command(const cmd_struct &cmd)
{
	SOCKET s;
	int ret_val;
	u_long addr;
	string cmd_param;
	struct in_addr ia;
	
	cmd_param = cmd.command + ' ' + cmd.parameters;

	if(cmp_nocase(cmd.address, "all") == 0)
	{
		WaitForSingleObject(socks_lock, INFINITE);
		cs_itr = client_socks.begin();
		while(cs_itr != client_socks.end())
		{
			ia.s_addr = cs_itr->first.first;
			cout << "IP " << inet_ntoa(ia)
					<< "\t\t Port " << ntohs(cs_itr->first.second) 
					<< ":\t " << cmd.command << endl;

			s = cs_itr->second;

			ret_val = send(s, cmd_param.c_str(), cmd_param.length(), 0);
			if(ret_val < 0)
				cout << "send error: " << errstr << endl;
			else
				cout << ret_val << " bytes sent" << endl;

			++cs_itr;

			if(sendall_delay > 0)
				Sleep(sendall_delay);
		}
		ReleaseMutex(socks_lock);
		return 0;
	}
	else if(cmp_nocase(cmd.address, "ordinal") == 0)
	{
		bool found = true;
		WaitForSingleObject(socks_lock, INFINITE);
		cs_itr = client_socks.begin();
		for(int i=0; i<cmd.port; ++i)
		{
			if (++cs_itr == client_socks.end())
			{
				found = false;
				break;
			}
		}
			
		if(found)
		{
			ia.s_addr = cs_itr->first.first;
			cout << "IP " << inet_ntoa(ia)
					<< "\t\t Port " << ntohs(cs_itr->first.second) 
					<< ":\t " << cmd.command << endl;

			s = cs_itr->second;

			ret_val = send(s, cmd_param.c_str(), cmd_param.length(), 0);
			if(ret_val < 0)
				cout << "send error: " << errstr << endl;
			else
				cout << ret_val << " bytes sent" << endl;
		}
		else
		{
			cout << "No connected client number " << cmd.port << endl;
		}
		ReleaseMutex(socks_lock);
		return 0;
	}
	else
	{
		WaitForSingleObject(socks_lock, INFINITE);
		addr = inet_addr(cmd.address.c_str());
		cs_itr = client_socks.find( make_pair(addr, htons(cmd.port)) );
		if(cs_itr == client_socks.end())
		{
			ReleaseMutex(socks_lock);
			cout << "No connected client found at IP " 
				  << cmd.address << ", Port " << cmd.port << endl;
			return SOCKET_ERROR;
		}
		s = cs_itr->second;
		ReleaseMutex(socks_lock);
		
		ret_val = send(s, cmd_param.c_str(), cmd_param.length(), 0);
		if(ret_val < 0)
			cout << "send error: " << errstr << endl;
		else
			cout << ret_val << " bytes sent" << endl;
		return ret_val;
	}
}

// check a regex and return the match groups in the passed stringstream
// Returns false if no match.  true otherwise.
bool regex_match(string cmd_str, string regex, stringstream *ss, int ss_size)
{
	ptrdiff_t len;
	REParseError status;
	CAtlRegExp<> re;
	CAtlREMatchContext<> mc;
	const CAtlREMatchContext<>::RECHAR *start=0, *end=0;
	
	status = re.Parse(regex.c_str());
	if(status != REPARSE_ERROR_OK)
		cout << "parse error" << endl;
	if(re.Match(cmd_str.c_str(), &mc)) //check to see if input matches
	{
		//look at all the 'match groups'
		for(int j=0; j<mc.m_uNumGroups; ++j)
		{
			//sanity check
			if (ss_size <= j) return false;
			
			//a match group is denoted by the regex between '{}'
			//in the re.Parse() call above.  they are numbered
			//starting with 0
			mc.GetMatch(j, &start, &end);
			len = end - start;
			//get the matched characters from this group
			//one at a time into a string stream
			for(int i=0; i<len; ++i)
				ss[j] << *(start+i);
		}
		return true;
	}
	
	return false;
}

void display_help()
{
	       //111111111111111111111111111111111111111111111111111111111111111111111111111111
 cout << "\n---===Client Commands===---\n"
		<< "insert (N command)  :Insert a new command into the client's batch loop at N\n"
		<< "remove (N)          :Remove the batch command at N\n"
		<< "newcmd              :Insert a new command into the batch loop at the end\n"
		<< "forkclient          :Run a new client instance\n"
		<< "pause               :Pause the client\n"
		<< "stop                :Stop the client's batch loop\n"
		<< "start               :Restart the client's batch loop\n"
		<< "report              :Show the current state of the client\n"
		<< "setdelay (N)        :Set the delay time between batch steps. (N milliseconds)\n"
		<< "setiterations (N)   :The client stops after N runs through the batch loop.\n"
		<< "diewhendone         :Used with setiterations.  Client quits when finished.\n"
		<< "quit                :Terminate the client program.\n"
		<< "\n"
		<< "Each client command is followed by either a dotted decimal IP address\n"
		<< "and a port number OR 'all'.\n"
		<< "\n"
		<< "Example 1:\n"
		<< "insert (2 echo hello) 127.0.0.1 2071\n"
		<< "\n"
		<< "This command inserts 'echo hello' as a command in the\n"
		<< "batch loop at position 2 on the client at 127.0.0.1 port 2071.\n"
		<< "\n"
		<< "Example 2:\n"
		<< "newcmd (doit.bat) all\n"
		<< "\n"
		<< "This command inserts 'doit.bat' as a command at the end of the\n"
		<< "batch loop on each client.\n"
		<< "\n"
		<< "---===Server Commands===---\n"
		<< "?                   :List all connected clients by IP and port.\n"
		<< "help                :Display this information page.\n"
		<< "diewhendone         :Close the server after all clients disconnect.\n"
		<< "setalldelay N       :Set the delay between sends when using 'all'.\n"
		<< "quit                :Close the server and all connected clients.\n"
		<< endl;
	
	return;
}

/*void display_recv_stream()
{
	WaitForSingleObject(ss_lock, INFINITE);
	cout << ss.str() << endl;
	ReleaseMutex(ss_lock);
}*/

