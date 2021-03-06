// eventpp library
// Copyright (C) 2018 Wang Qi (wqking)
// Github: https://github.com/wqking/eventpp
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//   http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "test.h"
#include "eventpp/eventqueue.h"

#include <thread>
#include <numeric>
#include <algorithm>
#include <random>
#include <vector>
#include <atomic>

TEST_CASE("queue, std::string, void (const std::string &)")
{
	eventpp::EventQueue<std::string, void (const std::string &)> queue;

	int a = 1;
	int b = 5;

	queue.appendListener("event1", [&a](const std::string &) {
		a = 2;
	});
	queue.appendListener("event1", eraseArgs1([&b]() {
		b = 8;
	}));

	REQUIRE(a != 2);
	REQUIRE(b != 8);

	queue.enqueue("event1");
	queue.process();
	REQUIRE(a == 2);
	REQUIRE(b == 8);
}

TEST_CASE("queue, int, void ()")
{
	eventpp::EventQueue<int, void ()> queue;

	int a = 1;
	int b = 5;

	queue.appendListener(3, [&a]() {
		a += 1;
	});
	queue.appendListener(3, [&b]() {
		b += 3;
	});

	REQUIRE(a != 2);
	REQUIRE(b != 8);

	queue.enqueue(3);
	queue.process();

	REQUIRE(a == 2);
	REQUIRE(b == 8);
}

TEST_CASE("queue, int, void (const std::string &, int)")
{
	struct NonDefaultConstructible
	{
		explicit NonDefaultConstructible(const int i) : i(i) {}
		int i;
	};

	eventpp::EventQueue<int, void (const std::string &, const NonDefaultConstructible &)> queue;

	const int event = 3;

	std::vector<std::string> sList(2);
	std::vector<int> iList(sList.size());

	queue.appendListener(event, [&sList, &iList](const std::string & s, const NonDefaultConstructible & n) {
		sList[0] = s;
		iList[0] = n.i;
	});
	queue.appendListener(event, [&sList, &iList](const std::string & s, NonDefaultConstructible n) {
		sList[1] = s + "2";
		iList[1] = n.i + 5;
	});

	REQUIRE(sList[0] != "first");
	REQUIRE(sList[1] != "first2");
	REQUIRE(iList[0] != 3);
	REQUIRE(iList[1] != 8);

	SECTION("Parameters") {
		queue.enqueue(event, "first", NonDefaultConstructible(3));
		queue.process();

		REQUIRE(sList[0] == "first");
		REQUIRE(sList[1] == "first2");
		REQUIRE(iList[0] == 3);
		REQUIRE(iList[1] == 8);
	}

	SECTION("Reference parameters should not be modified") {
		std::string s = "first";
		queue.enqueue(event, s, NonDefaultConstructible(3));
		s = "";
		queue.process();

		REQUIRE(sList[0] == "first");
		REQUIRE(sList[1] == "first2");
		REQUIRE(iList[0] == 3);
		REQUIRE(iList[1] == 8);
	}
}

TEST_CASE("queue, customized event")
{
	struct MyEvent {
		int type;
		std::string message;
		int param;
	};

	struct MyEventPolicies
	{
		static int getEvent(const MyEvent & e, std::string) {
			return e.type;
		}
	};

	eventpp::EventQueue<int, void (const MyEvent &, std::string), MyEventPolicies> queue;

	std::string a = "Hello ";
	std::string b = "World ";

	queue.appendListener(3, [&a](const MyEvent & e, const std::string & s) {
		a += e.message + s + std::to_string(e.param);
	});
	queue.appendListener(3, [&b](const MyEvent & e, const std::string & s) {
		b += e.message + s + std::to_string(e.param);
	});

	REQUIRE(a == "Hello ");
	REQUIRE(b == "World ");

	queue.enqueue(MyEvent { 3, "very ", 38 }, "good");
	queue.process();

	REQUIRE(a == "Hello very good38");
	REQUIRE(b == "World very good38");
}

TEST_CASE("queue, no memory leak in queued arguments")
{
	using SP = std::shared_ptr<int>;
	using WP = std::weak_ptr<int>;
	using EQ = eventpp::EventQueue<int, void (SP)>;

	std::unique_ptr<EQ> queue(new EQ());
	std::vector<WP> wpList;

	auto add = [&wpList, &queue](int n) {
		SP sp(std::make_shared<int>(n));
		queue->enqueue(n, sp);
		wpList.push_back(WP(sp));
	};

	add(1);
	add(2);
	add(3);

	REQUIRE(! checkAllWeakPtrAreFreed(wpList));

	SECTION("No memory leak after process()") {
		queue->process();
		REQUIRE(checkAllWeakPtrAreFreed(wpList));
	}

	SECTION("No memory leak after queue is deleted") {
		queue.reset();
		REQUIRE(checkAllWeakPtrAreFreed(wpList));
	}
}

