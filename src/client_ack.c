/* UDP client in the internet domain */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>

#define data_size 1400
#define UDP_BURST 1
#define BAD_SERVER_ADDR "10.1.1.2"
#define GOOD_SERVER_ADDR "10.1.2.3"

/*
PACKET TYPE DESCRIPTIONS (to enumerate later)
type 0: client --> server: init_packet
type 1: client --> server: normal-sized data packets
type 2: client --> server: client request server's missing packet (ack) sequence
type 2: server --> client: server sends updated missing packet (ack) sequence 
type 3: server --> client: receiver ACKs everything except last packet
type 4: server --> client: ACK - init_packet receipt
type 5: server --> client: entire transaction done
type 6: client --> server: final data packet
*/

struct packet{
	uint8_t type;
	int sequence_number;
	char data[data_size];
};

struct Init_PACKET{
	uint8_t type;
	u_int file_size;
	u_int chunk_size;
};

struct ack_packet{
	uint8_t type;
	int sequenceNumber;
	uint8_t packet_tracker[data_size];
};

struct full_ack{
	int numAckPackets;
	int currentChunk;
	uint8_t* full_packet_tracker;
};

void error(const char *);

int main(int argc, char *argv[])
{
   // file initializationa
	FILE * pFile;
	FILE *f2;
	long lSize;

	char data[60000];
	size_t result;
	int chunks;
	int final_chunk;
	struct packet data_packet = {0};
	char * packet_tobe_sent;
	char buffer1[256];
	bool state_ch = 1;
	
	// control init
	struct Init_PACKET init_packet = {0};
	char * send_buffer;
	struct ack_packet *ack_packet1;
	struct ack_packet ack_packet2;

	// socket initializations
	int sock, n;
	unsigned int length;
	struct sockaddr_in server, from;
	struct hostent *hp;



	// filename
	if (argc < 3) { printf("Usage: no filename or server name provided\n");
	              exit(1);
	}
	char* servername = malloc(256 * ( sizeof(char)));
	servername = argv[1];
	char* filename = malloc(256 * sizeof(char));
	filename = argv[2];
	
	/******************************************************************************************************
	 * STEP 0a: open socket and connect to server 
	*******************************************************************************************************/
	sock= socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) error("socket");

	server.sin_family = AF_INET;
	hp = gethostbyname(servername);
	if (hp==0) error("Unknown host");

    printf("servername = %s\n", servername);

	bcopy((char *)hp->h_addr, (char *)&server.sin_addr,hp->h_length);
	server.sin_port = htons(atoi("12345"));
	length=sizeof(struct sockaddr_in);

	/******************************************************************************************************
	 * STEP 0b: open source files and copy to DRAM
	*******************************************************************************************************/
  	//opening up a file 
	pFile = fopen ( filename , "rb" );
	f2 = fopen ( "log_client.txt" , "w" ); // log file
	if (pFile==NULL) {fputs ("File error",stderr); exit (1);}

	// obtain file size:
	fseek (pFile , 0 , SEEK_END);
	lSize = ftell (pFile);
	rewind (pFile);
	printf("File Size: %ld\n", lSize);
	chunks = ceil(lSize/(float)data_size);

	// allocate memory to contain the whole file:
	char **buffer = (char**) malloc (chunks*sizeof(char*));
	int count = 0;

	/// reading from file into memory
	for(count = 0; count < chunks - 1;count++)
	{
		buffer[count] = (char*) malloc (data_size*sizeof(char));
		result = fread (buffer[count],1,data_size,pFile);
	} 
	printf("No of shunks : %d\n", chunks);
	printf("Max file size : %ld\n", lSize);
	printf("Memory read : %d\n", (chunks - 1)*data_size);
	printf("Memory left to read : %ld\n", lSize - (chunks-1)*data_size);
	printf("count value  : %d\n", count);


	/// remaining memory chunk lSize - chunks*data_size
	final_chunk = lSize - (chunks-1)*data_size;
	buffer[count] = (char*) malloc (final_chunk*sizeof(char));
	result = fread (buffer[count],1,final_chunk,pFile);
	printf("File Size read: %ld, count : %d\n", result,count);

	// memory buffer for all acks
	struct full_ack fullAck;
	fullAck.full_packet_tracker = (uint8_t*) malloc (chunks * sizeof(uint8_t));
	fullAck.numAckPackets = ceil(chunks/(float)data_size);

	/******************************************************************************************************
	 * STEP 1: send init packet to start file stream
	*******************************************************************************************************/
	// build out init_packet struct
	init_packet.type = 0;
	init_packet.file_size = lSize;
	init_packet.chunk_size = data_size;
	send_buffer = (unsigned char*)malloc(sizeof(struct Init_PACKET));

	// allocate memory for send_buffer to contain init_packet
	memset(send_buffer,0,sizeof(struct Init_PACKET));
	memcpy(send_buffer,(const unsigned char*)&init_packet,sizeof(init_packet));

	// send send_buffer (Containing init_packet)
	n=sendto(sock,send_buffer,sizeof(init_packet),0,(const struct sockaddr *)&server,length);
	if (n < 0) error("Sendto");
	free(send_buffer);
	printf("Init packet sent \n");
	
	// declare no-receipt timeout interval {seconds, microseconds}
	int send_counter = 0;
	struct timeval timeout={0,10000};
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(struct timeval));

	// timer init
	struct timespec start , stop;
	double total;

	// wait for ack from receiver for init_packet
	while(1)
	{
		n = recvfrom(sock,buffer1,sizeof(struct Init_PACKET),0,(struct sockaddr *)&from, &length);

		// if we've received something, parse to see if it's an ack of type 4
		if(n > 0)
		{
			ack_packet1 = (struct ack_packet*)buffer1;
			if(ack_packet1->type == 4)
			{
			    printf("\n");
				printf("Ack of init received \n");
				break;
			}	
		}

		// if we haven't received anythin, resent init_packet
		else
		{
			for (send_counter = 0; send_counter < UDP_BURST;send_counter++)
			{
				init_packet.type = 0;
				init_packet.file_size = lSize;
				init_packet.chunk_size = data_size;
				send_buffer = (unsigned char*)malloc(sizeof(struct Init_PACKET));
				memset(send_buffer,0,sizeof(struct Init_PACKET));
				memcpy(send_buffer,(const unsigned char*)&init_packet,sizeof(init_packet));
				n=sendto(sock,send_buffer,sizeof(init_packet),0,(const struct sockaddr *)&server,length);
				if (n < 0) error("Sendto");
				free(send_buffer);
				printf("Init packet no : %d\n",send_counter);
			}
		}
		
	}

	/******************************************************************************************************
	 * STEP 2a: send data (packet type 1)
	*******************************************************************************************************/
	// start timer as we start sending data
	clock_gettime(CLOCK_REALTIME , &start);
	printf("Started timer!\n");	
	packet_tobe_sent = (unsigned char*)malloc(sizeof(struct packet));
	while(1)
	{
		// --------------------------------- STEP 2a: send all data packets that aren't missing ----------------------------------
		// state 1: sending type 1 packets
		if(state_ch == 1)
		{
			// log ack sequence before sending out missing packets
			fprintf(f2, "before update\n");
			int b;
			for(b = 0; b < chunks;b++)
			{
				fprintf(f2,"%d",ack_packet1->packet_tracker[b]);
			}
			fprintf(f2,"\n");
			

			// send out missing data packets (type 1)
			int send_count = 0;
			for(send_count = 0; send_count < chunks-1; send_count++)
			{
				if(fullAck.full_packet_tracker[send_count] == 0)
				{
					data_packet.type = 1;
					data_packet.sequence_number = send_count;
					memcpy(data_packet.data,buffer[send_count],data_size);
					memset(packet_tobe_sent,0,sizeof(struct packet));
					memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
					n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
					
					// error catch sendto and print out packet info
					if (n < 0) error("Sendto");
					//printf("type: %d, sequence number : %d\n",data_packet.type,data_packet.sequence_number);
				}
			}
			/******************************************************************************************************
	 		* STEP 2b: request updated ack sequence again
			*******************************************************************************************************/	
			// set state to 0 --> send type 2 packet (request updated ack sequence from server)
			state_ch = 0;
			data_packet.type = 2;
			data_packet.sequence_number = -1;
			memset(packet_tobe_sent,0,sizeof(struct packet));
			memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
			n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
			
			// error catch sendto and print out packet info
			if (n < 0) error("Sendto");
			//printf("type: %d, sequence number : %d\n",data_packet.type,data_packet.sequence_number);
			
		}

		/******************************************************************************************************
		* STEP 2c: wait for cummulative ack and/pr re-request ack sequence
		*******************************************************************************************************/	
		// send type 2 packets
		else
		{
            for (int i = 0; i < fullAck.numAckPackets; i++)
            {
                // wait to receive updated ack sequence from server
                n = recvfrom(sock,buffer1,sizeof(struct ack_packet),0,(struct sockaddr *)&from, &length);
                ack_packet1 = (struct ack_packet*)buffer1;
                memcpy((uint8_t*)&fullAck.full_packet_tracker + data_size * i, ack_packet1->packet_tracker, sizeof(ack_packet1->packet_tracker));
                printf("type %d\n",ack_packet1->type);
                printf("1\n");
                if(n > 0)
                {
                    // if packet is of type 2 it is the updated ack sequence from the receiver
                    if(ack_packet1->type == 2)
                    {
                        // print and log updated sequence
                        //printf("update received \n");
                        // fprintf(f2, "update\n");
                        // int b;
                        // for(b = 0; b < (data_size < chunks ? data_size : chunks); b++)
                        // {
                        //     fprintf(f2,"%d",ack_packet1->packet_tracker[b]);
                        // }
                        // fprintf(f2,"\n");

                        // change back to state one to resend missing packets (As identified by the updated ack sequence)
                        state_ch = 1;
                    }

                    // if packet type 3 --> move on to receiving last packet
                    else if(ack_packet1->type == 3)
                    {
                        printf("all packets received except last\n");
                        break;
                    }
                    
                }
                // if we did not receive the updated sequence --> resend the update sequence request
                else
                {
                    
                    data_packet.type = 2;
                    data_packet.sequence_number = -1;
                    memset(packet_tobe_sent,0,sizeof(struct packet));
                    memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
                    n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
                
                    if (n < 0) error("Sendto");
                    //printf("type: %d, sequence number : %d\n",data_packet.type,data_packet.sequence_number);
                }
                printf("2\n");
            }
            if(ack_packet1->type == 3)
            {
                printf("all packets received except last\n");
                break;
            }
            printf("3\n");
		}
	}

	/******************************************************************************************************
	* STEP 3: send final packet and wait for ack of receipt
	*******************************************************************************************************/	
	while(1)
	{
		// final packet is of type 6
		data_packet.type = 6;
		data_packet.sequence_number = chunks-1;
		memcpy(data_packet.data,buffer[chunks-1],data_size);
		memset(packet_tobe_sent,0,sizeof(struct packet));
		memcpy(packet_tobe_sent,(const unsigned char*)&data_packet,sizeof(data_packet));
		n=sendto(sock,packet_tobe_sent,sizeof(data_packet),0,(const struct sockaddr *)&server,length);
		if (n < 0) error("Sendto");
		//printf("type: %d, sequence number : %d , Bytes sent : %d\n",data_packet.type,data_packet.sequence_number,n);
		
		// wait for ack of receipt of final packet
		n = recvfrom(sock,buffer1,sizeof(struct ack_packet),0,(struct sockaddr *)&from, &length);
		if(n > 0)
		{
			printf("Packet Type 5 (cummulative ack) received : \n");
			struct ack_packet* ack_packet3 = (struct ack_packet*)buffer1;
			printf("type last %d\n",ack_packet3->type);
			if(ack_packet3->type == 5 || ack_packet3->type == 3)
			{
				printf("all packets sent \n");
				break;
			}
		}
		// if we didn't get ack --> resend final packet
	}

	// stop timer
  	clock_gettime(CLOCK_REALTIME, &stop);
	total = stop.tv_sec - start.tv_sec + (double)(stop.tv_nsec - start.tv_nsec)/1000000000;
	printf("Total Transfer Time: %f seconds\n", total);

	// clean up
	close(sock);
	fclose (pFile);
	fclose(f2);
	free (buffer);
	return 0;
}

void error(const char *msg)
{
    perror(msg);
    exit(0);
}
