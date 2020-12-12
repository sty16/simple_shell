EXECUTABLE=myshell

all: shell

shell: shell.c
	g++ -g -o $(EXECUTABLE) shell.c -Wall -W

clean:
	rm $(EXECUTABLE)