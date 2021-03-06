#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "crc32.h"
using namespace std;

int port=6789;

#define RECV_BUFFER_SIZE 1024

struct DataPacket {
	unsigned id;
	char data[RECV_BUFFER_SIZE];
	unsigned crc32_value;
};

bool pkt_id_compare(const DataPacket &a,const DataPacket &b) {
	return a.id<b.id;
}

int listenControl() {

int sfp,nfp; 
struct sockaddr_in s_add,c_add;
int sin_size;
unsigned short portnum=port;

sfp = socket(AF_INET, SOCK_STREAM, 0);
int reuse_buf=1;
setsockopt(sfp,SOL_SOCKET,SO_REUSEADDR,(char*)&reuse_buf,sizeof(reuse_buf));

if(sfp==-1)
{
    return -1;
}

bzero(&s_add,sizeof(struct sockaddr_in));
s_add.sin_family=AF_INET;
s_add.sin_addr.s_addr=htonl(INADDR_ANY);
s_add.sin_port=htons(portnum);
if(-1 == bind(sfp,(struct sockaddr *)(&s_add), sizeof(struct sockaddr)))
{
    return -1;
}
if(-1 == listen(sfp,5))
{
    return -1;
}

sin_size = sizeof(struct sockaddr_in);
nfp = accept(sfp, (struct sockaddr *)(&c_add), (socklen_t*)&sin_size);
if(-1 == nfp)
{
    return -1;
}
return nfp;
}

void requestResend(int tcpConn,int udpSock,vector<DataPacket>& packets,vector<unsigned>& lostPackets) {
    unsigned lostPackets_size=lostPackets.size();
    write(tcpConn,&lostPackets_size,sizeof(unsigned));
    if(lostPackets_size>0) {
	for(int i=0;i<lostPackets.size();i++) write(tcpConn,&lostPackets[i],sizeof(unsigned));
    }

}

int main(int argc, char** argv) {

    unsigned sin_len;
    char message[256];

    int socket_descriptor;
    struct sockaddr_in sin;

    cout<<"Waiting control connection"<<endl;

    int tcpConn=listenControl();

    if(tcpConn<=0) {
	cout<<"Unable to establish control connection"<<endl;
	exit(1);
    }

    printf("Waiting for data from sender \n");

    bzero(&sin,sizeof(sin));
    sin.sin_family=AF_INET;
    sin.sin_addr.s_addr=htonl(INADDR_ANY);
    sin.sin_port=htons(port);
    sin_len=sizeof(sin);

    socket_descriptor=socket(AF_INET,SOCK_DGRAM,0);
    bind(socket_descriptor,(struct sockaddr *)&sin,sizeof(sin));

    unsigned length;

    read(tcpConn,&length,sizeof(unsigned));

    cout<<"Length: "<<length<<endl;

    unsigned recv_size;

    vector<char*> data;

    unsigned next_id_predicted=0;

    unsigned pkt_count=(length-length%RECV_BUFFER_SIZE)/RECV_BUFFER_SIZE+1;

    unsigned real_pkt_count=pkt_count;

    vector<DataPacket> packets;

    send_start:

    cout<<"pkt_count: "<<pkt_count<<endl;

    for(int i=0;i<pkt_count;i++)
    {
	DataPacket rPkt;
	recvfrom(socket_descriptor,&rPkt,sizeof(rPkt),0,(struct sockaddr *)&sin,&sin_len);
	if(crc32((unsigned char*)rPkt.data,RECV_BUFFER_SIZE)!=rPkt.crc32_value) {
		cout<<"[!] CRC32 mismatch: "<<rPkt.crc32_value<<endl;
		i--;
		continue;
	}
	if(rPkt.id==0x1fffffff) {
		if(i==0) {
			i--;
			continue;
		}
		else break;
	}
	packets.push_back(rPkt);
//	cout<<"Packet "<<rPkt.id<<" received"<<endl;
    }

    for(int i=0;i<packets.size();i++) {
//	cout<<packets[i].id<<endl;
    }

    vector<unsigned> lostPackets;

    for(int i=0;i<real_pkt_count;i++) {
	bool found=false;
	for(int j=0;j<packets.size();j++) {
		if(packets[j].id==i) {
			found=true;
			break;
		}
	}
	if(!found) {
		lostPackets.push_back((unsigned)i);
		cout<<"[X] "<<i<<endl;
	}
    }

    unsigned lostPackets_size=lostPackets.size();

    cout<<"[*] lostPackets_size: "<<lostPackets_size<<endl;    

    requestResend(tcpConn,socket_descriptor,packets,lostPackets);

    if(lostPackets_size==0) goto write_file_start;

    pkt_count=lostPackets_size;
    goto send_start;

    write_file_start:
    cout<<"Receiving finished, writing file"<<endl;

    sort(packets.begin(),packets.end(),pkt_id_compare);

//    for(int i=0;i<packets.size();i++) {
//	cout<<"#"<<packets[i].id<<" "<<packets[i].crc32_value<<endl;
//    }

    ofstream outFile("received.data");
    char *outData=new char [length];
    for(int i=0;i<length;i++) {
	int tgt_pkt_id=(i-i%RECV_BUFFER_SIZE)/RECV_BUFFER_SIZE;
	int tgt_data_pos=i%RECV_BUFFER_SIZE;
	outData[i]=packets[tgt_pkt_id].data[tgt_data_pos];
    }

    outFile.write(outData,length);
    outFile.close();

    close(socket_descriptor);
    exit(0);

    return (EXIT_SUCCESS);
}
