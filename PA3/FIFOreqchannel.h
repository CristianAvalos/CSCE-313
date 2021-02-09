
#ifndef _FIFOreqchannel_H_
#define _FIFOreqchannel_H_

#include "common.h"
#include "Reqchannel.h"
class FIFORequestChannel: public RequestChannel {
public:
	FIFORequestChannel(const string _name, const Side _side);
	~FIFORequestChannel();
	int cread (void* msgbuf, int bufcapacity);
	int cwrite(void *msgbuf , int msglen);
	int open_ipc(string name, int mode);
};

#endif