#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <map>
#include <list>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>
#include <sys/socket.h>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>

#define BUFSIZE 4096

using namespace std;
int GETSTART(string input,int fd);
int echo(int fd);
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
	int fd;
    int ID;
    string ip;
    string username;
	vector<npipe> numberpipe_vector;
	vector<int> num;
	map<string, string> enviroment;
};

struct broadcast_order{
	int type;
	string msg;
	client *cnt;
	int tarfd;
};

/* used to store user information */
vector<client> clientdata;
/* userd to store user pipe information */
vector<userpipe> userpipe_vector;

int msocket;
fd_set rfds;
fd_set afds;
int available_id[30];

int select_min(){
	for(int i = 0;i < 30;++i){
		if(available_id[i] == 0){
			available_id[i] = 1;
			return i+1;
		}
	}
	return 87;
}

void broadcast(int type,string msg,client *client,int target){
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
			if(target == -1){
				sprintf(buf,"*** User '%s' already exists. ***\n",msg.c_str());
				string tmp = buf;
				int wr = write(client->fd, tmp.c_str(), tmp.length()) < 0;
				return;
			}else{
				sprintf(buf,"*** User from %s is named '%s'. ***\n",client->ip.c_str(),msg.c_str());
			}
			break;
		case 2:
			/* Yell with msg */
			sprintf(buf,"*** %s yelled ***: %s\n",client->username.c_str(),msg.c_str());
			break;
		case 3:
			/* Tell with msg and target */
			if(target == -1){
				sprintf(buf,"*** Error: user #%s does not exist yet. ***\n",msg.c_str());
				string tmp = buf;
				int wr = write(client->fd, tmp.c_str(), tmp.length()) < 0;
			}else{
				sprintf(buf,"*** %s told you ***: %s\n",client->username.c_str(),msg.c_str());
				string tmp = buf;
				int wr = write(target, tmp.c_str(), tmp.length()) < 0;
			}
			break;
		case 4:
			/* Logout */
			sprintf(buf,"*** User '%s' left. ***\n",client->username.c_str());
			break;
		case 5:
			/* send user pipe information */
			/* Success to send userpipe */
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].ID == target){
					sprintf(buf,"*** %s (#%d) just piped '%s' to %s (#%d) ***\n",
					client->username.c_str(),client->ID,msg.c_str(),clientdata[i].username.c_str(),clientdata[i].ID);
					break;
				}
			}
			break;
		case 6:
			/* recv user pipe information */
			/* Success to send userpipe */
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].ID == target){
					sprintf(buf,"*** %s (#%d) just received from %s (#%d) by '%s' ***\n",
					client->username.c_str(),client->ID,clientdata[i].username.c_str(),clientdata[i].ID,msg.c_str());
					break;
				}
			}
			break;
		default:
			perror("unknown broadcast type");
			break;
	}
	/* Not tell msg */
	string tmp = buf;
	if(type == 4){
		for (int fd = 0; fd < nfds; ++fd)
		{
			//send to all active fd
			if (fd != msocket && FD_ISSET(fd, &afds) && fd != client->fd)
			{
				int wr = write(fd, tmp.c_str(), tmp.length());
			}
		}
	}else if(type != 3)
		for (int fd = 0; fd < nfds; ++fd)
		{
			//send to all active fd
			if (fd != msocket && FD_ISSET(fd, &afds))
			{
				int wr = write(fd, tmp.c_str(), tmp.length());
			}
		}
}

string original_input;
list<broadcast_order> broadcast_list;
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

