/**
By MSong
**/
#include "SenderSocket.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#define debug 0

Timer::Timer(int epollfd) {
      this->fd = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK);
      if (this->fd == -1) 
        throw "Unable to create timerfd!";
      this->event.events = EPOLLIN; // notification is  read event
      this->event.data.fd = this->fd; // user data
      int r = epoll_ctl(epollfd, EPOLL_CTL_ADD, this->fd, &event);
      if (r == -1)
        throw "Unable to add timer to epollfd!";
}

void Timer::startTimer(double fsecs) {
      int sec = std::floor(fsecs);
      int nsec = (int) (1000000000 * (fsecs - sec));
      itimerspec new_timer;
      new_timer.it_interval.tv_sec = 0;
      new_timer.it_interval.tv_nsec = 0;
      new_timer.it_value.tv_sec = sec;
      new_timer.it_value.tv_nsec = nsec;
      int s = timerfd_settime(this->fd, 0, &new_timer, NULL);
      if (s == -1)
        throw "Unable to do timerfd_settime";
}



SenderSocket::SenderSocket() {

	seq    = 0;

	if((sockt = socket(AF_INET, SOCK_DGRAM, 0))<0) {// initializes socket of IP type and of reliable connection 
       printf("Error with %d\n", errno);
       exit(0);
    }
    RTO = 1.0; estRTT = 0.0; devRTT = 0.0;

     sndBase = 0; effectiveWindow = 0;
 	 W = 0; fastrtx = 0; lastReleased = 0;
    numtime = 0;

    memset(&remote, 0, sizeof(struct sockaddr_in));
    start = high_resolution_clock::now();

    isShuttleDown = false;
    buff = new char[MAX_PKT_SIZE];
    boundedBuffer = NULL;
    // memset(&local, 0, sizeof(local));
    // local.sin_family = AF_INET;
   	// local.sin_addr.s_addr = htonl(INADDR_ANY);
    // local.sin_port = 0;

    // if (bind(sockt, (struct sockaddr*)&local, sizeof(local)) < 0) {
    //    printf("bind failed %d\n", errno);
    //    exit(0);
    // }	
}

void SenderSocket::Start() {
    isShuttleDown = false;
    sta = new thread(bind(&SenderSocket::statWorker, this));
    wk = new thread(bind(&SenderSocket::Worker, this));
    pr = new thread([this]() {

    		 //    pthread_t this_thread = pthread_self();
			    // struct sched_param params;
			    // params.sched_priority = sched_get_priority_max(SCHED_FIFO);
			    // if (pthread_setschedparam(this_thread, SCHED_FIFO, &params) != 0) {
			    //      printf("Unsuccessful in setting thread realtime prio\n");
			    // }
    				int nxtSend = 0;
 				fd_set fd;
				int ret = 0;

    				while (!this->isShuttleDown) {

    					this->full.wait();
					    FD_ZERO (&fd);
					    FD_SET (this->sockt, &fd);
					    timeval tp = {(int)this->RTO, (int)((this->RTO - int(this->RTO)) * 1000000)};
					    ret = select(this->sockt + 1, 0, &fd, NULL, &tp);

					    Packet& pkt = this->boundedBuffer[nxtSend % this->W];
					    pkt.time    = std::chrono::high_resolution_clock::now();
	if (debug)
						printf("trasmit %u with  %u /50\n", nxtSend, 0);
					   	sendto(sockt, pkt.buff, pkt.len, 0, (struct sockaddr*)&this->remote, sizeof(this->remote));
				 	    if (!this->hasStarted) {
							this->hasStarted = true;
								this->pendingData.release(1);

				        	}
	   	nxtSend++;
				   }
    });
    // pthread_getschedparam(pthread_self(), &policy, &sch);

}

void SenderSocket::End() {

	// printf("Ending the threads now\n");
	isShuttleDown = true;

	if (this->wk) {
    	wk->join();
    	delete wk;
	}
	if (this->sta) {
   		sta->join();
    	delete sta;
    }

}

SenderSocket::~SenderSocket() {
	// if (!sem) {
	// 	delete sem;
	// }
	if (!boundedBuffer) {
		delete[] boundedBuffer;
	}
	if (!buff) {
		delete[] buff;
	}
}

