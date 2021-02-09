#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "TCPreqchannel.h"
#include <stdio.h>
#include <thread>
#include <sys/epoll.h>
#include <unordered_map>
#include <fcntl.h>
#include <vector>
using namespace std;

void patient_thread_function(int n, int pNumber, BoundedBuffer* requestBuf) {
    /* What will the patient threads do? */
    datamsg d(pNumber, 0.0, 1);
    for (int i = 0; i < n; i++) {
        requestBuf->push((char*) &d, sizeof (datamsg));
        d.seconds += 0.004;
    }
}

void file_thread_function(string fileName, BoundedBuffer* requestBuf, TCPRequestChannel* chan, int mb) {
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

void event_polling_function(int w, int mb, TCPRequestChannel** wchans, BoundedBuffer* requestBuf, HistogramCollection* hc){
    char buf[1024];
    double response = 0.0;
    char recvbuf[mb];

    struct epoll_event ev;
    struct epoll_event events[w];
    // create an empty epoll list
    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        EXITONERROR("epoll_create1");
    }
    unordered_map<int, int> fd_to_index;
    vector<vector<char>> state(w);
    // priming + adding each fd to the list
    int nsent = 0, nrecv = 0;
    for (int i = 0; i < w; i++) {
        int size = requestBuf->pop(buf, 1024);
        if (*(MESSAGE_TYPE*) buf == QUIT_MSG) {
            break;
        }
        wchans[i]->cwrite(buf, size);
        state[i] = vector<char>(buf, buf + size);
        nsent++;
        int rfd = wchans[i]->getfd();

        fcntl(rfd, F_SETFL, O_NONBLOCK);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = rfd;
        fd_to_index[rfd] = i;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, rfd, &ev) == -1) {
            EXITONERROR("epoll_ctr: listen_sock");
        }
    }

    bool quitRecv = false;
    while (true) {
        if (quitRecv == true && nrecv == nsent) {
            break;
        }
        int nfds = epoll_wait(epollfd, events, w, -1);
        if (nfds == -1) {
            EXITONERROR("epoll_wait");
        }
        for (int i = 0; i < nfds; i++) {
            int rfd = events[i].data.fd;
            int index = fd_to_index[rfd];
            int respSize = wchans[index]->cread(recvbuf, mb);
            nrecv++;
            // process recvbuf
            vector<char> req = state[index];
            char* request = req.data();

            // process response
            MESSAGE_TYPE* m = (MESSAGE_TYPE*) request;
            if (*m == DATA_MSG) {
                hc->update(((datamsg*) request)->person, *(double*) recvbuf);
            } else if (*m == FILE_MSG) {
                filemsg* fm = (filemsg*) request;
                string fileName = (char*)(fm + 1);
                int size = sizeof (filemsg) + fileName.size() + 1;

                string recvName = "recv/" + fileName;
                FILE* fp = fopen(recvName.c_str(), "r+");
                fseek(fp, fm->offset, SEEK_SET);
                fwrite(recvbuf, 1, fm->length, fp);
                fclose(fp);
            }

            // reuse 
            if (!quitRecv) {
                int reqSize = requestBuf->pop(buf, sizeof (buf));
                if (*(MESSAGE_TYPE*) buf == QUIT_MSG) {
                    quitRecv = true;
                }
                else {
                    wchans[index]->cwrite(buf, reqSize);
                    state[index] = vector<char> (buf, buf + reqSize);
                    nsent++;
                }
            }
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
    string host, port;

    int opt = -1;
    while ((opt = getopt(argc, argv, "n:p:w:b:m:f:h:r:")) != -1) {
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
            case 'h':
                host = optarg;
                break;
            case 'r':
                port = optarg;
                break;
        }
    }
    
    TCPRequestChannel* chan = new TCPRequestChannel(host, port);
    BoundedBuffer request_buffer(b);
	HistogramCollection hc;
	
    // making histograms and adding them to the collection
    for (int i = 0; i < p; i++) {
        Histogram* hist = new Histogram(10, -2.0, 2.0);
        hc.add(hist);
    }
	
    // making worker channels
    TCPRequestChannel** wchans = new TCPRequestChannel*[w];
    for (int i = 0; i < w; i++) {
        wchans[i] = new TCPRequestChannel(host, port);
    }
	
    struct timeval start, end;
    gettimeofday (&start, 0);
    /* Start all threads here */
    thread patient[p];
    for (int i = 0; i < p; i++) {
        patient[i] = thread(patient_thread_function, n, i + 1, &request_buffer);
    }

    thread filethread (file_thread_function, filename, &request_buffer, chan, m);

    thread evp (event_polling_function, w, m, wchans, &request_buffer, &hc);

	/* Join all threads here */
    for (int i = 0; i < p; i++) {
        patient[i].join();
    }
    filethread.join();
    cout << "Patient/File thread finished." << endl;
    
    MESSAGE_TYPE quitMsg = QUIT_MSG;
    request_buffer.push((char*) &quitMsg, sizeof (quitMsg));
    evp.join();
    cout << "Worker thread finsihed." << endl;
    gettimeofday (&end, 0);

    // print the results
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    for (int i = 0; i < w; i++) {
        wchans[i]->cwrite(&quitMsg, sizeof (MESSAGE_TYPE));
        delete wchans[i];
    }
    delete [] wchans;
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;   
}