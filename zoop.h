#define MAXBYTES 256
#define SERVPORT 1134
#define HEARTBEAT_SEC 60
#define HEARTBEAT_USEC 0
#define errstr gai_strerror(WSAGetLastError())
#include <string>

//typedef pair<struct in_addr, u_short> index_t;
typedef pair<u_long, u_short> index_t;
static const basic_string <char>::size_type npos = -1;

typedef struct cmd_struct
{
	string address;
	int port;
	string command;
	string parameters;
} cmd_struct;

int cmp_nocase(const string &s, const string &s2)
{
	string::const_iterator p = s.begin();
	string::const_iterator p2 = s2.begin();

	while(p != s.end() && p2 != s2.end())
	{
		if(toupper(*p) != toupper(*p2))
			return (toupper(*p) < toupper(*p)) ? -1 : 1;
		++p;
		++p2;
	}
	return s2.size() - s.size();
}

void display_syserror()
{
	LPVOID lpMsgBuf;
	FormatMessage( 
		 FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		 FORMAT_MESSAGE_FROM_SYSTEM | 
		 FORMAT_MESSAGE_IGNORE_INSERTS,
		 NULL,
		 GetLastError(),
		 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		 (LPTSTR) &lpMsgBuf,
		 0,
		 NULL 
	);
	// Process any inserts in lpMsgBuf.
	// ...
	// Display the string.
	cout << "Error: " << (LPCTSTR)lpMsgBuf << endl;
	//MessageBox( NULL, (LPCTSTR)lpMsgBuf, "Error", MB_OK | MB_ICONINFORMATION );
	// Free the buffer.
	LocalFree( lpMsgBuf );
}

/*BOOL SetPrivilege(
    HANDLE hToken,          // access token handle
    LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
    BOOL bEnablePrivilege   // to enable or disable privilege
    ) 
{
	TOKEN_PRIVILEGES tp;
	LUID luid;

	if ( !LookupPrivilegeValue( 
			  NULL,            // lookup privilege on local system
			  lpszPrivilege,   // privilege to lookup 
			  &luid ) ) {      // receives LUID of privilege
		 cout << "LookupPrivilegeValue error: ";
			display_syserror();
		 return FALSE; 
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	if (bEnablePrivilege)
		 tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	else
		 tp.Privileges[0].Attributes = 0;

	// Enable the privilege or disable all privileges.

	AdjustTokenPrivileges(
			 hToken, 
			 FALSE, 
			 &tp, 
			 sizeof(TOKEN_PRIVILEGES), 
			 (PTOKEN_PRIVILEGES) NULL, 
			 (PDWORD) NULL); 
	 
	// Call GetLastError to determine whether the function succeeded.

	if (GetLastError() != ERROR_SUCCESS) { 
			cout << "AdjustTokenPrivileges failed: ";
			display_syserror();
			return FALSE; 
	} 

	return TRUE;
}*/

