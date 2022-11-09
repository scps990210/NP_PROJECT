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

#define CLIENTMAX 30
#define PATHMAX 20
#define BUFSIZE 16384
#define PIPE_PATH "user_pipe/"

using namespace std;

struct npipe{
	int in;
	int out;
	int num;
};

struct client{
	bool valid;
    int ID;
    char ip[INET6_ADDRSTRLEN];
    char nickname[20];
	int cpid;
};

struct FIFOunit{
	int in;
	int out;
	bool used;
	char name[PATHMAX];
};

struct fifo_info{
	FIFOunit fifolist[CLIENTMAX][CLIENTMAX];
};

struct broadcast_order{
	int type;
	string msg;
	int ID;
	int tarfd;
};

int shm_fd;
int info_shared_fd;
int userpipe_shared;
int server_pid;
int client_id;
/* used to store user information */
vector<client> client_info;
/* Get  minimum number of client */
int get_min_num(){
	client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
	for(int i = 0;i < CLIENTMAX;++i){
		if(!c[i].valid){
			munmap(c, sizeof(client) * CLIENTMAX);
			return i+1;
		}
	}
	munmap(c, sizeof(client) * CLIENTMAX);
	return -1;
}

/* broadcast method with shared memory */
void broadcast(int type,string msg,int ID,int tarfd){
	char buf[BUFSIZE];
	memset( buf, 0, sizeof(char)*BUFSIZE );
	client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
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
				munmap(c, sizeof(client) * CLIENTMAX);
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
	usleep(50);
	if(type != 3){
		for(int i = 0;i < CLIENTMAX;++i){
			/* check id valid */
			if(c[i].valid == true){
				kill(c[i].cpid,SIGUSR1);
			}
		}
	}else if(tarfd != -1){
		kill(c[tarfd-1].cpid,SIGUSR1);
	}
	munmap(c, sizeof(client) * CLIENTMAX);
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
		fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ | PROT_WRITE, MAP_SHARED, userpipe_shared, 0);
		for(int i = 0;i < CLIENTMAX;++i){
			if(f->fifolist[client_id-1][i].used){
				close(f->fifolist[client_id-1][i].out);
				memset(&f->fifolist[client_id-1][i].name, 0, sizeof(f->fifolist[client_id-1][i].name));
				f->fifolist[client_id-1][i].out = -1;
				f->fifolist[client_id-1][i].used = false;
				//cerr << getpid()<< " close out " << f->fifolist[client_id-1][i].out << endl;
			}
			if(f->fifolist[i][client_id-1].used){
				f->fifolist[i][client_id-1].used = false;
				memset(&f->fifolist[i][client_id-1].name, 0, sizeof(f->fifolist[i][client_id-1].name));
				f->fifolist[i][client_id-1].in = -1;
				close(f->fifolist[i][client_id-1].in);
				//cerr << getpid()<< " close in " << f->fifolist[i][client_id-1].in << endl;
			}
			if(f->fifolist[i][client_id-1].in == -1 && f->fifolist[i][client_id-1].name[0] != 0){
				f->fifolist[i][client_id-1].in = open(f->fifolist[i][client_id-1].name,O_RDONLY);
				//cerr << getpid()<< " open in " << f->fifolist[i][client_id-1].in << endl;
			}
		}
		munmap(f,  sizeof(fifo_info));
	}
	else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM)
    {
		exit (0);
	}
	signal(sig, SIGHANDLE);
}

