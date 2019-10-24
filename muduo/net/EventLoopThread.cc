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
    // ��thread_�̵߳����к���
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
    // �˳�I/O�̣߳���I/O�̵߳�loopѭ���˳����Ӷ��˳���I/O�߳�
    loop_->quit();
    // ��ǰ�߳�����, ֪��thread_��ֹ��,�ż���ִ��, ��������ǰ�߳�
    thread_.join();
  }
}

// ������Աthread_�̣߳����߳̾ͳ���I/O�̣߳��ڲ�����thread_.start()
// �������߳���EventLoop����õ�ַ, �������������ȴ��̵߳ô���������
EventLoop* EventLoopThread::startLoop()
{
  assert(!thread_.started());
  // �����߳�, ��ʱ�������߳�������
  // ִ��threadFunc().
  // ������ᴴ��loop, loop.loop(),����loop�����ø�loop_
  // ���캯����ʼ���б���thread_(boost::bind(&EventLoopThread::threadFunc, this))
  thread_.start();

  EventLoop* loop = NULL;
  {
    MutexLockGuard lock(mutex_);
    // ִ�е�����, �Ῠ��whileѭ����, �ȴ������߳�thread_��ִ��
    // �����߳�thread_�Ḻ���ʼ��loop_, Ȼ����ִ�����������, ���������loop
    while (loop_ == NULL)
    {
      // startLoop()��wait��
      // threadFunc()��notify��
      cond_.wait();
    }
    loop = loop_;
  }

  return loop;
}


// thread_�̵߳����к���
// ����loopѭ��
// �ú����������startLoop��������ִ��(�����߳�)��������Ҫ������condition
void EventLoopThread::threadFunc()
{
  // ��ʼ��һ��loop
  EventLoop loop;

  // ���캯�����ݽ����ģ��߳�����ִ�лص�����
  if (callback_)
  {
    callback_(&loop);
  }

  {
    MutexLockGuard lock(mutex_);
    // һ�������EventLoopThread������������������������loop_->quit() ʹ��loop.loop() �˳�ѭ��
    // ����threadFunc �˳���loopջ�϶���������loop_ ָ��ʧЧ������ʱ�Ѿ�������ͨ��loop_ ����loop��
    // �ʲ���������
    // Ȼ��loop_ָ��ָ�������������ջ�ϵĶ���threadFunc�˳�֮�����ָ���ʧЧ��
    loop_ = &loop;
    // startLoop()��wait��
    // threadFunc()��notify��
    // �ú����˳�����ζ���߳̾��Ƴ��ˣ�EventLoopThread����Ҳ��û�д��ڵļ�ֵ�ˡ�����muduo��EventLoopThread
    // ʵ��Ϊ�Զ����ٵġ�һ��loop�����˳�����������˳��ˣ����������ʲô�������
    // ��Ϊmuduo����̳߳ؾ�������ʱ���䣬��û���ͷš������߳̽���һ����˵�����������������
    cond_.notify();
  }

  loop.loop();
  // ����ѭ��, ѭ�������������ǲ���ִ�е�
  //assert(exiting_);
  MutexLockGuard lock(mutex_);
  loop_ = NULL;
}

