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
#include <map>
#include <list>

#define QLEN 5
#define BUFSIZE 4096
#define CLIENTMAX 30

using namespace std;
void EXECINSTR(vector<string> instr,int client_fd);
int GETSTART(string input,int fd);
int passiveTCP(int qlen);
int echo(int fd);
void welcome(int fd);
void Piping(vector<string> input,int fd);

struct npipe{
	int in;
	int out;
};

vector<npipe> pipe_vector;
struct userpipe{
	int in;
	int out;
	int send_id;
	int recv_id;
	bool used;
};

struct client{
    int ID;
    string ip;
    string nickname;
	int fd;
	/* used to store numberpipe */
	vector<npipe> numberpipe_vector;
	vector<int> num;
	/* used to sotre user env setting */
	map<string, string> mapenv;
};

struct broadcast_order{
	int type;
	string msg;
	client *cnt;
	int tarfd;
};

/* used to store user information */
vector<client> client_info;
/* userd to store user pipe information */
vector<userpipe> up_vector;

int msock;               /* master server socket	*/
fd_set rfds;             /* read file descriptor set	*/
fd_set afds;             /* active file descriptor set */
int ID_arr[CLIENTMAX];

int get_min_num(){
	for(int i = 0;i < CLIENTMAX;++i){
		if(ID_arr[i] == 0){
			ID_arr[i] = 1;
			return i+1;
		}
	}
	return -1;
}

void broadcast(int type,string msg,client *client,int tarfd){
	int nfds = getdtablesize();
	char buf[BUFSIZE];
	bzero(buf,sizeof(buf));
	switch(type){
		case 0:
			/* Login */
			sprintf(buf, "*** User '(no name)' entered from %s. ***\n", client->ip.c_str());
			break;
		case 1:
			/* Change name */
			if(tarfd == -1){
				sprintf(buf,"*** User '%s' already exists. ***\n",msg.c_str());
				string tmp(buf);
				if(write(client->fd, tmp.c_str(), tmp.length()) < 0)
					perror("change name unknown error");
				return;
			}else{
				sprintf(buf,"*** User from %s is named '%s'. ***\n",client->ip.c_str(),msg.c_str());
			}
			break;
		case 2:
			/* Yell with msg */
			sprintf(buf,"*** %s yelled ***: %s\n",client->nickname.c_str(),msg.c_str());
			break;
		case 3:
			/* Tell with msg and target */
			if(tarfd == -1){
				sprintf(buf,"*** Error: user #%s does not exist yet. ***\n",msg.c_str());
				string tmp(buf);
				if(write(client->fd, tmp.c_str(), tmp.length()) < 0)
					perror("Tell unknown error");
			}else{
				sprintf(buf,"*** %s told you ***: %s\n",client->nickname.c_str(),msg.c_str());
				string tmp(buf);
				if(write(tarfd, tmp.c_str(), tmp.length()) < 0)
					perror("Tell error");
			}
			break;
		case 4:
			/* Logout */
			sprintf(buf,"*** User '%s' left. ***\n",client->nickname.c_str());
			break;
		case 5:
			/* send user pipe information */
			/* Success to send userpipe */
			for(int i = 0;i < client_info.size();i++){
				if(client_info[i].ID == tarfd){
					sprintf(buf,"*** %s (#%d) just piped '%s' to %s (#%d) ***\n",
					client->nickname.c_str(),client->ID,msg.c_str(),client_info[i].nickname.c_str(),client_info[i].ID);
					break;
				}
			}
			break;
		case 6:
			/* recv user pipe information */
			/* Success to send userpipe */
			for(int i = 0;i < client_info.size();i++){
				if(client_info[i].ID == tarfd){
					sprintf(buf,"*** %s (#%d) just received from %s (#%d) by '%s' ***\n",
					client->nickname.c_str(),client->ID,client_info[i].nickname.c_str(),client_info[i].ID,msg.c_str());
					/* tarfd is ID*/
					break;
				}
			}
			break;
		default:
			perror("unknown brroadcast type");
			break;
	}
	/* Not tell msg */
	string tmp(buf);
	if(type != 3)
		for (int fd = 0; fd < nfds; ++fd)
		{
			//send to all active fd
			if (fd != msock && FD_ISSET(fd, &afds))
			{
				int cc = write(fd, tmp.c_str(), tmp.length());
			}
		}
}

