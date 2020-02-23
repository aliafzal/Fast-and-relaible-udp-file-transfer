#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#define data_size 25000

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

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

struct Init_PACKET{
	uint8_t type;
	u_int file_size;
	u_int chunk_size;
};

struct packet{
	uint8_t type;
	int sequence_number;
	unsigned char data[data_size];
};

struct ack_packet{
  uint8_t type;
  bool packet_tracker[4195];
};

int main(int argc, char *argv[])
{
	FILE * oFile;
	FILE * f1;
	int sock, length, n;
	socklen_t fromlen;
	struct sockaddr_in server;
	struct sockaddr_in from;
	char buf[63000];
	char send_buffer[100000];
	int chunks, final_chunk;
	bool init_check = 0, mem_alloc_ch=0, data_check=0;
	int cons_check = 0;
	int type; 
	struct ack_packet ack_packet1;
	int packet_counter = 0; 

	f1 = fopen ( "log_server.txt" , "w" );
	
	/*if (argc < 2) 
	{
		fprintf(stderr, "ERROR, no port provided\n");
		exit(0);
	}*/

	/******************************************************************************************************
	 * STEP 0: open socket and bind
	*******************************************************************************************************/
	sock=socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) error("Opening socket");
	length = sizeof(server);
	bzero(&server,length);
	server.sin_family=AF_INET;
	server.sin_addr.s_addr=INADDR_ANY;
	server.sin_port=htons(atoi("12345"));
	if (bind(sock,(struct sockaddr *)&server,length)<0) 
		error("binding");
	fromlen = sizeof(struct sockaddr_in);

	/******************************************************************************************************
	 * Step 1a: wait for init_packet to establish connection
	 * get file and data information
	 * allocate memory to receive file based on init_packet info
	*******************************************************************************************************/
	int file_sz;
	int send_counter = 0;
	printf("Waiting for init packet \n");
	while(1)
	{
		n = recvfrom(sock,buf,sizeof(struct Init_PACKET),0,(struct sockaddr *)&from,&fromlen);
		if (n < 0) error("recvfrom");
		
		printf("No of bytes received: %d\n",n);

		// build instance of init_packet and get file/data information
		struct Init_PACKET* recv_init_packet = (struct Init_PACKET*)buf;
		if(recv_init_packet->type == 0)
		{
			init_check = 1;
			printf("Init packet received decoding ... \n");
			file_sz = recv_init_packet->file_size;
			printf("File size : %d\n",file_sz);
			printf("Data size : %d\n",recv_init_packet->chunk_size);
			break;
		}
	}
	
	// allocate memory to receive buffer
	printf("allocating memory\n");
	chunks = ceil(file_sz/(float)data_size);
	char **buffer = (char**) malloc (chunks*sizeof(char*));
	int count = 0;
	for(count = 0; count < chunks - 1;count++)
	{
		buffer[count] = (char*) malloc (data_size*sizeof(char));
	} 
	final_chunk = file_sz - (chunks-1)*data_size;
	buffer[count] = (char*) malloc (final_chunk*sizeof(char));

	/******************************************************************************************************
	 * Step 1b: send type 4 (ack receipt of init_packet)
	*******************************************************************************************************/
	ack_packet1.type = 4;
	int b=0;
	printf("Type packet1 %d\n",ack_packet1.type);
	memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
	printf("failed at send_buffer memcpy\n");
	n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
	if (n  < 0) error("sendto");
	mem_alloc_ch = 1;
	printf("allocating memory done \n");
	printf("Now waiting for data\n");

	/******************************************************************************************************
	 * Step 2: wait for type 1 data packets
	*******************************************************************************************************/
	while (1) 
	{
		n = recvfrom(sock,buf,data_size,0,(struct sockaddr *)&from,&fromlen);
		struct packet* data_packet = (struct packet*)buf;
		type = data_packet->type;
		
		// if packet is of type 0 (init_packet) send type 4 (ack) again
		if(type == 0)
		{
			ack_packet1.type = 4;
			memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
			n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
			if (n  < 0) error("sendto");
		}

		// if packet is of type 1 (data) process, store, and update ack sequence 
		else if(type == 1)
		{

			struct packet* data_packet = (struct packet*)buf;
			// process only if we haven't received the packet beforehand
			if(ack_packet1.packet_tracker[data_packet->sequence_number] <= 0) 
			{
				printf("Received seq no : %d\n",data_packet->sequence_number);
				memcpy(buffer[data_packet->sequence_number],data_packet->data,data_size); // store
				ack_packet1.packet_tracker[data_packet->sequence_number] = 1; // update ack sequence
				packet_counter++;
			}
		}

		// if packet if of type 2 (client requesting updated ack sequence), send updated ack sequence
		else if(type == 2)
		{
			printf("Packet counter val %d\n",packet_counter);			
			int b;
			fprintf(f1,"\n");

			// if we've already received all packets (kept track of by packet_counter)...
			// send type 3 ack (received all packets except last)
			if(packet_counter >= (chunks-2)) // 
			{
				printf("All packets received except last \n");
				ack_packet1.type = 3;
				memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
				n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
				if (n  < 0) error("sendto");
				fprintf(f1, "p: ");
				for(b = 0; b<chunks;b++)
				{
					fprintf(f1,"%d",ack_packet1.packet_tracker[b]);

				}
				fprintf(f1,"\n");
				break;
			}

			// if we haven't yet received all packets, send ack sequence back to client (packet type 2)
			else
			{
				ack_packet1.type = 2;
				memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
				n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
				if (n  < 0) error("sendto");
			}

		}
	}

	/******************************************************************************************************
	 * Step 3: wait for type 6 packet (last packet)
	*******************************************************************************************************/
	while(1)
	{
		printf("Waiting for last packet\n");
		n = recvfrom(sock,buf,final_chunk,0,(struct sockaddr *)&from,&fromlen);
		printf("No of bytes received %d\n",n );
		struct packet* data_packet = (struct packet*)buf;
		printf("type %d : Received seq no : %d \n",data_packet->type,data_packet->sequence_number);

		// if packet is of type 6, send type 5 ack (transaction done)
		if(data_packet->type == 6)
		{
			memcpy(buffer[data_packet->sequence_number],data_packet->data,final_chunk-1);
			int s_count =0;
			for(s_count = 0;s_count<100;s_count++)
			{
				printf("Sending type5\n");
				ack_packet1.type = 5;
				memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
				n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
				if (n  < 0) error("sendto");
			}
			break;
		}

		// else if packet is of type 2 (client requesting updated ack sequence), resend updated ack sequence
		else if(data_packet->type == 2)
		{
			ack_packet1.type = 3;
			memcpy(send_buffer,(unsigned char*)&ack_packet1,sizeof(ack_packet1));
			n = sendto(sock,send_buffer,sizeof(ack_packet1),0,(struct sockaddr *)&from,fromlen);
			if (n  < 0) error("sendto");
		}
		
	}
  
	/******************************************************************************************************
	 * Step 4: write memory buffer to file
	*******************************************************************************************************/
	oFile = fopen ( "received_data.bin" , "wb" );
	printf("3\n");
	int w_count = 0;
	for(w_count = 0; w_count<chunks-1;w_count++)
	{
		fwrite (buffer[w_count] , sizeof(char), data_size, oFile);
	}
	printf("4\n");
	fwrite (buffer[w_count], sizeof(char), final_chunk, oFile);
	printf("5\n");

	fclose (oFile);
	close(sock);

	return 0;
}
