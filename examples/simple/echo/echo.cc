#include "echo.h"
#include <muduo/base/Logging.h>

// ռλ��
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

// �ڹ��캯����,ע��ص�����
EchoServer::EchoServer(muduo::net::EventLoop* loop,
	const muduo::net::InetAddress& listenAddr)
	:server_(loop, listenAddr, "echoServer")
{
	server_.setConnectionCallback(
		std::bind(&EchoServer::onConnection, this, _1)); // ����һ������,
														 // ���������ʹ��this����onConnection,������_1
	server_.setMessageCallback(
		std::bind(&EchoServer::onMessage, this, _1, _2, _3)); // ����һ������,
															  // ���������ʹ��this����onMessage,������_1,_2,_3                                                       
}

void EchoServer::start()
{
	server_.start();
}

// onConnection�Ĳ�����һ��TcpConnection��shared_ptr
// conn��TcpConnection��shared_ptr
// ����һ��boolֵ,����Ŀǰ�����ӽ������ǶϿ�
// TcpConnection��peerAddress()��localAddress()��Ա�����ֱ𷵻ضԷ��ͱ��صĵ�ַ(��InetAddress�����ʾ��IP��port)
void EchoServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
{
	LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
		<< conn->localAddress().toIpPort() << " is "
		<< (conn->connected() ? "UP" : "DOWN");
}

// onMessage��
// conn�������յ����ݵ��Ǹ�TCP����
// buf���Ѿ��յ�������
//   buf�����ݻ��ۻ�,֪���û�����ȡ��(retrieve)����
//   buf��ָ��,�û������޸�
// time���յ����ݵ�ȷ��ʱ��
//   ��epoll_sait(2)���ص�ʱ��
//   ʹ��pass-by-value,����Ϊ��x86-64�Ͽ���ֱ��ͨ���Ĵ�������
void EchoServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
	muduo::net::Buffer* buf,
	muduo::Timestamp time)
{
	muduo::string msg(buf->retrieveAllAsString()); // ��buf����ȥȡ����
	LOG_INFO << conn->name() << "echo" << msg.size() << " bytes, "
		<< "data received at " << time.toString();
	conn->send(msg); // ���յ�������,ԭ�ⲻ���ط��ͻؿͻ���
					 // ���õ����Ƿ������ط���������, muduo����������ǹ����ͻ�����
}