void DeleteClient(int fd){
	int id;
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd){
			ID_arr[client_info[i].ID-1] = 0;
			id = client_info[i].ID;
			client_info.erase(client_info.begin()+i);
			break;
		}
	}
	for(int i = 0;i < up_vector.size();++i){
		if(up_vector[i].send_id == id || up_vector[i].recv_id == id){
			close(up_vector[i].in);
			close(up_vector[i].out);
			up_vector.erase(up_vector.begin()+i);
		}
	}
}
string original_input;
list<broadcast_order> fix_order;
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
void SETENV(string name,string val,int fd){
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd){
			client_info[i].mapenv[name] = val;
			break;
		}
	}
}

string PRINTENV(string name,int fd){
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd){
			auto it = client_info[i].mapenv.find(name);
			if (it != client_info[i].mapenv.end()) {
				return it->second;
			}
		}
	}
	return  name + " not found!";
}

void WHO(int fd){
	printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd){
			printf("%d\t%s\t%s\t<-me\n",client_info[i].ID,client_info[i].nickname.c_str(),client_info[i].ip.c_str());
		}
		else{
			printf("%d\t%s\t%s\t\n",client_info[i].ID,client_info[i].nickname.c_str(),client_info[i].ip.c_str());
		}
	}
	fflush(stdout);
}

void NAME(int fd,string name){
	client c_tmp;
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd) c_tmp = client_info[i];
		if(client_info[i].nickname == name){
			if(client_info[i].fd != fd){
				broadcast(1,name,&c_tmp,-1);
				return;
			}
		}
	}
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd && client_info[i].nickname != name){
			client_info[i].nickname = name;
			broadcast(1,name,&client_info[i],0);
			break;
		}
	}
}

void YELL(int fd,string msg){
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd){
			broadcast(2,msg,&client_info[i],0);
		}
	}
}

void TELL(int fd,string msg,int target){
	client me,tar;
	tar.fd = -1;
	for(int i = 0;i < client_info.size();i++){
		if(client_info[i].fd == fd){
			me = client_info[i];
		}
		if(client_info[i].ID == target){
			tar = client_info[i];
		}
	}
	if(tar.fd == -1) msg = to_string(target);
	broadcast(3,msg,&me,tar.fd);
}

void HandleChild(int sig){
	while(waitpid(-1,NULL,WNOHANG) > 0){
	}
}

void CreatePipe(){
	int pipes[2];
	pipe(pipes);
	npipe np = {pipes[0],pipes[1]};
	pipe_vector.push_back(np);
}

