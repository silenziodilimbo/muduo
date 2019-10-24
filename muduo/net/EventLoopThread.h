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

// 它创建一个线程
// 在线程函数中. 创建了一个EventLoop对象, 并调用EventLoop::loop
class EventLoopThread : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(),
                  const string& name = string());
  ~EventLoopThread();

  // 启动成员thread_线程，该线程就成了I/O线程，内部调用thread_.start()
  EventLoop* startLoop();

 private:
   // 线程运行函数
  void threadFunc();

  // 指向一个EventLoop对象，一个I/O线程有且只有一个EventLoop对象
  EventLoop* loop_ GUARDED_BY(mutex_);
  bool exiting_;
  // 基于对象，包含了一个thread类对象
  Thread thread_;
  MutexLock mutex_;
  Condition cond_ GUARDED_BY(mutex_);
  // 回调函数在EventLoop::loop事件循环之前被调用
  ThreadInitCallback callback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOPTHREAD_H

