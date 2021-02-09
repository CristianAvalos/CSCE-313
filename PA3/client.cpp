/*
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date  : 2/8/20
 */
#include "common.h"
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
using namespace std;


int main(int argc, char *argv[]){

    // sending a data message
    datamsg* x = new datamsg(0,0,0);
    int command;
    int person = 0;
    string userFileName;
    bool eg1 = 0, eg2 = 0, newChan = 0;
    bool timeCheck = 0, fileCheck = 0, bufferCheck = 0;
    int bufferSize = MAX_MESSAGE;
    // string filename;
    string ipcmethod = "f";
    int nchannels = 1;
    double totalDataTime = 0;
    double totalTransTime = 0;
    vector<RequestChannel*> channels;
    while ((command = getopt(argc, argv, "p:t:e:f:c:m:i:")) != -1) {
        switch (command) {
            case 'p':
                x->person = atoi(optarg);
                person = atoi(optarg); // this is for me to know for the output file
                break;
            case 't':
                timeCheck = 1; // if the user does not specify a time, the first 1000 lines will be used
                x->seconds = atof(optarg);
                break;
            case 'e':
                x->ecgno = atoi(optarg);
                if (atoi(optarg) == 1) { // this will let me know if the user wants egc 1 for output file
                    eg1 = 1;
                }
                else if (atoi(optarg) == 2) { // this will let me know if user wants egc 2 for output file
                    eg2 = 1;
                }
                break;
            case 'f':
                userFileName = optarg;
                fileCheck = 1;
                break;
            case 'c':
                newChan = 1;
                nchannels = atoi(optarg);
                break;
            case 'm':
                bufferSize = atoi(optarg);
                bufferCheck = 1;
                break;
            case 'i':
                ipcmethod = optarg;
                break;
            default:
                break;
        }
    }

    if (fork()==0){ // child 
        char* args [] = {"./server", "-m", (char *) to_string(bufferSize).c_str(), "-i", (char*) ipcmethod.c_str(), NULL};
        if (execvp (args [0], args) < 0){
            perror ("exec filed");
            exit (0);
        }
    }
    RequestChannel* control_chan = NULL;
    if (ipcmethod == "f") {
        control_chan = new FIFORequestChannel ("control", FIFORequestChannel::CLIENT_SIDE);
    }
    else if (ipcmethod == "q") {
        control_chan = new MQRequestChannel ("control", FIFORequestChannel::CLIENT_SIDE);
    }
    else if (ipcmethod == "m") {
        control_chan = new SHMRequestChannel ("control", FIFORequestChannel::CLIENT_SIDE, bufferSize);
    }
    cout << "Client is connected to the server." << endl << endl;
    
    RequestChannel* chan = control_chan;
    // for file transfer
    filemsg fm (0,0); // this is to get the lengths of the file
    char buf [sizeof (filemsg) + userFileName.size() + 1];
    memcpy (buf, &fm, sizeof (filemsg));
    strcpy (buf + sizeof (filemsg), userFileName.c_str());
    chan->cwrite(buf, sizeof (buf)); // sends a message to the server
    __int64_t filelen;
    chan->cread(&filelen, sizeof (__int64_t)); // retrieves the file length from the server
    cout << "The length of " << userFileName << " is " << filelen << " bytes." << endl;

    // int requests;
    int byteSize = ceil(double(filelen) / double(nchannels));
    int byteSize2 = byteSize;
    int size1 = 0;
    int size2 = 0;
    int requests = 0;
    double totalFileTrans = 0;

    for (int i = 0; i < nchannels; i++) {
        if (newChan == 1) {
            // write new channel
            MESSAGE_TYPE c = NEWCHANNEL_MSG;
            control_chan->cwrite (&c, sizeof (MESSAGE_TYPE));

            // read new channel
            char chanName[100];
            control_chan->cread(&chanName, sizeof(chanName));

            if (ipcmethod == "f") {
                chan = new FIFORequestChannel (chanName, RequestChannel::CLIENT_SIDE);
            }
            else if (ipcmethod == "q") {
                chan = new MQRequestChannel (chanName, RequestChannel::CLIENT_SIDE);
            }
            else if (ipcmethod == "m") {
                chan = new SHMRequestChannel (chanName, RequestChannel::CLIENT_SIDE, bufferSize);
            }
            channels.push_back(chan);
            cout << "New channel created. New Channel name is " << chanName << endl;
            cout << "All further communication will happen through it instead of the main channel" << endl;
        }

        if (person != 0) {
            bool bothEg = (eg1 == 0) && (eg2 == 0); // this is if the user does not specify which egc, therefore, they will get both

            struct timeval start, finish; // this is to get the time it took for the data to be collected
            gettimeofday(&start, NULL);
            // send message to server
            chan->cwrite (x, sizeof (datamsg));
            // receiving a response for the data message
            double result;
            if (timeCheck) { // if the user wants a specific time, they will get it
                chan->cread(&result, sizeof(double));
                cout << "Result for patient " << x->person << " eg " << x->ecgno << " at time " << x->seconds << " is " << result << endl;
            }
            else if (timeCheck == 0 && fileCheck == 0) { // if user does not specify time, they will get the first 1000
                ofstream outputFile("BIMDC/x1.csv"); // this creates the file with the first 1000 lines of data
                double curr = 0.00;
                string filename = "BIMDC/" + to_string(person) + ".csv"; // this will be used to get the data and copy it to the new file
                string line, val;
                ifstream ifs (filename.c_str());
                if (ifs.fail()){
                    EXITONERROR ("Data file: " + filename + " does not exist in the BIMDC/ directory");
                }
                while(getline(ifs, line) && curr < 4) {
                    if (bothEg) {
                        outputFile << line;
                    }
                    else if (eg1 == 1 && eg2 == 0) {
                        string time = split (string(line), ',')[0];
                        string egOne = split (string(line), ',')[1];
                        outputFile << time << ',' << egOne;
                    }
                    else if (eg2 == 1 && eg1 == 0) {
                        string time = split (string(line), ',')[0];
                        string egTwo = split (string(line), ',')[2];
                        outputFile << time << ',' << egTwo;
                    }
                    outputFile << endl;
                    curr += 0.004;
                }
                outputFile.close();
                cout << "The patients data will be outputted to the file BIMDC/x1.csv" << endl;
            }
            gettimeofday(&finish, NULL); // stops the time
            double elapsed = (finish.tv_sec - start.tv_sec) + ((finish.tv_usec - start.tv_usec)/1000000.0); // this calculates the elapsed time in seconds
            totalDataTime += elapsed;
            cout << "The time it took to collect all of the data points was " << elapsed << " seconds." << endl << endl;
        }

        if (fileCheck == 1) {
            bool done  = false;
            if (bufferCheck) { // the following cond. statemend determine the amoun fo requests to the server based on the file length and -m
                requests = ceil(double(byteSize - size1)/double(bufferSize));
                size2 = bufferSize;
            }
            else {
                requests = ceil(double(byteSize - size1)/double(MAX_MESSAGE));
                size2 = MAX_MESSAGE;
            }
            // cout << "size1: " << size1 << " size2 " << size2 << " bytesize: " << byteSize << " req " << requests << endl;
            struct timeval start, finish; // this is to get the time it took for the file to transfer
            gettimeofday(&start, NULL);
            if (i == 0) {
                ofstream outfile ("received/" + userFileName, ios::out | ios::binary); // this will be the output file
                for (int j = 0; j < requests; j++) { // this will iterate based on the requests amount
                    if (j < requests - 1) {
                        // cout << "i: " << i << " j: " << j << " size1: " << size1 << " size2: "  << size2 << " bytesize: " << byteSize << " req: " << requests << endl;
                        filemsg fm(size1, size2);
                        char buffer [sizeof (filemsg) + userFileName.size() + 1];
                        memcpy (buffer, &fm, sizeof (filemsg));
                        strcpy (buffer + sizeof (filemsg), userFileName.c_str());
                        chan->cwrite(buffer, sizeof (buffer)); // writes to server

                        char data [size2];
                        chan->cread(data, sizeof (data)); // gets data from server
                        outfile.write ((char *) &data, size2); // writes that data to the output file
                        size1 += size2;
                    }
                    else { // on the last request, a change has to be made to the filemsg length
                        size2 = byteSize - size1;
                        // cout << "last i: " << i << " j: " << j << " size1: " << size1 << " size2: "  << size2 << " bytesize: " << byteSize << " req: " << requests << endl;                        filemsg fm(size1, size2);
                        char buffer [sizeof (filemsg) + userFileName.size() + 1];
                        memcpy (buffer, &fm, sizeof (filemsg));
                        strcpy (buffer + sizeof (filemsg), userFileName.c_str());
                        chan->cwrite(buffer, sizeof (buffer)); // writes to server

                        char data [size2];
                        chan->cread(data, sizeof (data)); // gets data from server
                        outfile.write ((char *) &data, size2); // writes that data to the output file
                    }
                }
                outfile.close(); // ensure to close output file
                size1 = byteSize + 1;
                byteSize += byteSize2;
                // cout << size1 << " " << byteSize << endl;
                // break;
            }
            else {
                ofstream outfile ("received/" + userFileName, ios::out | ios::binary | ios_base::app); // this will be the output file
                if (i < nchannels - 1) {
                    for (int j = 0; j < requests; j++) { // this will iterate based on the requests amount
                        if (j < requests - 1) {
                            // cout << "i: " << i << " j: " << j << " size1: " << size1 << " size2: "  << size2 << " bytesize: " << byteSize << " req: " << requests << endl;
                            filemsg fm(size1, size2);
                            char buffer [sizeof (filemsg) + userFileName.size() + 1];
                            memcpy (buffer, &fm, sizeof (filemsg));
                            strcpy (buffer + sizeof (filemsg), userFileName.c_str());
                            chan->cwrite(buffer, sizeof (buffer)); // writes to server

                            char data [size2];
                            chan->cread(data, sizeof (data)); // gets data from server
                            outfile.write ((char *) &data, size2); // writes that data to the output file
                            size1 += size2;
                        }
                        else { // on the last request, a change has to be made to the filemsg length
                            size2 = byteSize - size1;
                            // cout << "last " << endl;
                            // cout << "i: " << i << " j: " << j << " size1: " << size1 << " size2: "  << size2 << " bytesize: " << byteSize << " req: " << requests << endl;
                            filemsg fm(size1, size2);
                            char buffer [sizeof (filemsg) + userFileName.size() + 1];
                            memcpy (buffer, &fm, sizeof (filemsg));
                            strcpy (buffer + sizeof (filemsg), userFileName.c_str());
                            chan->cwrite(buffer, sizeof (buffer)); // writes to server

                            char data [size2];
                            chan->cread(data, sizeof (data)); // gets data from server
                            outfile.write ((char *) &data, size2); // writes that data to the output file
                        }
                    }
                }
                else { // on the last request, a change has to be made to the filemsg length
                    for (int j = 0; j < requests; j++) { // this will iterate based on the requests amount
                        if (j < requests - 1) {
                            // cout << "i: " << i << " j: " << j << " size1: " << size1 << " size2: "  << size2 << " bytesize: " << byteSize << " req: " << requests << endl;
                            filemsg fm(size1, size2);
                            char buffer [sizeof (filemsg) + userFileName.size() + 1];
                            memcpy (buffer, &fm, sizeof (filemsg));
                            strcpy (buffer + sizeof (filemsg), userFileName.c_str());
                            chan->cwrite(buffer, sizeof (buffer)); // writes to server

                            char data [size2];
                            chan->cread(data, sizeof (data)); // gets data from server
                            outfile.write ((char *) &data, size2); // writes that data to the output file
                            size1 += size2;
                        }
                        else { // on the last request, a change has to be made to the filemsg length
                            size2 = byteSize - size1;
                            // cout << "last " << endl;
                            // cout << "i: " << i << " j: " << j << " size1: " << size1 << " size2: "  << size2 << " bytesize: " << byteSize << " req: " << requests << endl;
                            filemsg fm(size1, size2);
                            char buffer [sizeof (filemsg) + userFileName.size() + 1];
                            memcpy (buffer, &fm, sizeof (filemsg));
                            strcpy (buffer + sizeof (filemsg), userFileName.c_str());
                            chan->cwrite(buffer, sizeof (buffer)); // writes to server

                            char data [size2];
                            chan->cread(data, sizeof (data)); // gets data from server
                            outfile.write ((char *) &data, size2); // writes that data to the output file
                        }
                    }
                }
                outfile.close(); // ensure to close output file
                size1 = byteSize + 1;
                byteSize += byteSize2;
                if (byteSize > filelen) {
                    byteSize = filelen;
                }
            }
            gettimeofday(&finish, NULL); // ends the time counter
            double elapsed = (finish.tv_sec - start.tv_sec) + ((finish.tv_usec - start.tv_usec)/1000000.0); // converts the time to seconds
            totalFileTrans += elapsed;
            cout << "The time it took to transfer part of the data was " << elapsed << " seconds." << endl;
        }
    }
    if (fileCheck == 1) {
        cout << "Your new file will be received/" << userFileName << endl;
        cout << "The time it took to transfer all of the data was " << totalFileTrans << " seconds." << endl;
    }
    delete x;
    if (totalDataTime != 0 && nchannels > 1) {
        cout << "The total time it to took to gather 1k data points for " << nchannels << " channels was " << totalDataTime << " seconds." << endl;
    }
    if (totalTransTime != 0 && nchannels > 1) {
        cout << "The total time it took to transfer the file for " << nchannels << " channels was " << totalTransTime << " seconds." << endl;
    }
    //closing the channel
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite (&q, sizeof (MESSAGE_TYPE));

    if (chan != control_chan){ // this means that the user requested a new channel, so the control_channel must be destroyed as well 
        control_chan->cwrite (&q, sizeof (MESSAGE_TYPE));
    }
    wait (0);
    for (int i = 0; i < channels.size(); i++) {
        delete channels[i];
    }
}
