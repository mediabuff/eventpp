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

#ifndef CALLBACKLIST_H_588722158669
#define CALLBACKLIST_H_588722158669

#include "eventpolicies.h"

#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <utility>

namespace eventpp {

namespace internal_ {

template <typename F, typename ...A>
struct CanInvoke
{
	template <typename U, typename ...X>
	static auto invoke(int) -> decltype(std::declval<U>()(std::declval<X>()...), std::true_type());

	template <typename U, typename ...X>
	static auto invoke(...) -> std::false_type;

	enum {
		value = !! decltype(invoke<F, A...>(0))()
	};
};

template <
	typename Prototype,
	typename PoliciesType
>
class CallbackListBase;

template <
	typename PoliciesType,
	typename ReturnType, typename ...Args
>
class CallbackListBase<
	ReturnType (Args...),
	PoliciesType
>
{
private:
	using Policies = PoliciesType;

	using Threading = typename SelectThreading<Policies, HasTypeThreading<Policies>::value>::Type;

	using Mutex = typename Threading::Mutex;
	using Callback_ = typename SelectCallback<
		Policies,
		HasTypeCallback<Policies>::value,
		std::function<ReturnType (Args...)>
	>::Type;

	using CanContinueInvoking = typename SelectCanContinueInvoking<
		Policies, HasFunctionCanContinueInvoking<Policies>::value
	>::Type;

	struct Node;
	using NodePtr = std::shared_ptr<Node>;

	struct Node
	{
		using Counter = uint64_t;

		Node(const Callback_ & callback, const Counter counter)
			: callback(callback), counter(counter)
		{
		}

		NodePtr previous;
		NodePtr next;
		Callback_ callback;
		Counter counter;
	};

	class Handle_ : public std::weak_ptr<Node>
	{
	private:
		using super = std::weak_ptr<Node>;

	public:
		using super::super;

		operator bool () const noexcept {
			return ! this->expired();
		}
	};

	using Counter = typename Node::Counter;
	enum : Counter {
		removedCounter = 0
	};

public:
	using Callback = Callback_;
	using Handle = Handle_;

public:
	CallbackListBase()
		:
			head(),
			tail(),
			mutex(),
			currentCounter(0)
	{
	}

	CallbackListBase(CallbackListBase &&) = delete;
	CallbackListBase(const CallbackListBase &) = delete;
	CallbackListBase & operator = (const CallbackListBase &) = delete;

	~CallbackListBase()
	{
		// Don't lock mutex here since it may throw exception

		NodePtr node = head;
		head.reset();
		while(node) {
			NodePtr next = node->next;
			node->previous.reset();
			node->next.reset();
			node = next;
		}
		node.reset();
	}

	bool empty() const {
		// Don't lock the mutex for performance reason.
		// !head still works even when the underlying raw pointer is garbled (for other thread is writting to head)
		//std::lock_guard<Mutex> lockGuard(mutex);

		return ! head;
	}

	operator bool() const {
		return ! empty();
	}

	Handle append(const Callback & callback)
	{
		NodePtr node(std::make_shared<Node>(callback, getNextCounter()));

		std::lock_guard<Mutex> lockGuard(mutex);

		if(head) {
			node->previous = tail;
			tail->next = node;
			tail = node;
		}
		else {
			head = node;
			tail = node;
		}

		return Handle(node);
	}

	Handle prepend(const Callback & callback)
	{
		NodePtr node(std::make_shared<Node>(callback, getNextCounter()));

		std::lock_guard<Mutex> lockGuard(mutex);

		if(head) {
			node->next = head;
			head->previous = node;
			head = node;
		}
		else {
			head = node;
			tail = node;
		}

		return Handle(node);
	}

	Handle insert(const Callback & callback, const Handle before)
	{
		NodePtr beforeNode = before.lock();
		if(beforeNode) {
			NodePtr node(std::make_shared<Node>(callback, getNextCounter()));

			std::lock_guard<Mutex> lockGuard(mutex);

			doInsert(node, beforeNode);

			return Handle(node);
		}

		return append(callback);
	}

	bool remove(const Handle handle)
	{
		std::lock_guard<Mutex> lockGuard(mutex);
		auto node = handle.lock();
		if(node) {
			doRemoveNode(node);
			return true;
		}

		return false;
	}

	template <typename Func>
	void forEach(Func && func) const
	{
		doForEachIf([&func, this](NodePtr & node) -> bool {
			doForEachInvoke<void>(func, node);
			return true;
		});
	}

	template <typename Func>
	bool forEachIf(Func && func) const
	{
		return doForEachIf([&func, this](NodePtr & node) -> bool {
			return doForEachInvoke<bool>(func, node);
		});
	}

	void operator() (Args ...args) const
	{
		forEachIf([&args...](Callback & callback) -> bool {
			callback(args...);
			return CanContinueInvoking::canContinueInvoking(args...);
		});
	}

private:
	template <typename F>
	bool doForEachIf(F && f) const
	{
		NodePtr node;

		{
			std::lock_guard<Mutex> lockGuard(mutex);
			node = head;
		}

		const Counter counter = currentCounter.load(std::memory_order_acquire);

		while(node) {
			if(node->counter != removedCounter && counter >= node->counter) {
				if(! f(node)) {
					return false;
				}
			}

			{
				std::lock_guard<Mutex> lockGuard(mutex);
				node = node->next;
			}
		}

		return true;
	}

	template <typename RT, typename Func>
	auto doForEachInvoke(Func && func, NodePtr & node) const
		-> typename std::enable_if<CanInvoke<Func, Handle, Callback &>::value, RT>::type
	{
		return func(Handle(node), node->callback);
	}

	template <typename RT, typename Func>
	auto doForEachInvoke(Func && func, NodePtr & node) const
		-> typename std::enable_if<CanInvoke<Func, Handle>::value, RT>::type
	{
		return func(Handle(node));
	}

	template <typename RT, typename Func>
	auto doForEachInvoke(Func && func, NodePtr & node) const
		-> typename std::enable_if<CanInvoke<Func, Callback &>::value, RT>::type
	{
		return func(node->callback);
	}

	void doRemoveNode(NodePtr & node)
	{
		if(node->next) {
			node->next->previous = node->previous;
		}
		if(node->previous) {
			node->previous->next = node->next;
		}

		if(head == node) {
			head = node->next;
		}
		if(tail == node) {
			tail = node->previous;
		}

		// Mark it as deleted
		node->counter = removedCounter;

		// don't modify node->previous or node->next
		// because node may be still used in a loop.
	}

	void doInsert(NodePtr & node, NodePtr & beforeNode)
	{
		node->previous = beforeNode->previous;
		node->next = beforeNode;
		if(beforeNode->previous) {
			beforeNode->previous->next = node;
		}
		beforeNode->previous = node;

		if(beforeNode == head) {
			head = node;
		}
	}

	Counter getNextCounter()
	{
		currentCounter.fetch_add(1, std::memory_order_acq_rel);

		Counter result = currentCounter.load(std::memory_order_acquire);
		if(result == 0) { // overflow, let's reset all nodes' counters.
			{
				std::lock_guard<Mutex> lockGuard(mutex);
				NodePtr node = head;
				while(node) {
					node->counter = 1;
					node = node->next;
				}
			}
			++currentCounter;
			result = currentCounter.load(std::memory_order_acquire);
		}

		return result;
	}

private:
	NodePtr head;
	NodePtr tail;
	mutable Mutex mutex;
	typename Threading::template Atomic<Counter> currentCounter;

};


} //namespace internal_


template <
	typename Prototype,
	typename Policies = DefaultPolicies
>
class CallbackList : public internal_::CallbackListBase<Prototype, Policies>
{
};


} //namespace eventpp


#endif
