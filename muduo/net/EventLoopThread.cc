// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoopThread.h>

#include <muduo/net/EventLoop.h>

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL),
    exiting_(false),
    // 绑定thread_线程的运行函数
    thread_(std::bind(&EventLoopThread::threadFunc, this), name),
    mutex_(),
    cond_(mutex_),
    callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    // 退出I/O线程，让I/O线程的loop循环退出，从而退出了I/O线程
    loop_->quit();
    // 当前线程阻塞, 知道thread_终止了,才继续执行, 再析构当前线程
    thread_.join();
  }
}

// 启动成员thread_线程，该线程就成了I/O线程，内部调用thread_.start()
// 返回新线程中EventLoop对象得地址, 用条件变量来等待线程得创建于运行
EventLoop* EventLoopThread::startLoop()
{
  assert(!thread_.started());
  // 启动线程, 此时有两个线程在运行
  // 执行threadFunc().
  // 这里面会创建loop, loop.loop(),并把loop的引用给loop_
  // 构造函数初始化列表中thread_(boost::bind(&EventLoopThread::threadFunc, this))
  thread_.start();

  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    // 执行到这里, 会卡在while循环中, 等待并行线程thread_的执行
    // 并行线程thread_会负责初始化loop_, 然后再执行下面的内容, 即返回这个loop
    while (loop_ == NULL)
    {
      // startLoop()中wait了
      // threadFunc()中notify了
      cond_.wait();
    }
    loop = loop_;
  }

  return loop;
}


// thread_线程的运行函数
// 启动loop循环
// 该函数和上面的startLoop函数并发执行(两个线程)，所以需要上锁和condition
void EventLoopThread::threadFunc()
{
  // 初始化一个loop
  EventLoop loop;

  // 构造函数传递进来的，线程启动执行回调函数
  if (callback_)
  {
    callback_(&loop);
  }

  {
    MutexLockGuard lock(mutex_);
    // 一般情况是EventLoopThread对象先析构，析构函数调用loop_->quit() 使得loop.loop() 退出循环
    // 这样threadFunc 退出，loop栈上对象析构，loop_ 指针失效，但此时已经不会再通过loop_ 访问loop，
    // 故不会有问题
    // 然后loop_指针指向了这个创建的栈上的对象，threadFunc退出之后，这个指针就失效了
    loop_ = &loop;
    // startLoop()中wait了
    // threadFunc()中notify了
    // 该函数退出，意味着线程就推出了，EventLoopThread对象也就没有存在的价值了。但是muduo的EventLoopThread
    // 实现为自动销毁的。一般loop函数退出整个程序就退出了，因而不会有什么大的问题
    // 因为muduo库的线程池就是启动时分配，并没有释放。所以线程结束一般来说就是整个程序结束了
    cond_.notify();
  }

  loop.loop();
  // 这是循环, 循环过程中下面是不会执行的
  //assert(exiting_);
  MutexLockGuard lock(mutex_);
  loop_ = NULL;
}