void EraseUserPipe(int ID){
	fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ | PROT_WRITE, MAP_SHARED, userpipe_shared, 0);
	for(int id = 0;id < CLIENTMAX;++id){
		if(f->fifolist[ID-1][id].in != -1){
			close(f->fifolist[ID-1][id].in);
			unlink(f->fifolist[ID-1][id].name);
		}
		if(f->fifolist[ID-1][id].out != -1){
			close(f->fifolist[ID-1][id].out);
			unlink(f->fifolist[ID-1][id].name);
		}
		f->fifolist[ID-1][id].in = -1;
		f->fifolist[ID-1][id].out = -1;
		f->fifolist[ID-1][id].used = false;
		memset(&f->fifolist[ID-1][id].name, 0, sizeof(f->fifolist[ID-1][id].name));
	}
	for(int id = 0;id < CLIENTMAX;++id){
		if(f->fifolist[id][ID-1].in != -1){
			close(f->fifolist[id][ID-1].in);
			unlink(f->fifolist[id][ID-1].name);
		}
		if(f->fifolist[id][ID-1].out != -1){
			close(f->fifolist[id][ID-1].out);
			unlink(f->fifolist[id][ID-1].name);
		}
		f->fifolist[id][ID-1].in = -1;
		f->fifolist[id][ID-1].out = -1;
		f->fifolist[id][ID-1].used = false;
		memset(&f->fifolist[id][ID-1].name, 0, sizeof(f->fifolist[id][ID-1].name));
	}
	munmap(f,  sizeof(fifo_info));
}
vector<npipe> numberpipe_vector;
string original_input;
list<broadcast_order> fix_order;
//used to store pipe
vector<npipe> pipe_vector;
vector<string> userpipe;
static void HandleChild(int);
void SETENV(string, string);
        void PRINTENV(string);
		void WHO();
		void NAME(string,int);
		void YELL(int,string);
		void TELL(int,string,int);
		void ClearUserPipe();
        int CheckBuiltIn(string *,int);
        int CheckPIPE(string,int);
        void CreatePipe(int,int,int);
        bool isWhitespace(string);
        int ParseCMD(vector<string>,int);
        int EXECCMD(vector<string>);
        int EXEC(int);

void HandleChild(int sig){
	int status;
	while(waitpid(-1,&status,WNOHANG) > 0){
	};
}

void SETENV(string name,string val){
	setenv(name.c_str(),val.c_str(),1);
}

void PRINTENV(string name){
	char *val = getenv(name.c_str());
	if(val) cout << val << endl;
}

void WHO(){
	printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
	client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
	for(int i = 0;i < CLIENTMAX;i++){
		if(c[i].valid == true){
			if(c[i].cpid == getpid()){
				printf("%d\t%s\t%s\t<-me\n",i+1,c[i].nickname,c[i].ip);
			}
			else
			{
				printf("%d\t%s\t%s\t\n",i+1,c[i].nickname,c[i].ip);
			}
		}
	}
	fflush(stdout);
	munmap(c, sizeof(client) * CLIENTMAX);
}

void NAME(string name,int ID){
	client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ| PROT_WRITE, MAP_SHARED, info_shared_fd, 0);
	for(int i = 0;i < CLIENTMAX;i++){
		if(c[i].valid == true && c[i].nickname == name){
			if(c[i].cpid != getpid()){
				broadcast(1,name,ID,-1);
				return;
			}
		}
	}
	name += '\0';
	strncpy(c[ID-1].nickname,name.c_str(),name.length());
	broadcast(1,name,ID,0);
	munmap(c, sizeof(client) * CLIENTMAX);
}

void YELL(int ID,string msg){
	broadcast(2,msg,ID,0);
}

void TELL(int ID,string msg,int target){
	client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
	int tarfd = -1;
	if(c[target-1].valid){
		broadcast(3,msg,ID,target);
	}
	else{
		string tmpstring(to_string(target));
		broadcast(3,tmpstring,ID,-1);
	}
	munmap(c, sizeof(client) * CLIENTMAX);
	return;
}

int CheckBuiltIn(string *input,int ID){
	istringstream iss(*input);
	string cmd;
	getline(iss,cmd,' ');
	if(cmd == "printenv"){
		getline(iss,cmd);
		PRINTENV(cmd);
		*input = "";
		return 1;
	}else if(cmd == "setenv"){
		string name,val;
		getline(iss,name,' ');
		getline(iss,val);
		SETENV(name,val);
		*input = "";
		return 1;
	}else if(cmd == "exit"){
		*input = "";
		return -1;
	}
	else if(cmd == "who"){
		WHO();
		*input = "";
		return 1;
	}else if(cmd == "name"){
		string name;
		getline(iss,name,' ');
		NAME(name,ID);
		*input = "";
		return 1;
	}else if(cmd == "yell"){
		string msg;
		getline(iss,msg);
		YELL(ID,msg);
		*input = "";
		return 1;
	}else if(cmd == "tell"){
		string msg,tmp;
		getline(iss,tmp,' ');
		getline(iss,msg);
		//cerr << stoi(tmp) << endl;
		TELL(ID,msg,stoi(tmp));
		*input = "";
		return 1;
	}
	return 0;
}

