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

using namespace std;
#define TRACKER_PORT 8080
#define BUFFER_SIZE 4096

// debug op file open
ofstream fout;
vector<bool> finished(10, 0);

class Group;
class Doc;
class User;

map<string, User*> users;
map<string, Group*> groups;
map<string, Doc*> files;

class User{
public:
	string user_id, password, ip_address, port;
	unordered_set<string> groups, downloads;

	User(string a, string pass, string ad, string p){
		user_id = a;
		password = pass;
		ip_address = ad;
		port = p;
	}
};

class Group{
public:
	string group_id, admin;
	unordered_set<string> users, files, pending_requests;

	Group(string a, string u){
		group_id = a;
		users.insert(u);
		admin = u;
	}
};

class Doc{
public:
	string name, chunks, hash, chunk_hash, size;
	unordered_set<string> groups, seeders, leechers;
	map<int, unordered_set<string>> chunkwise_leechers;

	Doc(string n, string g, string u, string s, string c){
		name = n;
		groups.insert(g);
		seeders.insert(u);
		size = s;
		chunks = c;
	}

	string metadata(){
		string meta;
		//meta+=this->size+"\n";
		for(int i=0; i<stoi(chunks); ++i){
			for(auto itr: this->seeders) meta += users[itr]->port + " ";
			for(auto user: this->chunkwise_leechers[i]) meta += users[user]->port +" ";
			meta += "\n";
		}
		return meta;
	}
};

string rx(int new_socket){
	int bytes_received;
	string received_message;
	char *buffer = new char[BUFFER_SIZE];
	while((bytes_received = recv(new_socket, buffer, BUFFER_SIZE, MSG_DONTWAIT))>0){
		received_message.append(string(buffer));
		memset(buffer, 0, BUFFER_SIZE);
	}
	delete[] buffer;
	fout<<"\n[>] message received : "<<received_message<<endl;
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
	fout<<"[<] message sent : "<<message<<endl;
}

void rx_ft(int socket){
    size_t readBytes;
    FILE *out = fopen("c3_fs/s3.mkv", "wb");
    char buffer[1024];
    while ((readBytes = recv(socket, buffer, sizeof(buffer), 0) > 0)){
    	fwrite (buffer , sizeof(buffer), 1, out);
    }
    fclose(out);
}