void Piping(vector<string> input,int fd){
	bool has_numberpipe = false,has_errpipe = false;
	string numpip = "|";
	string errpip = "!";
	string recpip = "<";
	string senpip = ">";
	string space = " ";
	client *c;
	for(int i = 0;i < client_info.size();i++){
		c = &client_info[i];
		if(c->fd == fd) break;
	}
	for(int i = 0 ; i < input.size() ; i++){
		bool has_user_sendpipe = false,has_user_recvpipe = false,dup_userpipe = false,recv_userpipe = false;
		int user_send_idx = 0,user_recv_idx = 0;
		int err_send_id = -1,err_recv_id = -1;
		vector<string> instr;
        vector<string> ret = split(input[i], space);
        for (auto& s : ret) {
			bool continue_id1 = false,continue_id2 = false;
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '!'){
					int numberpipe[2];
					int number = atoi(s.erase(0,j + errpip.length()).c_str());
					for(int k = 0; k < c->numberpipe_vector.size() ; k++){
						if(number == c->num[k]){
							numberpipe[0] = c->numberpipe_vector[k].in;
							numberpipe[1] = c->numberpipe_vector[k].out;
						}
						else{
							pipe(numberpipe);
						}
					}
					if(c->numberpipe_vector.size() == 0) pipe(numberpipe);
					npipe np = {numberpipe[0],numberpipe[1]};
					c->numberpipe_vector.push_back(np);
					c->num.push_back(number);
					has_errpipe = true;	
					break;
				}
			}
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '|'){
					int numberpipe[2];
					int number = atoi(s.erase(0,j + numpip.length()).c_str());
					for(int k = 0; k < c->numberpipe_vector.size() ; k++){
						if(number == c->num[k]){
							numberpipe[0] = c->numberpipe_vector[k].in;
							numberpipe[1] = c->numberpipe_vector[k].out;
						}
						else{
							pipe(numberpipe);
						}
					}
					if(c->numberpipe_vector.size() == 0) pipe(numberpipe);
					npipe np = {numberpipe[0],numberpipe[1]};
					c->numberpipe_vector.push_back(np);
					c->num.push_back(number);
					has_numberpipe = true;	
					break;
				}
			}
			for(int j = 0 ; j < s.size() ; j++){
				if(s[j] == '<'){
					if(s.size() != 1){
						int send_id = atoi(s.erase(0,j + recpip.length()).c_str());
						if(send_id > 30 || ID_arr[send_id-1] == 0){
							/* exceed max client number */
							recv_userpipe = true;
							err_recv_id = send_id;
							continue_id1 = true;
							break;
						}
						for(int k = 0;k < up_vector.size();++k){
							if(up_vector[k].recv_id == c->ID && up_vector[k].send_id == send_id && !up_vector[k].used){
								/* had userpipe before */
								user_recv_idx = k;
								up_vector[k].used = true;
								has_user_recvpipe = true;
								broadcast_order tbo = {6,original_input,c,send_id};
								fix_order.push_front(tbo);
								break;
							}
						}
						if(!has_user_recvpipe){
							/* error msg */
							recv_userpipe = true;
							fprintf(stdout,"*** Error: the pipe #%d->#%d does not exist yet. ***\n",
							send_id,c->ID);
							fflush(stdout);
						}
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
						if(recv_id > 30 || ID_arr[recv_id-1] == 0){
							/* exceed max client number */
							dup_userpipe = true;
							err_send_id = recv_id;
							continue_id2 = true;
							break;
						}
						for(int k = 0;k < up_vector.size();++k){
							if(up_vector[k].recv_id == recv_id && up_vector[k].send_id == c->ID && !up_vector[k].used){
								/* had userpipe before */
								dup_userpipe = true;
								/* error msg */
								fprintf(stdout,"*** Error: the pipe #%d->#%d already exists. ***\n",
								c->ID,recv_id);
								fflush(stdout);
							}
						}
						if(!dup_userpipe){
							/* hadn't userpipe */
							int upipes[2];
							pipe(upipes);
							/* int in,int out,int send_id,int recv_id */ 
							user_send_idx = up_vector.size();
							userpipe tmpuserpipe = {upipes[0],upipes[1],c->ID,recv_id,false};
							up_vector.push_back(tmpuserpipe);
							has_user_sendpipe = true;
							/* broadcast msg */
							broadcast_order tbo = {5,original_input,c,recv_id};
							fix_order.push_back(tbo);
						}
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
		while(!fix_order.empty()){
			broadcast_order tbo = fix_order.front();
			broadcast(tbo.type,tbo.msg,tbo.cnt,tbo.tarfd);
			fix_order.pop_front();
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
			usleep(10000);
			pid = fork();
		}
		/* Parent */
		if(pid != 0){
			if(i != 0){
				close(pipe_vector[i-1].in);
				close(pipe_vector[i-1].out);
			}
			//numberpipe reciever close
			for(int j = 0; j < c->numberpipe_vector.size() ; j++){
				c->num[j]--;
				//numberpipe erase
				if(c->num[j] < 0){
					close(c->numberpipe_vector[j].in);
					close(c->numberpipe_vector[j].out);	
					c->numberpipe_vector.erase(c->numberpipe_vector.begin() + j);
					c->num.erase(c->num.begin() + j);
					j--;
				}
			}
			for(int j = 0;j < up_vector.size();++j){
				if(up_vector[j].used){
					//cerr << "parent close " << up_vector[j].in << up_vector[j].out << endl;
					close(up_vector[j].in);
					close(up_vector[j].out);
					up_vector.erase(up_vector.begin()+j);
				}
			}
			if(i == input.size()-1 && !(has_numberpipe || has_errpipe) && !has_user_sendpipe){
				waitpid(pid,NULL,0);
			}
		}
		/* Child */
		else{
			//numberpipe recieve
			if(i == 0){
				bool has_front_pipe = false;
				int front_fd = 0;
				for(int j = c->numberpipe_vector.size()-1 ; j >= 0 ; j--){
					//cout << "child numpipe_vecotr.num[" << j << "] = " << numberpipe_vector[j].num << endl;
					if(c->num[j] == 0){
						if(has_front_pipe && front_fd != 0 && front_fd != c->numberpipe_vector[j].in){
							fcntl(front_fd, F_SETFL, O_NONBLOCK);
							while (1) {
								char tmp;
								if (read(front_fd, &tmp, 1) < 1){
									break;
								}
								int rt = write(c->numberpipe_vector[j].out,&tmp,1);
							}
							has_front_pipe = false;
							close(0);
							dup(c->numberpipe_vector[j].in);
						}
						else{
							close(0);
							dup(c->numberpipe_vector[j].in);
							front_fd = c->numberpipe_vector[j].in;
							has_front_pipe = true;
						}
					}
				}
				for(int j = 0;j < c->numberpipe_vector.size();++j)	{
					if(c->num[j] == 0){
						close(c->numberpipe_vector[j].in);
						close(c->numberpipe_vector[j].out);
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
				dup2(c->numberpipe_vector[c->numberpipe_vector.size()-1].out,1);
				close(c->numberpipe_vector[c->numberpipe_vector.size()-1].in);
				close(c->numberpipe_vector[c->numberpipe_vector.size()-1].out);
			}
			if(i == input.size()-1 && has_errpipe){
				dup2(c->numberpipe_vector[c->numberpipe_vector.size()-1].out,2);
				dup2(c->numberpipe_vector[c->numberpipe_vector.size()-1].out,1);
				close(c->numberpipe_vector[c->numberpipe_vector.size()-1].in);
				close(c->numberpipe_vector[c->numberpipe_vector.size()-1].out);
			}
			/* Process user pipe */
			/* Recv */
			if(has_user_recvpipe){
				dup2(up_vector[user_recv_idx].in,STDIN_FILENO);
			}
			/* Send */
			if(has_user_sendpipe){
				dup2(up_vector[user_send_idx].out,STDOUT_FILENO);
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
			/* Checkout user pipe vector (child)*/
			for(int j = 0;j < up_vector.size();++j){
				close(up_vector[j].in);
				close(up_vector[j].out);
			}
			for(int j = 0;j < pipe_vector.size();j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
			EXECINSTR(instr,fd);
		}	
	}
	pipe_vector.clear();
}

void EXECINSTR(vector<string> instr,int client_fd){
	int fd;
	const char **argv = new const char* [instr.size()+1];
	for(int i=0;i < instr.size();++i){
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
	clearenv();
	setenv("PATH",PRINTENV("PATH",client_fd).c_str(),1);
	if(execvp(argv[0],(char **)argv) == -1){
		//stderr for unknown command
		if(instr[0] != "setenv" && instr[0] != "printenv" && instr[0] != "exit"&&
		   instr[0] != "who" && instr[0] != "name" && instr[0] != "yell")
			fprintf(stderr,"Unknown command: [%s].\n",instr[0].c_str());
			fflush(stdout);
		exit(0);
	}
}

int GETSTART(string input,int fd){
        input.erase(remove(input.begin(), input.end(), '\n'),input.end());
		input.erase(remove(input.begin(), input.end(), '\r'),input.end());
		original_input = input;

		bool exit_sig = false;
        char del_c = ' ';
        istringstream is(input);
	    string str;
	    getline(is,str,del_c);
		if(str == "printenv"){
			getline(is,str);
			cout << PRINTENV(str,fd) << endl;
			input = "";
		}else if(str == "setenv"){
			string name,val;
			getline(is,name,del_c);
			getline(is,val);
			SETENV(name,val,fd);
			input = "";
		}else if(str == "exit"){
			input = "";
			exit_sig = true;
		}else if(str == "who"){
			WHO(fd);
			input = "";
		}else if(str == "name"){
			string name;
			getline(is,name,del_c);
			NAME(fd,name);
			input = "";
		}else if(str == "yell"){
			string msg;
			getline(is,msg);
			YELL(fd,msg);
			input = "";
		}else if(str == "tell"){
			string msg,tmp;
			getline(is,tmp,del_c);
			getline(is,msg);
			//cerr << stoi(tmp) << endl;
			TELL(fd,msg,stoi(tmp));
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
				Piping(commands,fd);
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
			Piping(commands,fd);
		}
		if(exit_sig) return -1;
		return 0;
	
}
int passiveTCP(int qlen, int port)
{
    struct sockaddr_in sin; /* an Internet endpoint address	*/
    int s, type;            /* socket descriptor and socket type	*/

    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    /* Allocate a socket */
    s = socket(PF_INET, SOCK_STREAM, 0);
    if (s < 0)
        perror("socket fail");
    int optval = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) 
    {
		perror("Error: set socket failed");
		return 0;
	}
    /* Bind the socket */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
        perror("bind fail");
    if (listen(s, qlen) < 0)
        perror("listen fail");
    return s;
}

bool sortid(client s1, client s2){
   return s1.ID < s2.ID;
}

int main(int argc, char *argv[])
{
    struct sockaddr_in fsin; /* the from address of a client */
    int alen;                /* from-address length	*/
    int fd, nfds;
    int port = atoi(argv[1]);
    for(int i = 0;i < CLIENTMAX;++i) ID_arr[i] = 0;
    msock = passiveTCP(QLEN, port);
    nfds = getdtablesize();
    FD_ZERO(&afds);
    //exclude server fd
    FD_SET(msock, &afds);

    while (1)
    {
        memcpy(&rfds, &afds, sizeof(rfds));
        int err,stat;
        do{
            stat = select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0);
            if( stat< 0) err = errno;
        } while ((stat < 0) && (err == EINTR)); /* blocked by pipe or other reason */
        if (stat < 0)
            perror("select fail");
        if (FD_ISSET(msock, &rfds))
        {
            int ssock;
            alen = sizeof(fsin);
            ssock = accept(msock, (struct sockaddr *)&fsin,
                           (socklen_t *)&alen);
            if (ssock < 0)
                perror("accept fail");
            /* Check number of clients*/
            int ID;
            ID = get_min_num();
            if(ID < 0){
                perror("Client MAX");
                continue;
            }
            FD_SET(ssock, &afds);
            /* send welcome msg */
            /* client ip */
            char ip[INET6_ADDRSTRLEN];
            sprintf(ip, "%s:%d", inet_ntoa(fsin.sin_addr), ntohs(fsin.sin_port));
            string tmp(ip);
            welcome(ssock);
            /* create user info */
            /* custome env information */
            map<string, string> ev;
            ev.clear();
            ev["PATH"] = "bin:.";
            /* custom number pipe vector */
            vector<npipe> np;
            np.clear();
			vector<int> num;
			num.clear();
            client c = {ID, tmp, "(no name)", ssock,np,num,ev};
            client_info.push_back(c);
            /* sort by client id */
            sort(client_info.begin(), client_info.end(), sortid);
            /* broadcast login information */
            broadcast(0, "", &c, 0);
            write(ssock, "% ", 2);
        }
        for (fd = 0; fd < nfds; ++fd)
            if (fd != msock && FD_ISSET(fd, &rfds))
            {
                char buf[BUFSIZE];
                memset( buf, 0, sizeof(char)*BUFSIZE );
                int cc;
                cc = recv(fd, buf, BUFSIZE, 0);
                if (cc == 0){
                    //cout << "socket: " << fd << "closed" << endl;
                    return -1;
                }
                else if(cc < 0){
                    //perror("receive error");
                    return -1;
                }
                buf[cc] = '\0';
                string input(buf);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                fix_order.clear();
                int status = GETSTART(input, fd);
                send(fd, "% ", 2,0);
                if (status == -1)
                {
                    /* broadcast logout information */
                    client c;
                    for(int i = 0;i < client_info.size();i++){
                        client tmp = client_info[i];
                        if(tmp.fd == fd){
                            c = tmp;
                            break;
                        }
                    }
                    broadcast(4, "", &c,0);
                    DeleteClient(fd);
                    close(fd);
                    close(1);
                    close(2);
                    dup2(0, 1);
                    dup2(0, 2);
                    FD_CLR(fd, &afds);
                }
            }
    }
}

void welcome(int fd)
{
    string buf =
        "****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";
    int cc;
    cc = send(fd, buf.c_str(), buf.length(), 0);
    return;
}