int CheckPIPE(string input,int ID){
	vector<string> cmds;
	string delim = " | ";
	size_t pos = 0;
	bool exit_sig = false;
	while((pos = input.find(delim)) != string::npos){
		cmds.push_back(input.substr(0,pos));
		input.erase(0,pos + delim.length());
	}
	if(CheckBuiltIn(&input,ID) == -1) exit_sig = true;
	cmds.push_back(input);
	ParseCMD(cmds,ID);
	if(exit_sig) return -1;
	return 0;
}

void CreatePipe(int in,int out,int num){
	npipe np = {in,out,num};
	pipe_vector.push_back(np);
}

bool isWhitespace(string s){
    for(int index = 0; index < s.length(); index++){
        if(!isspace(s[index]))
            return false;
    }
    return true;
}

void ClearUserPipe(){
	fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ | PROT_WRITE, MAP_SHARED, userpipe_shared, 0);
	for(int id = 0;id < CLIENTMAX;++id){
		if(f->fifolist[id][client_id-1].used == true){
			close(f->fifolist[id][client_id-1].in);
			f->fifolist[id][client_id-1].in = -1;
			f->fifolist[id][client_id-1].out = -1;
			f->fifolist[id][client_id-1].used = false;
			unlink(f->fifolist[id][client_id-1].name);
			memset(&f->fifolist[id][client_id-1].name, 0, sizeof(f->fifolist[id-1][client_id-1].name));
		}
		if(f->fifolist[client_id-1][id].used == true){
			close(f->fifolist[client_id-1][id].out);
			f->fifolist[client_id-1][id].in = -1;
			f->fifolist[client_id-1][id].out = -1;
			f->fifolist[client_id-1][id].used = false;
			unlink(f->fifolist[client_id-1][id].name);
			memset(&f->fifolist[client_id-1][id].name, 0, sizeof(f->fifolist[client_id-1][id].name));
		}
	}
	munmap(f,  sizeof(fifo_info));
}

