#ifndef MUDUO_EXAMPLES_SIMPLE_ECHO_ECHO_H
#define MUDUO_EXAMPLES_SIMPLE_ECHO_ECHO_H

#include <muduo/net/TcpServer.h>

// RFC 862
// 定义EchoServer class 不需要派生自任何基类
class EchoServer
{
public:
	EchoServer(muduo::net::EventLoop* loop,
		const muduo::net::InetAddress& listenAddr);
	void start(); // calls server_.start();
private:
	void onConnection(const muduo::net::TcpConnectionPtr& conn);

	void onMessage(const muduo::net::TcpConnectionPtr& conn,
		muduo::net::Buffer* buf,
		muduo::Timestamp time);

	muduo::net::TcpServer server_;

};

#endif // !MUDUO_EXAMPLES_SIMPLE_ECHO_ECHO_H