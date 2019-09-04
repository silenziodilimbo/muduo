#include "echo.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include <unistd.h>

int main()
{
	LOG_INFO << "pid =" << getpid();
	muduo::net::EventLoop loop; // ����loop
	muduo::net::InetAddress listenAddr(2007); // �����˿�
	EchoServer server(&loop, listenAddr); // ����ҵ�����
										  // ��һ����Ϊָ��loop��ָ��
										  // �ڶ�����ΪlistenAddr�����ô���
										  // ����������,�Լ���������"EchoServer"������server_
										  // ������server_.setConnectionCallback��server_.setMessageCallback��������
										  // ���Լ��Ļص����а�
	server.start(); // �ڲ���server_.start()
	loop.loop(); // �¼��������,
				 // ��ע��ص�,��start()֮����Ҫ������ʲô,���Ǳ����ȴ��¼�����
}