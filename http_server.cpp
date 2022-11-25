#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <string>
#include <sstream>
#include <map>

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_service;

boost::asio::io_service io_context;

class session
: public std::enable_shared_from_this<session>
{
	public:
		map<string,string> env;
		char REQUEST_METHOD[100];
		char REQUEST_URI[1124];
		char QUERY_STRING[1024];
		char SERVER_PROTOCOL[100];
		char HTTP_HOST[100];
		char status_str[200] = "HTTP/1.1 200 OK\n";
		string server_addr;
		string server_port;
		string remote_addr;
		string remote_port;
		session(tcp::socket socket)
			: socket_(std::move(socket))
		{
		}
		void BOOST_SETENV(){
			setenv("REQUEST_METHOD",REQUEST_METHOD,1);
			setenv("REQUEST_URI",REQUEST_URI,1);
			setenv("QUERY_STRING",QUERY_STRING,1);
			setenv("SERVER_PROTOCOL",SERVER_PROTOCOL,1);
			setenv("HTTP_HOST",HTTP_HOST,1);
			setenv("SERVER_ADDR",server_addr.c_str(),1);
			setenv("SERVER_PORT",server_port.c_str(),1);
			setenv("REMOTE_ADDR",remote_addr.c_str(),1);
			setenv("REMOTE_PORT",remote_port.c_str(),1);
		}
		void start()
		{
			do_read();
		}

	private:
		void do_read()
		{
			auto self(shared_from_this());
			socket_.async_read_some(boost::asio::buffer(data_, max_length),
					[this, self](boost::system::error_code ec, std::size_t length){
					if (!ec){
						do_write(length);
					}
					});
		}

		void do_write(std::size_t length)
		{
			auto self(shared_from_this());
			boost::asio::async_write(socket_, boost::asio::buffer(status_str, strlen(status_str)),
					[this, self](boost::system::error_code ec, std::size_t /*length*/){
					if (!ec){
						io_context.notify_fork(io_service::fork_prepare);
						if (fork() != 0) {
							io_context.notify_fork(io_service::fork_parent);
							socket_.close();
						} else {
							io_context.notify_fork(io_service::fork_child);
							/* Parse input env */
							std::stringstream ss;
							sscanf(data_,"%s %s %s\n\rHost: %s\n",
							REQUEST_METHOD,REQUEST_URI,SERVER_PROTOCOL,HTTP_HOST);
							string tmp = string(REQUEST_METHOD);
							env.insert(make_pair("REQUEST_METHOD",tmp));
							remote_addr = socket_.remote_endpoint().address().to_string();
							remote_port = to_string(socket_.remote_endpoint().port());
							server_addr = socket_.local_endpoint().address().to_string();
							server_port = to_string(socket_.local_endpoint().port());
							char real_REQRI[100];
							sscanf(REQUEST_URI,"%[^?]?%s",real_REQRI,QUERY_STRING);
							tmp = string(real_REQRI);
							env.insert(make_pair("REQUEST_URI",tmp));
							tmp = string(QUERY_STRING);
							env.insert(make_pair("QUERY_STRING",tmp));
							tmp = string(SERVER_PROTOCOL);
							env.insert(make_pair("SERVER_PROTOCOL",tmp));
							tmp = string(HTTP_HOST);
							env.insert(make_pair("HTTP_HOST",tmp));
							env.insert(make_pair("REMOTE_ADDR",remote_addr));
							env.insert(make_pair("REMOTE_PORT",remote_port));
							env.insert(make_pair("SERVER_ADDR",server_addr));
							env.insert(make_pair("SERVER_PORT",server_port));
							for(auto it=env.begin();it!=env.end();it++){
								setenv(it->first.c_str(),it->second.c_str(),1);
							}
							int sock = socket_.native_handle();
							close(0);
							close(1);
							dup(sock);
							dup(sock);
							string uri = string(real_REQRI);
							uri = "."+uri;
							socket_.close();
							if (execlp(uri.c_str(), uri.c_str(), NULL) < 0) {
								cout << "Content-type:html/plain\r\n\r\nFAIL";
							}
						}
						do_read();
					}
					});
		}

		tcp::socket socket_;
		enum { max_length = 1024 };
		char data_[max_length];
};

class server
{
	public:
		server(boost::asio::io_context& io_context, short port)
			: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
		{
			do_accept();
		}

	private:
		void do_accept()
		{
			acceptor_.async_accept(
					[this](boost::system::error_code ec, tcp::socket socket)
					{
					if (!ec)
					{
					std::make_shared<session>(std::move(socket))->start();
					}

					do_accept();
					});
		}

		tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
	try
	{
		if (argc != 2)
		{
			std::cerr << "Usage: async_tcp_echo_server <port>\n";
			return 1;
		}
		int port = atoi(argv[1]);
		server s(io_context, port);

		io_context.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}