unsigned short SenderSocket::Open(char* host, int _port, int _senderWindow, LinkProperties* lp) {

    if (remote.sin_port != 0) {
        return ALREADY_CONNECTED;
    }

    in_addr_t IP = inet_addr(host);
    memset(&remote, 0, sizeof(remote));

	struct hostent *addr; 
	
	if (IP == INADDR_NONE)
	{	
		if ((addr = gethostbyname (host)) == NULL) {
            auto sent_time = time.getTime(start);
			printf("[ %.3f] --> target %s is invalid\n", sent_time, host);
            return INVALID_NAME;
		}
		else {
			memcpy ((char *)&(remote.sin_addr), addr->h_addr, addr->h_length);
		}
	} else {
		remote.sin_addr.s_addr = IP;
	}
   	
    // sem  = new Semaphore(_senderWindow);
    sem.create(_senderWindow);
    full.create(_senderWindow);
    pendingData.create(1);

    //create epollfd
    this->epollfd = epoll_create(events_size);
    if (this->epollfd == -1) {
        printf("unable to create epollfd\n");
        return TIMEOUT;
    }

    ptimer = new Timer(epollfd);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sockt;
    int r  = epoll_ctl(epollfd, EPOLL_CTL_ADD, sockt, &event);

    if (r == -1)  {
        printf("unable to add epollfd\n");
        return TIMEOUT;
    }

    boundedBuffer = new Packet[_senderWindow];

    lp->bufferSize = _senderWindow +  MAX_ATTEMPTS;

    W = _senderWindow;

    remote.sin_family = AF_INET;
    remote.sin_port = _port;

    SenderSynHeader ssh;
    ssh.sdh.flags.SYN = 1;
    ssh.sdh.seq = 0;

    memcpy(&(ssh.lp), lp, sizeof(LinkProperties));
 	char *synbuf = (char*)&ssh;
    char *rcvbuf = new char[sizeof(ReceiveHeader)];

    int count = 1;
    RTO = max((float)1.0, 2 * lp->RTT);
	// time (&start);

    int kernelBuffer = 20e6;                    
    if (setsockopt(sockt, SOL_SOCKET, SO_RCVBUF, (char *)&kernelBuffer, (int)sizeof(int)) < 0)
    {
        printf("failed recvfrom (open) with \n");
        return TIMEOUT;
    }
    if (setsockopt(sockt, SOL_SOCKET, SO_SNDBUF, (char *)&kernelBuffer, (int)sizeof(int)) < 0)
    {
        printf("failed sendto (open) with \n");
        return TIMEOUT;
    }    

    int ret = 0;
    while (count <= MAX_ATTEMPTS) {

        auto sent_time = time.getTime(start);
        printf("[ %.3f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n", sent_time, ssh.sdh.seq, count, MAX_ATTEMPTS, RTO, inet_ntoa(remote.sin_addr));

    	//send it from
    	if ((ret=sendto(sockt, synbuf, sizeof(SenderSynHeader), 0, (struct sockaddr*)&remote, sizeof(remote))) < 0) {
            auto sent_time = time.getTime(start);
            printf("[ %.3f] --> failed sendto with %d\n", sent_time, FAILED_SEND);
	    	return FAILED_SEND;
    	}

        // initialize the fd values
    	fd_set fd;
    	FD_ZERO (&fd);
    	FD_SET (sockt, &fd);
    	timeval tp = {1, 0};
    	ret = select(sockt + 1, &fd, NULL, NULL, &tp);

    	if (ret > 0) {

    		socklen_t size = sizeof(remote);
    		if((ret = recvfrom(sockt, rcvbuf, sizeof(ReceiveHeader), 0, (struct sockaddr*)&remote, &size)) < 0) {

                auto sent_time = time.getTime(start);
                printf("[ %.3f] --> failed recvrom with %d\n", sent_time, FAILED_RECV);
                return FAILED_RECV;
            }

    		ReceiveHeader *rcvheader = (ReceiveHeader*)rcvbuf;
    		lastReleased = min(W, rcvheader->recvWnd);
    		sem.release(lastReleased);

            auto recv_t = time.getTime(start);
            double delta_t = recv_t - sent_time;

            RTO = delta_t * 3.0;
    		printf("[ %.3f] <-- SYN-ACK %u window %u; setting intial RTO to %.3f\n", recv_t, rcvheader->ackSeq, rcvheader->recvWnd, RTO);
    		
            start_transfer = high_resolution_clock::now();
            return STATUS_OK;

    	}  else if (ret == 0) {
            count++;
            tp.tv_sec = 1;
            tp.tv_usec = 0;
        } else {

            auto sent_time = time.getTime(start);
            printf("[ %.3f] --> failed select with %d\n", sent_time, FAILED_SELECT);
            return FAILED_SELECT;
        }

    }

	return TIMEOUT;
}

