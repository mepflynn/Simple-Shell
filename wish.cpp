// Wish, a simple Unix shell with both interactive and batch modes.
// Implemented by Max Flynn

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>
#include <algorithm>
#include <locale>

#include <string>
#include <cstring>
#include <fstream>

using namespace std;

// Printing related functions for error call and debugging
void callError() {
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}
// end printing related functions, all debugging functions have been removed

void lineOfInput(bool interactMode, string& line_in, ifstream& input_file) {
            // Take one line of user input, either from file or user input
        if (interactMode)  {
            cout << "wish>";
            getline(cin, line_in);
        }
        else {
            getline(input_file, line_in); // In batch mode, no need to print 'wish>'
            if (input_file.eof()) exit(0);
        }

        return;
}


// Input string manipulating and parsing functions /////////////////////////

// Remove '>' and '&' that are attached to arguments and need to be split off from them,
// and return a vector of all of the parts the arg split into.
void separateSpecialChar(string token, vector<string>& args) {
    size_t found;
    string to_locate = (">&");

    args.push_back(token);
    vector<string>::iterator args_itr;

    while (true) {
        // Start an iterator to the last string in the vector
        args_itr = args.end() - 1;
        found = (*args_itr).find_first_of(to_locate); // Default starting search to pos = 0, since only 1 element
        // Skip if char not found, or if input string length == 1 (can't separate 1 char)
        if (found == string::npos || (*args_itr).length() == 1) {
            break; // Line contains no more special chars
        } else {
            string token1 = (*args_itr).substr(0, found); // Chars up till special
            string token2 = (*args_itr).substr(found,1); // Special char itself
            string token3 = (*args_itr).substr(found + 1, string::npos); // Rest until end

            args.pop_back(); // Delete the element that we just broke up

            // Push the three parts in its place (as long as they're not whitespace)
            args.push_back(token1); 
            args.push_back(token2);
            args.push_back(token3);
        }
    }
    return;
}

// Fully separate the input string into vector<string>, by whitespace and also by special chars with the above fcn
bool tokenizeInput(vector<string>& token_line, string line) {

    istringstream argstream(line);
    string temp;

    // Utilize copy as a robust way to tokenize by whitespace, and the back_inserter to place
    // Each element neatly onto the back of the tokenized string vector token_line
    copy(istream_iterator<string>(argstream),istream_iterator<string>(), back_inserter(token_line));

    if(token_line.empty()) return false;

    // Call my own function to seperate all of the '>' and '&' into their own separate string if they are attached to other words 
    for (unsigned int i = 0; i < token_line.size(); i++) {
        // If this entry contains a special character
        if (token_line[i].find_first_of(">&") != string::npos) {
            // Separate the special char out from its neighbors
            vector<string> temp;
            separateSpecialChar(token_line[i],temp);

            if (temp.size() == 1) continue;      

            // Erase the element that was just split up, and insert the split up parts
            token_line.erase(token_line.begin() + i);
            token_line.insert(token_line.begin() + i, temp.begin(), temp.end());
            i += temp.size() - 1; // Increment i to reflect the fact that the vector just grew
        }
    }

    return true;
}


// check a list of args for valid redirect syntax
bool hasValidRedirect(const vector<string>& command) {
    bool oneRedirect = false;

    // Loop through each string in the vector
    for(vector<string>::const_iterator itr = command.begin(); itr < command.end(); itr++) {
        if ((*itr).compare(">") == 0) {
            // Single redirect found. Is its position valid? Does it have at least one arg to its left and exactly one to its right?
            if (!oneRedirect && distance(command.begin(),itr) > 0 && distance(itr, (command.end() - 1)) == 1) {
                oneRedirect = true;
            } else {
                // A second redirect has been found, or it was in the wrong place. command invalid, call an error in the parent function of this one
                return false;
            }
        }
    }

    return true;
}

// check if a list of args contains any discrete '>'
bool hasNoRedirect(const vector<string>& command) {

    for (vector<string>::const_iterator itr = command.begin(); itr < command.end(); itr++) {
        if ((*itr).compare(">") == 0) return false; // 
    }
    return true;
}

// Call the former two functions across each command in splitArgs
bool verifyRedirectCmds(const vector<vector<string>>& splitArgs) {
    // Loop through the commands of splitArgs. For each command containing '>', verify that they have correct syntax.
    // If any incorrect redirect syntax is detected, return false;
    for (const vector<string> command : splitArgs) {
        if (!hasNoRedirect(command)) {
            if (hasValidRedirect(command)) continue;
            callError();
            return false;
        }
    }
    return true;
}