TEST_CASE("queue, no memory leak or double free in queued arguments")
{
	struct Item {
		Item(const int index, std::vector<int> * counterList)
			: index(index), counterList(counterList)
		{
			++(*counterList)[index];
		}

		~Item()
		{
			--(*counterList)[index];
		}

		Item(Item && other)
			: index(other.index), counterList(other.counterList)
		{
			++(*counterList)[index];
		}

		Item(const Item & other)
			: index(other.index), counterList(other.counterList)
		{
			++(*counterList)[index];
		}

		Item & operator = (const Item & other)
		{
			index = other.index;
			counterList = other.counterList;
			++(*counterList)[index];
			return *this;
		}

		int index;
		std::vector<int> * counterList;
	};
	using EQ = eventpp::EventQueue<int, void (const Item &)>;

	std::vector<int> counterList(4);
	std::unique_ptr<EQ> queue(new EQ());

	auto add = [&counterList, &queue](int n) {
		queue->enqueue(n, Item(n, &counterList));
	};

	add(0);
	add(1);
	add(2);
	add(3);

	REQUIRE(counterList == std::vector<int>{ 1, 1, 1, 1 });

	SECTION("No memory leak or double free after process()") {
		queue->process();
		REQUIRE(counterList == std::vector<int>{ 0, 0, 0, 0 });
	}

	SECTION("No memory leak or double free after queue is deleted") {
		queue.reset();
		REQUIRE(counterList == std::vector<int>{ 0, 0, 0, 0 });
	}
}

TEST_CASE("queue, non-copyable but movable unique_ptr")
{
	using PTR = std::unique_ptr<int>;
	using EQ = eventpp::EventQueue<int, void (const PTR &)>;
	EQ queue;

	constexpr int itemCount = 3;

	std::vector<int> dataList(itemCount);

	queue.appendListener(3, [&dataList](const PTR & ptr) {
		++dataList[*ptr];
	});

	queue.enqueue(3, PTR(new int(0)));
	queue.enqueue(3, PTR(new int(1)));
	queue.enqueue(3, PTR(new int(2)));

	SECTION("process") {
		queue.process();
		REQUIRE(dataList == std::vector<int>{ 1, 1, 1 });
	}

	// peekEvent doesn't compile, it requires the argument copyable.

	SECTION("takeEvent/dispatch") {
		EQ::QueuedEvent event;
		REQUIRE(queue.takeEvent(&event));
		queue.dispatch(event);
		REQUIRE(dataList == std::vector<int>{ 1, 0, 0 });
	}

	SECTION("takeEvent/process") {
		EQ::QueuedEvent event;
		REQUIRE(queue.takeEvent(&event));
		queue.process();
		REQUIRE(dataList == std::vector<int>{ 0, 1, 1 });
	}
}

