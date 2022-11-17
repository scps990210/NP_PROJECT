#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <dirent.h>
#include <list>

#define PIPE_PATH "user_pipe/"

using namespace std;

struct npipe{
	int in;
	int out;
};

struct client{
	bool valid;
    int ID;
    char ip[INET6_ADDRSTRLEN];
    char nickname[20];
	int pid;
};

struct FIFOunit{
	int in;
	int out;
	bool used;
	char name[20];
};

struct fifo_data{
	FIFOunit fifolist[30][30];
};

struct broadcast_order{
	int type;
	string msg;
	int ID;
	int tarfd;
};

int shm_fd;
int data_fd;
int up_fd;
int server_pid;
int client_id;

vector<client> client_info;
vector<npipe> numberpipe_vector;
vector<int> num;
string original_input;
list<broadcast_order> broadcast_list;
vector<npipe> pipe_vector;
vector<string> userpipe;

int select_min(){
	client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
	for(int i = 0;i < 30; i++){
		if(!c[i].valid){
			munmap(c, sizeof(client) * 30);
			return i+1;
		}
	}
	munmap(c, sizeof(client) * 30);
	return 87;
}

vector<string> split(string str, string pattern) {
    vector<string> result;
    size_t begin, end;

    end = str.find(pattern);
    begin = 0;

    while (end != string::npos) {
        if (end - begin != 0) {
            result.push_back(str.substr(begin, end-begin)); 
        }    
        begin = end + pattern.size();
        end = str.find(pattern, begin);
    }

    if (begin != str.length()) {
        result.push_back(str.substr(begin));
    }
    return result;
}

/* broadcast method with shared memory */
void broadcast(int type,string msg,int ID,int tarfd){
	char buf[4096];
	bzero( buf,sizeof(char)*4096 );
	client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
	switch(type){
		case 0:
			/* Login */
			sprintf(buf, "*** User '(no name)' entered from %s. ***", c[ID-1].ip);
			break;
		case 1:
			/* Change name */
			if(tarfd == -1){
				sprintf(buf,"*** User '%s' already exists. ***",msg.c_str());
				string tmpstring(buf);
				cout << tmpstring << endl;
				munmap(c, sizeof(client) * 30);
				return;
			}else{
				sprintf(buf,"*** User from %s is named '%s'. ***",c[ID-1].ip,msg.c_str());
			}
			break;
		case 2:
			/* Yell with msg */
			sprintf(buf,"*** %s yelled ***: %s",c[ID-1].nickname,msg.c_str());
			break;
		case 3:
			/* Tell with msg and target */
			if(tarfd == -1){
				sprintf(buf,"*** Error: user #%s does not exist yet. ***",msg.c_str());
				string tmp(buf);
				cout << tmp << endl;
			}else{
				sprintf(buf,"*** %s told you ***: %s",c[ID-1].nickname,msg.c_str());
			}
			break;
		case 4:
			/* Logout */
			sprintf(buf,"*** User '%s' left. ***",c[ID-1].nickname);
			break;
		case 5:
			/* send user pipe information */
			/* Success to send userpipe */
			sprintf(buf,"*** %s (#%d) just piped '%s' to %s (#%d) ***",
				c[ID-1].nickname,ID,msg.c_str(),c[tarfd-1].nickname,tarfd);
			break;
		case 6:
			/* recv user pipe information */
			/* Success to send userpipe */
			sprintf(buf,"*** %s (#%d) just received from %s (#%d) by '%s' ***",
				c[ID-1].nickname,ID,c[tarfd-1].nickname,tarfd,msg.c_str());
			break;
		default:
			perror("unknown brroadcast type");
			break;
	}
	char *p = static_cast<char*>(mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
	string tmpstring(buf);
	/* end of string */
	tmpstring += '\0';
	strncpy(p, tmpstring.c_str(),tmpstring.length());
	munmap(p, 16384);
	usleep(1000);
	if(type != 3){
		for(int i = 0;i < 30 ; i++){
			/* check id valid */
			if(c[i].valid == true){
				kill(c[i].pid,SIGUSR1);
			}
		}
	}else if(tarfd != -1){
		kill(c[tarfd-1].pid,SIGUSR1);
	}
	munmap(c, sizeof(client) * 30);
}

static void SIGHANDLE(int sig){
	/* receive msg from broadfcast */
	if (sig == SIGUSR1)
    {
		char *p = static_cast<char*>(mmap(NULL, 16384, PROT_READ, MAP_SHARED, shm_fd, 0));
		string tmpstring(p);
		cout << tmpstring << endl;
		munmap(p, 16384);
	}
	/* receive msg from userpipe */
	else if(sig == SIGUSR2){
		fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ | PROT_WRITE, MAP_SHARED, up_fd, 0);
		for(int i = 0;i < 30; i++){
			if(fifo->fifolist[client_id-1][i].used){
				close(fifo->fifolist[client_id-1][i].out);
				bzero(&fifo->fifolist[client_id-1][i].name, sizeof(fifo->fifolist[client_id-1][i].name));
				fifo->fifolist[client_id-1][i].out = -1;
				fifo->fifolist[client_id-1][i].used = false;
				//cerr << getpid()<< " close out " << fifo->fifolist[client_id-1][i].out << endl;
			}
			if(fifo->fifolist[i][client_id-1].used){
				fifo->fifolist[i][client_id-1].used = false;
				bzero(&fifo->fifolist[i][client_id-1].name, sizeof(fifo->fifolist[i][client_id-1].name));
				fifo->fifolist[i][client_id-1].in = -1;
				close(fifo->fifolist[i][client_id-1].in);
				//cerr << getpid()<< " close in " << fifo->fifolist[i][client_id-1].in << endl;
			}
			if(fifo->fifolist[i][client_id-1].in == -1 && fifo->fifolist[i][client_id-1].name[0] != 0){
				fifo->fifolist[i][client_id-1].in = open(fifo->fifolist[i][client_id-1].name,O_RDONLY);
				//cerr << getpid()<< " open in " << fifo->fifolist[i][client_id-1].in << endl;
			}
		}
		munmap(fifo,  sizeof(fifo_data));
	}
	else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM)
    {
		exit (0);
	}
	signal(sig, SIGHANDLE);
}

