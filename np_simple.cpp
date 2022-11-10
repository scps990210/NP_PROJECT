#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>

using namespace std;
void EXECINSTR(vector<string>);

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

void HandleChild(int sig){
	while(waitpid(-1,NULL,WNOHANG) > 0){
	}
}

struct npipe{
	int in;
	int out;
};

vector<npipe> numberpipe_vector;
vector<npipe> pipe_vector;
vector<int> num;

void CreatePipe(){
	int pipes[2];
	pipe(pipes);
	npipe np = {pipes[0],pipes[1]};
	pipe_vector.push_back(np);
}

void PushNumPipe(int in,int out){
	npipe np = {in,out};
	numberpipe_vector.push_back(np);
}

void Piping(vector<string> input){
	bool has_numberpipe = false,has_errpipe = false;
	string numpip = "|";
	string errpip = "!";
	string space = " ";
	for(int i = 0 ; i < input.size() ; i++){
		vector<string> instr;
        vector<string> ret = split(input[i], space);
        for (auto& s : ret) {
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
			//cout << "instr = " << s << endl;
			if(!(has_errpipe || has_numberpipe))
				instr.push_back(s);
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
			for(int j = 0; j < numberpipe_vector.size() ; j++){
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
			if(i == input.size()-1 && !(has_numberpipe || has_errpipe)){
				waitpid(pid,NULL,0);
			}
		}
		/* Child */
		else{
			//numberpipe recieve
			if(i == 0){
				bool has_front_pipe = false;
				int front_fd = 0;
				for(int j = numberpipe_vector.size()-1 ; j >= 0 ; j--){
					//cout << "child numpipe_vecotr.num[" << j << "] = " << numberpipe_vector[j].num << endl;
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
							close(0);
							dup(numberpipe_vector[j].in);
						}
						else{
							close(0);
							dup(numberpipe_vector[j].in);
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
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,1);
				close(numberpipe_vector[numberpipe_vector.size()-1].in);
				close(numberpipe_vector[numberpipe_vector.size()-1].out);
			}
			if(i == input.size()-1 && has_errpipe){
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,2);
				dup2(numberpipe_vector[numberpipe_vector.size()-1].out,1);
				close(numberpipe_vector[numberpipe_vector.size()-1].in);
				close(numberpipe_vector[numberpipe_vector.size()-1].out);
			}
			for(int j = 0;j < pipe_vector.size();j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
			EXECINSTR(instr);
		}	
	}
}

void EXECINSTR(vector<string> instr){
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
	if(execvp(argv[0],(char **)argv) == -1){
		//stderr for unknown command
		if(instr[0] != "setenv" && instr[0] != "printenv" && instr[0] != "exit")
			fprintf(stderr,"Unknown command: [%s].\n",instr[0].c_str());
		exit(0);
	}
}

void GETSTART(){
    setenv("PATH","bin:.",1);
	while(1){
		string input;
		cout << "% ";
		getline(cin,input);
        input.erase(remove(input.begin(), input.end(), '\n'),input.end());
		input.erase(remove(input.begin(), input.end(), '\r'),input.end());
        char del_c = ' ';
        istringstream is(input);
	    string str;
	    getline(is,str,del_c);
	    if(str == "printenv"){
		    getline(is,str,del_c);
            char *val = getenv(str.c_str());
	        if(val) cout << val << endl;
	    }else if(str == "setenv"){
		    string name,val;
		    getline(is,name,del_c);
		    getline(is,val,del_c);
            setenv(name.c_str(),val.c_str(),1);
	    }else if(str == "exit"){
		    exit(0);
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
				Piping(commands);
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
			Piping(commands);
		}
	}	
}
int main(int argc,char const *argv[]){
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
		perror("Error: set socket fail");
		exit(1);
	}
	if (bind(msocket, (struct sockaddr *)&sin,sizeof(sin)) < 0){
		perror("Error: bind fail");
		exit(1);
	}
	if (listen(msocket, 5) < 0){
		perror("Error: listen fail");
		exit(1);
	}
	struct sockaddr_in child_addr;	
	int alen = sizeof(child_addr);
	while(1){
		if((ssocket = accept(msocket,(struct sockaddr *)&child_addr,(socklen_t*)&alen))< 0){
			perror("Error : accept fail");
			exit(1);
		}
		pid_t pid;
		pid = fork();
		while(pid < 0){
			usleep(1000);
			pid = fork();
		}
		if(pid > 0){
			//parent
			close(ssocket);
		}else{
			//child
            close(msocket);
            close(0);
            close(1);
            close(2);
            dup(ssocket);
            dup(ssocket);
            dup(ssocket);
			close(ssocket);
			GETSTART();

			exit(0);
		}
	}
	return 0;
}