int ParseCMD(vector<string> input,int ID){
	size_t pos = 0;
	bool has_numberpipe = false,has_errpipe = false;
	string numpipe_delim = "|";
	string errpipe_delim = "!";
	string user_recvpipe_delim = "<";
	string user_sendpipe_delim = ">";
	for(int i = 0;i < input.size();++i){
		string cmd;
		istringstream iss(input[i]);
		vector<string> parm;
		bool has_user_sendpipe = false,has_user_recvpipe = false,dup_userpipe = false,recv_userpipe = false;
		int user_send_idx = 0,user_recv_idx = 0;
		int err_send_id = -1,err_recv_id = -1;
		char send_fifo_name[PATHMAX];
		char recv_fifo_name[PATHMAX];
		// Create pipe for number pipe, last one is for number
		while(getline(iss,cmd,' ')){
			if(isWhitespace(cmd)) continue;
			//if still find "!" means errorpipe with number,and record the number
			if((pos = cmd.find(errpipe_delim)) != string::npos){
				int numberpipe[2];
				int tmpnum = atoi(cmd.erase(0,pos+numpipe_delim.length()).c_str());
				for(int j = 0;j < numberpipe_vector.size();++j){
					if(tmpnum == numberpipe_vector[j].num){
						numberpipe[0] = numberpipe_vector[j].in;
						numberpipe[1] = numberpipe_vector[j].out;
					}
					else{
						pipe(numberpipe);
					}
				}
				if(numberpipe_vector.size() == 0) pipe(numberpipe);
				npipe np = {numberpipe[0],numberpipe[1],tmpnum};
				numberpipe_vector.push_back(np);
				has_errpipe = true;
				continue;
			}

			//if still find "|" means numberpipe with number,and record the number
			if((pos = cmd.find(numpipe_delim)) != string::npos){
				int numberpipe[2];
				int tmpnum = atoi(cmd.erase(0,pos+numpipe_delim.length()).c_str());
				for(int j = 0;j < numberpipe_vector.size();++j){
					if(tmpnum == numberpipe_vector[j].num){
						numberpipe[0] = numberpipe_vector[j].in;
						numberpipe[1] = numberpipe_vector[j].out;
					}
					else{
						pipe(numberpipe);
					}
				}
				if(numberpipe_vector.size() == 0) pipe(numberpipe);
				npipe np = {numberpipe[0],numberpipe[1],tmpnum};
				numberpipe_vector.push_back(np);
				has_numberpipe = true;
				continue;
			}
			/* check user pipe (receive<) exclude file redirection */
			if((pos = cmd.find(user_recvpipe_delim)) != string::npos){
				if(cmd.size() != 1){
					int send_id = atoi(cmd.erase(0,pos+user_recvpipe_delim.length()).c_str());
					user_send_idx = send_id;
					client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
					if(send_id > 30 || c[send_id-1].valid == false){
						/* exceed max client number */
						recv_userpipe = true;
						err_recv_id = send_id;
						munmap(c, sizeof(client) * CLIENTMAX);
						continue;
					}
					sprintf(recv_fifo_name,"%s%d_%d",PIPE_PATH,send_id,ID);
					fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ, MAP_SHARED, userpipe_shared, 0);
					if(f->fifolist[send_id-1][ID-1].name[0] != 0){//access(recv_fifo_name,0) == 0
						/* had userpipe before */
						has_user_recvpipe = true;
						//broadcast(6,original_input,ID,send_id);
						broadcast_order tbo = {6,original_input,ID,send_id};
						fix_order.push_front(tbo);
						munmap(c, sizeof(client) * CLIENTMAX);
					}
					munmap(f,  sizeof(fifo_info));
					if(!has_user_recvpipe){
						/* error msg */
						recv_userpipe = true;
						fprintf(stdout,"*** Error: the pipe #%d->#%d does not exist yet. ***\n",
						send_id,ID);
						fflush(stdout);
					}
					munmap(c, sizeof(client) * CLIENTMAX);
					continue;
				}
			}
			/* check user pipe (send>) exclude file redirection */
			if((pos = cmd.find(user_sendpipe_delim)) != string::npos){
				/* exclude file redirection */
				if(cmd.size() != 1){
					int recv_id = atoi(cmd.erase(0,pos+user_sendpipe_delim.length()).c_str());
					user_recv_idx = recv_id;
					client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
					if(recv_id > 30 || c[recv_id-1].valid == false){
						/* exceed max client number */
						dup_userpipe = true;
						err_send_id = recv_id;
						munmap(c, sizeof(client) * CLIENTMAX);
						continue;
					}
					sprintf(send_fifo_name,"%s%d_%d",PIPE_PATH,ID,recv_id);
					fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ, MAP_SHARED, userpipe_shared, 0);
					if(f->fifolist[ID-1][recv_id-1].name[0] != 0){//access(send_fifo_name,0) == 0
						/* had userpipe before */
						dup_userpipe = true;
						/* error msg */
						fprintf(stdout,"*** Error: the pipe #%d->#%d already exists. ***\n",
						ID,recv_id);
						fflush(stdout);
					}
					munmap(f,  sizeof(fifo_info));
					if(!dup_userpipe){
						/* hadn't userpipe */
						has_user_sendpipe = true;
						mkfifo(send_fifo_name, S_IFIFO | 0777);
						fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ | PROT_WRITE, MAP_SHARED, userpipe_shared, 0);
						/* Copy filename and open input fd */
						strncpy(f->fifolist[ID-1][recv_id-1].name,send_fifo_name,PATHMAX);
						//usleep(50);
						kill(c[recv_id-1].cpid,SIGUSR2);
						f->fifolist[ID-1][recv_id-1].out = open(send_fifo_name,O_WRONLY);
						//cout <<getpid() << " open out" << f->fifolist[ID-1][recv_id-1].out << endl;
						munmap(f,  sizeof(fifo_info));
						/* broadcast msg
						broadcast(5,original_input,ID,recv_id);
						*/
						broadcast_order tbo = {5,original_input,ID,recv_id};
						fix_order.push_back(tbo);
					}
					munmap(c, sizeof(client) * CLIENTMAX);
					continue;
				}
			}
			parm.push_back(cmd);
		}
		while(!fix_order.empty()){
			broadcast_order tbo = fix_order.front();
			broadcast(tbo.type,tbo.msg,tbo.ID,tbo.tarfd);
			fix_order.pop_front();
			usleep(50);
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
			int pipes[2];
			pipe(pipes);
			CreatePipe(pipes[0],pipes[1],i);
		}

		signal(SIGCHLD, HandleChild);
		pid_t cpid;
		int status;
		cpid = fork();
		while (cpid < 0)
		{
			usleep(1000);
			cpid = fork();
		}
		/* Parent */
		if(cpid != 0){
			//Check fork information
			//cout << "fork " << cpid << endl;
			if(i != 0){
				close(pipe_vector[i-1].in);
				close(pipe_vector[i-1].out);
			}
			//numberpipe reciever close
			for(int j = 0;j < numberpipe_vector.size();++j){
				numberpipe_vector[j].num--;
				//numberpipe erase
				if(numberpipe_vector[j].num < 0){
					close(numberpipe_vector[j].in);
					close(numberpipe_vector[j].out);	
					numberpipe_vector.erase(numberpipe_vector.begin() + j);
					j--;
				}
			}
			if(i == input.size()-1 && !(has_numberpipe || has_errpipe) && !has_user_sendpipe){
				waitpid(cpid,&status,0);
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
					if(numberpipe_vector[j].num == 0){
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
							dup2(numberpipe_vector[j].in,STDIN_FILENO);
						}
						else{
							dup2(numberpipe_vector[j].in,STDIN_FILENO);
							front_fd = numberpipe_vector[j].in;
							has_front_pipe = true;
						}
					}
				}
				for(int j = 0;j < numberpipe_vector.size();++j)	{
					if(numberpipe_vector[j].num == 0){
						close(numberpipe_vector[j].in);
						close(numberpipe_vector[j].out);
					}
				}
			}
			//connect pipes of each child process
			if(i != input.size()-1){
				dup2(pipe_vector[i].out,STDOUT_FILENO);	
			}
			if(i != 0){
				dup2(pipe_vector[i-1].in,STDIN_FILENO);
			}
			//numberpipe send
			if(i == input.size()-1 && has_numberpipe){
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,STDOUT_FILENO);
				close(numberpipe_vector[numberpipe_vector.size()-1].in);
				close(numberpipe_vector[numberpipe_vector.size()-1].out);
			}
			if(i == input.size()-1 && has_errpipe){
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,STDERR_FILENO);
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,STDOUT_FILENO);
				close(numberpipe_vector[numberpipe_vector.size()-1].in);
				close(numberpipe_vector[numberpipe_vector.size()-1].out);
			}
			/* Send */
			if(has_user_sendpipe){
				fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ, MAP_SHARED, userpipe_shared, 0);
				/* dup2 output fd */
				dup2(f->fifolist[ID-1][user_recv_idx-1].out,STDOUT_FILENO);
				close(f->fifolist[ID-1][user_recv_idx-1].out);
				munmap(f,  sizeof(fifo_info));
			}
			/* Recv */
			if(has_user_recvpipe){
				fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ | PROT_WRITE, MAP_SHARED, userpipe_shared, 0);
				/* dup2 input fd */
				usleep(50);
				dup2(f->fifolist[user_send_idx-1][ID-1].in,STDIN_FILENO);
				//cerr <<getpid() << " open "<< f->fifolist[user_send_idx-1][ID-1].in << endl;
				f->fifolist[user_send_idx-1][ID-1].used = true;
				close(f->fifolist[user_send_idx-1][ID-1].in);
				client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ, MAP_SHARED, info_shared_fd, 0);
				kill(c[user_send_idx-1].cpid,SIGUSR2);
				munmap(c, sizeof(client) * CLIENTMAX);
				munmap(f,  sizeof(fifo_info));
			}
			/* send to null*/
			if(dup_userpipe){
				/* dev/null */
				int devNull = open("/dev/null", O_RDWR);
				dup2(devNull,STDOUT_FILENO);
				close(devNull);
			}
			/* recv from null*/
			if(recv_userpipe){
				int devNull = open("/dev/null", O_RDWR);
				dup2(devNull,STDIN_FILENO);
				close(devNull);
			}
			for(int j = 0;j < pipe_vector.size();j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
			EXECCMD(parm);
			ClearUserPipe();
		}
	}
	pipe_vector.clear();
	return 0;
}

