
#ifndef _TCPreqchannel_H_
#define _TCPreqchannel_H_

#include <sys/socket.h>
#include <netdb.h>
#include "common.h"

class TCPRequestChannel
{
private:
	int sockfd;
public:
	TCPRequestChannel(const string host, const string port);

	TCPRequestChannel(int fd);

	~TCPRequestChannel();

	int cread(void* msgbuf, int bufcapacity);

	int cwrite(void *msgbuf , int msglen);
	 
	int getfd();
};

#endif
