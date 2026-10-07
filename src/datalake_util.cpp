#include "datalake_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"

#include <functional>
#include <unordered_set>

namespace duckdb {

constexpr const char *DatalakeUtil::DEFAULT_SCHEMA_NAME;

static string EscapeValue(const string &value) {
	string result;
	result.reserve(value.size() + 2);
	result += '\'';
	for (auto ch : value) {
		if (ch == '\'') {
			result += '\'';
		}
		result += ch;
	}
	result += '\'';
	return result;
}

string DatalakeUtil::JoinRootPath(FileSystem &fs, const string &root, const string &relative_path) {
	if (root.empty()) {
		return relative_path;
	}
	auto last_char = root.back();
	if (last_char == '/' || last_char == '\\') {
		return root + relative_path;
	}
	if (root.find("://") != string::npos) {
		return root + "/" + relative_path;
	}
	return fs.JoinPath(root, relative_path);
}

vector<string> DatalakeUtil::ListRelativePaths(ClientContext &context, const string &directory) {
	auto &fs = FileSystem::GetFileSystem(context);
	vector<string> result;

	std::function<void(const string &, const string &)> visit = [&](const string &dir, const string &prefix) {
		vector<std::pair<string, bool>> children;
		fs.ListFiles(dir, [&](const string &child, bool is_dir) { children.emplace_back(child, is_dir); }, nullptr);
		for (auto &child : children) {
			auto child_relative = prefix.empty() ? child.first : prefix + "/" + child.first;
			if (child.second) {
				visit(fs.JoinPath(dir, child.first), child_relative);
			} else {
				result.push_back(std::move(child_relative));
			}
		}
	};
	visit(directory, string());
	return result;
}

vector<DatalakeObject> DatalakeUtil::MapObjects(FileSystem &fs, const string &root,
                                                const vector<string> &relative_paths) {
	vector<DatalakeObject> result;
	std::unordered_set<string> seen;

	for (auto &relative_path : relative_paths) {
		auto last_slash = relative_path.find_last_of('/');
		auto base_name = last_slash == string::npos ? relative_path : relative_path.substr(last_slash + 1);
		auto last_dot = base_name.find_last_of('.');
		if (last_dot == string::npos || last_dot == 0) {
			continue;
		}
		auto reader = ReaderForExtension(StringUtil::Lower(base_name.substr(last_dot + 1)));
		if (!reader) {
			continue;
		}

		auto components = StringUtil::Split(relative_path, '/');
		if (components.size() > 1 && components[0] == "public") {
			components.erase(components.begin());
		}
		if (components.empty()) {
			continue;
		}

		DatalakeObject object;
		// every discovered file becomes a view, no entry is ever materialized as a table
		object.entry_type = CatalogType::VIEW_ENTRY;
		object.reader = reader;
		object.full_path = JoinRootPath(fs, root, relative_path);
		// keep the file extension so it is always obvious which file backs the entry
		object.name = base_name;
		if (components.size() == 1) {
			object.schema = DEFAULT_SCHEMA_NAME;
		} else {
			object.schema = components[0];
			string folders;
			for (idx_t i = 1; i + 1 < components.size(); i++) {
				if (!folders.empty()) {
					folders += "_";
				}
				folders += components[i];
			}
			if (!folders.empty()) {
				object.name = folders + "_" + object.name;
			}
		}

		auto key = StringUtil::Lower(object.schema) + "\x01" + StringUtil::Lower(object.name);
		if (!seen.insert(key).second) {
			continue;
		}
		result.push_back(std::move(object));
	}
	return result;
}

const char *DatalakeUtil::ReaderForExtension(const string &extension) {
	if (extension == "parquet") {
		return "read_parquet";
	}
	if (extension == "csv") {
		return "read_csv";
	}
	if (extension == "json") {
		return "read_json";
	}
	return nullptr;
}

string DatalakeUtil::BuildSelectStatement(const string &reader, const string &full_path) {
	return "SELECT * FROM " + reader + "(" + EscapeValue(full_path) + ")";
}

static LogicalOperator *FindScanOperator(LogicalOperator &op) {
	if (op.type == LogicalOperatorType::LOGICAL_GET) {
		return &op;
	}
	for (auto &child : op.children) {
		if (!child) {
			continue;
		}
		auto result = FindScanOperator(*child);
		if (result) {
			return result;
		}
	}
	return nullptr;
}

void DatalakeUtil::BindReader(ClientContext &context, const string &reader, const string &full_path,
                              TableFunction &function, unique_ptr<FunctionData> &bind_data, vector<LogicalType> &types,
                              vector<string> &names) {
	auto statement = BuildSelectStatement(reader, full_path);

	Parser parser;
	parser.ParseQuery(statement);
	if (parser.statements.size() != 1) {
		throw InternalException("Failed to parse reader for datalake object \"%s\"", full_path);
	}

	auto binder = Binder::CreateBinder(context);
	auto bound = binder->Bind(*parser.statements[0]);
	auto plan = std::move(bound.plan);
	auto get = plan ? FindScanOperator(*plan) : nullptr;
	if (!get) {
		throw InternalException("Unexpected bind result for datalake object \"%s\"", full_path);
	}

	function = std::move(get->Cast<LogicalGet>().function);
	bind_data = std::move(get->Cast<LogicalGet>().bind_data);
	types = std::move(get->Cast<LogicalGet>().returned_types);
	names = std::move(get->Cast<LogicalGet>().names);
}

void DatalakeUtil::GetColumns(ClientContext &context, const string &reader, const string &full_path,
                              vector<LogicalType> &types, vector<string> &names) {
	TableFunction function;
	unique_ptr<FunctionData> bind_data;
	BindReader(context, reader, full_path, function, bind_data, types, names);
}

} // namespace duckdb
