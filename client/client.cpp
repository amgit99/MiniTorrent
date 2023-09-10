#include <bits/stdc++.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <iostream>
#include <thread>
#include <fstream>
#include <cmath>
#include <chrono>

using namespace std;
#define TRACKER_PORT 8080
#define BUFFER_SIZE 4096
#define CHUNK_SIZE 524288

string ip_addr = "127.0.0.1";
string session_port;
string user_id = "-1";

ofstream aout;
vector<bool> serve_fin(10, 0);
vector<bool> down_fin(10, 0);

void string_break(vector<string> &words, string sentence);

////////////////////////DATA TRANSFER/////////////////////////

string rx(int new_socket, int set){
	int bytes_received;
	string received_message;
	char *buffer = new char[BUFFER_SIZE];
	memset(buffer, 0, BUFFER_SIZE);
	while((bytes_received = recv(new_socket, buffer, BUFFER_SIZE, set))>0){
		received_message.append(string(buffer));
		memset(buffer, 0, BUFFER_SIZE);
	}
	delete[] buffer;
	return received_message;
}

void tx(int new_socket, string message){
	int left = message.size();
	int i=0;
	string piece;
	while(left>0){
		piece = message.substr(i++*BUFFER_SIZE, min(left, BUFFER_SIZE));
		send(new_socket, piece.c_str(), BUFFER_SIZE, 0);
		left-=min(left, BUFFER_SIZE);
	}
}

void rx_ft(int socket, string destination_path){
	vector<string> file_offset;
	string_break(file_offset, destination_path);
    size_t readBytes;
    FILE *out;
    out = fopen(file_offset[0].c_str(), "rb+");
    if(!out){
    	out = fopen(file_offset[0].c_str(), "wb");
    }
    fseek(out, CHUNK_SIZE*stoi(file_offset[1]), SEEK_SET);
    char buffer[BUFFER_SIZE];
    while ((readBytes = recv(socket, buffer, sizeof(buffer), 0) > 0)){
    	fwrite (buffer , sizeof(buffer), 1, out);
    	memset(buffer, 0, sizeof(buffer));
    }
    fclose(out);
}

void tx_ft(int new_socket, string filename){
	vector<string> file_offset;
	string_break(file_offset, filename);
    char buffer[BUFFER_SIZE] = {0};

    int iters = CHUNK_SIZE/BUFFER_SIZE;
    FILE *in = fopen(file_offset[0].c_str(), "rb");
    fseek(in, CHUNK_SIZE*stoi(file_offset[1]), SEEK_SET);

    while(!feof(in) && iters--){
        fread(buffer, sizeof(buffer), 1, in);
        send(new_socket, buffer, sizeof(buffer), 0);
        memset(buffer, 0, sizeof(buffer));
    }
    fclose(in);
}

/////////////////////// ESTABLISH CONNECTION ///////////////////////

