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
		, codec_(std::bind(&ChatServer::onStringMessage, this, _1, _2, _3)) // todo �����?
	{
		server_.setConnectionCallback(
			std::bind(&ChatServer::onConnection, this, _1));
		// ����ص���������ͨ
		// �����ǰѱ�class��onMessageע���server_
		// �����LengthHeaderCodec::onMessage()ע�����server_
		// ��������server_�յ���Ϣ��,�����LengthHeaderCodec::onMessage(),��������Ϣ
		// ��LengthHeaderCodec::onMessage()����Ҳ�����һ���ص�
		// ����ص�����ChatServer::onStringMessage(),���㲥��Ϣ.���Ƿ����ڳ�ʼ���б�,��ʼ����һ��codec_����,�����cb��������Ĳ��������
		// ���̾���,server_�յ���Ϣ,�����ص�LengthHeaderCodec::onMessage()������Ϣ,�����ص�ChatServer::onStringMessage()�㲥��Ϣ
		server_.setMessageCallback(
			std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
	}

	void start()
	{
		server_.start();
	}

private:

	// ��������
	// ��conn����
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

	// �յ���Ϣ
	// ֱ�ӹ㲥
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
	LengthHeaderCodec codec_; // ���������,���ڼӹ���Ϣ
	ConnectionList connections_; // ������������,���ڹ㲥
};