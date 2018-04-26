
#pragma once

#define MAGIC_PORT 22345  // receiver listens on this port 
#define MAX_PKT_SIZE (1500-28) // maximum UDP packet size accepted by receiver 
#define MAGIC_PROTOCOL 0x8311AA
#define events_size 11

#include <mutex> 
#include <condition_variable>
#include <chrono>

#pragma pack(push,1)
class Flags {
public:
	unsigned int reseved:5;
	unsigned int SYN:1;
	unsigned int ACK:1;
	unsigned int FIN:1;
	unsigned int magic:24;
	Flags () { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; } 
};

class SenderDataHeader {
public:
	 Flags flags;
	 unsigned int seq; // must begin from 0
}; 

class ReceiveHeader {
public:
	Flags flags;
	unsigned int recvWnd; // receiver window for flow control (in pkts) 
	unsigned int ackSeq;  // ack value = next expected sequence 
};


class LinkProperties {
public:
	 // transfer parameters
	 float RTT; // propagation RTT (in sec)
	 float speed; // bottleneck bandwidth (in bits/sec)
	 float pLoss [2]; // probability of loss in each direction
	 unsigned int bufferSize; // buffer size of emulated routers (in packets)
	 LinkProperties () { memset(this, 0, sizeof(*this)); } 
	 LinkProperties(float _RTT, float _speed, float _floss, float _bloss) {
	 	RTT = _RTT; speed = _speed; pLoss[0] = _floss; pLoss[1] = _bloss;
	 }
};


class SenderSynHeader {
public:
	SenderDataHeader sdh;
	LinkProperties lp;
};
#pragma pack(pop)



class Semaphore {
private:
	std::mutex m;
	std::condition_variable cv;
	unsigned int count;
	unsigned int semCount;
public:
	explicit Semaphore(unsigned int _maximum=0): count(0), semCount(_maximum) {}

	void create(unsigned int _maximum) {semCount = _maximum;}

	void wait() {
		std::unique_lock<std::mutex> lk(m);
		while(count == 0) {
			cv.wait(lk);
		}
		--count;
	}

	void release(unsigned int k) {
		std::unique_lock<std::mutex> lk(m);
		count = count + k;
	//	count = std::min(count + k, semCount);
		cv.notify_all();
	}

};


class Time {
public:
	Time(){}
	double getTime(std::chrono::time_point<std::chrono::high_resolution_clock> start, std::chrono::time_point<std::chrono::high_resolution_clock> end = std::chrono::high_resolution_clock::now()) {
		    // auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            double sent_time =  duration.count() / 1000000.0;
            return sent_time;
	}
};

class Packet {
public:
	Packet(){
		memset(buff, 0, MAX_PKT_SIZE);
		time    = std::chrono::high_resolution_clock::now();
		len     = 0;
		seq     = 0;	
		rtx     = false;	
	}
	Packet(const char* _buff, unsigned int _seq, std::chrono::time_point<std::chrono::high_resolution_clock> _time, unsigned int _len) {
		memset(buff, 0, MAX_PKT_SIZE);
		memcpy(buff, _buff, _len);
		// buff     = _buff;
		time    = _time;
		len     = _len;
		seq     = _seq;
		rtx		= false;
	}
	char buff[MAX_PKT_SIZE];
	unsigned int seq;
	std::chrono::time_point<std::chrono::high_resolution_clock> time;	
	unsigned int len;
	bool rtx;
};