void tx_ft(int new_socket, string filename){
    char buffer[1024] = {0};
    FILE *in = fopen(filename.c_str(), "rb");
    while(!feof(in)){
        fread(buffer, sizeof(buffer), 1, in);
        send(new_socket, buffer, sizeof(buffer), 0);
        memset(buffer, 0, sizeof(buffer));
    }
    fclose(in);
}

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
	if(listen(server_socket_fd, 3) < 0) {
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
	int sock = 0, client_fd;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fout<<"\nSocket creation error\n";
        return -1;
    }
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0){
        fout<<"\nInvalid address/ Address not supported \n";
        return -1;
    }
	if ((client_fd = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        fout<<"\nConnection Failed \n";
        return -1;
    }
    return sock;
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

string parse_control(string client_query){
	string opcode="", response="", word="";
	vector<string> args;
	string_break(args, client_query);

	if(args[0]=="create_user"){   //////// create_user--user_id--password--ip--port
		if(args.size()<5) response = "Too few Arguments";
		else{
			if(users.find(args[1])==users.end()){
				users[args[1]] = new User(args[1], args[2], args[3], args[4]);
				response = "Success";
			}
			else response = "Failed";
		}
	}
	else if(args[0]=="login"){   //////// login--user_id--password--ip--port
		if(args.size()<5) response = "Too few Arguments";
		else{
			if(users.find(args[1])!=users.end() && users[args[1]]->password == args[2]){
				response="Success";
				users[args[1]]->ip_address = args[3];
				users[args[1]]->port = args[4];
			}
			else response = "Failed";
		}
	}
	else if(args[0]=="create_group"){   //////// create_group--group_id--user_id
		if(args.size()<3) response = "Too few Arguments";
		else{
			if(groups.find(args[1])==groups.end()){
				groups[args[1]] = new Group(args[1], args[2]);
				response="Success";
				fout<<"admin set to: "<<args[2]<<endl;
			}
			else response = "Failed";
		}
	}
	else if(args[0]=="join_group"){   //////// join_group--group_id--user_id
		if(args.size()<3) response = "Too few Arguments";
		else{
			if(groups.find(args[1])==groups.end())response="Group does not exist";
			else if(groups[args[1]]->users.find(args[2])!=groups[args[1]]->users.end())response="Already member of group";
			else{
				groups[args[1]]->pending_requests.insert(args[2]);
				response = "Request sent to admin";
			}
		}
	}
	else if(args[0]=="leave_group"){   //////// leave_group--group_id--user_id
		if(args.size()<3) response = "Too few Arguments";
		else{
			if(groups.find(args[1])==groups.end())response="Group does not exist";
			else if(groups[args[1]]->users.find(args[2])==groups[args[1]]->users.end())response="Not a member of group";
			else{
				groups[args[1]]->users.erase(args[2]);
				users[args[2]]->groups.erase(args[1]);
			}
		}
	}
	else if(args[0]=="list_requests"){   //////// list_requests--group_id
		if(args.size()<2) response = "Too few Arguments";
		else{
			if(groups.find(args[1])==groups.end())response="Group does not exist";
			else for(auto itr: groups[args[1]]->pending_requests)response+=itr + "\n";
		}
	}
	else if(args[0]=="accept_request"){   //////// accept_request--group_id--requesting_user--admin_user
		if(args.size()<3) response = "Too few Arguments";
		else{
			fout<<"admin of group is: "<<groups[args[1]]->admin<<endl;
			fout<<"to be accepted: "<<args[2]<<" "<<"admin id: "<<args[3]<<endl;
			if(groups[args[1]]->admin!=args[3]) response = "You're not authorised to accept";
			else if(groups[args[1]]->pending_requests.find(args[2])==groups[args[1]]->pending_requests.end()) response = "Request does not exist";
			else{
				groups[args[1]]->pending_requests.erase(args[2]);
				groups[args[1]]->users.insert(args[2]);
				users[args[2]]->groups.insert(args[1]);
				response = "Request Accepted";
			}
		}
	}
	else if(args[0]=="list_groups"){   //////// list_groups
		for(auto itr: groups)response+=itr.first + "\n";
	}
	else if(args[0]=="list_files"){   //////// list_requests--group_id
		if(args.size()<2) response = "Too few Arguments";
		else for(auto itr: groups[args[1]]->files)response+=itr + "\n";
	}
	else if(args[0]=="upload_file"){   //////// upload_file--file_path--group_id--user_id--size--#of_chunks
		if(args.size()<6) response = "Too few Arguments";
		else{
			if(files.find(args[1])!=files.end() && files[args[1]]->seeders.find(args[3])!=files[args[1]]->seeders.end()) response="Already uploaded and a seeder";
			else{
				files[args[1]] = new Doc(args[1], args[2], args[3], args[4], args[5]);
				groups[args[2]]->files.insert(args[1]);
				response = "File added to Group";
			}
		}
	}
	else if(args[0]=="download_file"){   //////// download_file--group_id--file_name--destination_path--user_id
		if(args.size()<5) response = "Too few Arguments";
		else{
			if(files.find(args[2])==files.end())response = "404";
			else if(groups[args[1]]->files.find(args[2])==groups[args[1]]->files.end())response = "File not in group";
			else if(groups[args[1]]->users.find(args[4])==groups[args[1]]->users.end())response = "User not in file group";
			else response = files[args[2]]->metadata();
		}
	}
	else if(args[0]=="make_seeder"){   //////// make_seeder--file_name--user_id
		if(args.size()<3) response = "Too few Arguments";
		else{
			files[args[1]]->seeders.insert(args[2]);
			users[args[2]]->downloads.insert(args[1]);
		}
	}
	else if(args[0]=="show_downloads"){   //////// make_seeder--file_name--user_id
		if(args.size()<2) response = "Too few Arguments";
		else for(auto itr: users[args[1]]->downloads)response+= itr+"\n";
	}
	else response = "Invalid operation";
	return response;
}

void comms(int thread_no){
	vector<int> comm_sockets = listen_comms(TRACKER_PORT);
	string client_request = rx(comm_sockets[1]);
	tx(comm_sockets[1], parse_control(client_request));
	close(comm_sockets[1]);
	shutdown(comm_sockets[0], SHUT_RDWR);
	finished[thread_no]=1;
}

void threadripper(){
	thread t_pool[10];
	vector<bool> active(10, 0);
	while(1){
		for(int i=0; i<10; ++i){
			if(!active[i]){
				t_pool[i] = thread(comms, i);
				active[i]=1;
			}
		}
		for(int i=0; i<10; ++i){
			if(active[i] && finished[i]){
				t_pool[i].join();
				active[i] = 0;
			}
		}
	}
}

int main(int argc, char** argv){
	system("clear");
	fout.open("tracker_logs.txt");
	fout << "logs:: before OPEN" << endl;
	string exit_string;
	thread first(threadripper);
	while(1){
		getline(cin, exit_string);
		if(exit_string == "quit")break;
	}

	fout.close();
	return 0;
}
