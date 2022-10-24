#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include<vector>
#include<queue>
#include<memory>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<future>
#include<functional>
#include<stdexcept>
#include<type_traits>

class ThreadPool{
public:
    ThreadPool(size_t);

    //result_of关键字可以用来获取可调用对象的返回值
    //future提供了访问异步操作结果的机制
    template<class F,class... Args>
    auto enqueue(F&& f,Args&&... args)
      -> std::future<typename std::result_of<F(Args...)>::type>;

    ~ThreadPool();
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void ()> > tasks;  //工作队列

    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

inline ThreadPool::ThreadPool(size_t threads)
    :stop(false){

    for(size_t i = 0;i < threads;++i){
        workers.emplace_back(
            [this]
             {
               for(;;){
                 std::function<void()> task; 
                 {
                   //这个unique_lock就相当于lockGuard
                   std::unique_lock<std::mutex> lock(this->queue_mutex);
                   //讲while循环的条件整合到了wait里面去,当被唤醒时，如果后面那个可调用对象的返回值=false，就继续wait,一直直到被唤醒时并且可调用对象的返回值=true，才能够跳出去
                   this->condition.wait(lock,
                       [this]{ return this->stop || !this->tasks.empty();} );
                   if(this->stop && this->tasks.empty()){
                       return ;
                   }
                   task = std::move(this->tasks.front());
                   this->tasks.pop();
                 }
                 task();
               }
             }
        );
    }
}

//添加新任务到工作队列
template<class F,class... Args>
auto ThreadPool::enqueue(F&& f,Args&&... args)
    ->std::future<typename std::result_of<F(Args...)>::type>{
    //typedef typename std::result_of<F(Args...)>::type> return_type
    using  return_type = typename std::result_of<F(Args...)>::type;
    //packaged_task是一个类模板，可以用来保证任何的可调用对象(函数、函数指针、lambda表达式、函数对象),以便他被异步调用
    auto task = std::make_shared<std::packaged_task<return_type()>> (
        std::bind(std::forward<F>(f),std::forward<Args>(args)...)  //这个属于是参数，所以末尾不加分号
    );
    std::future<return_type> res = task->get_future();
    {
       std::unique_lock<std::mutex> lock(queue_mutex);
       if(stop){
         throw std::runtime_error("enqueue on stopped ThreadPool");
       }
       //packaged_task不会自己自动，需要我们显式的进行调用
       tasks.emplace([task](){ (*task)();});
    }
    condition.notify_one();
    return res;
}

inline ThreadPool::~ThreadPool(){
   {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
   }
   condition.notify_all();
   for(std::thread& worker : workers){
       worker.join();
   }
}

#endif