void HandleChild(int sig){
	while(waitpid(-1,NULL,WNOHANG) > 0){
	}
}

void PushNumPipe(int in,int out){
	npipe np = {in,out};
	numberpipe_vector.push_back(np);
}

void CreatePipe(){
	int pipes[2];
	pipe(pipes);
	npipe np = {pipes[0],pipes[1]};
	pipe_vector.push_back(np);
}

void ClearUserPipe(){
	fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ | PROT_WRITE, MAP_SHARED, up_fd, 0);
	for(int id = 0;id < 30; id++){
		if(fifo->fifolist[id][client_id-1].used == true){
			close(fifo->fifolist[id][client_id-1].in);
			fifo->fifolist[id][client_id-1].in = 0;
			fifo->fifolist[id][client_id-1].out = 0;
			fifo->fifolist[id][client_id-1].used = false;
			unlink(fifo->fifolist[id][client_id-1].name);
			bzero(&fifo->fifolist[id][client_id-1].name, 0);
		}
		if(fifo->fifolist[client_id-1][id].used == true){
			close(fifo->fifolist[client_id-1][id].out);
			fifo->fifolist[client_id-1][id].in = 0;
			fifo->fifolist[client_id-1][id].out = 0;
			fifo->fifolist[client_id-1][id].used = false;
			unlink(fifo->fifolist[client_id-1][id].name);
			bzero(&fifo->fifolist[client_id-1][id].name, 0);
		}
	}
	munmap(fifo,  sizeof(fifo_data));
}

