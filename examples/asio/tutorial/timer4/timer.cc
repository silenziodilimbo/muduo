#include <muduo/net/EventLoop.h>

#include <iostream>

class Printer : muduo::noncopyable
{
 public:
  Printer(muduo::net::EventLoop* loop)
    : loop_(loop),
      count_(0)
  {
    // Note: loop.runEvery() is better for this use case.
		// 给loop_添加了定时器, 1秒后调用print
    loop_->runAfter(1, std::bind(&Printer::print, this));
  }

  ~Printer()
  {
    std::cout << "Final count is " << count_ << "\n";
  }

  void print()
  {
		// loop_后的1秒调用
		// 循环5次
    if (count_ < 5)
    {
      std::cout << count_ << "\n";
      ++count_;
			// 再注册1秒定时器定时器
			// 共计5次循环,5个定时器
      loop_->runAfter(1, std::bind(&Printer::print, this));
    }
    else
    {
      loop_->quit();
    }
  }

private:
  muduo::net::EventLoop* loop_;
  int count_;
};

int main()
{
  muduo::net::EventLoop loop;
	// 进入构造
  Printer printer(&loop);
  loop.loop();
}

