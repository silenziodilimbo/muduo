#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <set>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

class ChatServer : noncopyable
{
public:
	ChatServer(EventLoop* loop,
		const InetAddress& listenAddr)
		: server_(loop, listenAddr, "ChatServer")
		, codec_(std::bind(&ChatServer::onStringMessage, this, _1, _2, _3)) // todo 干嘛的?
	{
		server_.setConnectionCallback(
			std::bind(&ChatServer::onConnection, this, _1));
		// 这个回调与以往不通
		// 以往是把本class的onMessage注册给server_
		// 这里把LengthHeaderCodec::onMessage()注册给了server_  在初始化列表中注册的
		// 然后向codec_注册了ChatServer::onStringMessage()
		// 等于说,server_收到消息,先触发codec_的回调onMessage,由codec_负责解析消息,(一系列的操作,主要是while循环取buf,当body完整后,调用回调onStringMessage)
		// 再把完整的消息回调给ChatServer::onStringMessage
		// codec_是一个间接层,
		server_.setMessageCallback(
			std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
	}

	void start()
	{
		server_.start();
	}

private:

	// 加入连接
	// 将conn保存
	void onConnection(const TcpConnectionPtr& conn)
	{
		LOG_INFO << conn->localAddress().toIpPort() << " -> "
			<< conn->peerAddress().toIpPort() << " is "
			<< (conn->connected() ? "UP" : "DOWN");

		if (conn->connected())
		{
			connections_.insert(conn);
		}
		else
		{
			connections_.erase(conn);
		}
	}

	// 收到消息
	// 直接广播,遍历connections_发送
	void onStringMessage(const TcpConnectionPtr&,
		const string& message,
		Timestamp)
	{
		for (ConnectionList::iterator it = connections_.begin();
			it != connections_.end();
			++it)
		{
			codec_.send(get_pointer(*it), message);
		}
	}

	typedef std::set<TcpConnectionPtr> ConnectionList;
	TcpServer server_;
	LengthHeaderCodec codec_; // 编译编码器,用于加工消息
	ConnectionList connections_; // 保存所有连接,用于广播
};

int main(int argc, char* argv[])
{
	LOG_INFO << "pid=" << getpid();
	if (argc > 1)
	{
		EventLoop loop;
		uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
		InetAddress serverAddr(port);
		ChatServer server(&loop, serverAddr);
		server.start();
		loop.loop();
	}
	else
	{
		printf("Usage: %s port\n", argv[0]);
	}
}