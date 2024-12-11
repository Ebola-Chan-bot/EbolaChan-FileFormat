#pragma once
#include<stdint.h>
namespace 埃博拉酱文件格式
{
	using 内存句柄 = uint64_t;
	//除非声明noexcept，否则本类所有方法可能抛出std::system_error
	struct 内存映射容器
	{
		//对象负责管理文件句柄，不需要用户关闭
		void* const 文件句柄;
		内存映射容器(const char* 文件路径);
		内存映射容器(const wchar_t* 文件路径);
		内存句柄 分配(uint64_t 字节数);
		//此指针在对容器进行任何操作后都可能失效
		void* 取指针(内存句柄 句柄);
		void 释放(内存句柄 句柄)const;
		void 重分配(内存句柄 句柄, uint64_t 字节数);
		~内存映射容器();
		内存映射容器(const 内存映射容器&) = delete;
		内存映射容器& operator=(const 内存映射容器&) = delete;
	protected:
		void* 映射句柄;
		void* 映射指针;
		uint64_t 文件大小;
	};
}