// Read through and execute each redirect by opening a file in the indicated name and using
// dup2() to redirect STDOUT and STDERR to that file
bool parseRedirectCmd(vector<string>& command) {
    // this command already has a valid '>' in it
    string fileName;

    // Locate the '>' in the command
    //vector<string>::iterator pos = find(command.begin(), command.end(), string(">"));

    // obtain the filename from the end of 'command'
    fileName = (*(command.end() - 1));

    int fd = open(fileName.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
    if (fd == -1) {
        callError();
        return false;
    }

    // File opening was a success. Redirect its STD OUT and ERR to this fd.
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    
    // Remove the last two args in command, these are the '>' and the fileName
    command.pop_back();
    command.pop_back();

    return true;
}

// Use malloc to create a char** 2D char array containing all of the arguments to each command
void stringsToCstr(vector<string> strings, char** exeArgs) {
    // 2D array for holding input args as C-strings

    // Take the char** and make it point at a 2D char array, numStrings in length
    // This char** exeArgs has allready had itself 
    
    // Populate string array from a vector of std::strings, 
    for (unsigned int i = 0; i < strings.size(); i++) {
        const char* cString = strings[i].c_str();
        exeArgs[i] = (char*)malloc(sizeof(cString));
        strcpy(exeArgs[i],cString);
    } 
    // // Copy in a nullchar as the last element of the 2D char array exeArgs
    exeArgs[strings.size()] = (char*)malloc(sizeof((char)NULL));
    exeArgs[strings.size()] = NULL;
    
}

// parallelize a list of commands with &s separating them
bool parseParallelCmds(vector<string>& args, vector<vector<string>>& splitArgs) {
    // First, check for valid input
    // We know this string contains an &, but are they validly placed?
    if (args[0].compare("&") == 0) {
        // Leading or trailing &, error and exit
        //callError();
        return false; // Indicate bad command syntax, continue; in main
    }

    if (args[args.size() - 1].compare("&") == 0) {
        // If the last arg in the command is an & with no cmd after, just delete it and proceed as normal
        args.pop_back();
    }

    for (unsigned int i = 1; i < args.size(); i++) {
        if (args[i-1].compare(args[i]) == 0 && args[i].compare("&") == 0) {
            //callError(); // Adjacent args are &, undefined behavior
            return false;
        }
    }

    vector<string> tempCmd;
    for(string arg : args) {
        // If & is not yet found, append each arg of the command
        if (arg.compare("&") != 0) tempCmd.push_back(arg); 
        else if (tempCmd.size() > 0) { // Wouldn't wanna push a blank command on
            // '&' detected, push the current cmd onto splitArgs and clear tempCmd
            splitArgs.push_back(tempCmd);
            tempCmd.clear();        
        }
        
    }
    // Push the last tempCmd on to splitArgs
    splitArgs.push_back(tempCmd);
    return true;
}

// End input manipulating functions ////////////////////////////////////////


// Helper function for findValidExePaths, takes one command and searches every entry in path 
// for a valid executable. Returns the path of the found valid executable, and else returns empty string
string findValidExePath(const vector<string>& path, string command) {
    // ensure there's a leading '/' for the command to go up against the blank end of each path entry
    if (command[0] != '/') {
        command = '/' + command;
    }

    for (string singlePath : path) {
        // Append command name to each path, then check via access()
        string fullPath = (singlePath) + command;
        if (access(fullPath.c_str(),X_OK) == 0) {
            // Success! File exists in this directory and is permitted to execute only
            return fullPath;
        }
    }

    // Command executable not found, return empty string
    return string("");
}
// Loop through each command, and use findValidExePath to determine an EXE for each one
// Returns false if it fails to find a valid path for each and every command of input
bool findValidExePaths(const vector<string>& path, const vector<vector<string>>& splitArgs, vector<string>& exePaths) {
    
    for (const vector<string> command : splitArgs) {
        // Call findValidPath with the leading term of *each* individual command in splitArgs
        exePaths.push_back(findValidExePath(path, command[0]));
        if (exePaths.back().empty()) {
            // If the element just appended came up blank, that means no valid path was found
            // One of a series of parallel commands is invalid.
            exePaths.pop_back();    // Delete the blank exePath
        }
    }

    if (exePaths.size() < splitArgs.size()) {
        callError();        // No valid path was found for some subset of the commands
        return false;         // If no valid path, reloop for new input
    }
    return true;
}


// checkBuiltins: returns true if a built-in command was successfully run, else returns false
bool checkBuiltins(vector<string>& path, vector<string> args) {
    bool ran_function = false; // To notify main whether to continue; if a command was run here successfully
    
    if ((*args.begin()).compare("exit") == 0) { 
        if (args.size() > 1) {  // Run exit: check for numArgs==1, then exit(0)
            callError();
        }
        exit(0);
    } else if ((*args.begin()).compare("path") == 0) {
        // First, clear path
        path.clear();
        // Push each argument of the user input to path (skipping argv[0] == "path")
        for (vector<string>::iterator it = args.begin() + 1; it < args.end(); it++) path.push_back(*it);
        
        ran_function = true;
        //printPath(path);
    } else if ((*args.begin()).compare("cd") == 0) {
        // Change working directories with the chdir() system call
        if (args.size() != 2) {
            callError(); // cd must take exactly one argument
            return true;
        } 
        if(chdir(args[1].c_str()) == -1) callError();
        // Else, chdir worked. Proceed.
        ran_function = true;
    }

    return ran_function;
}

// Fork enough times to run each command (aka row) in the 2D vector splitArgs
void multipleFork(const vector<vector<string>>& splitArgs, vector<pid_t>& pids) {
    pids.push_back(fork());
    if (pids[0] == -1) callError();
    for (unsigned int i = 1; i < splitArgs.size(); i++) {
        if (pids[i-1] == 0) break;
        // pids[prev loop] != 0, hence this is the parent process;
            pids.push_back(fork());
            if (pids[i] == -1) callError();
    }
    return;
}

// children: run the command alotted to them via execv()
// parent: waitpid() for the pid of each child process in pids[]
void executeArguments(vector<vector<string>>& splitArgs, const vector<string>& exePaths, const vector<pid_t>& pids) {
            int status;
        if (*(pids.end() - 1) != 0) {
            // Parent Process
            for (pid_t pid : pids) {
                waitpid(pid, &status, 0); 
            }
                     
        } else if (*(pids.end() - 1) == 0) {
            // Child process: 

            // If the command has a redirect, parse and handle that redirect by calling dup2 to redirect to STDOUT and STDERR
            // Furthermore, return an ammended list of args minus the '>' and filename. Replace them into this command's
            // Spot in splitArgs
            if (!hasNoRedirect(splitArgs[pids.size() - 1])) {
                // Run redirect parsing. FAK. I NEED TO CHECK SYNTAX EARLIER, BEFORE I FORK.
                if(!parseRedirectCmd(splitArgs[pids.size() - 1])) {
                    // parse fcn returned false, indicating... bad file?
                }
            } 
            
            // Populate exeArgs with the input strings converted to char*
            char** exeArgs = (char**)malloc((splitArgs[pids.size() - 1].size()) *  sizeof(char*));
            stringsToCstr(splitArgs[pids.size() - 1], exeArgs); 

            // Plug the args and verified exePath into execv
            execv(strdup(exePaths[pids.size() - 1].c_str()), exeArgs);

            callError();  // Execv should never return
            // This is a child process, and shouldn't be allowed to go on running
            exit(0);
        }
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        callError();
        exit(1);
    }

    vector<string> path;
    path.push_back("/bin");
    bool interactMode = false;

    // Batch mode
    ifstream input_file;
    if (argc == 2) {
        input_file.open(argv[1]);
        if (!input_file.is_open()) {
            callError();
            // Bad batch file, failed to open
            exit(1);
        }
    }

    // Interactive mode: take input from cin
    if (argc == 1) interactMode = true;

    // Main while loop variables
    string line_in;
    while(true) {
        
        // Line of input from either file or stdin, reloop if line is empty
        lineOfInput(interactMode, line_in, input_file);
        if(line_in.empty()) continue;

        // Tokenize the line by whitespace
        // TokenizeInput will return false if its input was all whitespace. In that case, continue;
        vector<string> args;
        if(!tokenizeInput(args, line_in)) continue;

        // If a built-in function is run, then reloop, and look for new user input
        if(checkBuiltins(path, args)) continue;

        // Hierarchical input processing:

        // Input will be broken up into a 2D vector splitArgs, where each entry is one command to execute
        vector<vector<string>> splitArgs;

        // First, if & are present, split commands off into a 2D vector of string
        if ((line_in.find("&") != string::npos)) {
            if(!parseParallelCmds(args, splitArgs)) continue;  // continue if invalid command syntax
        }
        else splitArgs.push_back(args);                                       // if no & present, just populate split_args with the one command

        // Then, verify the correctness of and '>' redirection symbols present
        // The redirection itself need not be handled until just before the call to exec, by the function parseRedirectCmds
        if (!verifyRedirectCmds(splitArgs)) continue;

        
        // Attempt to obtain a valid executable from all of the entries in 'path'
        vector<string> exePaths;
        if (!findValidExePaths(path, splitArgs, exePaths)) continue; // If full command matching isn't succesful, continue; for new input
        
        // Fork splitArgs.size() times, and store a list of the PIDs returned by fork
        // This list of PIDs will also be important to the children as they will use it
        // to determine with command is theirs to execute 
        vector<pid_t> pids;
        multipleFork(splitArgs, pids);

        // Children: Execute each command in split args 
        // Parent: waidpid() for all children in pids
        executeArguments(splitArgs, exePaths, pids);
    }
}