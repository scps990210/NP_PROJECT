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
int EXECCMD(vector<string>);
int ParseCMD(vector<string>);

vector<string> split(string& str, string& pattern) {
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

void SETENV(string name,string val){
	setenv(name.c_str(),val.c_str(),1);
}

void PRINTENV(string name){
	char *val = getenv(name.c_str());
	if(val) cout << val << endl;
}



struct npipe{
	int in;
	int out;
	int num;
};



//used to store numberpipe
vector<npipe> numberpipe_vector;

//used to store pipe
vector<npipe> pipe_vector;

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

int ParseCMD(vector<string> input){
	size_t pos = 0;
	for(int i = 0;i < input.size();++i){
		string cmd;
		istringstream iss(input[i]);
		vector<string> parm;
        int k = 0;
		// Create pipe for number pipe, last one is for number
		while(getline(iss,cmd,' ')){
			//if(isWhitespace(cmd)) continue;
			parm.push_back(cmd);
            cout << "parm = " << parm[k++] << endl;
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
			if(i == input.size()-1 ){
				waitpid(cpid,&status,0);
			}
            for(int j = 0 ; j < parm.size() ; j++)
                cout << " parm origin parent = " << parm[j] << endl;
		}
		/* Child */
		else{
			//connect pipes of each child process
			if(i != input.size()-1){
				dup2(pipe_vector[i].out,STDOUT_FILENO);	
			}
			if(i != 0){
				dup2(pipe_vector[i-1].in,STDIN_FILENO);
			}
			for(int j = 0;j < pipe_vector.size();j++){
				close(pipe_vector[j].in);
				close(pipe_vector[j].out);
			}
            for(int j = 0 ; j < parm.size() ; j++)
                cout << " parm origin child = " << parm[j] << endl;
			EXECCMD(parm);
		}	
	}
	return 0;
}
int t = 0;

int EXECCMD(vector<string> parm){
    cout << "time = " << t << endl;
    t++;
    for(int i = 0 ;i < parm.size() ; i++){
        cout << "parm[" << i <<"] = " << parm[i] << endl;
    }
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
        string space = " ";
		vector<string> cmds;
    	vector<string> ret = split(input, pattern);

		int k = 0;
        int j = 0;
    	for (auto& s : ret) {
			cmds.push_back(s);
        	//cout << "cmds = " << cmds[k++] << "\n";
    	}

        vector<string> cmds2;
        for(int i = 0 ; i < cmds.size() ; i ++){
            vector<string> ret2 = split(cmds[i], space);
            for (auto& s : ret2) {
			    cmds2.push_back(s);
        	    //cout << "cmds2 = " << cmds2[j++] << "\n";
    	    }
        }
    	
        






		ParseCMD(cmds);
        
	}	
	return 0;
}
