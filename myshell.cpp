#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <string.h>
#include <sstream>
#include <vector>
#include <stdio.h>
#include <fcntl.h>

// here's a new comment

//declare constants for standard input and standard output
#define INPUT 0
#define OUTPUT 1

//declare constants to flag which type of redirection is used
#define RE_NO -1 //no redirection
#define RE_IN 0 //input redirection
#define RE_OUT 1 //output redirection
#define RE_APP 2 //append redirection

//function headers so the compiler knows they exist and doesn't yell at me
void runCommand(std::string command, bool hasSemiColons);
void getArgs(std::string command, std::vector<const char*> &args, int &redirect, std::string &redirectLoc);
std::vector<std::string> splitCommands(std::string command);
void splitPipes(std::string command, std::vector<std::vector<const char *> > &commands, int &redirect, std::string &redirectLoc);
int forkCommands(std::vector <std::vector < const char*> > commands, int redirect, const char* redirectLoc);
int runSingleCommand(int inputID, int outputID, std::vector<const char*> args);

//main function to manage the shell
int main(int argc, char* argv[])
{
	//save the prompt for readability's sake
	char prompt = '$';
	//declare input string so I don't have to keep doing it
	std::string input;

	//prompt needs to show at least once -> do while loop
	do
	{
		//prompt user and read in their input
		std::cout << prompt << " ";
		getline(std::cin, input);

		//if the input isn't empty and isn't exit,
		if (input != "" || input != "exit")
		{
			//check if input has semicolons
			int semi = input.find(';');
			//run the input command(s)
			runCommand(input, semi >= 0);
		}
	} while (input != "exit");

	return 0;
}

//a function to manage running commands
//takes a string command input and a flag indicating if there are semicolons or not
void runCommand(std::string command, bool hasSemiColons)
{
	//declare a bunch of stuff that needs to be used later
	int status;
	int redirect = RE_NO; //set this to default of no redirection, just in case
	std::string redirectLoc;
	std::vector<std::vector<const char*> > commands;

	//commands with semicolons process differently, so check the flag
	if (hasSemiColons)
	{
		//if there are semicolons, split the input into the commands delimited by semicolons
		std::vector<std::string> tempComs = splitCommands(command);
		
		//loop through the split commands and run each of them
		for (int i = 0; i < tempComs.size(); i++)
		{
			//fork a new process each time a new command is run so all the commands actually run
			pid_t newPID = fork();

			//if the fork failed
			if (newPID < 0)
				std::cerr << "Fork failed" << std::endl;
			//if we are the child
			else if (newPID == 0)
			{
				//split the command on pipes
				splitPipes(tempComs[i], commands, redirect, redirectLoc);

				//run the commands
				forkCommands(commands, redirect, redirectLoc.c_str());

				//if we somehow get here, we have an error, ergo return 1
				exit(1);
			}
			//if we are the parent
			else
			{
				//sit here and wait for the child to finish
				waitpid(newPID, &status, 0);
			}
		}
	}
	//no semicolons
	else
	{
		pid_t pid = fork();

		//if the fork failed
		if (pid < 0)
			std::cerr << "Fork failed" << std::endl;
		//if we are the child
		else if (pid == 0)
		{
			//split the command on pipes
			splitPipes(command, commands, redirect, redirectLoc);
			forkCommands(commands, redirect, redirectLoc.c_str());
			exit(1);
		}
		//we're the parent
		else
			waitpid(pid, &status, 0);
	}
}

//splits the command string into a vector of arguments
//arguments are delimited by spaces
//args, redirect, and redirectLoc need to be used by calling functions, so pass by reference
void getArgs(std::string command, std::vector<const char*> &args, int &redirect, std::string &redirectLoc)
{
	std::stringstream stream(command);
	std::string temp;

	//while there's still something left to process
	while (stream >> temp)
	{
		//if it's not some kind of input redirection
		if (temp.find("<") == std::string::npos && temp.find(">") == std::string::npos) //find returns npos if it doesn't find what it's looking for
		{
			//add the new argument to the back of the vector
			//vector of c-strings so get the c-string of the argument
			args.push_back(strdup(temp.c_str()));
		}
		//if there's input redirection
		else if (temp.find("<") != std::string::npos)
		{
			//set the input redirection flag
			redirect = RE_IN;
			//check if there's more text after the flag or not
			if (temp.size() > 1)
			{
				//if there is, the redirection location is the token minus <
				redirectLoc = temp.substr(1);
			}
			else
			{
				//redirection location is the next token, so read it in
				stream >> redirectLoc;
			}
		}
		//if there's append redirection
		//check for this first because > is a substring of >>
		else if (temp.find(">>") != std::string::npos)
		{
			//set the append redirection flag
			redirect = RE_APP;
			//check if there's more text after the flag or not
			if (temp.size() > 2)
				redirectLoc = temp.substr(2);
			else
				stream >> redirectLoc;
		}
		//if there's output redirection
		else if (temp.find(">") != std::string::npos)
		{
			//set the output redirection flag
			redirect = RE_OUT;
			//check if there's more text after the flag or not
			if (temp.size() > 1)
				redirectLoc = temp.substr(1);
			else
				stream >> redirectLoc;
		}
	}
}

