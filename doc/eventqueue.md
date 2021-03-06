# Class EventQueue reference

## Table Of Contents

- [API reference](#apis)
- [Internal data structure](#internal-data-structure)

EventQueue includes all functions of [EventDispatcher](eventdispatcher.md) and adds event queue features. Note: EventQueue doesn't inherit from EventDispatcher, don't try to cast EventQueue to EventDispatcher.  

<a name="apis"></a>
## API reference

**Header**

eventpp/eventqueue.h

**Template parameters**

```c++
template <
	typename Key,
	typename Prototype,
	typename Policies = DefaultPolicies
>
class EventQueue;
```

EventQueue has the exactly same template parameters with EventDispatcher. Please reference [EventDispatcher document](eventdispatcher.md) for details.

**Public types**  

`QueuedEvent`: the data type of event stored in the queue. It's declaration is,  
```c++
using QueuedEvent = std::tuple<
	typename std::remove_cv<typename std::remove_reference<Event>::type>::type,
	typename std::remove_cv<typename std::remove_reference<Args>::type>::type...
>;
```
It's a `std::tuple`, the first member is always the event type, the other members are the arguments.

**Functions**

```c++
EventQueue() = default;
EventQueue(EventQueue &&) = delete;
EventQueue(const EventQueue &) = delete;
EventQueue & operator = (const EventQueue &) = delete;
```

EventQueue can not be copied, moved, or assigned.

```c++
template <typename ...A>
void enqueue(A ...args);

template <typename T, typename ...A>
void enqueue(T && first, A ...args);
```  
Put an event into the event queue. The event type is deducted from the arguments of `enqueue`.  
All copyable arguments are copied to internal data structure. All non-copyable but movable arguments are moved.  
EventQueue requires the arguments either copyable or movable.  
If an argument is a reference to a base class and a derived object is passed in, only the base object will be stored and the derived object is lost. Usually shared pointer should be used in such situation.  
If an argument is a pointer, only the pointer will be stored. The object it points must be available until the event is processed.  
`enqueue` wakes up any threads that are blocked by `wait` or `waitFor`.  
The time complexity is O(1).  

```c++
void process();
```  
Process the event queue. All events in the event queue are dispatched once and then removed from the queue.  
The listeners are called in the thread same as the caller of `process`.  
Any new events added to the queue during `process` are not dispatched during current `process`.  
Note: if `process()` is called from multiple threads simultaneously, the events in the event queue are guaranteed dispatched only once.  

```c++
bool empty() const;
```
Return true if there is no any event in the event queue, false if there are events in the event queue.  
Note: in multiple threading environment, the empty state may change immediately after the function returns.  
Note: don't write loop as `while(! eventQueue.empty()) ;`. It's dead loop since the compiler will inline the code and the change of empty state is never seen by the loop. The safe approach is `while(eventQueue.waitFor(std::chrono::nanoseconds(0))) ;`.  

```c++
void wait() const;
```
`wait` causes the current thread to block until there is new event arrives in the queue.  
Note: though `wait` has work around with spurious wakeup internally, the queue is not guaranteed not empty after `wait` returns.  
`wait` is useful when a thread processes the event queue. A sampel usage is,
```c++
for(;;) {
	eventQueue.wait();
	eventQueue.process();
}
```
The code works event if it doesn't `wait`, but doing that will waste CPU power resource.

```c++
template <class Rep, class Period>
bool waitFor(const std::chrono::duration<Rep, Period> & duration) const;
```
Wait for no longer than *duration* time out.  
Return true if the queue is not empty, false if the return is caused by time out.  
`waitFor` is useful when a event queue processing thread has other condition to check. For example,
```c++
std::atomic<bool> shouldStop(false);
for(;;) {
	while(! eventQueue.waitFor(std::chrono::milliseconds(10)) && ! shouldStop.load()) ;
	if(shouldStop.load()) {
		break;
	}

	eventQueue.process();
}
```

```c++
bool peekEvent(EventQueue::QueuedEvent * queuedEvent);
```
Retrieve an event from the queue. The event is returned in `queuedEvent`.  
If the queue is empty, the function returns false, otherwise true if an event is retrieved successfully.  
After the function returns, the original even is still in the queue.  
Note: `peekEvent` doesn't work with any non-copyable event arguments. If `peekEvent` is called when any arguments are non-copyable, compile fails.

```c++
bool takeEvent(EventQueue::QueuedEvent * queuedEvent);
```
Take an event from the queue and remove the original event from the queue. The event is returned in `queuedEvent`.  
If the queue is empty, the function returns false, otherwise true if an event is retrieved successfully.  
After the function returns, the original even is removed from the queue.  
Note: `takeEvent` works with non-copyable event arguments.

```c++
void dispatch(const QueuedEvent & queuedEvent);
void dispatch(QueuedEvent && queuedEvent);
```
Dispatch an event which was returned by `peekEvent` or `takeEvent`.  

**Inner class EventQueue::DisableQueueNotify**  

`EventQueue::DisableQueueNotify` is a RAII class that temporarily prevents the event queue from waking up any waiting threads. When any `DisableQueueNotify` object exist, calling `enqueue` doesn't wake up any threads that are blocked by `wait`. When the `DisableQueueNotify` object is out of scope, the waking up is resumed. If there are more than one `DisableQueueNotify` objects, the waking up is only resumed after all `DisableQueueNotify` objects are destroyed.  

To use `DisableQueueNotify`, construct it with a pointer to event queue.

Sampe code
```c++
using EQ = eventpp::EventQueue<int, void ()>;
EQ queue;
{
	EQ::DisableQueueNotify disableNotify(&queue);
	// any blocking threads will not be waken up by the below two lines.
	queue.enqueue(1);
	queue.enqueue(2);
}
// any blocking threads are waken up here immediately.

// any blocking threads will be waken up by below line since there is no DisableQueueNotify.
queue.enqueue(3);
```

<a name="internal-data-structure"></a>
## Internal data structure

EventQueue uses three `std::list` to manage the event queue.  
The first busy list holds all nodes with queued events.  
The second idle list holds all idle nodes. After an event is dispatched and removed from the queue, instead of freeing the memory, EventQueue moves the unused node to the idle list. This can improve performance and avoid memory fragment.  
The third list is a local temporary list used in function `process()`. During processing, the busy list is swapped to the temporary list, all events are dispatched from the temporary list, then the temporary list is returned and appended to the idle list.

