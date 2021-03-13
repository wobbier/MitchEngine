#pragma once
#include <atomic>
#include <new>
#include <array>

class Job
{
public:
	Job() = default;
	Job(void(*jobFunction)(Job&), Job* parent = nullptr);

	void Run();
	bool IsFinished() const;


private:
	void(*JobFuntion)(Job&);
	void Finish();

	Job* ParentJob = nullptr;
	std::atomic_size_t UnfinishedJobs;

	static constexpr std::size_t kPayloadSize = sizeof(JobFuntion) + sizeof(ParentJob) + sizeof(UnfinishedJobs);
    static constexpr std::size_t kMaxPaddingSize = 64;//std::hardware_destructive_interference_size;
	static constexpr std::size_t kPaddingSize = kMaxPaddingSize - kPayloadSize;

	std::array<unsigned char, kPaddingSize> Padding;
public:
	void OnFinished(void(*jobCallback)(Job&));

	template <typename T, typename... Args>
	void ConstructData(Args&&... args);

	template<typename Data>
	std::enable_if_t<std::is_pod<Data>::value && (sizeof(Data) <= kPaddingSize)> SetData(const Data& InData)
	{
		std::memcpy(Padding.data(), &InData, sizeof(Data));
	}

	template<typename Data>
	const Data& GetData() const
	{
		return *reinterpret_cast<const Data*>(Padding.data());
	}

	template<typename Data>
	Job(void(*jobFunction)(Job&), const Data& InData, Job* InParent = nullptr)
		: Job{ jobFunction, InParent }
	{
		SetData(InData);
	}
};

template <typename T, typename... Args>
void Job::ConstructData(Args&&... args)
{
	new(Padding.data()) T{ std::forward<Args>(args)... };
}
