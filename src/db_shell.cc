#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include "database.h"
#include "logger.h"
#include "wal.h"
#include "lock_manager.h"

namespace FenrirDB {

class DBShell {
private:
    std::unique_ptr<Database> db;
    std::unique_ptr<LogManager> log_manager;
    std::unique_ptr<LockManager> lock_manager;
    bool is_open = false;
    uint32_t active_tx_id = 0;
    bool in_transaction = false;

    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream iss(line);
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    void handle_insert(const std::vector<std::string>& tokens) {
        if (!is_open) {
            std::cout << "Error: No database open." << std::endl;
            return;
        }
        if (tokens.size() < 4) {
            std::cout << "Usage: insert <key> <field> <value>" << std::endl;
            return;
        }

        std::string key = tokens[1];
        std::string field = tokens[2];
        std::string val_str = tokens[3];

        Variant value;
        try {
            size_t idx;
            int int_val = std::stoi(val_str, &idx);
            if (idx == val_str.size()) {
                value = Variant(int_val);
            } else {
                value = Variant(val_str);
            }
        } catch (...) {
            if (val_str == "true") value = Variant(true);
            else if (val_str == "false") value = Variant(false);
            else value = Variant(val_str);
        }

        Document doc;
        doc.set_field(field, value);

        // Append log if inside transaction
        if (in_transaction && log_manager) {
            log_manager->append_record(active_tx_id, LogRecordType::INSERT, 0, 0, {}, doc.serialize());
        }

        DBErrorCode res = db->insert(key, doc);
        if (res == DBErrorCode::SUCCESS) {
            std::cout << "Success: Document inserted." << std::endl;
        } else {
            std::cout << "Error: " << db_error_to_string(res) << std::endl;
        }
    }

    void handle_get(const std::vector<std::string>& tokens) {
        if (!is_open) {
            std::cout << "Error: No database open." << std::endl;
            return;
        }
        if (tokens.size() < 2) {
            std::cout << "Usage: get <key>" << std::endl;
            return;
        }

        std::string key = tokens[1];
        Document doc;
        DBErrorCode res = db->get(key, doc);
        if (res == DBErrorCode::SUCCESS) {
            std::cout << "Found Document for key '" << key << "':" << std::endl;
            // Access fields
            Variant val;
            if (doc.get_field("name", val)) {
                std::cout << "  name: " << val.get_string() << std::endl;
            }
            if (doc.get_field("age", val)) {
                std::cout << "  age: " << val.get_int() << std::endl;
            }
            if (doc.get_field("active", val)) {
                std::cout << "  active: " << (val.get_bool() ? "true" : "false") << std::endl;
            }
        } else {
            std::cout << "Error: " << db_error_to_string(res) << std::endl;
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
            std::cout << "Transaction " << active_tx_id << " aborted and locks released." << std::endl;
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
            std::cout << "Opened database: " << path << std::endl;
        } else {
            std::cout << "Failed to open database: " << db_error_to_string(res) << std::endl;
        }
    }

    void close_db() {
        if (is_open) {
            db->close();
            db.reset();
            log_manager.reset();
            lock_manager.reset();
            is_open = false;
            std::cout << "Closed database." << std::endl;
        }
    }

    void run() {
        std::string line;
        std::cout << "Welcome to FenrirDB Interactive Shell." << std::endl;
        std::cout << "Type 'help' to see available commands." << std::endl;

        while (true) {
            std::cout << "fenrirdb> " << std::flush;
            if (!std::getline(std::cin, line)) {
                break;
            }

            std::vector<std::string> tokens = tokenize(line);
            if (tokens.empty()) continue;

            std::string cmd = tokens[0];
            if (cmd == "exit" || cmd == "quit") {
                break;
            } else if (cmd == "help") {
                std::cout << "Commands:" << std::endl;
                std::cout << "  open <filepath>          - Open database file" << std::endl;
                std::cout << "  insert <key> <f> <v>     - Insert document record" << std::endl;
                std::cout << "  get <key>                - Fetch document by key" << std::endl;
                std::cout << "  tx <begin|commit|abort>  - Manage transaction states" << std::endl;
                std::cout << "  close                    - Close active database" << std::endl;
                std::cout << "  exit                     - Exit shell" << std::endl;
            } else if (cmd == "open") {
                if (tokens.size() < 2) {
                    std::cout << "Usage: open <filepath>" << std::endl;
                } else {
                    open_db(tokens[1]);
                }
            } else if (cmd == "insert") {
                handle_insert(tokens);
            } else if (cmd == "get") {
                handle_get(tokens);
            } else if (cmd == "tx") {
                handle_tx(tokens);
            } else if (cmd == "close") {
                close_db();
            } else {
                std::cout << "Unknown command: '" << cmd << "'. Type 'help' for options." << std::endl;
            }
        }
    }
};

} // namespace FenrirDB

int main_shell() {
    FenrirDB::DBShell shell;
    shell.run();
    return 0;
}
