CLINK=wsock32.lib kernel32.lib user32.lib advapi32.lib
SLINK=wsock32.lib kernel32.lib
SWITCHES=/MT /EHsc /Z7

all: client.exe server.exe

client.exe: client.cpp zoop.h
	cl $(SWITCHES) client.cpp /link $(CLINK)

server.exe: server.cpp zoop.h
	cl $(SWITCHES) server.cpp /link $(SLINK)

clean: 
	del *.obj
	del *.exe
	del *.pdb
	del *.ilk