unsigned short SenderSocket::Send(char* str, unsigned int size)  {

    sem.wait();

    // char* buff = new char[MAX_PKT_SIZE];

    memset(buff, 0, MAX_PKT_SIZE);

    SenderDataHeader sd;

    sd.seq = this->seq;
    memcpy(buff, &sd, sizeof(SenderDataHeader));
    memcpy(buff + sizeof(SenderDataHeader), str, size);

    // fd_set fd;
    // int ret = 0;
    // FD_ZERO (&fd);
    // FD_SET (sockt, &fd);
    // timeval tp = {(int)RTO, (int)((RTO - int(RTO)) * 1000000)};
    // ret = select(sockt + 1, 0, &fd, NULL, &tp);

    // if (ret > 0) {

        // if (sendto(sockt, buff, size + sizeof(SenderDataHeader), 0, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
        // 	printf("Sending Data Error\n");
        //     return FAILED_SEND;
        // }

        // bytesSend += size + sizeof(SenderDataHeader);
        // Packet pkt = {buff, seq, high_resolution_clock::now(), (unsigned int)(size + sizeof(SenderDataHeader))};
        boundedBuffer[seq % W] = {buff, seq, high_resolution_clock::now(), (unsigned int)(size + sizeof(SenderDataHeader))};
        seq++;
        // cv.notify_all();
        full.release(1);
     //    if (!hasStarted) {
     //     	hasStarted = true;
     //     	pendingData.release(1);
    	// }
        return STATUS_OK;

    // } else if (ret == 0) {
    //     printf("timeout in send select call\n");
    //     return TIMEOUT;

    // } else {

    //     printf("failed in select with %d\n", FAILED_SELECT);
    //     return FAILED_SELECT;
    // }
}

unsigned short SenderSocket::Close() {

    if (sockt == -1) {
        return NOT_CONNECTED;
    }

    SenderSynHeader ssh;
    ssh.sdh.flags.FIN = 1;
    ssh.sdh.seq = seq;
    int count = 1;
    int ret = 1;

    char *synbuf = (char*)&ssh;
    char *rcvbuf = new char[sizeof(ReceiveHeader)];

    while (count <= MAX_ATTEMPTS) {

        double sent_time = time.getTime(start);
        printf("[ %.3f] --> FIN %d (attempt %d of %d, RTO %.3f)\n", sent_time, ssh.sdh.seq, count, MAX_ATTEMPTS, RTO);

        //send it from
        if (sendto(sockt, synbuf, sizeof(SenderSynHeader), 0, (struct sockaddr*)&remote, sizeof(remote)) < 0) {

            double sent_time = time.getTime(start);
            printf("[ %.3f] --> failed sendto with %d\n", sent_time, FAILED_SEND);
            close(sockt);
            sockt = -1;
            return FAILED_SEND;
        }

        // initialize the fd values
        fd_set fd;
        FD_ZERO (&fd);
        FD_SET (sockt, &fd);
        timeval tp = {(int)RTO, (int)((RTO - int(RTO)) * 1000000)};
        ret = select(sockt + 1, &fd, NULL, NULL, &tp);

        if (ret > 0) {
            socklen_t size = sizeof(remote);
            if((ret = recvfrom(sockt, rcvbuf, sizeof(ReceiveHeader), 0, (struct sockaddr*)&remote, &size)) < 0) {
               
                double sent_time = time.getTime(start);
                printf("[ %.3f] --> failed recvrom with %d\n", sent_time, FAILED_RECV);
                close(sockt);
                sockt = -1;
                return FAILED_RECV;
            }

            ReceiveHeader *rcvheader = (ReceiveHeader*)rcvbuf;

            if (rcvheader->flags.FIN == 1 && rcvheader->flags.ACK == 1) {

                double recv_t = time.getTime(start);
                printf("[ %.3f] <-- FIN %u window %X\n", recv_t, rcvheader->ackSeq, rcvheader->recvWnd);
                lastchecksum = rcvheader->recvWnd;
                return STATUS_OK;

            } else {

                count++;
            }

            // printf("Main:\tconnected to %s in %.3f sec, pkt size %d bytes\n", host, delta_t, MAX_PKT_SIZE);

        }  else if (ret == 0) {
            count++;
        } else {
 
            double sent_time = time.getTime(start);

            printf("[ %.3f] --> failed select with %d\n", sent_time, FAILED_SELECT);
            return FAILED_SELECT;
        }

    }

	return TIMEOUT;
}


