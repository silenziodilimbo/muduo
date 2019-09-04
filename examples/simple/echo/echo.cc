#include "echo.h"
#include <muduo/base/Logging.h>

// 占位符
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

// 在构造函数里,注册回调函数
EchoServer::EchoServer(muduo::net::EventLoop* loop,
	const muduo::net::InetAddress& listenAddr)
	:server_(loop, listenAddr, "echoServer")
{
	server_.setConnectionCallback(
		std::bind(&EchoServer::onConnection, this, _1)); // 返回一个函数,
														 // 这个函数将使用this调用onConnection,并传入_1
	server_.setMessageCallback(
		std::bind(&EchoServer::onMessage, this, _1, _2, _3)); // 返回一个函数,
															  // 这个函数将使用this调用onMessage,并传入_1,_2,_3                                                       
}

void EchoServer::start()
{
	server_.start();
}

// onConnection的参数是一个TcpConnection的shared_ptr
// conn是TcpConnection的shared_ptr
// 返回一个bool值,表面目前是连接建立还是断开
// TcpConnection的peerAddress()和localAddress()成员函数分别返回对方和本地的地址(以InetAddress对象表示的IP和port)
void EchoServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
{
	LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
		<< conn->localAddress().toIpPort() << " is "
		<< (conn->connected() ? "UP" : "DOWN");
}

// onMessage中
// conn参数是收到数据的那个TCP连接
// buf是已经收到的数据
//   buf的数据会累积,知道用户从中取走(retrieve)数据
//   buf是指针,用户可以修改
// time是收到数据的确切时间
//   即epoll_sait(2)返回的时间
//   使用pass-by-value,是因为在x86-64上可以直接通过寄存器传参
void EchoServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
	muduo::net::Buffer* buf,
	muduo::Timestamp time)
{
	muduo::string msg(buf->retrieveAllAsString()); // 从buf里面去取数据
	LOG_INFO << conn->name() << "echo" << msg.size() << " bytes, "
		<< "data received at " << time.toString();
	conn->send(msg); // 将收到的数据,原封不动地发送回客户端
					 // 不用担心是否完整地发送了数据, muduo网络库会帮我们管理发送缓冲区
}