string PRINTENV(string name,int fd){
	for(int i = 0;i < clientdata.size();i++){
		if(clientdata[i].fd == fd){
			auto env = clientdata[i].enviroment.find(name);
			if (env != clientdata[i].enviroment.end()) {
				return env->second;
			}
		}
	}
	return  name + " not found!";
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

void EXECINSTR(vector<string> instr,client *cnt){
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
	setenv("PATH",PRINTENV("PATH",cnt->fd).c_str(),1);
	if(execvp(argv[0],(char **)argv) == -1){
		//stderr for unknown command
		if(instr[0] != "setenv" && instr[0] != "printenv" && instr[0] != "exit"&&
		   instr[0] != "who" && instr[0] != "name" && instr[0] != "yell")
			fprintf(stderr,"Unknown command: [%s].\n",instr[0].c_str());
			fflush(stdout);
		exit(0);
	}
}

void Piping(vector<string> input,int fd){
	bool has_numberpipe = false,has_errpipe = false;
	string numpip = "|";
	string errpip = "!";
	string recpip = "<";
	string senpip = ">";
	string space = " ";
	client *c;
	client tmp_c;
	for(int i = 0;i < clientdata.size();i++){
		c = &clientdata[i];
		tmp_c = clientdata[i];
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
						if(send_id > 30 || available_id[send_id-1] == 0){
							/* exceed max client number */
							recv_userpipe = true;
							err_recv_id = send_id;
							continue_id1 = true;
							break;
						}
						for(int k = 0;k < userpipe_vector.size(); k++){
							if(userpipe_vector[k].recv_id == c->ID && userpipe_vector[k].send_id == send_id && !userpipe_vector[k].used){
								/* had userpipe before */
								user_recv_idx = k;
								userpipe_vector[k].used = true;
								has_user_recvpipe = true;
								broadcast_list.push_front({6,original_input,c,send_id});
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
						if(recv_id > 30 || available_id[recv_id-1] == 0){
							/* exceed max client number */
							dup_userpipe = true;
							err_send_id = recv_id;
							continue_id2 = true;
							break;
						}
						for(int k = 0;k < userpipe_vector.size(); k++){
							if(userpipe_vector[k].recv_id == recv_id && userpipe_vector[k].send_id == c->ID && !userpipe_vector[k].used){
								/* had userpipe before */
								dup_userpipe = true;
								/* error msg */
								fprintf(stdout,"*** Error: the pipe #%d->#%d already exists. ***\n",c->ID,recv_id);
								fflush(stdout);
							}
						}
						if(!dup_userpipe){
							/* hadn't userpipe */
							int upipes[2];
							pipe(upipes);
							/* int in,int out,int send_id,int recv_id */ 
							user_send_idx = userpipe_vector.size();
							userpipe tmp = {upipes[0],upipes[1],c->ID,recv_id,false};
							userpipe_vector.push_back(tmp);
							has_user_sendpipe = true;
							broadcast_list.push_back({5,original_input,c,recv_id});
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
		while(!broadcast_list.empty()){
			broadcast(broadcast_list.front().type,broadcast_list.front().msg,broadcast_list.front().cnt,broadcast_list.front().tarfd);
			broadcast_list.pop_front();
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
			for(int j = 0;j < userpipe_vector.size(); j++){
				if(userpipe_vector[j].used){
					//cerr << "parent close " << userpipe_vector[j].in << userpipe_vector[j].out << endl;
					close(userpipe_vector[j].in);
					close(userpipe_vector[j].out);
					userpipe_vector.erase(userpipe_vector.begin()+j);
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
				close(0);
				dup(userpipe_vector[user_recv_idx].in);
			}
			/* Send */
			if(has_user_sendpipe){
				close(1);
				dup(userpipe_vector[user_send_idx].out);
			}
			/* send to null*/
			if(dup_userpipe){
				int devNull = open("/dev/null", O_RDWR);
				close(1);
				dup(devNull);
				close(devNull);
			}
			/* recv from null*/
			if(recv_userpipe){
				int devNull = open("/dev/null", O_RDWR);
				close(0);
				dup(devNull);
				close(devNull);
			}
			for(int j = 0;j < userpipe_vector.size(); j++){
				close(userpipe_vector[j].in);
				close(userpipe_vector[j].out);
			}
			for(int j = 0;j < pipe_vector.size(); j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
			EXECINSTR(instr,&tmp_c);
		}	
	}
	pipe_vector.clear();
}

int GETSTART(string input,int fd){
        input.erase(remove(input.begin(), input.end(), '\n'),input.end());
		input.erase(remove(input.begin(), input.end(), '\r'),input.end());
		original_input = input;

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
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].fd == fd){
					clientdata[i].enviroment[name] = val;
					break;
				}
			}
			input = "";
		}else if(str == "exit"){
			input = "";
			return -1;
		}else if(str == "who"){
			printf("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].fd == fd){
					printf("%d\t%s\t%s\t<-me\n",clientdata[i].ID,clientdata[i].username.c_str(),clientdata[i].ip.c_str());
				}
				else{
					printf("%d\t%s\t%s\t\n",clientdata[i].ID,clientdata[i].username.c_str(),clientdata[i].ip.c_str());
				}
			}
			fflush(stdout);
			input = "";
		}else if(str == "name"){
			string name;
			getline(is,name,del_c);
			client c_tmp;
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].fd == fd) c_tmp = clientdata[i];
				if(clientdata[i].username == name){
					if(clientdata[i].fd != fd){
						broadcast(1,name,&c_tmp,-1);
						return 0;
					}
				}
			}
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].fd == fd && clientdata[i].username != name){
					clientdata[i].username = name;
					broadcast(1,name,&clientdata[i],0);
					break;
				}
			}
			input = "";
		}else if(str == "yell"){
			string msg;
			getline(is,msg);
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].fd == fd){
					broadcast(2,msg,&clientdata[i],0);
				}
			}
			input = "";
		}else if(str == "tell"){
			string msg,tmp;
			getline(is,tmp,del_c);
			getline(is,msg);
			client me,tar;
			tar.fd = -1;
			for(int i = 0;i < clientdata.size();i++){
				if(clientdata[i].fd == fd){
					me = clientdata[i];
				}
				if(clientdata[i].ID == stoi(tmp)){
					tar = clientdata[i];
				}
			}
			if(tar.fd == -1) msg = to_string(stoi(tmp));
			broadcast(3,msg,&me,tar.fd);
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
		return 0;
	
}

