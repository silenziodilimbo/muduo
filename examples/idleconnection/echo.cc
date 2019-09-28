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
  // 注册每秒的回调
  // runEvery是因为用了boost::circular_buffer来管理桶
  // 每秒都更新一次
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
    // 建立链接时候, 创建一个conn的shared_ptr
    EntryPtr entry(new Entry(conn));
    // 并且放在当前队尾的桶里
    connectionBuckets_.back().insert(entry);
    dumpConnectionBuckets();
    // 再把刚才entry的weak_ptr保存起来
    // 因为收到数据的时候, 还会用到entry来更新桶
    // 处理的时候会暂时提升为shared_ptr
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

  // 一个可能的改进措施
  // 给每一个conn保存最后一次往队尾添加引用时的tail位置
  // 收到消息先检查tail是否移动过, 标识该conn目前在哪个桶
  // 若无变化则不添加EntryPtr, 即这一秒, 已经进行过shared_ptr的移动了
  // 若有变化, 则把EntryPtr从旧的bucket移动到当前队尾的Bucket, 换桶
  assert(!conn->getContext().empty());
  // 创建一个该conn的weak_ptr Context的副本weak_ptr, 不要影响原weak_ptr
  WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
  // 提升为shared_ptr
  EntryPtr entry(weakEntry.lock());
  // 放在队尾桶
  if (entry)
  {
    connectionBuckets_.back().insert(entry);
    dumpConnectionBuckets();
  }
  // weak_ptr副本(已经被提升为shared_ptr)析构
  // 原weak_ptr并没有析构
}

void EchoServer::onTimer()
{
  // 往boost::circular_buffer的队尾添加一个空的Bucket()桶
  // 它会自动弹出队首的桶
  // 由于bucket是shared_ptr, 会自动析构, 释放链接
  connectionBuckets_.push_back(Bucket());
  dumpConnectionBuckets();
}

// 就是打印
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

