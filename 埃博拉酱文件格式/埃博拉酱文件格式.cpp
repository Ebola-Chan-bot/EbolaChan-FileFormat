#include"埃博拉酱文件格式.hpp"
#include<Windows.h>
#ifdef __cpp_lib_modules
import std;
#else
#include<system_error>
#include<queue>
#ifdef __cpp_lib_span
#include<span>
#endif
#endif
#undef min
static void 内存映射(HANDLE 文件句柄, HANDLE& 映射句柄, LPVOID& 映射指针, const LARGE_INTEGER* 文件大小)
{
	if (!(映射句柄 = CreateFileMapping(文件句柄, NULL, PAGE_READWRITE, 文件大小->HighPart, 文件大小->LowPart, nullptr)))
	{
		const DWORD 错误代码 = GetLastError();
		CloseHandle(文件句柄);
		throw std::system_error(错误代码, std::system_category());
	}
	if (!(映射指针 = MapViewOfFile(映射句柄, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0)))
	{
		const DWORD 错误代码 = GetLastError();
		CloseHandle(映射句柄);
		CloseHandle(文件句柄);
		throw std::system_error(错误代码, std::system_category());
	}
}
static inline void 确保至少空间(uint64_t 至少空间, HANDLE 文件句柄, HANDLE& 映射句柄, LPVOID& 映射指针, uint64_t& 文件大小)
{
	if (至少空间 > 文件大小) [[unlikely]]
	{
		文件大小 = 至少空间 * 2;
		UnmapViewOfFile(映射指针);
		CloseHandle(映射句柄);
		内存映射(文件句柄, 映射句柄, 映射指针, reinterpret_cast<LARGE_INTEGER*>(&文件大小));
	}
}
struct 分配块
{
	uint64_t 上一块;
	uint64_t 偏移;
	uint64_t 大小;
	uint64_t 下一块;
};
struct 文件头
{
	static constexpr uint64_t 无效值 = -1;
	uint64_t 分配块个数 = 0;
	uint64_t 第一块索引 = 无效值;
	分配块 分配块区[];
	void 设置上下块(uint64_t 上块索引, uint64_t 本块索引, uint64_t 下块索引)
	{
		if (上块索引 == 无效值)
			第一块索引 = 本块索引;
		else
			分配块区[上块索引].下一块 = 本块索引;
		if (下块索引 != 无效值)
			分配块区[下块索引].上一块 = 本块索引;
	}
	分配块 分配块区扩张(uint64_t 新块索引,uint64_t 字节数, HANDLE 文件句柄, HANDLE& 映射句柄, LPVOID& 映射指针, uint64_t& 文件大小)
	{
		const uint64_t 原本分配块个数 = 分配块个数;
		分配块个数 = (新块索引 + 1) * 2;
		分配块 新块值{ 无效值, sizeof(文件头) + sizeof(分配块) * 分配块个数, 字节数, 第一块索引 };
		// 新扩展的块区都要填充无效值，因此可以预分配
		确保至少空间(sizeof(文件头) + sizeof(分配块) * 分配块个数, 文件句柄, 映射句柄, 映射指针, 文件大小);
		// 原有数据块现在可能占用了新的分配块位置，需要向后挪
		std::queue<char> 缓冲区;
		while (新块值.下一块 != 无效值)
		{
			if (分配块区[新块值.下一块].偏移 >= 新块值.偏移) [[unlikely]]
			{
				// 写出头后有空余空间可写，先尝试清缓存
				uint64_t 字节数 = std::min(分配块区[新块值.下一块].偏移 - 新块值.偏移, 缓冲区.size());
				确保至少空间(新块值.偏移 + 字节数, 文件句柄, 映射句柄, 映射指针, 文件大小);
				char* 写出头 = reinterpret_cast<char*>(映射指针) + 新块值.偏移;
				新块值.偏移 += 字节数;
				for (; 字节数; --字节数)
				{
					*写出头++ = 缓冲区.front();
					缓冲区.pop();
				}
				if (缓冲区.empty())
					// 如果缓冲区已清空，说明后续数据块无需再挪动
					break;
			}
			// 当前块数据无法直接写出，需要缓存。上个if块中可能发生过重分配，必须重新取得块指针。
			确保至少空间(分配块区[新块值.下一块].偏移 + 分配块区[新块值.下一块].大小, 文件句柄, 映射句柄, 映射指针, 文件大小);
			分配块& 下块引用 = 分配块区[新块值.下一块];
			下块引用.偏移 = 新块值.偏移 + 缓冲区.size();
#ifdef __cpp_lib_span
			缓冲区.push_range(std::span<const char>(reinterpret_cast<char*>(映射指针) + 下块引用.偏移, 下块引用.大小));
#else
			const char* 读入头 = reinterpret_cast<char*>(映射指针) + 下块引用.偏移;
			for (uint64_t i = 0; i < 下块引用.大小; i++)
				缓冲区.push(*读入头++);
#endif
			新块值.上一块 = 新块值.下一块; // 保存上块索引，分配新块时将要用到
			新块值.下一块 = 下块引用.下一块;
		}
		// 将缓冲区剩余数据写出
		if (缓冲区.size())
		{
			确保至少空间(新块值.偏移 + 缓冲区.size(), 文件句柄, 映射句柄, 映射指针, 文件大小);
			char* 写出头 = reinterpret_cast<char*>(映射指针) + 新块值.偏移;
			新块值.偏移 += 缓冲区.size();
			while (缓冲区.size())
			{
				*写出头++ = 缓冲区.front();
				缓冲区.pop();
			}
		}
		// 确保上块和下块之间有足够的空间分配给新块
		while (新块值.下一块 != 无效值)
		{
			const 分配块& 下块引用 = 分配块区[新块值.下一块];
			if (下块引用.偏移 >= 新块值.大小 + 新块值.偏移)
				break;
			新块值.偏移 = 下块引用.偏移 + 下块引用.大小;
			新块值.上一块 = 新块值.下一块;
			新块值.下一块 = 下块引用.下一块;
		}
		// 为新块后的空块填充无效值
		std::fill(分配块区 + 原本分配块个数, 分配块区 + 分配块个数, 分配块{ 无效值, 无效值, 无效值, 无效值 });
		设置上下块(新块值.上一块, 新块索引, 新块值.下一块);
		return 新块值;
	}
	分配块 搜索空隙(uint64_t 字节数, uint64_t 块索引)
	{
		分配块 新块值{ 无效值, sizeof(文件头) + sizeof(分配块) * 分配块个数, 字节数, 第一块索引 };
		do
		{
			const 分配块& 下块引用 = 分配块区[新块值.下一块];
			if (下块引用.偏移 >= 新块值.偏移 + 新块值.大小)
				break;
			新块值.偏移 = 下块引用.偏移 + 下块引用.大小;
			新块值.上一块 = 新块值.下一块;
			新块值.下一块 = 下块引用.下一块;
		} while (新块值.下一块 != 无效值);
		设置上下块(新块值.上一块, 块索引, 新块值.下一块);
		return 新块值;
	}
	分配块& 检查句柄(埃博拉酱文件格式::内存句柄 句柄)
	{
		if (句柄 >= 分配块个数)
			throw std::out_of_range("未分配此句柄");
		分配块& 分配块引用 = 分配块区[句柄];
		if (分配块引用.偏移 == 文件头::无效值)
			throw std::out_of_range("未分配此句柄");
		return 分配块引用;
	}
};
static void 初始化(HANDLE 文件句柄, uint64_t& 文件大小, HANDLE& 映射句柄, void*& 映射指针)
{
	if (文件句柄 == INVALID_HANDLE_VALUE)
		throw std::system_error(GetLastError(), std::system_category());
	LARGE_INTEGER* const 文件大小指针 = reinterpret_cast<LARGE_INTEGER*>(&文件大小);
	GetFileSizeEx(文件句柄, 文件大小指针);
	const bool 新文件 = 文件大小 < sizeof(文件头);
	if (新文件)[[unlikely]]
		文件大小 = sizeof(文件头);
	内存映射(文件句柄, 映射句柄, 映射指针, 文件大小指针);
	if (新文件)
		*reinterpret_cast<文件头*>(映射指针) = 文件头();
}
namespace 埃博拉酱文件格式
{
	内存映射容器::内存映射容器(const char* 文件路径) :文件句柄(CreateFileA(文件路径, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL))
	{
		初始化(文件句柄, 文件大小, 映射句柄, 映射指针);
	}
	内存映射容器::内存映射容器(const wchar_t* 文件路径) :文件句柄(CreateFileW(文件路径, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL))
	{
		初始化(文件句柄, 文件大小, 映射句柄, 映射指针);
	}
	内存句柄 内存映射容器::分配(uint64_t 字节数)
	{
		uint64_t 新块索引;
		文件头* const* const 文件头指针 = reinterpret_cast<文件头**>(&映射指针);
		for (新块索引 = 0; 新块索引 < (*文件头指针)->分配块个数 && (*文件头指针)->分配块区[新块索引].偏移 != 文件头::无效值; 新块索引++);
		(*文件头指针)->分配块区[新块索引] = 新块索引 < (*文件头指针)->分配块个数 ? (*文件头指针)->搜索空隙(字节数, 新块索引) : (*文件头指针)->分配块区扩张(新块索引, 字节数, 文件句柄, 映射句柄, 映射指针, 文件大小);
		return 新块索引;
	}
	void* 内存映射容器::取指针(内存句柄 句柄)
	{
		文件头* const* const 文件头指针 = reinterpret_cast<文件头**>(&映射指针);
		const 分配块& 分配块引用 = (*文件头指针)->检查句柄(句柄);
		确保至少空间(分配块引用.偏移 + 分配块引用.大小, 文件句柄, 映射句柄, 映射指针, 文件大小);//新分配的空间可能还没有实现
		return reinterpret_cast<char*>(映射指针) + (*文件头指针)->分配块区[句柄].偏移;
	}
	void 内存映射容器::释放(内存句柄 句柄)const
	{
		文件头* const 文件头指针 = reinterpret_cast<文件头*>(映射指针);
		分配块& 旧块引用 = 文件头指针->检查句柄(句柄);
		旧块引用.偏移 = 文件头::无效值;
		const uint64_t 上块索引 = 旧块引用.上一块;
		const uint64_t 下块索引 = 旧块引用.下一块;
		if (上块索引 == 文件头::无效值)
			文件头指针->第一块索引 = 下块索引;
		else
			文件头指针->分配块区[上块索引].下一块 = 下块索引;
		if (下块索引 != 文件头::无效值)
			文件头指针->分配块区[下块索引].上一块 = 上块索引;
	}
	void 内存映射容器::重分配(内存句柄 句柄, uint64_t 字节数)
	{
		文件头* const* const 文件头指针 = reinterpret_cast<文件头**>(&映射指针);
		if (句柄 < (*文件头指针)->分配块个数)
		{
			分配块 块值 = (*文件头指针)->分配块区[句柄]; // 无法维持指针有效且所有字段都被用到，不如直接拷贝
			块值.大小 = 字节数;
			if (块值.偏移 == 文件头::无效值)
				块值 = (*文件头指针)->搜索空隙(块值.大小, 句柄);
			else if (块值.下一块 != 文件头::无效值)
			{
				if ((*文件头指针)->分配块区[块值.下一块].偏移 < 块值.偏移 + 块值.大小)
				{
					(*文件头指针)->分配块区[块值.下一块].上一块 = 块值.上一块;
					if (块值.上一块 == 文件头::无效值)
						(*文件头指针)->第一块索引 = 块值.下一块;
					else
						(*文件头指针)->分配块区[块值.上一块].下一块 = 块值.下一块;
					块值 = (*文件头指针)->搜索空隙(块值.大小, 句柄);
				}
			}
			(*文件头指针)->分配块区[句柄] = 块值;
		}
		else
			(*文件头指针)->分配块区[句柄] = (*文件头指针)->分配块区扩张(句柄, 字节数, 文件句柄, 映射句柄, 映射指针, 文件大小);
	}
	内存映射容器::~内存映射容器()
	{
		UnmapViewOfFile(映射指针);
		CloseHandle(映射句柄);
		CloseHandle(文件句柄);
	}
}