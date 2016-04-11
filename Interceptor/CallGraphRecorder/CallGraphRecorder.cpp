#include "CallGraphRecorder.h"
#include "CallGraphHTML.h"
#include "InterceptorUtils.h"
#include "Interceptor_Internal.h"
#include <fstream>
#include<iostream>
#include <set>
#include <stack>
#include <string>
#include<sstream>
#include <Windows.h>
using namespace Interceptor;


Interceptor::CallGraphRecorder::CallGraphRecorder() {
}

Interceptor::CallGraphRecorder::~CallGraphRecorder() {
	m_lock.lock();
	m_lazy_record_lock.lock();
	m_lazy_record_lock.unlock();
	m_lock.unlock();
}

void CallGraphRecorder::record(const std::string &_function_name,
	const std::string &_function_file_path,
	CALL_STATUS _call_status) {

	auto thread_id = std::this_thread::get_id();
	record(_function_name, _function_file_path, thread_id, _call_status);
}

void CallGraphRecorder::record(const std::string &_function_name,
	const std::string &_function_file_path,
	const std::thread::id &_thread_id,
	CALL_STATUS _call_status) {

	m_lock.lock();
	auto fn_id = m_string_indexer.record(_function_name);
	auto fn_file_id = m_string_indexer.record(_function_file_path);
	m_call_stack_records[_thread_id].emplace_back(CallStackRecord(fn_id, fn_file_id, _call_status));
	m_lock.unlock();
}

void CallGraphRecorder::record_lazy(void *_pa,
									CALL_STATUS _call_status ) {

	auto thread_id = std::this_thread::get_id();
	m_lazy_record_lock.lock();
	m_lazy_records[thread_id].push_back(CallStackLazyRecord(_pa, _call_status));
	m_lazy_record_lock.unlock();
}


void CallGraphRecorder::populate_lazy_data() {
	for (auto &thread : m_lazy_records) {
		for (auto &thread_data : thread.second) {
			auto pa = thread_data.get_pa();
			auto fn_name = Interceptor_Internal::get().get_function_name_internal(pa);
			auto fn_file_name = Interceptor_Internal::get().get_function_file_internal(pa);
			record(fn_name, fn_file_name, thread.first, thread_data.get_call_status());
		}
	}
}

void Interceptor::CallGraphRecorder::print() {

}

void get_call_chart(const std::vector<CallStackRecord> &_call_stack,
	CALL_GRAPH &_call_graph,
	const InterceptorMode _mode) {
	if (_call_stack.empty())
		return;

	std::stack<string_id> call_stack;
	
	for (auto &call_record : _call_stack) {
		if (call_record.get_call_status() == CALL_STATUS::CALL_IN) {
			string_id caller = 0;
			bool caller_found = false;
			if (!call_stack.empty()) {
				caller = call_stack.top();
				caller_found = true;
			}

			Interceptor::string_id callee;
			if (_mode == Interceptor::InterceptorMode::CALL_DIAGRAM_FILES) {
				callee = call_record.get_file_data();
			}
			else if (_mode == Interceptor::InterceptorMode::CALL_DIAGRAM_FUNCTION) {
				callee = call_record.get_function_name();
			}
			else {
				std::cout << "ERROR	:	unknown InterceptorMode!" << std::endl;
			}

			call_stack.push(callee);
			if (caller_found) {
				auto iter = _call_graph.find(caller);
				if (iter == _call_graph.end()) {
					_call_graph[caller][callee] = 1;
				}
				else {
					auto callee_iter = iter->second.find(callee);
					if (callee_iter == iter->second.end()) {
						iter->second[callee] = 1;
					}
					else {
						++(callee_iter->second);
					}
				}
			}
			
		}
		else {
			if(!call_stack.empty())
				call_stack.pop();
		}
	}
}

void AddCallData(std::string& str, const std::string& oldStr, const std::string& newStr) {
	size_t pos = 0;
	while ((pos = str.find(oldStr, pos)) != std::string::npos) {
		str.replace(pos, oldStr.length(), newStr);
		pos += newStr.length();
	}

}

void Interceptor::CallGraphRecorder::create_call_chart(InterceptorMode _mode) {

	{
		m_lazy_record_lock.lock();
		if (!m_lazy_records.empty()) {
			populate_lazy_data();
		}
		m_lazy_record_lock.unlock();
	}
	
	m_lock.lock();
	
	CALL_GRAPH call_graph;
	for (auto &call_stacks : m_call_stack_records) {
		get_call_chart(call_stacks.second, call_graph, _mode);
	}
	std::string html_data = get_header(call_graph)+get_connectivity(call_graph);
	auto str = Interceptor::html;
	html_data = R"(")" + html_data + R"(")";
	AddCallData(str, html_data_key, html_data);
	std::ofstream ofs;
	auto directory = Utils::get_current_directory();
	ofs.open(directory+"\\call_graph_force_view.html");
	std::cout << "Saving the file at : " << (directory + "\\call_graph_force_view.html") << std::endl;
	ofs << str;
	m_lock.unlock();
}

std::string CallGraphRecorder::get_header(const CALL_GRAPH &_call_graph)const {
	std::set<string_id> function_names;
	for (auto &call : _call_graph) {
		function_names.insert(call.first);
		for (auto &call_details : call.second) {
			function_names.insert(call_details.first);
		}
	}
	std::stringstream str;
	str << function_names.size() << "|";
	for (auto &function_name : function_names) {
		str << m_string_indexer[function_name] << "|";
	}
	for (auto &function_name : function_names) {
		auto fn = m_string_indexer[function_name];
		str << fn << ";" << fn << ";"<< 0.01 << "|";
	}
	
	return str.str();
}

std::string CallGraphRecorder::get_connectivity(const CALL_GRAPH &_call_graph)const {
	std::stringstream str;
	for (auto &call : _call_graph) {
		auto fn1 = m_string_indexer[call.first];
		for (auto &call_details : call.second) {
			auto fn2 = m_string_indexer[call_details.first];
			str << fn1 << ";";
			str << fn2 << ";" <<0.01<< "|";
		}
	}
	return str.str();
}