TEST_CASE("queue, peekEvent/takeEvent/dispatch")
{
	using SP = std::shared_ptr<int>;
	using WP = std::weak_ptr<int>;
	using EQ = eventpp::EventQueue<int, void (SP)>;

	std::unique_ptr<EQ> queue(new EQ());
	std::vector<WP> wpList;
	constexpr int itemCount = 3;

	std::vector<int> dataList(itemCount);

	queue->appendListener(3, [&dataList](const SP & sp) {
		++dataList[*sp];
	});

	auto add = [&wpList, &queue](int e, int n) {
		SP sp(std::make_shared<int>(n));
		queue->enqueue(e, sp);
		wpList.push_back(WP(sp));
	};

	add(3, 0);
	add(3, 1);
	add(3, 2);

	SECTION("peek") {
		EQ::QueuedEvent event;
		REQUIRE(queue->peekEvent(&event));
		REQUIRE(std::get<0>(event) == 3);
		REQUIRE(*std::get<1>(event) == 0);
		REQUIRE(wpList[0].use_count() == 2);
	}

	SECTION("peek/peek") {
		EQ::QueuedEvent event;
		REQUIRE(queue->peekEvent(&event));
		REQUIRE(std::get<0>(event) == 3);
		REQUIRE(*std::get<1>(event) == 0);
		REQUIRE(wpList[0].use_count() == 2);

		EQ::QueuedEvent event2;
		REQUIRE(queue->peekEvent(&event2));
		REQUIRE(std::get<0>(event2) == 3);
		REQUIRE(*std::get<1>(event2) == 0);
		REQUIRE(wpList[0].use_count() == 3);
	}

	SECTION("peek/take") {
		EQ::QueuedEvent event;
		REQUIRE(queue->peekEvent(&event));
		REQUIRE(std::get<0>(event) == 3);
		REQUIRE(*std::get<1>(event) == 0);
		REQUIRE(wpList[0].use_count() == 2);

		EQ::QueuedEvent event2;
		REQUIRE(queue->takeEvent(&event2));
		REQUIRE(std::get<0>(event2) == 3);
		REQUIRE(*std::get<1>(event2) == 0);
		REQUIRE(wpList[0].use_count() == 2);
	}

	SECTION("peek/take/peek") {
		EQ::QueuedEvent event;
		REQUIRE(queue->peekEvent(&event));
		REQUIRE(std::get<0>(event) == 3);
		REQUIRE(*std::get<1>(event) == 0);
		REQUIRE(wpList[0].use_count() == 2);

		EQ::QueuedEvent event2;
		REQUIRE(queue->takeEvent(&event2));
		REQUIRE(std::get<0>(event2) == 3);
		REQUIRE(*std::get<1>(event2) == 0);
		REQUIRE(wpList[0].use_count() == 2);

		EQ::QueuedEvent event3;
		REQUIRE(queue->peekEvent(&event3));
		REQUIRE(std::get<0>(event3) == 3);
		REQUIRE(*std::get<1>(event3) == 1);
		REQUIRE(wpList[0].use_count() == 2);
		REQUIRE(wpList[1].use_count() == 2);
	}

	SECTION("peek/dispatch/peek/dispatch again") {
		EQ::QueuedEvent event;
		REQUIRE(queue->peekEvent(&event));
		REQUIRE(std::get<0>(event) == 3);
		REQUIRE(*std::get<1>(event) == 0);
		REQUIRE(wpList[0].use_count() == 2);

		queue->dispatch(event);

		EQ::QueuedEvent event2;
		REQUIRE(queue->peekEvent(&event2));
		REQUIRE(std::get<0>(event2) == 3);
		REQUIRE(*std::get<1>(event2) == 0);
		REQUIRE(wpList[0].use_count() == 3);

		REQUIRE(dataList == std::vector<int>{ 1, 0, 0 });

		queue->dispatch(event);
		REQUIRE(dataList == std::vector<int>{ 2, 0, 0 });
	}

	SECTION("process") {
		// test the queue works with simple process(), ensure the process()
		// in the next "take all/process" works correctly.
		REQUIRE(dataList == std::vector<int>{ 0, 0, 0 });
		queue->process();
		REQUIRE(dataList == std::vector<int>{ 1, 1, 1 });
	}

	SECTION("take all/process") {
		for(int i = 0; i < itemCount; ++i) {
			EQ::QueuedEvent event;
			REQUIRE(queue->takeEvent(&event));
		}

		EQ::QueuedEvent event;
		REQUIRE(! queue->peekEvent(&event));
		REQUIRE(! queue->takeEvent(&event));

		REQUIRE(dataList == std::vector<int>{ 0, 0, 0 });
		queue->process();
		REQUIRE(dataList == std::vector<int>{ 0, 0, 0 });
	}
}

TEST_CASE("queue multi threading, int, void (int)")
{
	using EQ = eventpp::EventQueue<int, void (int)>;
	EQ queue;

	constexpr int threadCount = 256;
	constexpr int dataCountPerThread = 1024 * 4;
	constexpr int itemCount = threadCount * dataCountPerThread;

	std::vector<int> eventList(itemCount);
	std::iota(eventList.begin(), eventList.end(), 0);
	std::shuffle(eventList.begin(), eventList.end(), std::mt19937(std::random_device()()));

	std::vector<int> dataList(itemCount);

	for(int i = 0; i < itemCount; ++i) {
		queue.appendListener(eventList[i], [&queue, i, &dataList](const int d) {
			dataList[i] += d;
		});
	}

	std::vector<std::thread> threadList;
	for(int i = 0; i < threadCount; ++i) {
		threadList.emplace_back([i, dataCountPerThread, &queue, itemCount]() {
			for(int k = i * dataCountPerThread; k < (i + 1) * dataCountPerThread; ++k) {
				queue.enqueue(k, 3);
			}
			for(int k = 0; k < 10; ++k) {
				queue.process();
			}
		});
	}
	for(int i = 0; i < threadCount; ++i) {
		threadList[i].join();
	}

	std::vector<int> compareList(itemCount);
	std::fill(compareList.begin(), compareList.end(), 3);
	REQUIRE(dataList == compareList);
}

