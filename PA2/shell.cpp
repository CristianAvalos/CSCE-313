#include <iostream>
#include <stdio.h> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
using namespace std;

string trim(string sentence) {
    int index = 0;
    while (index < sentence.size() && sentence[index] == ' ') {
        index++;
    }
    if (index < sentence.size()) {
        sentence = sentence.substr(index);
    }
    else {
        return "";
    }

    index = sentence.size() - 1;
    while (index >= 0 && sentence[index] == ' ') {
        index--;
    }
    if (index >= 0) {
        sentence = sentence.substr(0, index + 1);
    }
    else {
        return "";
    }
    return sentence;
}

vector<string> split(string sentence, char delim = ' ') {
    string word = "";
    vector<string> parts;

    size_t foundPipe = sentence.find("|");
    vector<size_t> vectQ; // This is to see if a | is in between " "
    vector<size_t> VectA; // ditto but for ' '
    size_t posQ = sentence.find("\""); // " | "
    size_t posA = sentence.find("\'"); // ' | '
    while (posQ != string::npos) { // " | "
        vectQ.push_back(posQ);
        posQ = sentence.find("\"", posQ + 1);
    }
    for(int i = 0; i < vectQ.size() / 2; i++) { // " | "
        if (vectQ[i * 2] < foundPipe && vectQ[(i * 2) + 1] > foundPipe) {
            foundPipe = string::npos;
        }
    }
    while (posA != string::npos) { // ' | '
        VectA.push_back(posA);
        posA = sentence.find("\'", posA + 1);
    }
    for(int i = 0; i < VectA.size() / 2; i++) { // ' | '
        if (VectA[i * 2] < foundPipe && VectA[(i * 2) + 1] > foundPipe) {
            foundPipe = string::npos;
        }
    }
    if (foundPipe != string::npos) {
        bool insideQuote = false;
        bool insideApos = false;
        for (int i = 0; i < sentence.size(); i++) {
            if (sentence[i] == delim) {
                if (word == "") {
                    continue;
                }
                else {
                    parts.push_back(trim(word));
                    word = "";
                }
            }
            else {
                word = word + sentence[i];
            }
        }
    }
    else {
        bool insideQuote = false;
        bool insideApos = false;
        for (int i = 0; i < sentence.size(); i++) {
            if (sentence[i] == '\"') {
                if (insideQuote == false) {
                    insideQuote = !insideQuote;
                    continue;
                }
                else {
                    insideQuote = !insideQuote;
                    parts.push_back(trim(word));
                    continue;
                }
            }
            if (insideQuote) {
                word = word + sentence[i];
                continue;
            }
            if (sentence[i] == '\'') {
                if (insideApos == false) {
                    insideApos = !insideApos;
                    continue;
                }
                else {
                    insideApos = !insideApos;
                    continue;
                }
            }
            if (insideApos) {
                word = word + sentence[i];
                continue;
            }
            if (sentence[i] == delim) {
                if (word == "") {
                    continue;
                }
                else {
                    parts.push_back(trim(word));
                    word = "";
                }
            }
            else {
                word = word + sentence[i];
            }
        }
    }
    if (word != "") {
        parts.push_back(trim(word));
    }
    return parts;
}

char** VectToCharArray(vector<string> parts) {
    char** array = new char*[parts.size() + 1]; // extra space for NULL
    for (int i = 0; i < parts.size(); i++) {
        array[i] = (char*) parts[i].c_str();
    }
    array[parts.size()] = NULL;
    return array;
}

int main() {
    dup2(0, 3);
    cout << "To exit shell, type \"exit\". " << endl;
    vector<int> backGrounds; // list of background processes
    char* user = getenv("USER");
    while(true) {
        dup2(3,0);
        for (int i = 0; i < backGrounds.size(); i++) {
            if (waitpid(backGrounds[i], 0, WNOHANG) == backGrounds[i]) { // kill the zombie
                backGrounds.erase(backGrounds.begin() + i);
                i--;
            }
        }
        time_t now = time(0);
        struct tm tstruct;
        char buffer[80];
        tstruct = *localtime(&now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d.%X", &tstruct);
        char wd[100];
        cout << buffer << " " << user << ":" << getcwd(wd, sizeof(wd)) << "$ ";

        string inputLine;
        getline(cin, inputLine);
        inputLine = trim(inputLine);

        if (inputLine == "exit") {
            time_t now = time(0);
            struct tm tstruct;
            char buffer[80];
            tstruct = *localtime(&now);
            strftime(buffer, sizeof(buffer), "%Y-%m-%d.%X", &tstruct);
            cout << "Time ended: " << buffer << endl;
            cout << "End of shell. Goodbye!" << endl;
            break;
        }
        char buf[100];
        // using background
        bool backGround = false;
        if (inputLine[inputLine.size() - 1] == '&') {
            backGround = true;
            inputLine = inputLine.substr(0, inputLine.size() - 1); // removing the '&' so that exec doesnt crash
            inputLine = trim(inputLine);
        }
        // using cd
        else if (trim(inputLine).find("cd") == 0) {
            string direc = trim(split(inputLine, ' ')[1]);
            if (direc == "-") {
                cout << buf << endl;
                chdir(buf);
                continue;
            }
            else {
                getcwd(buf, sizeof(buf));
                chdir(direc.c_str());
                continue;
            }
        }
        vector<string> pipeParts = split(inputLine, '|');
        for (int i = 0; i < pipeParts.size(); i++) {
            int fds[2];
            pipe(fds);
            int pid = fork(); // creating child
            if (!pid) { // child process             
                // using > or <
                //int position = inputLine.find('>');
                int position = pipeParts[i].find('>');
                size_t echoPos = pipeParts[i].find("echo");
                if (position >= 0 && echoPos == string::npos) {
                    string command = trim(pipeParts[i].substr(0, position));
                    string filename = (trim(pipeParts[i].substr(position + 1)));
                    pipeParts[i] = command;
                    int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_RDONLY, S_IWUSR | S_IRUSR);
                    dup2(fd, 1);
                    close(fd);
                }
                // position = inputLine.find('<');
                position = pipeParts[i].find('<');
                if (position >= 0) {
                    string command = trim(pipeParts[i].substr(0, position));
                    string filename = (trim(pipeParts[i].substr(position + 1)));
                    pipeParts[i] = command;
                    int fd = open(filename.c_str(), O_RDONLY | O_CREAT, S_IWUSR | S_IRUSR);
                    dup2(fd, 0);
                    close(fd);
                }
                // using pipe
                if(i < pipeParts.size() - 1) {
                    dup2(fds[1], 1);
                    close(fds[1]);
                }
                // other
                inputLine = pipeParts.at(i);
                vector<string> parts = split(inputLine, ' ');
                char** arguments = VectToCharArray(parts);
                execvp(arguments[0], arguments);
            }
            else {
                if (!backGround) {
                    waitpid(pid, 0, 0); // wait for the child process
                    // dup2(fds[0], 0);
                    // close(fds[1]);
                }
                else {
                    backGrounds.push_back(pid);
                }
                dup2(fds[0], 0);
                close(fds[1]);
            }
        }
    }
    return 0;
}