int EXECCMD(vector<string> parm){
	int fd;
	bool file_redirection = false;	
	const char **argv = new const char* [parm.size()+1];
	for(int i=0;i < parm.size();++i){
		//file redirect
		if(parm[i] == ">"){
			file_redirection = true;
			fd = open(parm.back().c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
			parm.pop_back();
			parm.pop_back();
		}
		argv[i] = parm[i].c_str();
	}
	argv[parm.size()] = NULL;
	if(file_redirection){
		// stdout to file
		if(dup2(fd,1) < 0){
			cerr << "dup error" << endl;
		}
		close(fd);
	}
	if(execvp(parm[0].c_str(),(char **)argv) == -1){
		//stderr for unknown command
		if(parm[0] != "setenv" && parm[0] != "printenv" && parm[0] != "exit")
			fprintf(stderr,"Unknown command: [%s].\n",parm[0].c_str());
		exit(0);
		//char *argv[] = {(char*)NULL};
		//execv("./bin/noop", argv);
		return -1;
	}
	return 0;
}

int EXEC(int ID){
	//cout << getpid() << endl;
	client_id = ID;
	clearenv();
	SETENV("PATH","bin:.");
	while(1){
		string input;
		cout << "% ";
		getline(cin,input);
		if(!cin)
			if(cin.eof())
			{
				cout << endl;
				return 0;
			}
		if(input.empty() || isWhitespace(input)) continue;
		input.erase(remove(input.begin(), input.end(), '\n'),input.end());
		input.erase(remove(input.begin(), input.end(), '\r'),input.end());
		original_input = input;
		//cout << original_input << endl;
		if(CheckPIPE(input,ID)  == -1){
			return -1;
		}
	}
	return 0;
}

bool sortid(client s1, client s2){
   return s1.ID < s2.ID;
}

void welcome(int fd)
{
    string buf =
        "****************************************\n\
** Welcome to the information server. **\n\
****************************************";
    cout << buf << endl;
    return;
}
void ServerSigHandler(int sig)
{
	if (sig == SIGCHLD)
    {
		while(waitpid (-1, NULL, WNOHANG) > 0);
	}
    else if(sig == SIGINT || sig == SIGQUIT || sig == SIGTERM)
    {
		exit (0);
	}
	signal (sig, ServerSigHandler);
}

void init_info_shared_memory(){
	/* shared memory store client info */
	info_shared_fd = shm_open("used_to_store_client_info", O_CREAT | O_RDWR, 0777);
	ftruncate(info_shared_fd, sizeof(client) * CLIENTMAX);
	client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ | PROT_WRITE, MAP_SHARED, info_shared_fd, 0);
	for(int i = 0;i < CLIENTMAX;++i){
		c[i].valid = false;
	}
	munmap(c, sizeof(client) * CLIENTMAX);
}

