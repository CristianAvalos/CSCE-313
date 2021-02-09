#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
#include <stdio.h>
using namespace std;

FIFORequestChannel* create_new_channel(FIFORequestChannel* mainChannel) {
    char name[1024];
    MESSAGE_TYPE m = NEWCHANNEL_MSG;
    mainChannel->cwrite(&m, sizeof (m));
    mainChannel->cread(name, 1024);
    FIFORequestChannel* newChan = new FIFORequestChannel(name, FIFORequestChannel::CLIENT_SIDE);
    return newChan;
}

void patient_thread_function(int n, int pNumber, BoundedBuffer* requestBuf) {
    /* What will the patient threads do? */
    datamsg d(pNumber, 0.0, 1);
    for (int i = 0; i < n; i++) {
        requestBuf->push((char*) &d, sizeof (datamsg));
        d.seconds += 0.004;
    }
}

void file_thread_function(string fileName, BoundedBuffer* requestBuf, FIFORequestChannel* chan, int mb) {
    // 1. create the file
    string recvName = "recv/" + fileName;
    // make it as long as the original length
    char buf[1024];
    filemsg f (0,0);
    memcpy(buf, &f, sizeof (f));
    strcpy(buf + sizeof(f), fileName.c_str());
    chan->cwrite(buf, sizeof (f) + fileName.size() + 1);

    __int64_t fileLength;
    chan->cread (&fileLength, sizeof (fileLength));
    FILE* fp = fopen(recvName.c_str(), "w");
    fseek(fp, fileLength, SEEK_SET);
    fclose(fp);
    // 2. generate all of the file messages
    filemsg* fm = (filemsg*) buf;
    __int64_t remlen = fileLength;
    while (remlen > 0) {
        fm->length = min(remlen, (__int64_t) mb);
        requestBuf->push(buf, sizeof (filemsg) + fileName.size() + 1);
        fm->offset += fm->length;
        remlen -= fm->length;
    }
}

void worker_thread_function(FIFORequestChannel* chan, BoundedBuffer* requestBuf, HistogramCollection* hc, int mb){
    /* Functionality of the worker threads*/
    char buf[1024];
    double response = 0.0;
    char recvbuf[mb];
    while(true) {
        requestBuf->pop(buf, 1024);
        MESSAGE_TYPE* m = (MESSAGE_TYPE *) buf;
        if (*m == DATA_MSG) {
            chan->cwrite(buf, sizeof (datamsg));
            chan->cread(&response, sizeof (double));
            hc->update(((datamsg*) buf)->person , response);
        }
        else if(*m == QUIT_MSG) {
            chan->cwrite(m, sizeof (MESSAGE_TYPE));
            delete chan;
            break;
        }
        else if(*m == FILE_MSG) {
            filemsg* fm = (filemsg*) buf;
            string fileName = (char*)(fm + 1);
            int size = sizeof (filemsg) + fileName.size() + 1;
            chan->cwrite(buf, size);
            chan->cread(recvbuf, mb);

            string recvName = "recv/" + fileName;
            FILE* fp = fopen(recvName.c_str(), "r+");
            fseek(fp, fm->offset, SEEK_SET);
            fwrite(recvbuf, 1, fm->length, fp);
            fclose(fp);
        }
    }
}

int main(int argc, char *argv[])
{
    int n = 15000;    // default number of requests per "patient"
    int p = 1;     // number of patients [1,15]
    int w = 200;    // default number of worker threads
    int b = 500; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the message buffer
    string filename = "10.csv";
    bool bufferCheck = false, fileCheck = false;
    srand(time_t(NULL));
    
    int opt = -1;
    while ((opt = getopt(argc, argv, "n:p:w:b:m:f:")) != -1) {
        switch (opt) {
            case 'n':
                n = atoi(optarg);
                break;
            case 'p':
                p = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'm':
                m = atoi(optarg);
                bufferCheck = true;
                break;
            case 'f':
                filename = optarg;
                fileCheck = true;
                break;
        }
    }
    int pid = fork();
    if (pid == 0){
		// modify this to pass along m
        // execl ("server", "server", (char *)NULL);
        if (bufferCheck) {
            char* arg[] = {"./server", "-m", (char*) to_string(m).c_str(), NULL};
            execvp("./server", arg);
        }
        else {
            char* arg[] = {"", NULL};
            execvp("./server", arg);
        }
    }
    
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer request_buffer(b);
	HistogramCollection hc;
	
    // making histograms and adding them to the collection
    for (int i = 0; i < p; i++) {
        Histogram* hist = new Histogram(10, -2.0, 2.0);
        hc.add(hist);
    }
	
    // making worker channels
    FIFORequestChannel* wchans[w];
    for (int i = 0; i < w; i++) {
        wchans[i] = create_new_channel(chan);
    }
	
    struct timeval start, end;
    gettimeofday (&start, 0);
    /* Start all threads here */
    thread patient[p];
    if (fileCheck == false) {
        for (int i = 0; i < p; i++) {
            patient[i] = thread(patient_thread_function, n, i + 1, &request_buffer);
        }
    }
    // if (fileCheck == true) {
        thread filethread (file_thread_function, filename, &request_buffer, chan, m);
        // filethread.join();
    // }
    thread workers[w];
    for (int i = 0; i < w; i++) {
        workers[i] = thread(worker_thread_function, wchans[i], &request_buffer, &hc, m);
    }

	/* Join all threads here */
    if (fileCheck == false) {
        for (int i = 0; i < p; i++) {
            patient[i].join();
        }
    }
    filethread.join();
    cout << "Patient/File thread finished." << endl;
    for (int i = 0; i < w; i++) {
        MESSAGE_TYPE q = QUIT_MSG;
        request_buffer.push((char*) &q, sizeof (q));
    }
    for (int i = 0; i < w; i++) {
        workers[i].join();
    }
    cout << "Worker thread finsihed." << endl;
    gettimeofday (&end, 0);

    // print the results
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;   
}