//splits a command string with semicolons into the commands between the semicolons
std::vector<std::string> splitCommands(std::string command)
{
	std::vector<std::string> commands;

	//make a stringstream to process the input
	std::stringstream stream(command);
	std::string temp;

	//while there's still stuff to get
	//adding the semicolon as an argument uses it as the delimiter instead of spaces
	while (getline(stream, temp, ';'))
	{
		//if there's a space at the beginning of the command, remove it to save some headaches later
		if (temp.at(0) == ' ')
			temp = temp.substr(1);
		commands.push_back(temp);
	}
	
	//return the split commands
	return commands;
}

//splits the commands by pipe
void splitPipes(std::string command, std::vector<std::vector<const char*> > &commands, int &redirect, std::string &redirectLoc)
{
	std::vector<const char *> args;

	//split the commands on spaces
	getArgs(command, args, redirect, redirectLoc);

	//add a new vector to the vector of vectors
	//the vectors in the vector store the arguments for each separate piped command
	commands.push_back(*(new std::vector<const char *>));

	//loop through all of args and get everything out of it
	for (int i = 0; i < args.size(); i++)
	{
		//check if there's a pipe
		if (strcmp(args[i], "|") == 0)
		{
			//add a null argument to the back of the current set of arguments
			//some commands act funky if there's not a null command at the end
			commands.back().push_back((const char*)0);
			//add a new vector for the next set of arguments
			commands.push_back(*(new std::vector<const char*>));
		}
		//if there's not a pipe
		else
		{
			//add the argument to the back of the current set of arguments
			commands.back().push_back(args[i]);
		}
	}
	commands.back().push_back((const char*)0);
}


//runs piped commands and single commands
int forkCommands(std::vector<std::vector <const char*> > commands, int redirect, const char* redirectLoc)
{
	pid_t pid;
	int fd[2]; //used for input and output

	int inputID;

	//if the input is coming from somewhere other than standard input
	if (redirect == RE_IN)
	{
		//open the redirection location
		int tempInputID = open(redirectLoc, O_RDONLY);
		dup2(tempInputID, INPUT);

		//close the input location
		close(tempInputID);
	}

	//for piped commands only because there will be more than one
	for (int i = 0; i < commands.size() - 1; i++)
	{
		//open a pipe for input and output
		pipe(fd);

		//run the command
		runSingleCommand(inputID, fd[1], commands[i]);

		close(fd[1]);

		//change input ID for later
		inputID = fd[0];
	}

	//if inputID isn't standard input
	if (inputID != INPUT)
	{
		dup2(inputID, INPUT);
	}

	//if the output needs to be redirect and/or appended
	if (redirect == RE_APP || redirect == RE_OUT)
	{
		int outputID;
		//if appeneding, use append instead of truncate on the output location
		if (redirect == RE_APP)
			outputID = open(redirectLoc, O_WRONLY | O_CREAT | O_APPEND, 0666);
		//otherwise truncate output location
		else
			outputID = open(redirectLoc, O_WRONLY | O_CREAT | O_TRUNC, 0666);

		dup2(outputID, OUTPUT);
		close(outputID);
	}

	//run the last (or only) command
	return execvp(commands.back()[0], (char**)commands.back().data());
}


//runs an individual command
int runSingleCommand(int inputID, int outputID, std::vector<const char*> args)
{
	//fork a new process
	pid_t pid = fork();

	//fork failed
	if (pid < 0)
		std::cerr << "Fork failed" << std::endl;
	//if we're the child
	else if (pid == 0)
	{
		//if input isn't standard input, change input
		if (inputID != INPUT)
		{
			dup2(inputID, INPUT);
			close(inputID);
		}
		//if output isn't standard output, change output
		if (outputID != OUTPUT)
		{
			dup2(outputID, OUTPUT);
			close(outputID);
		}

		//run the command (args[0] is the command), everything else is arguments
		//use data() to change from vector to const char* []
		return execvp(args[0], (char**)args.data());
	}

	return pid;
}
