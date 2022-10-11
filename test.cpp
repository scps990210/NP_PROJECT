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
		vector<string> cmds;
    	vector<string> ret = split(input, pattern);

		//int k = 0;
    	for (auto& s : ret) {
			cmds.push_back(s);
        	//cout << cmds[k++] << "\n";
    	}
        
	}	
	return 0;
}
