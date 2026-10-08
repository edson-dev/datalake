#include "datalake_schema_entry.hpp"
#include "datalake_catalog.hpp"
#include "datalake_util.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/enum_util.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception/catalog_exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parsed_data/alter_info.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/parser/parsed_data/drop_info.hpp"

namespace duckdb {

DatalakeSchemaEntry::DatalakeSchemaEntry(Catalog &catalog, CreateSchemaInfo &info, DatalakeCatalog &datalake_catalog_p)
    : SchemaCatalogEntry(catalog, info), datalake_catalog(datalake_catalog_p) {
}

static void ThrowReadOnly(const string &schema_name, const string &action) {
	throw CatalogException("Cannot %s in datalake schema \"%s\" - datalake catalogs are read-only", action,
	                       schema_name);
}

bool DatalakeSchemaEntry::IsEntrySupported(CatalogType type) {
	return type == CatalogType::TABLE_ENTRY || type == CatalogType::VIEW_ENTRY;
}

void DatalakeSchemaEntry::Scan(ClientContext &context, CatalogType type,
                               const std::function<void(CatalogEntry &)> &callback) {
	CreateMissingEntries(context, true);
	ScanInternal(type, callback);
}

void DatalakeSchemaEntry::Scan(CatalogType type, const std::function<void(CatalogEntry &)> &callback) {
	ScanInternal(type, callback);
}

void DatalakeSchemaEntry::ScanInternal(CatalogType type, const std::function<void(CatalogEntry &)> &callback) {
	vector<reference<CatalogEntry>> snapshot;
	{
		lock_guard<mutex> guard(entry_lock);
		for (auto &entry : entries) {
			// table scans also yield views, mirroring how DuckDB collects the entries of a schema
			auto matches = type == CatalogType::TABLE_ENTRY
			                   ? IsEntrySupported(entry.second->type)
			                   : entry.second->type == type;
			if (matches) {
				snapshot.push_back(*entry.second);
			}
		}
	}
	for (auto &entry : snapshot) {
		callback(entry.get());
	}
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::LookupEntry(CatalogTransaction transaction,
                                                            const EntryLookupInfo &lookup_info) {
	if (!IsEntrySupported(lookup_info.GetCatalogType())) {
		return nullptr;
	}
	if (transaction.HasContext()) {
		CreateMissingEntries(transaction.GetContext(), false);
	}
	lock_guard<mutex> guard(entry_lock);
	auto entry = entries.find(lookup_info.GetEntryName());
	if (entry == entries.end()) {
		return nullptr;
	}
	return entry->second.get();
}

void DatalakeSchemaEntry::CreateMissingEntries(ClientContext &context, bool ignore_errors) {
	auto objects = datalake_catalog.GetObjects(name);

	vector<reference<DatalakeObject>> missing;
	{
		lock_guard<mutex> guard(entry_lock);
		for (auto &object : objects) {
			if (entries.find(object.name) == entries.end()) {
				missing.push_back(object);
			}
		}
	}

	for (auto &object_ref : missing) {
		auto &object = object_ref.get();
		unique_ptr<CatalogEntry> new_entry;
		try {
			new_entry = CreateEntry(context, object);
		} catch (...) {
			if (!ignore_errors) {
				throw;
			}
			continue;
		}
		lock_guard<mutex> guard(entry_lock);
		if (entries.find(object.name) == entries.end()) {
			entries[object.name] = std::move(new_entry);
		}
	}
}

unique_ptr<CatalogEntry> DatalakeSchemaEntry::CreateEntry(ClientContext &context, const DatalakeObject &object) {
	// every file is exposed as a view over the original object, nothing is materialized
	auto select = DatalakeUtil::BuildSelectStatement(object.reader, object.full_path);
	auto qualified_name = KeywordHelper::WriteQuoted(name, '"') + "." + KeywordHelper::WriteQuoted(object.name, '"');

	CreateViewInfo view_info(*this, object.name);
	view_info.sql = "CREATE VIEW " + qualified_name + " AS " + select;
	view_info.query = CreateViewInfo::ParseSelect(select);
	return make_uniq<ViewCatalogEntry>(catalog, *this, view_info);
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateIndex(CatalogTransaction transaction, CreateIndexInfo &info,
                                                            TableCatalogEntry &table) {
	ThrowReadOnly(name, "create an index");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateFunction(CatalogTransaction transaction,
                                                               CreateFunctionInfo &info) {
	ThrowReadOnly(name, "create a function");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateTable(CatalogTransaction transaction, BoundCreateTableInfo &info) {
	ThrowReadOnly(name, "create a table");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateView(CatalogTransaction transaction, CreateViewInfo &info) {
	if (transaction.HasContext()) {
		// COPY ... FROM DATABASE hands over the view definitions of the source catalog
		auto copied = CopyViewFromOtherCatalog(transaction.GetContext(), info);
		if (copied) {
			return copied;
		}
	}
	ThrowReadOnly(name, "create a view");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CopyViewFromOtherCatalog(ClientContext &context, CreateViewInfo &info) {
	if (info.view_name.empty()) {
		return nullptr;
	}
	string reader;
	string source_path;
	if (!DatalakeUtil::ParseViewDefinition(info.sql, reader, source_path)) {
		return nullptr;
	}
	// only a view that reads an object of another attached datalake catalog is copied
	auto source_root = DatalakeCatalog::FindSourceRoot(source_path, datalake_catalog);
	if (source_root.empty()) {
		return nullptr;
	}
	auto relative_path = DatalakeUtil::RelativeToRoot(source_root, source_path);
	if (relative_path.empty()) {
		return nullptr;
	}
	auto &fs = FileSystem::GetFileSystem(context);
	auto target_path = DatalakeUtil::JoinRootPath(fs, datalake_catalog.GetRootPath(), relative_path);
	try {
		DatalakeUtil::CopyFileRaw(fs, source_path, target_path);
	} catch (std::exception &ex) {
		ErrorData error(ex);
		error.Throw(StringUtil::Format("Could not copy the datalake object \"%s\" to \"%s\": ", source_path,
		                               target_path));
	}

	// the view of the target catalog reads its own copy of the object
	auto select = DatalakeUtil::BuildSelectStatement(reader, target_path);
	auto qualified_name = KeywordHelper::WriteQuoted(name, '"') + "." + KeywordHelper::WriteQuoted(info.view_name, '"');
	CreateViewInfo view_info(*this, info.view_name);
	view_info.sql = "CREATE VIEW " + qualified_name + " AS " + select;
	view_info.query = CreateViewInfo::ParseSelect(select);
	auto entry = make_uniq<ViewCatalogEntry>(catalog, *this, view_info);
	auto result = entry.get();
	lock_guard<mutex> guard(entry_lock);
	entries[info.view_name] = std::move(entry);
	return result;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateSequence(CatalogTransaction transaction,
                                                               CreateSequenceInfo &info) {
	ThrowReadOnly(name, "create a sequence");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateTableFunction(CatalogTransaction transaction,
                                                                    CreateTableFunctionInfo &info) {
	ThrowReadOnly(name, "create a table function");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateCopyFunction(CatalogTransaction transaction,
                                                                   CreateCopyFunctionInfo &info) {
	ThrowReadOnly(name, "create a copy function");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreatePragmaFunction(CatalogTransaction transaction,
                                                                     CreatePragmaFunctionInfo &info) {
	ThrowReadOnly(name, "create a pragma function");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateCollation(CatalogTransaction transaction,
                                                                CreateCollationInfo &info) {
	ThrowReadOnly(name, "create a collation");
	return nullptr;
}

optional_ptr<CatalogEntry> DatalakeSchemaEntry::CreateType(CatalogTransaction transaction, CreateTypeInfo &info) {
	ThrowReadOnly(name, "create a type");
	return nullptr;
}

void DatalakeSchemaEntry::DropEntry(ClientContext &context, DropInfo &info) {
	ThrowReadOnly(name, "drop " + StringUtil::Lower(CatalogTypeToString(info.type)) + " \"" + info.name + "\"");
}

void DatalakeSchemaEntry::Alter(CatalogTransaction transaction, AlterInfo &info) {
	ThrowReadOnly(name, "alter an entry");
}

} // namespace duckdb
