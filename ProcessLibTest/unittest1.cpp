#include "stdafx.h"
#include "CppUnitTest.h"
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <condition_variable>
#include <Process\FindLoadedModuleHandle.h>
#include <Process\GetProcAddress.h>
#include <Process\NtDll.h>
#include <Process\User32.h>
#include <Process\ThreadEx.h>
#include <Process\Fiber.h>
#include <Process\ThreadPool.h>
#include <Process\ThreadPoolSimpleCallback.h>
#include <Process\ThreadPoolWork.h>
#include <Process\ThreadPoolWait.h>
#include <Process\ThreadPoolTimer.h>
#include <Process\Coroutine.h>
#include <Process\Pipe.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace ProcessLibTest
{
	using namespace Process::EnvironmentBlock;

	TEST_CLASS(UnitTest1)
	{
	public:

		TEST_METHOD(TestLdrDataTable)
		{
			auto dllName = L"user32.dll";
			auto handle = FindLoadedModuleHandle(dllName);
			auto handle2 = LoadLibrary(dllName);
			Assert::IsTrue(handle == handle2);

		}
		TEST_METHOD(TestGetProcAddress)
		{
			auto dllName = L"user32.dll";
			auto dllHandle = FindLoadedModuleHandle(dllName);
			auto funcNameGrp = { "MessageBoxW" ,"NotExist" };
			for each (auto var in funcNameGrp)
			{
				auto funcName = var;
				auto funcHandle1 = Process::Overwrite::GetProcAddress(dllHandle, [funcName](LPCSTR val)
				{
					return strcmp(funcName, val) == 0;
				});
				auto funcHandle2 = Process::Overwrite::GetProcAddress(dllHandle, funcName);
				auto trueValue = ::GetProcAddress(dllHandle, funcName);
				Assert::IsTrue(trueValue == funcHandle1);
				Assert::IsTrue(trueValue == funcHandle2);
			}

		}
		TEST_METHOD(TestLazyload)
		{
			using Process::LazyLoad::NtDll;
			using Process::LazyLoad::User32;
			auto& ldrLoadDll = NtDll::Instance().LdrLoadDll;
			ldrLoadDll.Load();
			// funtionû��ȡָ��ĺ�����֤����ֻ���¶ϵ���
			auto ntdll = GetModuleHandleA("ntdll.dll");
			if (ntdll == NULL)
			{
				Assert::Fail();
				return;
			}
			auto trueValue = GetProcAddress(ntdll, "LdrLoadDll");
			if (trueValue == NULL)
			{
				Assert::Fail();
				return;
			}
			auto& mbox = User32::Instance().MessageBoxA;
			mbox(0, "Hello World!", "Goodbye World!", MB_OK);
		}
		static _STD pair<bool, CONTEXT> OldContext;
		static bool IsContextTestSucceed;
		static void Output()
		{
			using namespace Process::Thread;
#ifdef _X86_
			Logger::WriteMessage("Set Eip Test");
#else
			Logger::WriteMessage("Set Rip Test");
#endif // _X86_
			for (size_t i = 0; i < 10; i++)
			{
				std::ostringstream s("-> ", std::ios::ate);
				s << i;
				Logger::WriteMessage(s.str().c_str());
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			Process::Thread::this_thread::SetContext(OldContext.second);
			IsContextTestSucceed = false;

		}
		TEST_METHOD(TestThreadEx)
		{
			using namespace Process::Thread;
			using ThreadEx = ThreadEx<>;

			ThreadEx t([]()
			{
				// ���Թ����߳�
				Logger::WriteMessage("Suspend");
				this_thread::Suspend();
				Logger::WriteMessage("Resume");
				for (size_t i = 0; i < 10; i++)
				{
					Logger::WriteMessage("-->");
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
				}
			});


			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			t.Resume();

			//�����������ȼ�
			auto priority = ThreadPriority::AboveNormal;
			Assert::IsTrue(t.SetPriority(priority));
			Assert::IsTrue(t.GetPriority() == priority);

			// ����cpu����
			auto mask = 1;
			auto oldMask = t.SetAffinityMask(mask);
			Assert::IsTrue(t.SetAffinityMask(oldMask) == mask);

			// �����޸�context
			std::this_thread::sleep_for(std::chrono::seconds(1));
			{
				ThreadEx::SuspendGuard guard(t);
				OldContext = t.GetContext(ContextFlag::Full);
				auto ctx = OldContext.second;
#ifdef _X86_
				auto xip = &CONTEXT::Eip;
				auto xsp = &CONTEXT::Esp;
				using reg = DWORD;
#else
				auto xip = &CONTEXT::Rip;
				auto xsp = &CONTEXT::Rsp;
				using reg = DWORD64;
#endif // _X86_
				ctx.*xip = reinterpret_cast<reg>(&Output);
				// ջ�����Ʒ�ֹ���������ݱ�����
				ctx.*xsp -= sizeof(reg) * 0x10;

				t.SetContext(ctx);
			}
			t.join();
			Assert::IsTrue(IsContextTestSucceed);
		}
		TEST_METHOD(TestFiber)
		{
			using namespace Process::Fiber;
			using namespace std;

			auto pf = Process::Fiber::ConvertThreadToFiber(NULL);

			PNT_TIB fiberCtx;


			// ������ͨ����
			auto test1 = MakeFiber([pf, &fiberCtx](const char* val)
			{
				// ȡ�����ģ����飩
				auto data = GetFiberData();
				// ʵ�鿴һ���л�ʱ���ʲô -> �����Ķ���ָ�룬����ṹʲô��û��
				// TODO: �ҳ���ʲô�洢Xip
				fiberCtx = GetCurrentFiberContext();
				assert(data == fiberCtx->FiberData);

				Logger::WriteMessage("Begin Test");
				SwitchToFiber(pf);

				Logger::WriteMessage(val);
				SwitchToFiber(pf);
			}, "Succeed");
			auto ptr = test1.NativeHandle();
			test1.Switch();

			// �����ƶ�����
			auto move1(_STD move(test1));

			move1.Switch();



			// �������ô���
			bool test = false;
			auto test2 = MakeFiber([pf](bool& val)
			{
				Logger::WriteMessage("value = false");
				SwitchToFiber(pf);
				val = true;
				Logger::WriteMessage("value = true");
				SwitchToFiber(pf);
			}, _STD ref(test));

			test2.Switch();
			Assert::IsTrue(!test);
			// �����ƶ�����
			auto move2(_STD move(test2));
			move2.Switch();
			Assert::IsTrue(test);

			// ���Դ����(˳�����ǽ��tuple�Ƿ���ȷ)
			int intVal = 123;
			short shortVal = 456;

			const void* val0 = &intVal;
			void* val1 = &shortVal;
			const int& val2 = intVal;
			short& val3 = shortVal;
			char val4 = 'X';
			shared_ptr<string> val5 = make_shared<string>("Str");
			tuple<int> val6 = make_tuple(789);
			unique_ptr<int> val9;
			auto test3 = MakeFiber([pf](
				const void* arg0,
				void* arg1,
				const int& arg2,
				short& arg3,
				char arg4,
				shared_ptr<string> arg5,
				tuple<int> arg6,
				const int& arg7,
				int&& arg8,
				unique_ptr<int>&& arg9
				)
			{

				SwitchToFiber(pf);
			}, val0, val1, _STD ref(val2), _STD ref(val3), val4, val5, val6, 12345, 67890, _STD move(val9));
			test3.Switch();


			// �޲β���
			auto test4 = MakeFiber([pf]()
			{
				Logger::WriteMessage("�޲β���");
				SwitchToFiber(pf);
			});
			test4.Switch();
			Logger::WriteMessage("End");
		}
		TEST_METHOD(TestThreadPool)
		{
			using namespace Process::Thread;
			using namespace std;
			// �������ֱ����

			std::condition_variable cv;
			typedef _STD vector<int> Vector;
			Vector vec(654321);
			_STD atomic<bool> inited = false;

			ThreadPool::SubmitCallback([&](CallbackInstance&)
			{
				_STD iota(begin(vec), end(vec), 1);
				inited.store(true, memory_order_release);
				cv.notify_all();
			});


			vector<future<long long>> futures;
			size_t vecLen = vec.size();
			size_t dataPerThread = vec.size() / thread::hardware_concurrency();
			// ÿ���̷߳�16�δ����꣬ÿ�δ������ݶ�64ȡ��
			size_t step = (dataPerThread / 16) & (-64);
			auto begin = vec.begin();
			auto end = vec.end();

			std::mutex mtx;
			std::unique_lock<std::mutex> lock(mtx);
			cv.wait(lock, [&inited]()
			{
				return inited.load(memory_order_acquire);
			});
			while (begin < end)
			{
				auto dst = _STD distance(begin, end);
				auto tmpstep = dst > step ? step : dst;
				auto tmpend = begin + tmpstep;
				_STD packaged_task<long long(CallbackInstance&)> task([begin_ = begin, end_ = tmpend](CallbackInstance& instance) mutable
				{
					return accumulate<Vector::iterator, long long>(begin_, end_, 0);
				});
				begin = tmpend;
				futures.emplace_back(task.get_future());
				auto sumitResult = ThreadPool::SubmitCallback(move(task));
				Assert::IsTrue(sumitResult);
			}
			auto result = accumulate<vector<future<long long>>::iterator, long long>(futures.begin(), futures.end(), 0, [](long long ret, future<long long>& val)
			{
				return ret + val.get();
			});

			Assert::IsTrue(futures.size() == 1 + (vec.size() - 1) / step);

			long long trueResult = (*vec.begin() + *vec.rbegin()) * (double)vec.size() / 2;

			Assert::IsTrue(trueResult == result);

		}
		TEST_METHOD(TestThreadPoolSimpleCallback)
		{
			using namespace Process::Thread;
			// ����ThreadPoolSimpleCallback
			bool isSimpleCallbackSucceed = false;
			std::condition_variable cv2;
			ThreadPoolSimpleCallback sc([&isSimpleCallbackSucceed, &cv2](CallbackInstance& inst)
			{
				Logger::WriteMessage("ThreadPoolSimpleCallback");
				isSimpleCallbackSucceed = true;
				cv2.notify_all();
			});
			ThreadPool::Submit(sc);

			std::mutex mtx;
			std::unique_lock<std::mutex> lock(mtx);
			cv2.wait_for(lock, std::chrono::seconds(1), [&isSimpleCallbackSucceed]()
			{
				return isSimpleCallbackSucceed;
			});

			Assert::IsTrue(isSimpleCallbackSucceed);
		}
		TEST_METHOD(TestThreadPoolWork)
		{
			using namespace Process::Thread;
			// ����ThreadPoolWork
			bool isWorkSucceed = false;
			ThreadPoolWork work([&isWorkSucceed](CallbackInstance& instance, ThreadPoolWork& self)
			{
				Logger::WriteMessage("ThreadPoolWork");

				isWorkSucceed = true;
			});
			ThreadPool::Submit(work);
			// ���Եȴ�
			work.WaitForCallbacks();

			Assert::IsTrue(isWorkSucceed);
		}
		TEST_METHOD(TestThreadPoolWait)
		{
			using namespace Process::Thread;

			// ����ThreadPoolWait
			// TODO: ֮������ͬ���Ķ���ҲҪ��һ����װ
			auto e = CreateEvent(NULL, true, false, NULL);
			if (e == NULL)
			{
				Assert::Fail(L"create event fail");
				return;
			}
			bool isWaitSucceed = false;
			std::condition_variable cv3;

			ThreadPoolWait wait(e, NULL, [&isWaitSucceed, &cv3](CallbackInstance&, ThreadPoolWait&, WaitResult)
			{
				Logger::WriteMessage("ThreadPoolWait");
				isWaitSucceed = true;
				cv3.notify_all();
			});
			ThreadPool::Register(wait);


			// ����
			SetEvent(e);
			CloseHandle(e);

			wait.WaitForCallbacks();

			std::mutex mtx;
			std::unique_lock<std::mutex> lock(mtx);
			cv3.wait_for(lock, std::chrono::seconds(1), [&isWaitSucceed]()
			{
				return isWaitSucceed;
			});

			Assert::IsTrue(isWaitSucceed);

		}
		TEST_METHOD(TestThreadPoolTimer)
		{
			using namespace Process::Thread;
			char outputStr[] = "ThreadPoolTimer";

			ULARGE_INTEGER dueTime;
			dueTime.QuadPart = (ULONGLONG)-1;// (1 * 10 * 1000 * 1000);

			FILETIME time;
			time.dwHighDateTime = dueTime.HighPart;
			time.dwLowDateTime = dueTime.LowPart;
			auto it = std::begin(outputStr);
			std::condition_variable cv;
			DWORD period = 100;
			ThreadPoolTimer timer(&time, period, 0, [&it, end = std::end(outputStr), &cv](CallbackInstance&, ThreadPoolTimer& self)
			{
				auto& chr = *(it + 1);
				auto bak = chr;
				chr = 0;
				Logger::WriteMessage(it);
				chr = bak;
				if (++it == end)
				{
					self.WaitForCallbacks(true);
					cv.notify_all();
				}
			});

			ThreadPool::Register(timer);
			std::mutex mtx;
			std::unique_lock<std::mutex> lock(mtx);
			cv.wait_for(lock, std::chrono::milliseconds(period*(sizeof(outputStr) + 10)), [&it, end = std::end(outputStr)]()
			{
				return it == end;
			});

			Assert::IsTrue(it == std::end(outputStr));
		}

		TEST_METHOD(TestCoroutine)
		{
			using namespace Process::Fiber;
			// ���ɲ�������
			_STD random_device seed;
			_STD default_random_engine ng(seed());

			_STD vector<int> vec;
			auto inst = _STD back_inserter(vec);
			_STD generate_n(inst, 10, ng);
			// ��������


			// ���Է���ָ��
			auto c = MakeCoroutine<int*>([&vec](_STD vector<int>& v)
			{
				Assert::IsTrue(&vec == &v);
				for (auto val : v)
				{
					YieldReturn(&val);
				}
			}, _STD ref(vec));

			int i = 0;

			for (auto val : c)
			{
				Assert::IsTrue(*val == vec[i++]);
			}

			// ���Բ�return
			auto c2 = MakeCoroutine<int>([]()
			{});

			Assert::IsTrue(c2.begin() == c2.end());

			// ���Ե���������ͷ���ֵ
			auto fi = MakeCoroutine<int>([](auto num)
			{
				int preLast = 1;
				YieldReturn(preLast);
				int last = 1;
				YieldReturn(last);
				int result;
				for (size_t i = 2; i < num; i++)
				{
					result = preLast + last;
					YieldReturn(result);
					preLast = last;
					last = result;
				}
			}, (size_t)10);


			_STD ostringstream out;
			_STD ostream_iterator<int> outIt(out, " ");

			_STD copy(_STD begin(fi), _STD end(fi), outIt);
			Logger::WriteMessage(out.str().c_str());


			// �������ò���
			fi.RetsetParams(20);
			out.swap(_STD ostringstream());
			// ����һ�������ʽ�����Է�������
			// ��Э����������
			auto output = MakeCoroutine<int&>([&out]()
			{
				int input = 0;
				do
				{
					YieldReturn<int&>(input);
					out << input << " ";
				} while (true);
			});

			_STD copy(_STD begin(fi), _STD end(fi), _STD begin(output));
			Logger::WriteMessage(out.str().c_str());

			// ֱ������adapter����
			char* outputStr[]{ "Coroutine","����","�ɹ�" };
			// ���Ժϲ�������������ȥ������̫�ң��ֿ���д
			auto lambda = [](char** begin, char** end)
			{
				_STD for_each(begin, end, [](char* str)
				{
					YieldReturn(_STD string(str));
				});
			};

			for (auto& str : MakeCoroutine<_STD string>(lambda, _STD begin(outputStr), _STD end(outputStr)))
			{
				Logger::WriteMessage(str.c_str());
			}

			// ������ʱ��������
			int num = 0;
			auto refTest = MakeCoroutine<_STD reference_wrapper<volatile int>>([](int& v)
			{
				volatile int result = 123;
				YieldReturn(_STD ref(result));
				Assert::IsTrue(result == 456);
				v = 123456;
			}, _STD ref(num));
			auto bgn = refTest.begin();
			auto val = *bgn;
			Assert::IsTrue(val == 123);
			val.get() = 456;
			++bgn;
			Assert::IsTrue(num == 123456);


			// Ƕ�ײ���
			// ����һ�²���
			fi.RetsetParams(30);
			auto qtest = MakeCoroutine<_STD string>([&fi, &out, &outIt]()
			{
				out.swap(_STD ostringstream());
				_STD copy(_STD begin(fi), _STD end(fi), outIt);
				YieldReturn(out.str());
			});

			Logger::WriteMessage((*qtest.begin()).c_str());

			Logger::WriteMessage("Exit");
		}


		TEST_METHOD(TestPipe)
		{
			using namespace Process::InterProcess;

			auto pipeName = L"\\\\.\\pipe\\echo";
			NamePipeServer<false> server(pipeName);

			Assert::IsTrue(server.IsOpen());

			_STD thread t([&]()
			{
				bool isStop = false;
				while (!isStop)
				{
					char buffer[128]{ 0 };
					if (server.WaitForConnection())
					{
						while (!isStop)
						{
							auto reads = server.Read(buffer, sizeof(buffer));
							if (!reads.Result)
							{
								break;
							}
							if (strcmp(buffer, "Exit") == 0)
							{
								isStop = true;
								break;
							}
							auto writes = server.Write(buffer, reads.NumberOfBytesTransferred);
							if (!writes.Result)
							{
								break;
							}
							Assert::IsTrue(writes.NumberOfBytesTransferred == reads.NumberOfBytesTransferred);
						}
					}
				}
			});



			std::string random("1234567890");
			auto randomLen = random.length();
			_STD random_shuffle(_STD begin(random), _STD end(random));

			NamePipeClient<true> client(pipeName);

			WaitNamedPipe(pipeName, NMPWAIT_WAIT_FOREVER);
			if (!client.Connect())
			{
				Assert::Fail();
				return;
			}

			char buffer[100]{ 0 };
			auto writes = client.Write(random.c_str(), random.length() + 1);
			Assert::IsTrue(writes.Result);

			auto result = writes.AsyncResult.get();
			if (result.IoResult != S_OK)
			{
				Assert::Fail();
				return;
			}
			auto reads = client.Read(buffer, result.NumberOfBytesTransferred);
			Assert::IsTrue(reads.Result);

			result = reads.AsyncResult.get();
			if (result.IoResult != S_OK)
			{
				Assert::Fail();
				return;
			}

			Assert::AreEqual(buffer, random.c_str());
			std::string exit("Exit");
			auto ret = client.Write(exit.c_str(), exit.length() + 1);
			ret.AsyncResult.get();
			server.Dispose();
			t.join();
		}
	};
	_STD pair<bool, CONTEXT> UnitTest1::OldContext;
	bool UnitTest1::IsContextTestSucceed = true;
}