TEST_CASE("queue multi threading, one thread waits")
{
	using EQ = eventpp::EventQueue<int, void (int)>;
	EQ queue;

	// note, all events will be process from the other thread instead of main thread
	constexpr int stopEvent = 1;
	constexpr int otherEvent = 2;

	constexpr int itemCount = 5;

	std::vector<int> dataList(itemCount);

	std::atomic<int> threadProcessCount(0);

	std::thread thread([stopEvent, otherEvent, &dataList, &queue, &threadProcessCount]() {
		volatile bool shouldStop = false;
		queue.appendListener(stopEvent, [&shouldStop](int) {
			shouldStop = true;
		});
		queue.appendListener(otherEvent, [&dataList](const int index) {
			dataList[index] += index + 1;
		});

		while(! shouldStop) {
			queue.wait();

			++threadProcessCount;

			queue.process();
		}
	});
	
	REQUIRE(threadProcessCount.load() == 0);

	auto waitUntilQueueEmpty = [&queue]() {
		while(queue.waitFor(std::chrono::nanoseconds(0))) ;
	};

	SECTION("Enqueue one by one") {
		queue.enqueue(otherEvent, 1);
		waitUntilQueueEmpty();
		REQUIRE(threadProcessCount.load() == 1);
		REQUIRE(queue.empty());
		REQUIRE(dataList == std::vector<int>{ 0, 2, 0, 0, 0 });

		queue.enqueue(otherEvent, 3);
		waitUntilQueueEmpty();
		REQUIRE(threadProcessCount.load() == 2);
		REQUIRE(queue.empty());
		REQUIRE(dataList == std::vector<int>{ 0, 2, 0, 4, 0 });
	}

	SECTION("Enqueue two") {
		queue.enqueue(otherEvent, 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		REQUIRE(threadProcessCount.load() == 1);
		REQUIRE(queue.empty());

		queue.enqueue(otherEvent, 3);
		waitUntilQueueEmpty();

		REQUIRE(threadProcessCount.load() == 2);
		REQUIRE(dataList == std::vector<int>{ 0, 2, 0, 4, 0 });
	}

	SECTION("Batching enqueue") {
		{
			EQ::DisableQueueNotify disableNotify(&queue);

			queue.enqueue(otherEvent, 2);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			REQUIRE(threadProcessCount.load() == 0);
			REQUIRE(! queue.empty());

			queue.enqueue(otherEvent, 4);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			REQUIRE(threadProcessCount.load() == 0);
			REQUIRE(! queue.empty());
		}

		waitUntilQueueEmpty();
		REQUIRE(threadProcessCount.load() == 1);
		REQUIRE(dataList == std::vector<int>{ 0, 0, 3, 0, 5 });
	}

	queue.enqueue(stopEvent, 1);
	thread.join();
}

TEST_CASE("queue multi threading, many threads wait")
{
	using EQ = eventpp::EventQueue<int, void (int)>;
	EQ queue;

	// note, all events will be process from the other thread instead of main thread
	constexpr int stopEvent = 1;
	constexpr int otherEvent = 2;

	constexpr int unit = 3;
	constexpr int itemCount = 30 * unit;

	std::vector<int> dataList(itemCount);

	std::vector<std::thread> threadList;

	std::atomic<bool> shouldStop(false);

	queue.appendListener(stopEvent, [&shouldStop](int) {
		shouldStop = true;
	});
	queue.appendListener(otherEvent, [&dataList](const int index) {
		++dataList[index];
	});

	for(int i = 0; i < itemCount; ++i) {
		threadList.emplace_back([stopEvent, otherEvent, i, &dataList, &queue, &shouldStop]() {
			for(;;) {
				// can't use queue.wait() because the thread can't be waken up by shouldStop = true
				while(! queue.waitFor(std::chrono::milliseconds(10)) && ! shouldStop.load()) ;

				if(shouldStop.load()) {
					break;
				}

				queue.process();
			}
		});
	}

	for(int i = 0; i < itemCount; ++i) {
		queue.enqueue(otherEvent, i);
		std::this_thread::sleep_for(std::chrono::milliseconds(0));
	}

	for(int i = 0; i < itemCount; i += unit) {
		EQ::DisableQueueNotify disableNotify(&queue);
		for(int k = 0; k < unit; ++k) {
			queue.enqueue(otherEvent, i);
			std::this_thread::sleep_for(std::chrono::milliseconds(0));
		}
	}

	queue.enqueue(stopEvent);

	for(auto & thread : threadList) {
		thread.join();
	}

	REQUIRE(std::accumulate(dataList.begin(), dataList.end(), 0) == itemCount * 2);
}

