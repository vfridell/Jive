//#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
//#include <map>
#include <list>
#include <process.h>
#include <atlrx.h>
using namespace std;
#include "zoop.h"

//commands for this program
int cmd_help(const string parameters);
int cmd_reboot(const string parameters);
int cmd_newcmd(const string parameters);
int cmd_insert(const string parameters);
int cmd_remove(const string parameters);
int cmd_forkclient(const string parameters);
int cmd_pause(const string parameters);
int cmd_quit(const string parameters);
int cmd_stop(const string parameters);
int cmd_start(const string parameters);
int cmd_terminate(const string parameters);
int cmd_report(const string parameters);
int cmd_realtime(const string parameters);
int cmd_setdelay(const string parameters);
int cmd_setiterations(const string parameters);
int cmd_diewhendone(const string parameters);
void verify_unpaused(); 

//interpret server cmds
int parse_cmd(string);

//batch thread proto
void batch(void*);

//to control the execution of the batch thread
HANDLE hthread;
HANDLE batch_lock;
HANDLE pause_lock;
HANDLE delay_lock;
bool run_thread = true;
bool paused = false;
bool realtime_report = false;
bool diewhendone = false;
int delay_time = 1000;
int num_iterations = -1;
int total_iterations = -1;

//the list of commands this client should execute
list<string> cmd_list;

//for control of the main loop
bool run = true;

//for talking to the server
string server_address("");
SOCKET      s;
	