void SenderSocket::Worker() {

	//set the thread priority to highest
    //pthread_t this_thread = pthread_self();
   // struct sched_param params;
     //params.sched_priority = sched_get_priority_max(SCHED_FIFO);
     //params.sched_priority = 4;
    // if (pthread_setschedparam(this_thread, SCHED_RR, &params) != 0) {
    //      printf("Unsuccessful in setting thread realtime priority\n");
    // }

    int dupAck = 0, rtx = 0 ,ret = 0;
    double alpha = 0.125, beta = 0.25;

    epoll_event events[events_size];

    ReceiveHeader *rh;
    char* rcvbuf = new char[sizeof(ReceiveHeader)];
    memset(rcvbuf, 0, sizeof(ReceiveHeader));

   timeval tp = {(int)RTO, (int)((RTO - int(RTO)) * 1000000)};

   // this->ptimer->startTimer(RTO);
    if (!hasStarted) {
	pendingData.wait();
    }
    //printf("RTO is %f\n", RTO);
    this->ptimer->startTimer(RTO);
    while (true) { 

            if (this->isShuttleDown && this->sndBase == this->seq ) {
                break;
            }  
	   
	   //if (!hasStarted) {
	//	pendingData.wait();
	//	if (debug)
	//	printf("start Timer");	
	//	this->ptimer->startTimer(RTO);
	  //  }
            // fd_set fd;
            // FD_ZERO (&fd);
            // FD_SET (sockt, &fd);

            // ret = select(sockt + 1, &fd, NULL, NULL, &tp);
             int r = epoll_wait(this->epollfd, events, events_size, -1);

            for (int i = 0; i < r; ++i) {

                    if (events[i].data.fd == this->ptimer->getFd()) { // timeout event
                            // remove the timeout event
                            size_t s;
                            read(ptimer->getFd(), &s, sizeof(s));

                            //resent the send base packet
                            Packet& pkt = boundedBuffer[sndBase % W];
                            pkt.rtx = true;
			    if (debug)
			    printf("Timeout Retrasmit seq number %u with %u / 50\n", sndBase,  rtx);		   
                            if (isShuttleDown) {
				//printf("shutdown called\n");
				int diff = seq - sndBase;
				 for (int i = 0; i <  diff; i++) {
				  Packet& pkt = boundedBuffer[(sndBase+i) % W];
 				  pkt.rtx = true;
			   	  sendto(sockt, pkt.buff, pkt.len, 0, (struct sockaddr*)&remote, sizeof(remote));
				}
                            } else {
				    sendto(sockt, pkt.buff, pkt.len, 0, (struct sockaddr*)&remote, sizeof(remote));
		
			    }
			    this->ptimer->startTimer(RTO);
                            // printf("the sequence in timeout is %u\n", pkt.seq);
                            // count++;
                            numtime++;
                            rtx++;

                    } else {


                                socklen_t size = sizeof(remote);
                                if((ret = recvfrom(sockt, rcvbuf, sizeof(ReceiveHeader), 0, (struct sockaddr*)&remote, &size)) < 0) {
                                    printf("Received error in worker thread\n");
                                }
                                auto rcvTime = std::chrono::high_resolution_clock::now();
                                rh = (ReceiveHeader*) rcvbuf;

                                if (rh->flags.FIN == 1) {
                                    printf("Closing now\n");
                                    break;
                                }
				if (debug)
                                printf("Ack seq is %u\n", rh->ackSeq);
                                unsigned int R = rh->recvWnd;
                                lastAck = rh->ackSeq;
                                if (rh->ackSeq > sndBase) {

                                    int diff = rh->ackSeq - sndBase;
                                    // for (int i = 0; i < diff; i++) {

                                    //  Packet packet = boundedBuffer[(sndBase + i) % W];
                                    //  // Packet packet = boundedBuffer.front();
                                    //     // boundedBuffer.pop();
                                    //     //not the retransmit case
                                    // }
                                    bool hasrtx = false;
                                    for (int i = 0; i < diff; i++) {
                                        Packet& packet = boundedBuffer[(sndBase+i) % W];
                                        if (packet.rtx) {
                                            hasrtx = true;
                                        }
                                    }

                                    sndBase = rh->ackSeq;
                                    
                                    if (!hasrtx) {
                                            Packet& packet = boundedBuffer[(sndBase-1) % W];
                                            double sampledRTT = time.getTime(packet.time, rcvTime);
                                            // printf("sampledRTT %f\n", sampledRTT);
                                            estRTT = (1 - alpha) * estRTT + alpha * sampledRTT;
                                            devRTT = (1 - beta) * devRTT + beta * abs(sampledRTT - estRTT);
                                            RTO = estRTT + 4 * max(devRTT, 0.010);
                                            tp = {(int)RTO, (int)((RTO - int(RTO)) * 1000000)};
                                    }
                                    effectiveWindow = min(W, R);
                                    // sndBase = rh->ackSeq;
                                    int newReleased = sndBase + effectiveWindow - lastReleased;
                                    if (newReleased < 0) {
                                        printf("newrelased is less than 0\n");
                                    } 
                                    size_t s;
                                    read(ptimer->getFd(), &s, sizeof(s));
                                    this->ptimer->startTimer(RTO);

                                    this->sem.release(newReleased);
                                    lastReleased += newReleased;
                                    nReleased = newReleased;
                                    dupAck = 0;
                                    rtx    = 0;

                                } else if (rh->ackSeq == sndBase ) {

                                        ++dupAck;

                                        if (rtx > 50) {
                                            printf("Maximum Attempt Reached\n");
                                            break;
                                        }

                                        if (dupAck == 3) {
                                            //retransmite after 3 times 
                                            // Packet pkt = boundedBuffer.front();
                                            Packet& pkt = boundedBuffer[sndBase % W];
                                            pkt.rtx = true;
					   if (debug)
					    printf("Fast Retrasmit seq number %u with  %u / 50\n", sndBase, rtx);
                                            sendto(sockt, pkt.buff, pkt.len, 0, (struct sockaddr*)&remote, sizeof(remote));
                                            fastrtx++;
                                            rtx++;
                                            size_t s;
                                            read(ptimer->getFd(), &s, sizeof(s));
                                            this->ptimer->startTimer(RTO);
                                        }

                                } 


                }
            }

    }
    nopacket.notify_all();
}

void SenderSocket::statWorker() {

	// timeval tx = {2, 0};
	mutex mtx;
	unique_lock<mutex> lck(mtx);
	auto preBase   = 0;
	auto last_time = start_transfer;
	auto dataSize = MAX_PKT_SIZE - sizeof(SenderDataHeader);
	while (nopacket.wait_for(lck,std::chrono::seconds(2))== cv_status::timeout) {
		
	auto elapse1 = time.getTime(this->start);
    	auto elapsed = time.getTime(last_time);
    	auto bytesAck = (this->sndBase) * 1.0 * dataSize / 1000 / 1000;
    	float speed  = (this->sndBase - preBase) * 8 * dataSize / (elapsed * 1e6);
   		preBase = this->sndBase;
   		last_time    =  std::chrono::high_resolution_clock::now();
    	printf("[%5.0f] B %9u (%5.1f MB) N %9u T %u F %u W %u S %5.3f Mbps RTT %5.3f LR %u nR %u \n",//BS %u\n",
    	elapse1, this->sndBase, bytesAck, this->seq + 1, this->numtime, this->fastrtx, this->effectiveWindow, speed , 
    	this->estRTT, this->lastAck, this->nReleased);//, this->boundedBuffer.size());

	}
}   