vector<int> listen_comms(int port){
	vector<int> sockets;
	int server_socket_fd, client_socket_fd;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);

	if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}
	if(setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))){
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	if(::bind(server_socket_fd, (struct sockaddr*)&address, sizeof(address))< 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	if(listen(server_socket_fd, 50) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	if ((client_socket_fd = accept(server_socket_fd, (struct sockaddr*)&address,(socklen_t*)&addrlen))< 0){
		perror("accept");
		exit(EXIT_FAILURE);
	}
	sockets.push_back(server_socket_fd);
	sockets.push_back(client_socket_fd);
	return sockets;
}

int connect_comms(int port){
	int server_socket_fd = 0, client_socket_fd;
    struct sockaddr_in serv_addr;
    
    if ((server_socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        //cout<<"Socket creation error";
        return -1;
    }
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0){
        //cout<<"Invalid address";
        return -1;
    }
	if ((client_socket_fd = connect(server_socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        //cout<<"Connection Failed";
        return -1;
    }
    return server_socket_fd;
}

string comms(string client_request){
	int socket_fd = connect_comms(TRACKER_PORT);
	tx(socket_fd, client_request);
	string server_msg = rx(socket_fd, 0);
	close(socket_fd);
	return server_msg;
}

///////////////////////// SERVER DOWNLOAD REQUESTS ///////////////////////////

void seed(int dora){
	vector<int> sockets = listen_comms(stoi(session_port));
	aout<<"Connection Established"<<endl;
	string filename = rx(sockets[1], MSG_DONTWAIT);
	aout<<"\n[>]Request from client: "<<filename<<endl;
	tx_ft(sockets[1], filename);
	aout<<"\n[<]Chunk sent to client: "<<filename<<endl;
	close(sockets[1]);
	shutdown(sockets[0], SHUT_RDWR);
	serve_fin[dora] = 1;
}

void serve_downloads(){
	thread t_pool[10];
	vector<bool> active(10, 0);
	while(1){
		for(int i=0; i<1; ++i){
			if(!active[i]){
				t_pool[i] = thread(seed, i);
				active[i]=1;
			}
		}
		for(int i=0; i<1; ++i){
			if(active[i] && serve_fin[i]){
				t_pool[i].join();
				active[i] = 0;
			}
		}
	}
}

void string_break(vector<string> &words, string sentence){
	string word="";
	for(auto itr: sentence){
		if(itr==' '){
			words.push_back(word);
			word="";
		}
		else word+=itr;
	}
	words.push_back(word);
}

//////////////////////////// DOWNLOAD ///////////////////////////////

void download(string metadata, string filename, string destination_path, int dora){	
	string session_user=user_id;
	vector<vector<string>> chunk_src;
	priority_queue<pair<int, int>> pq;
	int i=0, total_size;

	int chunk=0, port;
	string sent="";
	for(auto itr: metadata){
		if(itr=='\n'){
			vector<string> ports;
			string_break(ports, sent);
			chunk_src.push_back(ports);
			pq.push(make_pair(-1*ports.size(), chunk++));
			sent = "";
		}
		else sent+=itr;
	}
	aout<<"priority_queue made"<<endl;
	while(!pq.empty()){
		pair<int ,int> top = pq.top();
		pq.pop();
		port = stoi(chunk_src[top.second][rand()%(chunk_src[top.second].size()-1)]);
		int socket = connect_comms(port);
		tx(socket, filename+" "+to_string(top.second)); /// sending filename + offset of chunk
		rx_ft(socket, destination_path+" "+to_string(top.second));
		close(socket);
		this_thread::sleep_for(chrono::milliseconds(100));
	}
	comms("make_seeder "+filename+" "+session_user);
	down_fin[dora]=1;
}

int main(int argc, char** argv){
	system("clear");
	session_port = argv[1];
	aout.open("client_"+session_port+".txt");

	thread down_pool[10];
	vector<bool> active(10, 0);
	thread server_dn(serve_downloads);
	while(1){
		string client_query, opcode, client_request;
		int pt=0, i=0;
		getline(cin, client_query);

		while(client_query[i]!=' ')i++;
		opcode = client_query.substr(0, i);
		pt=++i;

		if(opcode=="create_user"){ 
			aout<<"\n[<]Message to tracker: "<<client_query + " " + ip_addr + " " + session_port<<endl;
			aout<<"[>]Response from tracker: "<<comms(client_query + " " + ip_addr + " " + session_port)<<endl;
		}
		else if(opcode=="login"){
			while(client_query[i]!=' ')i++;
			aout<<"\n[<]Message to tracker: "<<client_query + " " + ip_addr + " " + session_port<<endl;
			if(comms(client_query + " " + ip_addr + " " + session_port)=="Success"){
				user_id = client_query.substr(pt, i-pt);
				aout<<"[>]Response from tracker: Success"<<"\n\nlogs for "<<user_id<<" ::\n"<<endl;
			}
		}
		else if(user_id!="-1" && (opcode=="create_group" || opcode=="join_group" || opcode=="leave_group" || opcode=="accept_request" || opcode=="show_downloads")){
			aout<<"\n[<]Message to tracker: "<<client_query + " " + user_id<<endl;
			aout<<"[>]Response from tracker: "<<comms(client_query + " " + user_id)<<endl;
		}
		else if(user_id!="-1" && (opcode=="list_requests" || opcode=="list_groups" || opcode=="list_files")){
			aout<<"\n[<]Message to tracker: "<<client_query<<endl;
			aout<<"[>]Response from tracker: "<<comms(client_query)<<endl;
		}
		else if(user_id!="-1" && opcode=="upload_file"){
			vector<string> args;
			string_break(args, client_query);

			ifstream testFile(args[1].c_str(), ios::binary);
			testFile.seekg (0, ios::end);
			const auto end = testFile.tellg();
			const auto fsize = end;
			aout<<fsize<<"   "<<to_string(fsize)<<endl;
			int chunks = ceil((float)fsize/CHUNK_SIZE);
			aout<<chunks<<endl;

			aout<<"\n[<]Message to tracker: "<<client_query + " " + user_id + " " + to_string((int)ceil((float)fsize/CHUNK_SIZE))<<endl;
			aout<<"[>]Response from tracker: "<<comms(client_query + " " + user_id + " " +to_string(fsize)+ " " +to_string((int)ceil((float)fsize/CHUNK_SIZE)))<<endl;
		}
		else if(user_id!="-1" && opcode=="download_file"){
			aout<<"\n[<]Message to tracker: "<<client_query + " " + user_id<<endl;
			string metadata = comms(client_query + " " + user_id);
			aout<<"[>]Metadata from tracker:\n"<<metadata<<endl;

			vector<string> args;
			string_break(args, client_query);

			if(client_request!="Too few args" || client_request!="404" || client_request!="File not in group"){
				for(int i=0; i<10; ++i){
					if(!active[i]){
						down_pool[i] = thread(download, metadata, args[2], args[3], i);
						active[i]=1;
						break;
					}
				}
			}
			else cout<<client_request<<endl;
		}
		else if(user_id!="-1" && opcode=="logout"){
			user_id="-1";
		}
		else cout<<"Invalid opcode"<<endl;

		for(int i=0; i<10; ++i){
			if(active[i] && down_fin[i]){
				down_pool[i].join();
				active[i] = 0;
			}
		}
	}
}
