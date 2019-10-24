// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Thread.h>

namespace muduo
{
namespace net
{

class EventLoop;

// ������һ���߳�
// ���̺߳�����. ������һ��EventLoop����, ������EventLoop::loop
class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();

  // ������Աthread_�̣߳����߳̾ͳ���I/O�̣߳��ڲ�����thread_.start()
  EventLoop* startLoop();

 private:
   // �߳����к���
  void threadFunc();

  // ָ��һ��EventLoop����һ��I/O�߳�����ֻ��һ��EventLoop����
  EventLoop* loop_ GUARDED_BY(mutex_);
  bool exiting_;
  // ���ڶ��󣬰�����һ��thread�����
  Thread thread_;
  MutexLock mutex_;
  Condition cond_ GUARDED_BY(mutex_);
  // �ص�������EventLoop::loop�¼�ѭ��֮ǰ������
  ThreadInitCallback callback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