void EXECINSTR(vector<string> instr){
	int fd;
	const char **argv = new const char* [instr.size()+1];
	for(int i=0;i < instr.size(); i++){
		//file redirect
		if(instr[i] == ">"){
			fd = open(instr.back().c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
			instr.pop_back();
			instr.pop_back();
			close(1);
			dup(fd);
			close(fd);
		}
		argv[i] = instr[i].c_str();
	}
	argv[instr.size()] = NULL;
	if(execvp(argv[0],(char **)argv) == -1){
		//stderr for unknown command
		if(instr[0] != "setenv" && instr[0] != "printenv" && instr[0] != "exit")
			fprintf(stderr,"Unknown command: [%s].\n",instr[0].c_str());
		exit(0);
	}
}

void Piping(vector<string> input,int ID){
	bool has_numberpipe = false,has_errpipe = false;
	string numpip = "|";
	string errpip = "!";
	string recpip = "<";
	string senpip = ">";
	string space = " ";
	for(int i = 0 ; i < input.size() ; i++){
		bool has_user_sendpipe = false,has_user_recvpipe = false,dup_userpipe = false,recv_userpipe = false;
		int user_send_idx = 0,user_recv_idx = 0;
		int err_send_id = -1,err_recv_id = -1;
        char sendbuf[20];
		char recvbuf[20];
		vector<string> instr;
        vector<string> ret = split(input[i], space);
        for (auto& s : ret) {
			bool continue_id1 = false,continue_id2 = false;
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '!'){
					int numberpipe[2];
					int number = atoi(s.erase(0,j + errpip.length()).c_str());
					for(int k = 0; k < numberpipe_vector.size() ; k++){
						if(number == num[k]){
							numberpipe[0] = numberpipe_vector[k].in;
							numberpipe[1] = numberpipe_vector[k].out;
						}
						else{
							pipe(numberpipe);
						}
					}
					if(numberpipe_vector.size() == 0) pipe(numberpipe);
					PushNumPipe(numberpipe[0],numberpipe[1]);
					num.push_back(number);
					has_errpipe = true;	
					break;
				}
			}
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '|'){
					int numberpipe[2];
					int number = atoi(s.erase(0,j + numpip.length()).c_str());
					for(int k = 0; k < numberpipe_vector.size() ; k++){
						if(number == num[k]){
							numberpipe[0] = numberpipe_vector[k].in;
							numberpipe[1] = numberpipe_vector[k].out;
						}
						else{
							pipe(numberpipe);
						}
					}
					if(numberpipe_vector.size() == 0) pipe(numberpipe);
					PushNumPipe(numberpipe[0],numberpipe[1]);
					num.push_back(number);
					has_numberpipe = true;	
					break;
				}
			}
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '<'){
					if(s.size() != 1){
						int send_id = atoi(s.erase(0,j + recpip.length()).c_str());
                        user_send_idx = send_id;
                        client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
						if(send_id > 30 || c[send_id-1].valid == false){
							/* exceed max client number */
							recv_userpipe = true;
							err_recv_id = send_id;
                            munmap(c, sizeof(client) * 30);
							continue_id1 = true;
							break;
						}
                        sprintf(recvbuf,"%s%d_%d",PIPE_PATH,send_id,ID);
					    fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ, MAP_SHARED, up_fd, 0);
						if(fifo->fifolist[send_id-1][ID-1].name[0] != 0){//access(recvbuf,0) == 0
                            /* had userpipe before */
                            has_user_recvpipe = true;
                            //broadcast(6,original_input,ID,send_id);
                            broadcast_order tbo = {6,original_input,ID,send_id};
                            broadcast_list.push_front(tbo);
                            munmap(c, sizeof(client) * 30);
                        }
                        munmap(fifo,  sizeof(fifo_data));
                        if(!has_user_recvpipe){
                            /* error msg */
                            recv_userpipe = true;
                            fprintf(stdout,"*** Error: the pipe #%d->#%d does not exist yet. ***\n",
                            send_id,ID);
                            fflush(stdout);
                        }
                        munmap(c, sizeof(client) * 30);
                        continue_id1 = true;
                        break;
					}
				}
			}
			if(continue_id1) continue;
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '>'){
					if(s.size() != 1){
						int recv_id = atoi(s.erase(0,j + senpip.length()).c_str());
						user_recv_idx = recv_id;
                        client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
                        if(recv_id > 30 || c[recv_id-1].valid == false){
                            /* exceed max client number */
                            dup_userpipe = true;
                            err_send_id = recv_id;
                            munmap(c, sizeof(client) * 30);
                            continue_id2 = true;
                            break;
                        }
                        sprintf(sendbuf,"%s%d_%d",PIPE_PATH,ID,recv_id);
                        fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ, MAP_SHARED, up_fd, 0);
                        if(fifo->fifolist[ID-1][recv_id-1].name[0] != 0){//access(sendbuf,0) == 0
                            /* had userpipe before */
                            dup_userpipe = true;
                            /* error msg */
                            fprintf(stdout,"*** Error: the pipe #%d->#%d already exists. ***\n",
                            ID,recv_id);
                            fflush(stdout);
                        }
                        munmap(fifo,  sizeof(fifo_data));
                        if(!dup_userpipe){
                            /* hadn't userpipe */
                            has_user_sendpipe = true;
                            mkfifo(sendbuf, S_IFIFO | 0777);
                            fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ | PROT_WRITE, MAP_SHARED, up_fd, 0);
                            /* Copy filename and open input fd */
                            strncpy(fifo->fifolist[ID-1][recv_id-1].name,sendbuf,20);
                            //usleep(50);
                            kill(c[recv_id-1].pid,SIGUSR2);
                            fifo->fifolist[ID-1][recv_id-1].out = open(sendbuf,O_WRONLY);
                            //cout <<getpid() << " open out" << fifo->fifolist[ID-1][recv_id-1].out << endl;
                            munmap(fifo,  sizeof(fifo_data));
                            /* broadcast msg
                            broadcast(5,original_input,ID,recv_id);
                            */
                            broadcast_order tbo = {5,original_input,ID,recv_id};
                            broadcast_list.push_back(tbo);
                        }
                        munmap(c, sizeof(client) * 30);
                        continue_id2 = true;
                        break;
					}
				}
			}
			if(continue_id2) continue;

			//cout << "instr = " << s << endl;
			if(!(has_errpipe || has_numberpipe) && !has_user_recvpipe)
				instr.push_back(s);
    	}
		while(!broadcast_list.empty()){
			broadcast(broadcast_list.front().type,broadcast_list.front().msg,broadcast_list.front().ID,broadcast_list.front().tarfd);
			broadcast_list.pop_front();
            usleep(100);
		}
		if(err_recv_id != -1){
			fprintf(stdout,"*** Error: user #%d does not exist yet. ***\n",err_recv_id);
			fflush(stdout);
		}
		if(err_send_id != -1){
			fprintf(stdout,"*** Error: user #%d does not exist yet. ***\n",err_send_id);
			fflush(stdout);
		}
		if(i != input.size()-1 &&input.size() != 1){
			CreatePipe();
		}

		signal(SIGCHLD, HandleChild);
		pid_t pid;
		pid = fork();
		while (pid < 0)
		{
			usleep(1000);
			pid = fork();
		}
		/* Parent */
		if(pid != 0){
			if(i != 0){
				close(pipe_vector[i-1].in);
				close(pipe_vector[i-1].out);
			}
			//numberpipe reciever close
			for(int j = 0;j < numberpipe_vector.size();++j){
				num[j]--;
				//numberpipe erase
				if(num[j] < 0){
					close(numberpipe_vector[j].in);
					close(numberpipe_vector[j].out);	
					numberpipe_vector.erase(numberpipe_vector.begin() + j);
                    num.erase(num.begin() + j);
					j--;
				}
			}
			if(i == input.size()-1 && !(has_numberpipe || has_errpipe) && !has_user_sendpipe){
				waitpid(pid,NULL,0);
			}
			ClearUserPipe();
		}
		/* Child */
		else{
			//numberpipe recieve
			if(i == 0){
				bool has_front_pipe = false;
				int front_fd = 0;
				for(int j = numberpipe_vector.size()-1;j >= 0;--j){
					if(num[j] == 0){
						if(has_front_pipe && front_fd != 0 && front_fd != numberpipe_vector[j].in){
							fcntl(front_fd, F_SETFL, O_NONBLOCK);
							while (1) {
								char tmp;
								if (read(front_fd, &tmp, 1) < 1){
									break;
								}
								int rt = write(numberpipe_vector[j].out,&tmp,1);

							}
							has_front_pipe = false;
							dup2(numberpipe_vector[j].in,0);
						}
						else{
							dup2(numberpipe_vector[j].in,0);
							front_fd = numberpipe_vector[j].in;
							has_front_pipe = true;
						}
					}
				}
				for(int j = 0;j < numberpipe_vector.size();++j)	{
					if(num[j] == 0){
						close(numberpipe_vector[j].in);
						close(numberpipe_vector[j].out);
					}
				}
			}
			//connect pipes of each child process
			if(i != input.size()-1){
				close(1);
				dup(pipe_vector[i].out);
			}
			if(i != 0){
				close(0);
				dup(pipe_vector[i-1].in);
			}
			//numberpipe send
			if(i == input.size()-1 && has_numberpipe){
				close(1);
				dup(numberpipe_vector[numberpipe_vector.size()-1].out);
				close(numberpipe_vector[numberpipe_vector.size()-1].in);
				close(numberpipe_vector[numberpipe_vector.size()-1].out);
			}
			if(i == input.size()-1 && has_errpipe){
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,2);
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,1);
				close(numberpipe_vector[numberpipe_vector.size()-1].in);
				close(numberpipe_vector[numberpipe_vector.size()-1].out);
			}
			/* Send */
			if(has_user_sendpipe){
				fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ, MAP_SHARED, up_fd, 0);
				/* dup2 output fd */
				close(1);
				dup(fifo->fifolist[ID-1][user_recv_idx-1].out);
				close(fifo->fifolist[ID-1][user_recv_idx-1].out);
				munmap(fifo,  sizeof(fifo_data));
			}
			/* Recv */
			if(has_user_recvpipe){
				fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ | PROT_WRITE, MAP_SHARED, up_fd, 0);
				/* dup2 input fd */
				usleep(1000);
				close(0);
				dup(fifo->fifolist[user_send_idx-1][ID-1].in);
				//cerr <<getpid() << " open "<< fifo->fifolist[user_send_idx-1][ID-1].in << endl;
				fifo->fifolist[user_send_idx-1][ID-1].used = true;
				close(fifo->fifolist[user_send_idx-1][ID-1].in);
				client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
				kill(c[user_send_idx-1].pid,SIGUSR2);
				munmap(c, sizeof(client) * 30);
				munmap(fifo,  sizeof(fifo_data));
			}
			/* send to null*/
			if(dup_userpipe){
				/* dev/null */
				int devNull = open("/dev/null", O_RDWR);
				dup2(devNull,1);
				close(devNull);
			}
			/* recv from null*/
			if(recv_userpipe){
				int devNull = open("/dev/null", O_RDWR);
				dup2(devNull,0);
				close(devNull);
			}
			for(int j = 0;j < pipe_vector.size();j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
			EXECINSTR(instr);
            ClearUserPipe();
		}	
	}
	pipe_vector.clear();
}

