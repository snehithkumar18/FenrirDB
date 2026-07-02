#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <memory>
#include <iomanip>
#include "database.h"
#include "logger.h"
#include "wal.h"
#include "lock_manager.h"
#include "sql_parser.h"
#include "query_planner.h"
#include "json_parser.h"

namespace FenrirDB {

class DBShell {
private:
    std::unique_ptr<Database> db;
    std::unique_ptr<LogManager> log_manager;
    std::unique_ptr<LockManager> lock_manager;
    std::unique_ptr<QueryPlanner> planner;
    bool is_open = false;
    uint32_t active_tx_id = 0;
    bool in_transaction = false;
    std::string current_db_path;

    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream iss(line);
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    void print_ascii_table(const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows) {
        if (headers.empty()) return;

        // Calculate column widths
        std::vector<size_t> widths(headers.size(), 0);
        for (size_t i = 0; i < headers.size(); ++i) {
            widths[i] = headers[i].size();
        }
        for (const auto& row : rows) {
            for (size_t i = 0; i < row.size(); ++i) {
                if (i < widths.size()) {
                    widths[i] = std::max(widths[i], row[i].size());
                }
            }
        }

        // Helper to print horizontal divider line
        auto print_divider = [&]() {
            std::cout << "+";
            for (size_t w : widths) {
                std::cout << std::string(w + 2, '-') << "+";
            }
            std::cout << std::endl;
        };

        // Print header
        print_divider();
        std::cout << "|";
        for (size_t i = 0; i < headers.size(); ++i) {
            std::cout << " " << std::left << std::setw(widths[i]) << headers[i] << " |";
        }
        std::cout << std::endl;
        print_divider();

        // Print rows
        for (const auto& row : rows) {
            std::cout << "|";
            for (size_t i = 0; i < row.size(); ++i) {
                std::cout << " " << std::left << std::setw(widths[i]) << row[i] << " |";
            }
            std::cout << std::endl;
        }
        print_divider();
    }

    void execute_sql_select(const SQLSelectStatement& stmt) {
        if (!planner) return;

        // Visualizing execution plan
        std::cout << "Execution Plan: " << std::endl;
        if (stmt.where_field == "id" && stmt.where_op == QueryOp::EQ) {
            std::cout << "  [IndexScan] key=\"" << stmt.where_value.get_string() << "\"" << std::endl;
        } else {
            std::cout << "  [Filter] field=\"" << stmt.where_field << "\" -> [SeqScan]" << std::endl;
        }
        std::cout << std::endl;

        auto executor = planner->plan_query(stmt);
        if (!executor) {
            std::cout << "Error: Failed to plan query." << std::endl;
            return;
        }

        executor->init();
        Document doc;
        RecordID rid;

        std::vector<std::vector<std::string>> row_data;
        while (executor->next(doc, rid)) {
            std::vector<std::string> row;
            for (const auto& field : stmt.fields) {
                Variant val;
                if (doc.get_field(field, val)) {
                    if (val.type == VariantType::STRING) {
                        row.push_back(val.get_string());
                    } else if (val.type == VariantType::INT) {
                        row.push_back(std::to_string(val.get_int()));
                    } else if (val.type == VariantType::BOOL) {
                        row.push_back(val.get_bool() ? "true" : "false");
                    } else {
                        row.push_back("null");
                    }
                } else {
                    row.push_back("null");
                }
            }
            row_data.push_back(row);
        }
        executor->close();

        print_ascii_table(stmt.fields, row_data);
        std::cout << row_data.size() << " rows returned." << std::endl;
    }

    void execute_sql_insert(const SQLInsertStatement& stmt) {
        Document doc;
        for (size_t i = 0; i < stmt.fields.size(); ++i) {
            if (i < stmt.values.size()) {
                doc.set_field(stmt.fields[i], stmt.values[i]);
            }
        }

        if (in_transaction && log_manager) {
            log_manager->append_record(active_tx_id, LogRecordType::INSERT, 0, 0, {}, doc.serialize());
        }

        DBErrorCode res = db->insert(stmt.key, doc);
        if (res == DBErrorCode::SUCCESS) {
            std::cout << "Success: 1 record inserted under key '" << stmt.key << "'." << std::endl;
        } else {
            std::cout << "Error: " << db_error_to_string(res) << std::endl;
        }
    }