int main(int argc, char *argv[])
{
	//Data structures for initializing Winsock.
   WSADATA wsData;
   WORD    wVer = MAKEWORD(2,2);

   //Data structures for setting up communications.
   SOCKADDR    sa;
   struct hostent    *he;
	string cmd_str;
	char data[MAXBYTES];
	memset(data, 0, MAXBYTES);

   struct sockaddr_in servaddr;

   int          ilen;
   ULONG        icmd;
   ULONG        ulLen;

	//for select
	fd_set fdset;
	struct timeval timeout;

   //Miscellaneous variables
	int ret_val;
   int status;
	int i;
	
	//check the command line
	if(argc != 2)
	{
		cout << "usage:  client <ipaddress>" << endl;
		return -1;
	}
	
	//read in the initial set of commands
	ifstream fin("commands.dat");
	if(!fin)
		cout << "Error: could not find commands.dat file." << endl;
	else
	{
		while(fin)
		{
			fin.getline(data, MAXBYTES);
			cmd_list.push_back(data);
		} 
		fin.close();
	}
	
	//CreateMutex(security attributes, initialy owned?, name)
	//do NOT name them because:
	//'If lpName matches the name of
	//an existing named mutex object, this function requests
	//MUTEX_ALL_ACCESS access to the existing object.'
	//Not what we want!
	batch_lock = CreateMutex( NULL, FALSE, NULL); 
	pause_lock = CreateMutex( NULL, FALSE, NULL); 
	delay_lock = CreateMutex( NULL, FALSE, NULL); 

   //Begin: Init Winsock2
   status = WSAStartup(wVer,&wsData);
   if (status != NO_ERROR)
     return -1;

   //Create the socket.
   s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
   if (s == INVALID_SOCKET) {
     cout	<< "Failed to create socket: " 
         	<< errstr << endl;
     WSACleanup();
     return -1;
   }
	

   //Bind the socket locally
   memset(&servaddr,0,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(0);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
   status = bind(s,(SOCKADDR *)&servaddr,sizeof(servaddr));
	
   //Disable non-blocking IO for purposes of this example.
   /*icmd = 0;   
   status = ioctlsocket(s,FIONBIO,&icmd);
*/
	
   memset(&servaddr,0,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERVPORT);
	servaddr.sin_addr.s_addr = inet_addr(argv[1]);
	
	ret_val = connect(s, (SOCKADDR *) &servaddr, sizeof(sockaddr));
	if(ret_val == SOCKET_ERROR)
	{
		cout << "connect error: " << errstr << endl;
		WSACleanup();
		return -1;
	}

	server_address = argv[1];


	//begin the batch loop
	hthread = (HANDLE)_beginthread(&batch, 0, NULL);
	
	while(run)
	{
		//erase the buffer
		memset(data, 0, MAXBYTES);

		ret_val = recv(s, data, MAXBYTES, 0);
		if(ret_val == SOCKET_ERROR)
		{
			cout << "recv error: " << errstr << endl;
			run = false;
		}	
		else
		{
			cout << data << endl;
			parse_cmd(data);
		}
		
		//	Sleep(1000);
	}


	shutdown(s, SD_SEND);

	ret_val = 1;
	while((ret_val > 0) && (ret_val != SOCKET_ERROR))
		ret_val = recv(s, data, MAXBYTES, 0);
	
	closesocket(s);
	WSACleanup();
	return 0;
}

void batch(void *args)
{
	int ret_val;
	char env_cstring[26];
	//map<unsigned int, string>::iterator cmd_itr;
	list<string>::iterator cmd_itr;
	
	while(1)
	{
		WaitForSingleObject(batch_lock, INFINITE);
		cmd_itr = cmd_list.begin();
		//make sure the list is not empty
		if(cmd_itr == cmd_list.end())
		{
			ReleaseMutex(batch_lock);
			cout << "No batch cmds to execute.  Batch thread is dying..." << endl;
			run_thread = false;
			//break;
		}

		// export the num_iterations variable to the environment
		sprintf(env_cstring, "NUM_ITERATIONS=%d", 
				  (total_iterations+1) - num_iterations);
		_putenv(env_cstring);
		
		ReleaseMutex(batch_lock);

		while(1)
		{
			//a lock for pausing the batch
			WaitForSingleObject(pause_lock, INFINITE); //paused
			ReleaseMutex(pause_lock); //unpaused
			
			//IPC.  Have we been told to die?
			WaitForSingleObject(batch_lock, INFINITE);
			if(run_thread == false)
			{
				ReleaseMutex(batch_lock);
				cout << "thread dying..." << endl;
				_endthread();
			}
			
			//have we done all the commands in the list?
			if(cmd_itr == cmd_list.end())
			{
				ReleaseMutex(batch_lock);
				break;
			}
			
			//execute the next command
			_flushall();  //must call this first
			system(cmd_itr->c_str());

			/*ret_val = send(s, data, strlen(data), 0);
			if(ret_val < 0)
			{
				cout << "send error: " << errstr << endl;
				i = 0;
			}	
			else
				cout << i << '\t' << ret_val << " bytes sent" << endl;
			*/
			++cmd_itr;
			ReleaseMutex(batch_lock);

			//delay for a given amount of time between steps
			WaitForSingleObject(delay_lock, INFINITE);
			//cout << "Delay..." << endl;
			Sleep(delay_time);
			ReleaseMutex(delay_lock);
		}

		//IPC.  Have we been told to die?
		WaitForSingleObject(batch_lock, INFINITE);

		//Are we only doing the batch loop a certain number of times?
		if(num_iterations > 0)
		{
			cout << num_iterations << endl;
			--num_iterations;
		}

		if(num_iterations == 0)
		{
			cout << "Iterations done." << endl;
			run_thread = false;
			if (diewhendone) exit(0);
		}

		// have we been told to die?
		if(run_thread == false)
		{
			ReleaseMutex(batch_lock);
			_endthread();
		}
		ReleaseMutex(batch_lock);
	}

	run_thread = false;
	_endthread();
}

int parse_cmd(string raw_cmd)
{
	string cmd, parameters, tempstr;
	stringstream ss[3];
	ptrdiff_t len;
	REParseError status;
	CAtlRegExp<> re;
	CAtlREMatchContext<> mc;
	const CAtlREMatchContext<>::RECHAR *start=0, *end=0;
		
	//regular expression
	//command at prompt should be of the form:
	//command "optional parameter"
	//the quotes MUST be there!
	status = re.Parse("{\\w}\\b+{\\(.*\\)}\\b*");
	if(status != REPARSE_ERROR_OK)
		cout << "parse error" << endl;
	if(!re.Match(raw_cmd.c_str(), &mc)) //check to see if input matches
	{
		cout << "Syntax error" << endl;
		return -1;
	}
	
	//look at all the 'match groups'
	for(int j=0; j<mc.m_uNumGroups; ++j)
	{
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

	//mow we can assign the command and parameters
/*	cout << ':' << ss[0].str() << ":\n:" 
		  << ss[1].str() << ":\n:"
		  << ss[2].str() << ':' << endl;*/
	//cout << " fixing parameters..." << endl;
	cmd = ss[0].str();
	parameters = ss[1].str();

	//remove the ""
	parameters = parameters.substr(1, parameters.length()-2);
	
	if(cmp_nocase(cmd, "help") == 0)
	{
		return cmd_help(parameters);
	}
	else if(cmp_nocase(cmd, "reboot") == 0)
	{
		return cmd_reboot(parameters);
	}
	else if(cmp_nocase(cmd, "newcmd") == 0)
	{
		return cmd_newcmd(parameters);
	}
	else if(cmp_nocase(cmd, "insert") == 0)
	{
		return cmd_insert(parameters);
	}
	else if(cmp_nocase(cmd, "remove") == 0)
	{
		return cmd_remove(parameters);
	}
	else if(cmp_nocase(cmd, "forkclient") == 0)
	{
		return cmd_forkclient(parameters);
	}
	else if(cmp_nocase(cmd, "pause") == 0)
	{
		return cmd_pause(parameters);
	}
	else if(cmp_nocase(cmd, "quit") == 0)
	{
		return cmd_quit(parameters);
	}
	else if(cmp_nocase(cmd, "stop") == 0)
	{
		return cmd_stop(parameters);
	}
	else if(cmp_nocase(cmd, "start") == 0)
	{
		return cmd_start(parameters);
	}
	else if(cmp_nocase(cmd, "terminate") == 0)
	{
		return cmd_terminate(parameters);
	}
	else if(cmp_nocase(cmd, "report") == 0)
	{
		return cmd_report(parameters);
	}
	else if(cmp_nocase(cmd, "realtime") == 0)
	{
		return cmd_realtime(parameters);
	}
	else if(cmp_nocase(cmd, "setdelay") == 0)
	{
		return cmd_setdelay(parameters);
	}
	else if(cmp_nocase(cmd, "setiterations") == 0)
	{
		return cmd_setiterations(parameters);
	}
	else if(cmp_nocase(cmd, "diewhendone") == 0)
	{
		return cmd_diewhendone(parameters);
	}
	else
	{
		int ret_val;
		string send_str;
		send_str = "Unrecognized command: " + cmd + '\n';
		cout << send_str << endl;
		ret_val = send(s, send_str.c_str(), send_str.length(), 0);
		if(ret_val < 0)
			cout << "send error: " << errstr << endl;
		else
			cout << ret_val << " bytes sent" << endl;

		return -1;
	}
}

int cmd_help(const string parameters)
{
	cout << "cmd_help: " << parameters << endl;
	
	return 0;
}

int cmd_reboot(const string parameters)
{
	cout << "cmd_reboot: " << parameters << endl;
	/*HANDLE hatoken;
	HANDLE hdup;
	HANDLE hmythread = GetCurrentThread();
	HANDLE hmyproc = GetCurrentProcess();
	if(!DuplicateHandle(hmyproc, hmythread, hmyproc, &hdup,
						 0, true, DUPLICATE_SAME_ACCESS))
	{
		cout << "DuplicateHandle failed: ";
		display_syserror();
		return 0;
	}
	if(OpenThreadToken(hmythread, TOKEN_ALL_ACCESS, TRUE, &hatoken))
	{
		if(SetPrivilege(hatoken, SE_SHUTDOWN_NAME, true))
		{
			ExitWindowsEx(EWX_REBOOT, 0);
			exit(0);
		}
	}
	else
	{
		cout << "OpenThreadToken failed: ";
		display_syserror();
	}*/
	
	return 0;
}

int cmd_newcmd(const string parameters)
{
	cout << "cmd_newcmd: " << parameters << endl;
	if(parameters == "")
		return -1;
	
	WaitForSingleObject(batch_lock, INFINITE);
	
	//list<string>::iterator cmd_itr = cmd_list.begin();
	cmd_list.push_back(parameters);
	
	ReleaseMutex(batch_lock);

	return 0;
}

int cmd_insert(const string parameters)
{
	stringstream ss(parameters);
	string b_cmd;
	
	//the batch thread must be stopped before we
	//can insert a command
	cmd_stop("");
	
	WaitForSingleObject(batch_lock, INFINITE);
	
	list<string>::iterator cmd_itr = cmd_list.begin();
	int cmd_num = -1;
	
	cout << "cmd_insert: " << parameters << endl;

	//make sure we dont try to remove from an empty list
/*	if(cmd_list.empty())
	{
		ReleaseMutex(batch_lock);
		return -1;
	}*/
	
	ss >> cmd_num;
	b_cmd = parameters.substr(parameters.find_first_of(' ') + 1);
	cout << "num: " << cmd_num
		  << "\nparam: " << b_cmd<< endl;

	if(cmd_list.size() >= cmd_num)
	{
		while(cmd_itr != cmd_list.end())
		{
			if(cmd_num-- == 0)
			{
				cmd_list.insert(cmd_itr, b_cmd);
				ReleaseMutex(batch_lock);
				return 0;
			}
			++cmd_itr;
		}
	}
	else
		cmd_list.push_back(b_cmd);
	
	ReleaseMutex(batch_lock);
	return 0;
}

//remove command at position N in the list
//N is provided by the parameter
int cmd_remove(const string parameters)
{
	stringstream ss(parameters);
	
	//the batch thread must be stopped before we
	//can remove any command
	cmd_stop("");
	
	WaitForSingleObject(batch_lock, INFINITE);
	
	list<string>::iterator cmd_itr = cmd_list.begin();
	int cmd_num = -1;
	
	cout << "cmd_remove: " << parameters << endl;

	//make sure we dont try to remove from an empty list
	if(cmd_list.empty())
	{
		ReleaseMutex(batch_lock);
		return -1;
	}
	
	ss >> cmd_num;
	//cout << "num: " << cmd_num << endl;
	if(cmd_num == -1)
	{
		ReleaseMutex(batch_lock);
		cout << "Invalid line number to remove: " << parameters << endl;
		return -1;
	}

	if(cmd_list.size() >= cmd_num)
	{
		while(cmd_itr != cmd_list.end())
		{
			if(cmd_num-- == 0)
			{
				cmd_list.erase(cmd_itr);
				ReleaseMutex(batch_lock);
				return 0;
			}
			++cmd_itr;
		}
	}
	else
		cmd_list.pop_back();
	
	ReleaseMutex(batch_lock);
	return 0;
}

int cmd_forkclient(const string parameters)
{
	STARTUPINFO sinfo;
	PROCESS_INFORMATION pinfo;
	int ret_val;
	string cmd_line("client ");
	char cmd_cstr[256];

	cout << "cmd_forkclient: " << parameters << endl;

	cmd_line += server_address;
	strncpy(cmd_cstr, cmd_line.c_str(), 256);
	
	//run the new client
	cout << "command line: " << cmd_cstr << endl;
	GetStartupInfo(&sinfo);
	ret_val = CreateProcess(NULL, cmd_cstr, NULL, NULL, FALSE,
									CREATE_NEW_CONSOLE, NULL, NULL, &sinfo, &pinfo);
	if(!ret_val)
		display_syserror();

	return 0;
}

//pause the batch thread
int cmd_pause(const string parameters)
{

	cout << "cmd_pause: " << parameters << endl;
	if(!paused)
	{
		WaitForSingleObject(pause_lock, INFINITE);
		paused = true;
		cout << "batch paused..." << endl;
		return 1;
	}
	else
	{
		ReleaseMutex(pause_lock);
		paused = false;
		cout << "batch unpaused..." << endl;
		return 0;
	}

}

//terminate the main program
int cmd_quit(const string parameters)
{
	cout << "cmd_quit: " << parameters << endl;
	run = false;
	return 0;
}

//try to stop the batch thread
int cmd_stop(const string parameters)
{
	cout << "cmd_stop: " << parameters << endl;
	WaitForSingleObject(batch_lock, INFINITE);
	verify_unpaused();
	run_thread = false;
	ReleaseMutex(batch_lock);
	return 0;
}

//try to start the batch thread
int cmd_start(const string parameters)
{
	cout << "cmd_start: " << parameters << endl;

	//make sure it isnt already running
	WaitForSingleObject(batch_lock, INFINITE);
	verify_unpaused();
	if(!run_thread)
	{
		cout << "starting batch thread..." << endl;
		run_thread = true;
		hthread = (HANDLE)_beginthread(&batch, 0, NULL);
	}
	else
	{
		cout << "batch thread is currently running..." << endl;
	}
	ReleaseMutex(batch_lock);

	return 0;
}

//terminate the batch thread
//dangerous function.  try cmd_stop first
int cmd_terminate(const string parameters)
{
	cout << "cmd_terminate: " << parameters << endl;
	WaitForSingleObject(batch_lock, INFINITE);
	verify_unpaused();
	cout << "I'll be back...";
	_flushall();
	TerminateThread(hthread, 0);
	CloseHandle(hthread);
	run_thread = false;
	cout << "(thread terminated)" << endl;
	ReleaseMutex(batch_lock);
	return 0;
}

//send a INF-style text stream to the server
//containing my current state info
int cmd_report(const string parameters)
{
	cout << "cmd_report: " << parameters << endl;
	int ret_val, index;
	string batch_state("[batch_state]\n");
	string batch_commands("[batch_commands]\n");
	string send_str;
	ostringstream ss;
	
	ss << "[batch_state]\n";
	//check the state of the batch thread
	if(run_thread)
	{
		if(paused)
			ss << "paused\n\n";//we are paused
		else 
			ss << "running\n\n";
	}
	else
		ss << "stopped\n\n";

	//list all the commands in our cmd_list
	ss << "[batch_commands]\n";
	WaitForSingleObject(batch_lock, INFINITE);
	list<string>::iterator pcmds = cmd_list.begin();
	index = 0;
	while(pcmds != cmd_list.end())
	{
		ss << index << ") " << *pcmds << '\n';
		++pcmds;
		++index;
	}
	ReleaseMutex(batch_lock);
	ss << '\n';

	ss << "[Other Settings]\n"
		<< "delay = " << delay_time << '\n'
		<< "diewhendone = " << diewhendone << '\n'
		<< "iterations left = " << num_iterations << "\n\n";
		
	//make a string for sending
	send_str = ss.str();
	
	ret_val = send(s, send_str.c_str(), send_str.length(), 0);
	if(ret_val < 0)
	{
		cout << "send error: " << errstr << endl;
		return -1;
	}	
	else
	{
		cout << ret_val << " bytes sent" << endl;
		return 0;
	}
}

int cmd_realtime(const string parameters)
{
	cout << "cmd_realtime: " << parameters << endl;

	return 0;
}

int cmd_setdelay(const string parameters)
{
	int new_delay;
	cout << "cmd_setdelay: " << parameters << endl;
	stringstream ss(parameters);
	ss >> new_delay;

	WaitForSingleObject(delay_lock, INFINITE);
	
	delay_time = new_delay;
	
	ReleaseMutex(delay_lock);
	
	return 0;
}

int cmd_setiterations(const string parameters)
{
	int new_num;
	cout << "cmd_setiterations: " << parameters << endl;
	stringstream ss(parameters);
	ss >> new_num;

	WaitForSingleObject(batch_lock, INFINITE);
	
	num_iterations = new_num;
	total_iterations = new_num;

	ReleaseMutex(batch_lock);
	
	return 0;
}

int cmd_diewhendone(const string parameters)
{
	cout << "cmd_diewhendone: " << parameters << endl;

	WaitForSingleObject(batch_lock, INFINITE);
	
	diewhendone = true;
	
	ReleaseMutex(batch_lock);
	
	return 0;
}

//make sure that the batch process is not paused
void verify_unpaused()
{
	if(cmd_pause(""))
		cmd_pause("");
}
