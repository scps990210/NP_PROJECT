#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>

using namespace std;
int EXECINSTR(vector<string>);
int Piping(vector<string>);

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
	int status;
	while(waitpid(-1,&status,WNOHANG) > 0){
	}
}

struct npipe{
	int in;
	int out;
	int num;
};

vector<npipe> numberpipe_vector;

vector<npipe> pipe_vector;

void CreatePipe(int num){
	int pipes[2];
	pipe(pipes);
	npipe np = {pipes[0],pipes[1],num};
	pipe_vector.push_back(np);
}

int Piping(vector<string> input){
	for(int i = 0 ; i < input.size() ; i++){
		vector<string> instr;
        string space = " ";
        vector<string> ret = split(input[i], space);
        for (auto& s : ret) {
			instr.push_back(s);
    	}
		if(i != input.size()-1 &&input.size() != 1){
			CreatePipe(i);
		}

		signal(SIGCHLD, HandleChild);
		int status;
		pid_t pid = fork();
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
			if(i == input.size()-1 ){
				waitpid(pid,&status,0);
			}
		}
		/* Child */
		else{
			//connect pipes of each child process
			if(i != input.size()-1){
				close(STDOUT_FILENO);
				dup(pipe_vector[i].out);
			}
			if(i != 0){
				close(STDIN_FILENO);
				dup(pipe_vector[i-1].in);
			}
			for(int j = 0;j < pipe_vector.size();j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
			EXECINSTR(instr);
		}	
	}
	return 0;
}

int EXECINSTR(vector<string> instr){
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
		//char *argv[] = {(char*)NULL};
		//execv("./bin/noop", argv);
		return -1;
	}
	return 0;
}

int main(){
    setenv("PATH","bin:.",1);
	while(1){
		string input;
		cout << "% ";
		getline(cin,input);
        char del_c = ' ';

        //Check built in
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
	    string pattern = " | ";
		vector<string> commands;
    	vector<string> ret = split(input, pattern);

		//int k = 0;
    	for (auto& s : ret) {
			commands.push_back(s);
        	//cout << "cmds = " << cmds[k++] << "\n";
    	}

		Piping(commands);

    	
	}	
	return 0;
}
