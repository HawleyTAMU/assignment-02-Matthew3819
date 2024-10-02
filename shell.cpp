#include <iostream> 
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <cstring>
#include <signal.h>
#include <sstream>
#include <chrono>
#include "Tokenizer.h"
#include <vector>
#include <string>

#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE    "\033[1;34m"
#define WHITE   "\033[1;37m"
#define NC      "\033[0m"

using namespace std;

int stdinBackup = dup(0);   // Backup stdin
int stdoutBackup = dup(1);  // Backup stdout
string prevDir = getcwd(nullptr, 0);
vector<pid_t> backgroundPIDs;  // Background Process Tracking

int main () {

    string shellInput;

    for (;;) {

        // Used to display the time to the Aggie Shell
        typedef std::chrono::system_clock Clock;

        // Gets the current time of the system the shell is being ran on
        auto currentTime = Clock::now();

        // Convert currenTime to a time_t object
        std::time_t currentTime_T = Clock::to_time_t(currentTime);

        // Breaks down the current time into its components (month, day, hour, min)
        struct tm *timeComponents = std::localtime(&currentTime_T);

        // Store month for display
        vector<string> months{"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        cout << YELLOW << "Shell$" << NC << " ";
        cout << months[timeComponents->tm_mon] << " " << timeComponents->tm_mday << ":" << timeComponents->tm_hour 
             << ":" << timeComponents->tm_min << " " << getenv("USER") << ":" << getcwd(nullptr, 0) << "$ ";

        
        // Get the user input from the shell
        getline(cin, shellInput);

        
        // Reap all current background processes that have finished
        for (size_t i = 0; i < backgroundPIDs.size(); ++i) {
            if (waitpid(backgroundPIDs[i], nullptr, WNOHANG) > 0) {
                cout << "Background process " << backgroundPIDs[i] << " has finished." << endl;
                backgroundPIDs.erase(backgroundPIDs.begin() + i);
                --i;
            }
        }
        
        
        // Get tokenized commands from user input
        Tokenizer tknr(shellInput);
        if (tknr.hasError()) {
            continue;
        }

        // // print out every command token-by-token on individual lines
        // // prints to cerr to avoid influencing autograder
        // for (auto cmd : tknr.commands) {
        //     for (auto str : cmd->args) {
        //         cerr << "|" << str << "| ";
        //     }
        //     if (cmd->hasInput()) {
        //         cerr << "in< " << cmd->in_file << " ";
        //     }
        //     if (cmd->hasOutput()) {
        //         cerr << "out> " << cmd->out_file << " ";
        //     }
        //     cerr << endl;
        // }

        // Convert all user input to character to lowercase to make 'exit' not case sensitive
        string CheckExit = shellInput;
        for (size_t i = 0; i < shellInput.size(); ++i) {
            CheckExit[i] = tolower(shellInput[i]);
        }

        // Handle when user types 'exit'
        if (CheckExit == "exit") {
            // If background processes still exist, make sure to let them finish
            if (!backgroundPIDs.empty()) {
                cout << "Background processes must complete..." << endl;
                while (!backgroundPIDs.empty()) {
                        for (size_t i = 0; i < backgroundPIDs.size(); ++i) {
                            if (waitpid(backgroundPIDs[i], nullptr, WNOHANG) > 0) {
                                cout << "Background process " << backgroundPIDs[i] << " has finished." << endl;
                                backgroundPIDs.erase(backgroundPIDs.begin() + i);
                                --i;
                            }
                        }
                }
            }
            cout << RED << "Now exiting shell..." << NC << endl;
            break;
        }



        // If the user is using the command 'cd'
        if (tknr.commands.size() == 1 && tknr.commands.at(0)->args.at(0) == "cd") {
            string targetDirectory;
            string currentDirectory = getcwd(nullptr, 0);

            // If just cd, go to the home directory
            if (tknr.commands[0]->args.size() == 1) {

                targetDirectory = getenv("HOME");
            } 
            // If the user inputed more than one command
            else {
                // If the user typed 'cd -' to go to the previous directory
                if (tknr.commands[0]->args[1] == "-") {
                    targetDirectory = prevDir;
                } 
                // If the user wants to access a specific directory
                else {
                    targetDirectory = tknr.commands[0]->args[1];
                }
            }
            
            // Change the directory and set the previous directory to one before the change for future cd's
            if (chdir(targetDirectory.c_str()) == 0) {
                prevDir = currentDirectory;
            } 
            // Error Check
            else {
                perror("chdir");
            }

            // No need to keep processing more commands, so continue on to next iteration
            continue;
        }

        // Handle piped commands
        int fd[2];

        // Used to determine if reading from shell or file
        int fdRead = 0;

        for (size_t i = 0; i < tknr.commands.size(); ++i) {

            Command* currentCommand = tknr.commands[i];

            // Create a pipe if there is still more than one command left (to give output as input for next command)
            if (i < tknr.commands.size() - 1) {
                if (pipe(fd) == -1) {
                    perror("pipe");
                    exit(1);
                }
            }

            // Used to determine if writing to file or shell
            int fdWrite;

            // If only one command left, write to shell for termination
            if (i >= tknr.commands.size() - 1) {
                fdWrite = 1;
            } 
           // If more than one command left, write to pipe for next process to read
            else {
                fdWrite = fd[1];
            }

            // Fork
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                exit(1);
            }
            
            if (pid == 0) { // Child process

                // Write process to file if not connected to shell user input
                if (fdWrite != 1) {
                    dup2(fdWrite, 1); // Redirect stdout to file instead of shell
                    close(fdWrite); // No longer need fdWrite since stdout has taken over the job
                }
                // Read process from file if not connected to shell user input
                if (fdRead != 0) {
                    dup2(fdRead, 0); // Redirect stdin to file instead of shell
                    close(fdRead); // No longer need fdRead since stdin has taken over the job
                }

                if (currentCommand->hasInput()) {
                    
                    // Open the file for reading
                    int fdRead = open(currentCommand->in_file.c_str(), O_RDONLY);
                    // Error check
                    if (fdRead < 0) {
                        perror("open input file");
                        exit(1);
                    }

                    dup2(fdRead, 0); // Redirect stdin to file instead of shell
                    close(fdRead); // Close to ensure no resources leaks in since stdin now takes over read process
                }

                if (currentCommand->hasOutput()) {

                    // Open/Create the file for writing
                    int fdWrite = open(currentCommand->out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    
                    // Error check
                    if (fdWrite < 0) {
                        perror("open output file");
                        exit(1);
                    }
                    
                    dup2(fdWrite, 1); // Redirect stdout to file instead of shell
                    close(fdWrite); // Close to ensure no resources leaks in since stdout now takes ofver write process
                }

                // Converting a string to an array of character
                char** cmds = new char*[currentCommand->args.size() + 1]; // +1 for nullptr
                for (size_t itt = 0; itt < currentCommand->args.size(); ++itt) {
                    cmds[itt] = const_cast<char*>(currentCommand->args[itt].c_str());  // Convert std::string to char*
                }

                // Null-terminate the argument list for execvp
                cmds[currentCommand->args.size()] = nullptr;

                // Call execvp  to replace the current process with a new one
                execvp(cmds[0], cmds);

                // IF execvp failed, error check
                perror("execvp");
                exit(1);
            } 
            else { // Parent process
                
                // If not on the last command, send data to read file
                if (i < tknr.commands.size() - 1) {
                    close(fd[1]); // Close write end
                    fdRead = fd[0]; // Next command will read from the pipe
                }
                // If it is a background process, push it to the pid vector
                if (currentCommand->isBackground()) {
                    backgroundPIDs.push_back(pid);
                }
                // If the current process is not meant to run in the background, wait for children to finish
                else {
                    waitpid(pid, nullptr, 0); 
                }
            }
        }

        // Restore stdin and stdout
        dup2(stdinBackup, 0);
        dup2(stdoutBackup, 1);
    }
}
