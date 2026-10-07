#include "datalake_storage.hpp"
#include "datalake_catalog.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/settings.hpp"
#include "duckdb/parser/parsed_data/attach_info.hpp"
#include "duckdb/transaction/transaction_manager.hpp"

namespace duckdb {

//! Datalake catalogs do not keep any transaction state
class DatalakeTransaction : public Transaction {
public:
	DatalakeTransaction(TransactionManager &manager, ClientContext &context) : Transaction(manager, context) {
	}
};

class DatalakeTransactionManager : public TransactionManager {
public:
	explicit DatalakeTransactionManager(AttachedDatabase &db_p) : TransactionManager(db_p) {
	}

	Transaction &StartTransaction(ClientContext &context) override {
		auto transaction = make_uniq<DatalakeTransaction>(*this, context);
		auto &result = *transaction;
		lock_guard<mutex> guard(transaction_lock);
		transactions[result] = std::move(transaction);
		return result;
	}

	ErrorData CommitTransaction(ClientContext &context, Transaction &transaction) override {
		DestroyTransaction(transaction);
		return ErrorData();
	}

	void RollbackTransaction(Transaction &transaction) override {
		DestroyTransaction(transaction);
	}

	void Checkpoint(ClientContext &context, bool force) override {
	}

private:
	void DestroyTransaction(Transaction &transaction) {
		lock_guard<mutex> guard(transaction_lock);
		transactions.erase(transaction);
	}

	mutex transaction_lock;
	reference_map_t<Transaction, unique_ptr<DatalakeTransaction>> transactions;
};

static unique_ptr<Catalog> DatalakeAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                          AttachedDatabase &db, const string &name, AttachInfo &info,
                                          AttachOptions &options) {
	if (!Settings::Get<EnableExternalAccessSetting>(context)) {
		throw PermissionException("Attaching datalake catalogs is disabled through configuration");
	}
	for (auto &entry : options.options) {
		throw BinderException("Unrecognized option for datalake attach: %s", entry.first);
	}
	return make_uniq<DatalakeCatalog>(db, info.path);
}

static unique_ptr<TransactionManager> DatalakeCreateTransactionManager(optional_ptr<StorageExtensionInfo> storage_info,
                                                                      AttachedDatabase &db, Catalog &catalog) {
	return make_uniq<DatalakeTransactionManager>(db);
}

DatalakeStorageExtension::DatalakeStorageExtension() {
	attach = DatalakeAttach;
	create_transaction_manager = DatalakeCreateTransactionManager;
}

} // namespace duckdb