int GETSTART(int ID){
	client_id = ID;
	clearenv();
	setenv("PATH","bin:.",1);
	while(1){
		bool name_flag = false;
		string input;
		cout << "% ";
		getline(cin,input);
		input.erase(remove(input.begin(), input.end(), '\n'),input.end());
		input.erase(remove(input.begin(), input.end(), '\r'),input.end());
		original_input = input;

		char del_c = ' ';
		istringstream is(input);
		string str;
		getline(is,str,del_c);
		if(str == "printenv"){
			getline(is,str);
			char *val = getenv(str.c_str());
			if(val) cout << val << endl;
			input = "";
		}else if(str == "setenv"){
			string name,val;
			getline(is,name,del_c);
			getline(is,val);
			setenv(name.c_str(),val.c_str(),1);
			input = "";
		}else if(str == "exit"){
			input = "";
			return 87;
		}
		else if(str == "who"){
			printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
			client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
			for(int i = 0;i < 30;i++){
				if(c[i].valid == true){
					if(c[i].pid == getpid()){
						printf("%d\t%s\t%s\t<-me\n",i+1,c[i].nickname,c[i].ip);
					}
					else
					{
						printf("%d\t%s\t%s\t\n",i+1,c[i].nickname,c[i].ip);
					}
				}
			}
			fflush(stdout);
			munmap(c, sizeof(client) * 30);
			input = "";
		}else if(str == "name"){
			string name;
			getline(is,name,del_c);
			client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ| PROT_WRITE, MAP_SHARED, data_fd, 0);
			for(int i = 0;i < 30;i++){
				if(c[i].valid == true && c[i].nickname == name){
					if(c[i].pid != getpid()){
						broadcast(1,name,ID,-1);
						name_flag = true;
						break;
					}
				}
			}
			if(name_flag == true) continue;
			name += '\0';
			strncpy(c[ID-1].nickname,name.c_str(),name.length());
			broadcast(1,name,ID,0);
			munmap(c, sizeof(client) * 30);
			input = "";
		}else if(str == "yell"){
			string msg;
			getline(is,msg);
			broadcast(2,msg,ID,0);
			input = "";
		}else if(str == "tell"){
			string msg,tmp;
			getline(is,tmp,del_c);
			getline(is,msg);
			//cerr << stoi(tmp) << endl;
			client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ, MAP_SHARED, data_fd, 0);
			int tarfd = -1;
			if(c[stoi(tmp)-1].valid){
				broadcast(3,msg,ID,stoi(tmp));
			}
			else{
				string tmpstring(to_string(stoi(tmp)));
				broadcast(3,tmpstring,ID,-1);
			}
			munmap(c, sizeof(client) * 30);
			input = "";
		}


		//Split | character
		string p = " | ";
		vector<string> c;
		vector<string> r = split(input, p);
		for (auto& s : r) {
			c.push_back(s);
			//cout << "commands = " << commands[k++] << endl;
		}	
		vector<string>number_record;
		for(int i = 0 ; i < c.size() ; i++){
		string numpip = "|";
		string space = " ";
		vector<string> instr;
        vector<string> ret = split(c[i], space);
        for (auto& s : ret) {
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '|' || s[j] == '!'){
					string number2 = s.erase(0,j + numpip.length());
					//cout << "number = " << number2 << endl;
					number_record.push_back(number2);
				}
			}
		}
		}
		int count = 0;
		int l = 0;
		int cnt = 0;
		int o = 0;
		vector<int> record;
		for(int i = 0 ; i < input.size() ; i++){
			cnt++;
			if(input[i] == '!' || input[i] == '|' && input[i+1] != ' '){
				count ++;
				record.push_back(cnt);
				if(number_record[o].length() == 1){
					cnt = -2;
					o++;
				}else if(number_record[o].length() == 2){
					cnt = -3;
					o++;
				}else if(number_record[o].length() == 3){
					cnt = -4;
					o++;
				}else if(number_record[o].length() == 4){
					cnt = -5;
					o++;
				}else{
					cnt = -6;
					o++;
				}
				//cout << "count = " << count << endl;
				//cout << "record = " << record[l++] << endl;
			}
		}
		vector<string> cmd;
		int k = 0;
		if(count > 0){
			for(int i = 0 ; i < count ; i++){
				if(number_record[i].length() == 1){
					cmd.push_back(input.substr(0,record[i])+number_record[i]);
					//cout << "cmds = " << cmd[k++] << endl;
					input.erase(0,record[i]+2);
					//cout << "remains = " << input.size() << endl;
				}else if(number_record[i].length() == 2){
					cmd.push_back(input.substr(0,record[i])+number_record[i]);
					//cout << "cmds = " << cmd[k++] << endl;
					input.erase(0,record[i]+3);
					//cout << "remains = " << input.size() << endl;
				}else if(number_record[i].length() == 3){
					cmd.push_back(input.substr(0,record[i])+number_record[i]);
					//cout << "cmds = " << cmd[k++] << endl;
					input.erase(0,record[i]+4);
					//cout << "remains = " << input.size() << endl;
				}else if(number_record[i].length() == 4){
					cmd.push_back(input.substr(0,record[i])+number_record[i]);
					//cout << "cmds = " << cmd[k++] << endl;
					input.erase(0,record[i]+5);
					//cout << "remains = " << input.size() << endl;
				}else{
					cmd.push_back(input.substr(0,record[i])+number_record[i]);
					//cout << "cmds = " << cmd[k++] << endl;
					input.erase(0,record[i]+6);
					//cout << "remains = " << input.size() << endl;
				}
				
			}
			if(input.size() > 0){
				cmd.push_back(input);
				//cout << "cmds = " << cmd[k++] << endl;
			}
			for(int j = 0 ; j < cmd.size() ; j++){
				//cout << "cmdsize = " << cmd.size() << endl;
				//Split | character
				string pattern = " | ";
				vector<string> commands;
				int k = 0;
				vector<string> ret = split(cmd[j], pattern);
				for (auto& s : ret) {
					commands.push_back(s);
					//cout << "commands = " << commands[k++] << endl;
				}
				Piping(commands,ID);
				usleep(10000);
			}
			
		}else{
			//Split | character
			string pattern = " | ";
			vector<string> commands;
			int k = 0;
			vector<string> ret = split(input, pattern);
			for (auto& s : ret) {
				commands.push_back(s);
				//cout << "commands = " << commands[k++] << endl;
			}
			Piping(commands,ID);
		}
	}
	return 0;
}