void init_FIFO_shared_memory(){
	/* shared memory store client info */
	userpipe_shared = shm_open("used_to_store_userpipe", O_CREAT | O_RDWR, 0777);
	ftruncate(userpipe_shared, sizeof(fifo_info));
	fifo_info* f =  (fifo_info *)mmap(NULL, sizeof(fifo_info) , PROT_READ | PROT_WRITE, MAP_SHARED, userpipe_shared, 0);
	for(int i = 0;i < CLIENTMAX;++i){
		for(int j = 0;j < CLIENTMAX;++j){
			f->fifolist[i][j].used = false;
			f->fifolist[i][j].in = -1;
			f->fifolist[i][j].out = -1;
			memset(&f->fifolist[i][j].name,0,sizeof(f->fifolist[i][j].name));
		}
	}
	munmap(f,  sizeof(fifo_info));
}

int main(int argc,char *argv[]){
	/* Initiallize user pipe folder */
	if(NULL==opendir(PIPE_PATH))
   		mkdir(PIPE_PATH,0777);
	/* Open shared memory */
	server_pid = getpid();
	shm_fd = shm_open("used_to_broadcast", O_CREAT | O_RDWR, 0777);
	ftruncate(shm_fd, 16384);
	init_info_shared_memory();
	init_FIFO_shared_memory();
	/* socket setting */
	int server_fd, child_socket;
	struct sockaddr_in sin;
	int port = atoi(argv[1]);
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd <= 0){
		perror("Error : socket fail");
        exit(1);
	}
	int optval = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
		perror("Error: set socket fail");
		exit(1);
	}
	if (bind(server_fd, (struct sockaddr *)&sin,sizeof(sin)) < 0){
		perror("Error: bind fail");
		exit(1);
	}
	if (listen(server_fd, 0) < 0){
		perror("Error: listen fail");
		exit(1);
	}
	while(1){
		struct sockaddr_in child_addr;	
		int addrlen = sizeof(child_addr);
		child_socket = accept(server_fd,(struct sockaddr *)&child_addr,(socklen_t*)&addrlen);
		
		/* Check number of clients*/
		int ID = get_min_num();
		if(ID < 0){
			perror("Client MAX");
			return 0;
		}
		if(child_socket < 0){
			perror("accept failed");
			exit(EXIT_FAILURE);
		}
		int cpid = fork();
		while(cpid < 0){
			//fork error
			usleep(500);
			cpid = fork();
		}
		if(cpid > 0){
			//parent close socket
			signal (SIGCHLD, ServerSigHandler);
			signal (SIGINT, ServerSigHandler);
			signal (SIGQUIT, ServerSigHandler);
			signal (SIGTERM, ServerSigHandler);
			close(child_socket);
		}else{
            /* client ip */
			dup2(child_socket,STDIN_FILENO);
			dup2(child_socket,STDOUT_FILENO);
			dup2(child_socket,STDERR_FILENO);
			close(child_socket);
			close(server_fd);
			/* set info shared memory */
			void *p = mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ | PROT_WRITE, MAP_SHARED, info_shared_fd, 0);
			client *cf = (client *) p;
			char ip[INET6_ADDRSTRLEN];
            sprintf(ip, "%s:%d", inet_ntoa(child_addr.sin_addr), ntohs(child_addr.sin_port));
			strncpy(cf[ID-1].ip,ip,INET6_ADDRSTRLEN);
			strcpy(cf[ID-1].nickname,"(no name)");
			cf[ID-1].cpid = getpid();
			cf[ID-1].valid = true;
			munmap(p, sizeof(client) * CLIENTMAX);
			/* Set signals for boradcast msg */
			signal(SIGUSR1, SIGHANDLE);
			signal(SIGUSR2, SIGHANDLE);
			/* Set signals for client */
			signal(SIGINT, SIGHANDLE);
			signal(SIGQUIT, SIGHANDLE);
			signal(SIGTERM, SIGHANDLE);
			/* send welcome msg */
			welcome(child_socket);
			/* broadcast login information */
			broadcast(0, "", ID, 0);
			//child exec shell
			if(EXEC(ID) == -1){
				close(STDIN_FILENO);
                close(STDOUT_FILENO);
                close(STDERR_FILENO);
				client *c =  (client *)mmap(NULL, sizeof(client) * CLIENTMAX, PROT_READ | PROT_WRITE, MAP_SHARED, info_shared_fd, 0);
				c[ID-1].valid = false;
				broadcast(4,"",ID,0);
				EraseUserPipe(ID);
				munmap(c, sizeof(client) * CLIENTMAX);
                exit(0);
			}
		}
	}
	return 0;
}
