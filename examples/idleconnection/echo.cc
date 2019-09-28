#include "echo.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include <assert.h>
#include <stdio.h>

using namespace muduo;
using namespace muduo::net;


EchoServer::EchoServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       int idleSeconds)
  : server_(loop, listenAddr, "EchoServer"),
    connectionBuckets_(idleSeconds)
{
  server_.setConnectionCallback(
      std::bind(&EchoServer::onConnection, this, _1));
  server_.setMessageCallback(
      std::bind(&EchoServer::onMessage, this, _1, _2, _3));
  // ע��ÿ��Ļص�
  // runEvery����Ϊ����boost::circular_buffer������Ͱ
  // ÿ�붼����һ��
  loop->runEvery(1.0, std::bind(&EchoServer::onTimer, this));
  connectionBuckets_.resize(idleSeconds);
  dumpConnectionBuckets();
}

void EchoServer::start()
{
  server_.start();
}

void EchoServer::onConnection(const TcpConnectionPtr& conn)
{
  LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
           << conn->localAddress().toIpPort() << " is "
           << (conn->connected() ? "UP" : "DOWN");

  if (conn->connected())
  {
    // ��������ʱ��, ����һ��conn��shared_ptr
    EntryPtr entry(new Entry(conn));
    // ���ҷ��ڵ�ǰ��β��Ͱ��
    connectionBuckets_.back().insert(entry);
    dumpConnectionBuckets();
    // �ٰѸղ�entry��weak_ptr��������
    // ��Ϊ�յ����ݵ�ʱ��, �����õ�entry������Ͱ
    // �����ʱ�����ʱ����Ϊshared_ptr
    WeakEntryPtr weakEntry(entry);
    conn->setContext(weakEntry);
  }
  else
  {
    assert(!conn->getContext().empty());
    WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
    LOG_DEBUG << "Entry use_count = " << weakEntry.use_count();
  }
}

void EchoServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer* buf,
                           Timestamp time)
{
  string msg(buf->retrieveAllAsString());
  LOG_INFO << conn->name() << " echo " << msg.size()
           << " bytes at " << time.toString();
  conn->send(msg);

  // һ�����ܵĸĽ���ʩ
  // ��ÿһ��conn�������һ������β�������ʱ��tailλ��
  // �յ���Ϣ�ȼ��tail�Ƿ��ƶ���, ��ʶ��connĿǰ���ĸ�Ͱ
  // ���ޱ仯�����EntryPtr, ����һ��, �Ѿ����й�shared_ptr���ƶ���
  // ���б仯, ���EntryPtr�Ӿɵ�bucket�ƶ�����ǰ��β��Bucket, ��Ͱ
  assert(!conn->getContext().empty());
  // ����һ����conn��weak_ptr Context�ĸ���weak_ptr, ��ҪӰ��ԭweak_ptr
  WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
  // ����Ϊshared_ptr
  EntryPtr entry(weakEntry.lock());
  // ���ڶ�βͰ
  if (entry)
  {
    connectionBuckets_.back().insert(entry);
    dumpConnectionBuckets();
  }
  // weak_ptr����(�Ѿ�������Ϊshared_ptr)����
  // ԭweak_ptr��û������
}

void EchoServer::onTimer()
{
  // ��boost::circular_buffer�Ķ�β���һ���յ�Bucket()Ͱ
  // �����Զ��������׵�Ͱ
  // ����bucket��shared_ptr, ���Զ�����, �ͷ�����
  connectionBuckets_.push_back(Bucket());
  dumpConnectionBuckets();
}

// ���Ǵ�ӡ
void EchoServer::dumpConnectionBuckets() const
{
  LOG_INFO << "size = " << connectionBuckets_.size();
  int idx = 0;
  for (WeakConnectionList::const_iterator bucketI = connectionBuckets_.begin();
      bucketI != connectionBuckets_.end();
      ++bucketI, ++idx)
  {
    const Bucket& bucket = *bucketI;
    printf("[%d] len = %zd : ", idx, bucket.size());
    for (const auto& it : bucket)
    {
      bool connectionDead = it->weakConn_.expired();
      printf("%p(%ld)%s, ", get_pointer(it), it.use_count(),
          connectionDead ? " DEAD" : "");
    }
    puts("");
  }
}

