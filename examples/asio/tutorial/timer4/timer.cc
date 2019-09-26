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
		// ��loop_����˶�ʱ��, 1������print
    loop_->runAfter(1, std::bind(&Printer::print, this));
  }

  ~Printer()
  {
    std::cout << "Final count is " << count_ << "\n";
  }

  void print()
  {
		// loop_���1�����
		// ѭ��5��
    if (count_ < 5)
    {
      std::cout << count_ << "\n";
      ++count_;
			// ��ע��1�붨ʱ����ʱ��
			// ����5��ѭ��,5����ʱ��
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
	// ���빹��
  Printer printer(&loop);
  loop.loop();
}

