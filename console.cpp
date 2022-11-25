#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <string>
#include <sstream>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <boost/bind/bind.hpp>
#include <boost/format.hpp>
#include <vector>

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_service;

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

struct user_data{
  string host;
  string port;
  string file;
};

boost::asio::io_service io_context;
user_data client[5];

void replace_content(string &content) {
  boost::replace_all(content, "'", "\\'");
  boost::replace_all(content, "<", "&lt;");
  boost::replace_all(content, ">", "&gt;");
}

void output_shell(int index,string content){
  replace_content(content);
  boost::replace_all(content, "\n\r", " ");
  boost::replace_all(content, "\n", "&NewLine;");
  boost::replace_all(content, "\r", " ");
  printf("<script>document.getElementById('s%d').innerHTML += '%s';</script>",index,content.c_str());
  fflush(stdout);
}

void output_command(int index,string content){
  replace_content(content);
  boost::replace_all(content, "\n\r", " ");
  boost::replace_all(content, "\n", "&NewLine;");
  boost::replace_all(content, "\r", " ");
  printf("<script>document.getElementById('s%d').innerHTML += '<b>%s</b>';</script>",index,content.c_str());
  fflush(stdout);
}

class session
: public std::enable_shared_from_this<session>
{
	public:
		session(tcp::socket socket_,int index,string host, string port,string file)
			: client_socket(std::move(socket_))
		{
      if(host.length() == 0){
        return;
      }
      id = index;
      bzero(data_,max_length);
      file_input.open("./test_case/"+file,ios::in);
      message = "";
      printf("<script>var table = document.getElementById('table_tr'); table.innerHTML += '<th>%s:%s</th>';</script>",host.c_str(),port.c_str());
      printf("<script>var table = document.getElementById('session'); table.innerHTML += '<td><pre id=\\'s%d\\' class=\\'mb-0\\'></pre></td>&NewLine;' </script>",index);
      fflush(stdout);
		}
		void start()
		{
			do_read();
		}

	private:
		void do_read()
		{
			auto self(shared_from_this());
      client_socket.async_read_some(boost::asio::buffer(data_, max_length),
          [this,self](boost::system::error_code ec, std::size_t length){
          if (!ec){
            message += data_;
            bzero(data_,length);
            size_t pos;
            if((pos = message.find("%")) != string::npos){
              output_shell(id,message);
              message = "";
              do_write();
            }
            do_read();
          }
          });
		}

		void do_write()
		{
			auto self(shared_from_this());
      string input;
      //if(file_input.eof()) return;
      getline(file_input,input);
      input += "\n";
      output_command(id,input);
      boost::asio::async_write(client_socket, boost::asio::buffer(input.c_str(), input.length()),
        [this,self](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec){
        }else{
          cerr << ec << endl;
        }
        });
    }

		tcp::socket client_socket;
		enum { max_length = 1024 };
		char data_[max_length];
    string message;
    ifstream file_input;
    int id;
};


void output_cgi(){
  cout << "Content-type: text/html\r\n\r\n";
  cout << "<!DOCTYPE html>\
<html lang=\"en\">\
  <head>\
    <meta charset=\"UTF-8\" />\
    <title>NP Project 3 Console</title>\
    <link\
      rel=\"stylesheet\"\
      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\
      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\
      crossorigin=\"anonymous\"\
    />\
    <link\
      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
      rel=\"stylesheet\"\
    />\
    <link\
      rel=\"icon\"\
      type=\"image/png\"\
      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
    />\
    <style>\
      * {\
        font-family: 'Source Code Pro', monospace;\
        font-size: 1rem !important;\
      }\
      body {\
        background-color: #212529;\
      }\
      pre {\
        color: #cccccc;\
      }\
      b {\
        color: #01b468;\
      }\
    </style>\
  </head>\
  <body>\
    <table class=\"table table-dark table-bordered\">\
      <thead>\
        <tr id=\"table_tr\">\
        </tr>\
      </thead>\
      <tbody>\
        <tr id=\"session\">\
        </tr>\
      </tbody>\
    </table>\
  </body>\
</html>";
}

class server
{
  public:
    server()
    :resolve(io_context)
    {
      for(int i = 0;i < 5;i++){
        if(client[i].host.length() !=0){
          tcp::resolver::query query(client[i].host, client[i].port);
          resolve.async_resolve(query,
          boost::bind(&server::connection, this,i,boost::asio::placeholders::error,boost::asio::placeholders::iterator ));
        }
      }
    }
    void connection(const int i,const boost::system::error_code& err,const tcp::resolver::iterator it){
      if (!err)
      {
        socket_[i] = new tcp::socket(io_context);
        (*socket_[i]).async_connect(*it,
        boost::bind(&server::create_session, this,i,boost::asio::placeholders::error,it ));
      }
    }
    void create_session(const int i,const boost::system::error_code& err,const tcp::resolver::iterator it){
      if (!err)
      {
          std::make_shared<session>(std::move(*socket_[i]),i,client[i].host, client[i].port,client[i].file)->start();
      }
    }
  private:
    tcp::socket *socket_[5];
    tcp::resolver resolve;
};

int main(int argc, char* argv[]){
  try
	{
		if (argc != 4)
		{
			std::cerr << "Usage: console.cgi <host> <port> <file>\n";
		}
    //h0=nplinux5.cs.nctu.edu.tw&p0=7000&f0=t2.txt&h1=nplinux5.cs.nctu.edu.tw&p1=7009&f1=t3.txt&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=
    output_cgi();
    string qs = getenv("QUERY_STRING");
    int flag = 0;
    int cnt = 0;
    string pattern = "&";
    vector<string> ret = split(qs, pattern);
    for (auto& s : ret) {
      s.erase(0,3);
          if(s.size() == 0) break;
          if(flag == 0){
            client[cnt].host = s;
              flag++;
          }else if(flag == 1){
            client[cnt].port = s;
              flag++;
          }else{
            client[cnt].file = s;
              flag = 0;
              cnt++;
          }
    }
    server server_obj;
    io_context.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}
