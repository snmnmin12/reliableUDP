/**
By MSong
**/

#pragma once 

#define FORWARD_PATH 0
#define RETURN_PATH  1

#define STATUS_OK 0 // no error
#define ALREADY_CONNECTED 1 // second call to ss.Open() without closing connection
#define NOT_CONNECTED 2 // call to ss.Send()/Close() without ss.Open()
#define INVALID_NAME 3 // ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND 4 // sendto() failed in kernel
#define TIMEOUT 5 // timeout after all retx attempts are exhausted
#define FAILED_RECV 6 // recvfrom() failed in kernel
#define FAILED_SELECT 7


#define MAX_ATTEMPTS 50 
#define MAX_ATTEMPTS_FIN 5

#include <string>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <cmath>
#include <cstring>
#include <thread>

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <queue>
#include <pthread.h>

#include "util.h"

using namespace std;
using namespace std::chrono;


class Timer {
	public:
	  // create new fd for timer,
	  // add to epollfd
	  Timer(int epollfd);
	  // reset timer, will raise an event
	  // after sec seconds and nsec nanoseconds
	  void startTimer(double secs); 
	  int getFd() {return fd;} // return file description
	  ~Timer() {close(this->fd);}
	private:
	  int fd; // file description
	  epoll_event event;
};


class SenderSocket {
public:
	SenderSocket();
	~SenderSocket();
	unsigned short Open(char* host, int _port, int _senderWindow, LinkProperties* lp);
	unsigned short Send(char* str, unsigned int size);
	unsigned short Close();
	void statWorker();
	void Worker();
	void Start();
	void End();

	double RTO, estRTT, devRTT;
	unsigned int lastchecksum;
private:
	int sockt;
	// struct sockaddr_in local;
	struct sockaddr_in remote;
	unsigned int seq;
	unsigned int sndBase;
	unsigned int effectiveWindow;
	unsigned int lastReleased;
	unsigned int W;
	unsigned int fastrtx;
	unsigned int numtime;
	unsigned int nReleased;
	unsigned int lastAck;
	unsigned int epollfd;
	Timer *ptimer;
	Time time;
	char* buff;

	bool hasStarted = false;

	time_point<std::chrono::high_resolution_clock> start, start_transfer;

	//bounded bufer and thread
	// queue< time_point<std::chrono::high_resolution_clock> > clocks;
	// queue<Packet> boundedBuffer;
	Packet *boundedBuffer;
	thread* sta, *wk, *pr;
	bool isShuttleDown;
	Semaphore sem, full, pendingData;
	condition_variable nopacket, haspacket;
	mutex cv_m;
	// mutex  m;
	// condition_variable cv;
};