void ServerSigHandler(int sig)
{
	if (sig == SIGCHLD){
		while(waitpid (-1, NULL, WNOHANG) > 0);
	}
    else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM){
		exit (0);
	}
}

int main(int argc,char *argv[]){
	/* Initiallize user pipe folder */
	if(NULL==opendir(PIPE_PATH))
   		mkdir(PIPE_PATH,0777);
	/* Open shared memory */
	server_pid = getpid();
	shm_fd = shm_open("share_fd", O_CREAT | O_RDWR, 0777);
	ftruncate(shm_fd, 16384);

	data_fd = shm_open("client_data", O_CREAT | O_RDWR, 0777);
	ftruncate(data_fd, sizeof(client) * 30);
	client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0);
	for(int i = 0;i < 30;++i){
		c[i].valid = false;
	}
	munmap(c, sizeof(client) * 30);

	up_fd = shm_open("up_data", O_CREAT | O_RDWR, 0777);
	ftruncate(up_fd, sizeof(fifo_data));
	fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ | PROT_WRITE, MAP_SHARED, up_fd, 0);
	for(int i = 0;i < 30;++i){
		for(int j = 0;j < 30;++j){
			fifo->fifolist[i][j].used = false;
			fifo->fifolist[i][j].in = -1;
			fifo->fifolist[i][j].out = -1;
			bzero(&fifo->fifolist[i][j].name,sizeof(fifo->fifolist[i][j].name));
		}
	}
	munmap(fifo,  sizeof(fifo_data));
	/* socket setting */
	int msocket, ssocket;
	struct sockaddr_in sin;
	int port = atoi(argv[1]);
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

	if ((msocket = socket(AF_INET, SOCK_STREAM, 0))<= 0){
		perror("Error : socket fail");
        exit(1);
	}
	int optval = 1;
	if (setsockopt(msocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
		perror("Error : set socket fail");
		exit(1);
	}
	if (bind(msocket, (struct sockaddr *)&sin,sizeof(sin)) < 0){
		perror("Error : bind fail");
		exit(1);
	}
	if (listen(msocket, 5) < 0){
		perror("Error : listen fail");
		exit(1);
	}
	while(1){
		struct sockaddr_in child_addr;	
		int alen = sizeof(child_addr);
		if((ssocket  = accept(msocket,(struct sockaddr *)&child_addr,(socklen_t*)&alen))< 0){
			perror("Error : accept fail");
			exit(0);
		}
		/* Check number of clients*/
		int ID = select_min();
		if(ID == 87){
			perror("Error : Client Exceed");
			exit(0);
		}
		
		int pid = fork();
		while(pid < 0){
			usleep(1000);
			pid = fork();
		}
		if(pid > 0){
			//parent close socket
			signal (SIGCHLD, ServerSigHandler);
			signal (SIGINT, ServerSigHandler);
			signal (SIGQUIT, ServerSigHandler);
			signal (SIGTERM, ServerSigHandler);
			close(ssocket);
		}else{
            /* client ip */
            close(0);
            close(1);
            close(2);
			dup(ssocket);
            dup(ssocket);
            dup(ssocket);
			close(msocket);
			/* set info shared memory */
			void *p = mmap(NULL, sizeof(client) * 30, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0);
			client *cf = (client *) p;
			char ip[INET6_ADDRSTRLEN];
            sprintf(ip, "%s:%d", inet_ntoa(child_addr.sin_addr), ntohs(child_addr.sin_port));
			strcpy(cf[ID-1].ip,ip);
			strcpy(cf[ID-1].nickname,"(no name)");
			cf[ID-1].pid = getpid();
			cf[ID-1].valid = true;
			munmap(p, sizeof(client) * 30);
			/* Set signals for boradcast msg */
			signal(SIGUSR1, SIGHANDLE);
			signal(SIGUSR2, SIGHANDLE);
			/* Set signals for client */
			signal(SIGINT, SIGHANDLE);
			signal(SIGQUIT, SIGHANDLE);
			signal(SIGTERM, SIGHANDLE);
			/* send welcome msg */
			string buf =
        "****************************************\n\
** Welcome to the information server. **\n\
****************************************";
    		cout << buf << endl;
			/* broadcast login information */
			broadcast(0, "", ID, 0);
			//child exec shell
			if(GETSTART(ID) == 87){
				close(0);
                close(1);
                close(2);
				client *c =  (client *)mmap(NULL, sizeof(client) * 30, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0);
				c[ID-1].valid = false;
				broadcast(4,"",ID,0);
				fifo_data* fifo =  (fifo_data *)mmap(NULL, sizeof(fifo_data) , PROT_READ | PROT_WRITE, MAP_SHARED, up_fd, 0);
				for(int i = 0;i < 30; i++){
					if(fifo->fifolist[ID-1][i].in != -1){
						close(fifo->fifolist[ID-1][i].in);
						unlink(fifo->fifolist[ID-1][i].name);
					}
					if(fifo->fifolist[ID-1][i].out != -1){
						close(fifo->fifolist[ID-1][i].out);
						unlink(fifo->fifolist[ID-1][i].name);
					}
					fifo->fifolist[ID-1][i].in = -1;
					fifo->fifolist[ID-1][i].out = -1;
					fifo->fifolist[ID-1][i].used = false;
					bzero(&fifo->fifolist[ID-1][i].name, sizeof(fifo->fifolist[ID-1][i].name));
				}
				for(int i = 0;i < 30; i++){
					if(fifo->fifolist[i][ID-1].in != -1){
						close(fifo->fifolist[i][ID-1].in);
						unlink(fifo->fifolist[i][ID-1].name);
					}
					if(fifo->fifolist[i][ID-1].out != -1){
						close(fifo->fifolist[i][ID-1].out);
						unlink(fifo->fifolist[i][ID-1].name);
					}
					fifo->fifolist[i][ID-1].in = -1;
					fifo->fifolist[i][ID-1].out = -1;
					fifo->fifolist[i][ID-1].used = false;
					bzero(&fifo->fifolist[i][ID-1].name, sizeof(fifo->fifolist[i][ID-1].name));
				}
				munmap(fifo,  sizeof(fifo_data));
				munmap(c, sizeof(client) * 30);
                exit(0);
			}
		}
	}
	return 0;
}
