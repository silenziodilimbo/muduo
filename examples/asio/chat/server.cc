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
		// 这里把LengthHeaderCodec::onMessage()注册给了server_
		// 当服务器server_收到消息后,会调用LengthHeaderCodec::onMessage(),来处理消息
		// 而LengthHeaderCodec::onMessage()本身也会接受一个回调
		// 这个回调就是ChatServer::onStringMessage(),来广播消息.绑定是发生在初始化列表,初始化了一个codec_对象,把这个cb当作构造的参数传入的
		// 流程就是,server_收到消息,触发回调LengthHeaderCodec::onMessage()解析消息,触发回调ChatServer::onStringMessage()广播消息
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
	// 直接广播
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