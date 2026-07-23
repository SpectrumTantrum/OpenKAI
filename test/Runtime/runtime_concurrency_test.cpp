#include "../../src/3D/PointCloud/_PCstream.h"
#include "../../src/Base/_Thread.h"
#include "../../src/Base/_ModuleBase.h"
#include "../../src/Module/ModuleMgr.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace
{
	using namespace kai;

	bool require(bool condition, const char *message)
	{
		if (condition)
			return true;

		std::cerr << "FAILED: " << message << std::endl;
		return false;
	}

	json streamConfig(int nP)
	{
		return {
			{"name", "runtime-test-stream"},
			{"class", "_PCstream"},
			{"nP", nP},
			{"thread", {
				{"name", "runtime-test-stream-thread"},
				{"class", "_Thread"},
				{"FPS", 100.0},
			}},
		};
	}

	json threadConfig(void)
	{
		return {
			{"name", "runtime-test-thread"},
			{"class", "_Thread"},
			{"FPS", 100.0},
		};
	}

	struct LifecycleState
	{
		std::atomic<int> touches{0};
		std::atomic<bool> destroying{false};
		std::atomic<bool> touchedDuringDestruction{false};
		std::atomic<bool> destroyed{false};
	};

	class LifecycleProbe : public _ModuleBase
	{
	public:
		explicit LifecycleProbe(LifecycleState *pState)
			: m_pState(pState)
		{
		}

		~LifecycleProbe() override
		{
			m_pState->destroying.store(true, std::memory_order_release);
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			m_pState->destroyed.store(true, std::memory_order_release);
		}

		bool start(void) override
		{
			return m_pT && m_pT->startThread(getUpdate, this);
		}

	private:
		void update(void)
		{
			while (m_pT->bAlive())
			{
				if (m_pState->destroying.load(std::memory_order_acquire))
					m_pState->touchedDuringDestruction.store(true, std::memory_order_release);
				m_pState->touches.fetch_add(1, std::memory_order_relaxed);
				m_pT->sleepT(1000);
			}
		}

		static void *getUpdate(void *pThis)
		{
			static_cast<LifecycleProbe *>(pThis)->update();
			return nullptr;
		}

		LifecycleState *m_pState;
	};

	bool testBatchPublishesImmutableSnapshot(void)
	{
		_PCstream stream;
		if (!require(stream.init(streamConfig(64)), "stream initializes"))
			return false;

		std::vector<GEOMETRY_POINT> frame(64);
		for (size_t i = 0; i < frame.size(); ++i)
		{
			frame[i].m_vP.set(1.0f, static_cast<float>(i), 3.0f);
			frame[i].m_vC.set(0.1f, 0.2f, 0.3f);
			frame[i].m_tStamp = 100;
		}

		stream.addBatch(frame);
		auto snapshot = stream.getSnapshot();
		if (!require(snapshot != nullptr, "snapshot exists") ||
			!require(snapshot->m_vP.size() == frame.size(), "snapshot contains the batch") ||
			!require(snapshot->m_sequence > 0, "snapshot has a publication sequence"))
			return false;

		stream.clear();
		return require(snapshot->m_vP.size() == frame.size(),
					   "published snapshot remains valid after the stream changes") &&
			   require(std::fabs(snapshot->m_vP[17].m_vP.y - 17.0f) < 0.001f,
					   "published snapshot owns its point data");
	}

	bool testTimestampZeroPointsRemainValid(void)
	{
		_PCstream stream;
		if (!require(stream.init(streamConfig(8)), "static replay stream initializes"))
			return false;

		GEOMETRY_POINT point;
		point.clear();
		point.m_vP.set(4.0f, 5.0f, 6.0f);
		point.m_vC.set(0.4f, 0.5f, 0.6f);
		point.m_tStamp = 0;
		stream.addBatch({point});

		auto snapshot = stream.getSnapshot();
		if (!require(snapshot != nullptr, "static replay snapshot exists"))
			return false;
		if (!require(snapshot->m_vP.size() == 1, "timestamp-zero replay point is not mistaken for an empty slot"))
			return false;
		if (!require(snapshot->m_vP.front().m_tStamp == 0, "static replay timestamp remains unchanged"))
			return false;

		_PCstream forwardedStream;
		if (!require(forwardedStream.init(streamConfig(8)), "static replay forwarding stream initializes"))
			return false;
		forwardedStream.addPCstream(&stream, 1);
		auto forwarded = forwardedStream.getSnapshot();
		return require(forwarded != nullptr, "static replay forwarding snapshot exists") && require(forwarded->m_vP.size() == 1, "positive expiry keeps timestamp-zero static replay data");
	}

	bool testConcurrentSnapshotsAreWholeBatches(void)
	{
		constexpr int nPoints = 256;
		constexpr int nFrames = 500;

		_PCstream stream;
		if (!require(stream.init(streamConfig(nPoints)), "concurrent stream initializes"))
			return false;

		std::atomic<bool> done(false);
		std::atomic<bool> whole(true);

		std::thread reader([&]()
		{
			while (!done.load())
			{
				auto snapshot = stream.getSnapshot();
				if (!snapshot || snapshot->m_vP.empty())
					continue;

				const float frameId = snapshot->m_vP.front().m_vP.x;
				for (const auto &point : snapshot->m_vP)
				{
					if (point.m_vP.x != frameId || point.m_tStamp == 0)
					{
						whole.store(false);
						return;
					}
				}
				std::vector<GEOMETRY_POINT> pointRing;
				stream.copyRingTo(&pointRing);
				const float ringFrameId = pointRing.front().m_vP.x;
				for (const auto &point : pointRing)
				{
					if (point.m_vP.x != ringFrameId || point.m_tStamp == 0)
					{
						whole.store(false);
						return;
					}
				}
			}
		});

		for (int iFrame = 1; iFrame <= nFrames; ++iFrame)
		{
			std::vector<GEOMETRY_POINT> frame(nPoints);
			for (auto &point : frame)
			{
				point.m_vP.set(static_cast<float>(iFrame), 0.0f, 0.0f);
				point.m_vC.set(1.0f);
				point.m_tStamp = static_cast<uint64_t>(iFrame);
			}
			stream.addBatch(frame);
		}

		done.store(true);
		reader.join();
		return require(whole.load(), "readers never observe a partially published batch");
	}

	bool testManagerJoinsBeforeDestruction(const char *configPath)
	{
		LifecycleState state;
		{
			ModuleMgr modules;
			if (!require(modules.parseJsonFile(configPath), "cleanup fixture parses"))
				return false;

			LifecycleProbe *pProbe = new LifecycleProbe(&state);
			if (!require(modules.addModule(pProbe, "lifecycleProbe"), "lifecycle probe is added"))
			{
				delete pProbe;
				return false;
			}

			if (!require(modules.initAll(), "lifecycle probe initializes") ||
				!require(modules.linkAll(), "lifecycle probe links") ||
				!require(modules.startAll(), "lifecycle probe starts"))
				return false;

			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			while (state.touches.load(std::memory_order_acquire) == 0 &&
				   std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			if (!require(state.touches.load(std::memory_order_acquire) > 0,
						 "lifecycle probe worker runs"))
				return false;

			modules.cleanAll();
		}

		return require(state.destroyed.load(std::memory_order_acquire),
					   "manager destroys the lifecycle probe") &&
			require(!state.touchedDuringDestruction.load(std::memory_order_acquire),
					"manager joins workers before derived destruction begins");
	}

	void *sleepingWorker(void *arg)
	{
		auto *pThread = static_cast<_Thread *>(arg);
		while (pThread->bAlive())
			pThread->sleepT(30 * SEC_2_USEC);

		return nullptr;
	}

	bool testStopWakesAndJoinsWorker(void)
	{
		_Thread thread;
		if (!require(thread.init(threadConfig()), "thread initializes") ||
			!require(thread.startThread(sleepingWorker, &thread), "thread starts"))
			return false;

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		const auto started = std::chrono::steady_clock::now();
		thread.stop();
		const bool joined = thread.join();
		const auto elapsed = std::chrono::steady_clock::now() - started;

		return require(joined, "thread joins") &&
			   require(elapsed < std::chrono::seconds(1), "stop wakes a sleeping worker") &&
			   require(thread.bStop(), "joined thread reports stopped");
	}
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::cerr << "usage: runtime_concurrency_test <cleanup-fixture>" << std::endl;
		return 2;
	}

	if (!testBatchPublishesImmutableSnapshot())
		return 1;
	if (!testTimestampZeroPointsRemainValid())
		return 1;
	if (!testConcurrentSnapshotsAreWholeBatches())
		return 1;
	if (!testStopWakesAndJoinsWorker())
		return 1;
	if (!testManagerJoinsBeforeDestruction(argv[1]))
		return 1;

	std::cout << "OpenKAI runtime concurrency tests passed" << std::endl;
	return 0;
}