    void handle_sql(const std::string& sql) {
        SQLLexer lexer(sql);
        std::vector<Token> tokens = lexer.tokenize();
        if (tokens.empty() || tokens[0].type == TokenType::END_OF_FILE) return;

        SQLParser parser(tokens);
        auto stmt = parser.parse();
        if (!stmt) {
            std::cout << "Error: Syntax compilation failed." << std::endl;
            return;
        }

        if (stmt->type == StatementType::SELECT) {
            execute_sql_select(*static_cast<SQLSelectStatement*>(stmt.get()));
        } else if (stmt->type == StatementType::INSERT) {
            execute_sql_insert(*static_cast<SQLInsertStatement*>(stmt.get()));
        }
    }

    void handle_tx(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            std::cout << "Usage: tx <begin|commit|abort>" << std::endl;
            return;
        }
        std::string cmd = tokens[1];
        if (cmd == "begin") {
            if (in_transaction) {
                std::cout << "Error: Transaction already active." << std::endl;
                return;
            }
            in_transaction = true;
            active_tx_id++;
            if (log_manager) {
                log_manager->append_record(active_tx_id, LogRecordType::BEGIN);
            }
            std::cout << "Transaction " << active_tx_id << " started." << std::endl;
        } else if (cmd == "commit") {
            if (!in_transaction) {
                std::cout << "Error: No active transaction." << std::endl;
                return;
            }
            if (log_manager) {
                log_manager->append_record(active_tx_id, LogRecordType::COMMIT);
                log_manager->flush();
            }
            if (lock_manager) {
                lock_manager->release_all(active_tx_id);
            }
            in_transaction = false;
            std::cout << "Transaction " << active_tx_id << " committed." << std::endl;
        } else if (cmd == "abort") {
            if (!in_transaction) {
                std::cout << "Error: No active transaction." << std::endl;
                return;
            }
            if (log_manager) {
                log_manager->append_record(active_tx_id, LogRecordType::ABORT);
                log_manager->flush();
            }
            if (lock_manager) {
                lock_manager->release_all(active_tx_id);
            }
            in_transaction = false;
            std::cout << "Transaction " << active_tx_id << " aborted and rolled back." << std::endl;
        }
    }

public:
    DBShell() = default;
    ~DBShell() { close_db(); }

    void open_db(const std::string& path) {
        db = std::make_unique<Database>();
        log_manager = std::make_unique<LogManager>(path + ".wal");
        lock_manager = std::make_unique<LockManager>();
        
        DBErrorCode res = db->open(path);
        if (res == DBErrorCode::SUCCESS) {
            is_open = true;
            current_db_path = path;
            
            // Re-instantiate planner with root index page 0
            planner = std::make_unique<QueryPlanner>(*db->get_disk_manager(), *db->get_cache_manager(), *db->get_index());
            std::cout << "Opened database: " << path << std::endl;
        } else {
            std::cout << "Failed to open database: " << db_error_to_string(res) << std::endl;
        }
    }

    void close_db() {
        if (is_open) {
            db->close();
            planner.reset();
            db.reset();
            log_manager.reset();
            lock_manager.reset();
            is_open = false;
            std::cout << "Closed database." << std::endl;
        }
    }

    void run() {
        std::string line;
        std::cout << "Welcome to FenrirDB SQL Command Shell." << std::endl;
        std::cout << "Type 'help' to see command references." << std::endl;

        while (true) {
            std::cout << "fenrirdb> " << std::flush;
            if (!std::getline(std::cin, line)) {
                break;
            }

            // Remove trailing semicolon if present for SQL matching
            std::vector<std::string> tokens = tokenize(line);
            if (tokens.empty()) continue;

            std::string cmd = tokens[0];
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if (cmd == "exit" || cmd == "quit") {
                break;
            } else if (cmd == "help") {
                std::cout << "SQL Commands:" << std::endl;
                std::cout << "  SELECT <field1>, <field2> FROM <table_name> [WHERE <field> = <val>];" << std::endl;
                std::cout << "  INSERT INTO <table_name> <doc_key> (<field1>, <field2>) VALUES (<val1>, <val2>);" << std::endl;
                std::cout << "Administrative Commands:" << std::endl;
                std::cout << "  open <filepath>          - Open database file" << std::endl;
                std::cout << "  tx <begin|commit|abort>  - Manage transactions" << std::endl;
                std::cout << "  close                    - Close database" << std::endl;
            } else if (cmd == "open") {
                if (tokens.size() < 2) {
                    std::cout << "Usage: open <filepath>" << std::endl;
                } else {
                    open_db(tokens[1]);
                }
            } else if (cmd == "tx") {
                handle_tx(tokens);
            } else if (cmd == "close") {
                close_db();
            } else {
                // Treat anything else as SQL
                handle_sql(line);
            }
        }
    }
};

} // namespace FenrirDB

int main() {
    FenrirDB::DBShell shell;
    shell.run();
    return 0;
}