bool sortclient(client s1, client s2){
   return s1.ID < s2.ID;
}

int main(int argc, char *argv[])
{
    struct sockaddr_in fsin;
	struct sockaddr_in sin;
    int alen;
    int fd, nfds;
    int port = atoi(argv[1]);
	bzero((char *)&sin, sizeof(sin));
    for(int i = 0;i < 30 ; i++) available_id[i] = 0;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    /* Allocate a socket */
    msocket = socket(PF_INET, SOCK_STREAM, 0);
    if (msocket < 0){
		perror("Error: socket fail");
		exit(1);
	}
    int optval = 1;
	if (setsockopt(msocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
		perror("Error: set socket failed");
		exit(1);
	}
    if (bind(msocket, (struct sockaddr *)&sin, sizeof(sin)) < 0){
		perror("Error: bind fail");
		exit(1);
	}
    if (listen(msocket, 5) < 0){
		perror("Error: listen fail");
		exit(1);
	}
    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msocket, &afds);

    while (1)
    {
        rfds = afds;
        int err,stat;
        do{
            stat = select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0);
            if( stat< 0) err = errno;
        } while ((stat < 0) && (err == EINTR)); /* blocked by pipe or other reason */
        if (stat < 0)
            perror("Error: select fail");
        if (FD_ISSET(msocket, &rfds))
        {
            int ssocket;
            alen = sizeof(fsin);
            ssocket = accept(msocket, (struct sockaddr *)&fsin, (socklen_t *)&alen);
            if (ssocket < 0)
                perror("Error: accept fail");
            /* Check number of clients*/
            int ID;
            ID = select_min();
            if(ID == 87){
                perror("Error: Client Exceed");
                continue;
            }
            FD_SET(ssocket, &afds);
            /* send welcome msg */
            /* client ip */
            char ip[INET6_ADDRSTRLEN];
            sprintf(ip, "%s:%d", inet_ntoa(fsin.sin_addr), ntohs(fsin.sin_port));
            string tmp = ip;
			string buf =
        "****************************************\n\
** Welcome to the information server. **\n\
****************************************\n";
			int wr = write(ssocket, buf.c_str(), buf.length());
            /* create user info */
            /* custome env information */
            map<string, string> env;
            env.clear();
            env["PATH"] = "bin:.";
            /* custom number pipe vector */
            vector<npipe> np;
            np.clear();
			vector<int> num;
			num.clear();
            client c = {ssocket, ID, tmp, "(no name)", np, num, env};
            clientdata.push_back(c);
            /* sort by client id */
            sort(clientdata.begin(), clientdata.end(), sortclient);
            /* broadcast login information */
            broadcast(0, "", &c, 0);
            write(ssocket, "% ", 2);
        }
        for (fd = 0; fd < nfds; fd++)
		{
			if (fd != msocket && FD_ISSET(fd, &rfds))
            {
                char buf[BUFSIZE];
				bzero(buf,sizeof(buf));
                int rd = read(fd, buf, BUFSIZE);
                if (rd <= 0){
                    exit(1);
                }
                buf[rd] = '\0';
                string input = buf;
				close(1);
				close(2);
				dup(fd);
				dup(fd);
                broadcast_list.clear();
                if (GETSTART(input, fd) == -1){
                    /* broadcast logout information */
                    client c;
                    for(int i = 0;i < clientdata.size();i++){
                        if(clientdata[i].fd == fd){
                            c = clientdata[i];
                            break;
                        }
                    }
                    broadcast(4, "", &c, 0);
					int id;
					for(int i = 0;i < clientdata.size();i++){
						if(clientdata[i].fd == fd){
							available_id[clientdata[i].ID-1] = 0;
							id = clientdata[i].ID;
							clientdata.erase(clientdata.begin()+i);
							break;
						}
					}
					for(int i = 0;i < userpipe_vector.size();++i){
						if(userpipe_vector[i].send_id == id || userpipe_vector[i].recv_id == id){
							close(userpipe_vector[i].in);
							close(userpipe_vector[i].out);
							userpipe_vector.erase(userpipe_vector.begin()+i);
						}
					}
                    close(fd);
                    close(1);
                    close(2);
                    dup(0);
                    dup(0);
                    FD_CLR(fd, &afds);
                }else{
					write(fd, "% ", 2);
				}
            }
		}
            
    }
}
