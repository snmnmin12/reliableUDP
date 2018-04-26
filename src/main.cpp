// main.cpp
#include "SenderSocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

int main (int argc, char** argv)
{
	if (argc != 8) {
		printf("Usage execution arg\n");
		exit(0);
	}
	// parse command-line parameters
	char *targetHost = argv[1];
	int power = atoi(argv[2]); // command-line specified integer
	int senderWindow = atoi(argv[3]); // command-line specified integer
	float RTT           = atof(argv[4]); // command-line specified rtt time
	float fLoss      = atof(argv[5]); // command-line specified forward loss probability
	float bLoss 	 = atof(argv[6]); // command-line specified reverse loss probability
	float speed    	 = 1e6 * atof(argv[7]); // command-line specified network speed

	LinkProperties lp(RTT, speed, fLoss, bLoss);
	lp.bufferSize  = senderWindow + MAX_ATTEMPTS;

	printf("Main:\tsender W = %d, RTT %.3f sec, loss %g / %g, link %d Mbps\n", senderWindow, RTT, fLoss, bLoss, (unsigned int)(speed / 1e6));

	Time time;

	auto start1 = high_resolution_clock::now();
	unsigned long int dwordBufSize = (long) 1 << power;
	unsigned int *dwordBuf = new unsigned int [dwordBufSize]; // user-requested buffer
	for (unsigned long int i = 0; i < dwordBufSize; i++) // required initialization
		 dwordBuf [i] = i;

    double init_time =  time.getTime(start1);
    printf("Main:\t initializating DWORD array with 2^%d elements... done in %.0f ms\n", power, init_time * 1000);

	SenderSocket ss; // instance of your class
	unsigned short status;

	if ((status = ss.Open (targetHost, htons(MAGIC_PORT), senderWindow, &lp)) != STATUS_OK) {
	 // error handling: print status and quit
		printf("connect failed with status %d\n", status);
		exit(0);
	}
	auto end2 = high_resolution_clock::now();
 //    auto duration2 = duration_cast<microseconds>(end2 - end1);
    double main_time =  time.getTime(start1) - init_time;
    printf("Main:\tconnected to %s in %.3f sec, pkt size %d bytes\n", targetHost, main_time, MAX_PKT_SIZE);

	char *charBuf = (char*) dwordBuf; // this buffer goes into socket
	unsigned long int byteBufferSize = dwordBufSize << 2; // convert to bytes
	unsigned long int off = 0; // current position in buffer

	ss.Start();
	while (off < byteBufferSize)
	{
		 // decide the size of next chunk
		int bytes = min (byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
		// send chunk into socket
		 if ((status = ss.Send (charBuf + off, bytes)) != STATUS_OK) {

		 		printf("transfer data error %d\n", status);
				exit(0);
		 }
		 // error handing: print status and quit
		 off += bytes;
	}

	ss.End();
	
	if ((status = ss.Close ()) != STATUS_OK) {
	 // error handing: print status and quit
		printf("connect failed with status %d\n", status);
		exit(0);
	}

    double transfer_time = time.getTime(end2);
    printf("Main:\ttransfer finished in %.3f sec, %.2f Kbps checksum %X\n", transfer_time, off * 8 / 1000.0 / transfer_time, ss.lastchecksum);
    float rate = senderWindow * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) * 8 / 1000.0 / ss.estRTT;
    printf("Main:\testRTT %.3f sec, ideal rate %.2f Kbps\n", ss.estRTT, rate);
	return 0;
} 