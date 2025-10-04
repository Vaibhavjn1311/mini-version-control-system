#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <openssl/sha.h>
#include <zlib.h>
using namespace std;
namespace fs = filesystem;

class MiniVCS {
private:
    const string GIT_DIR = ".mygit";
    const string OBJECTS_DIR = GIT_DIR + "/objects";
    const string REFS_DIR = GIT_DIR + "/refs";
    const string HEAD_FILE = GIT_DIR + "/HEAD";
    const string INDEX_FILE = GIT_DIR + "/index";

    struct IndexEntry {
        string path;
        string sha1;
        time_t timestamp;
    };

    struct Commit {
        string tree_sha;
        string parent_sha;
        string message;
        time_t timestamp;
        string author;
    };

    vector<IndexEntry> index;

    // Helper functions
    string calculateSHA1(const string& content) {
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(content.c_str()), content.length(), hash);
        
        stringstream ss;
        for(int i = 0; i < SHA_DIGEST_LENGTH; i++) {
            ss << hex << setw(2) << setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

    string compressData(const string& data) {
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        
        if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
            throw runtime_error("Compression initialization failed");
        }

        vector<unsigned char> compressed;
        const size_t CHUNK = 16384;
        vector<unsigned char> out(CHUNK);

        strm.avail_in = data.size();
        strm.next_in = (Bytef*)data.data();

        do {
            strm.avail_out = CHUNK;
            strm.next_out = out.data();

            deflate(&strm, Z_FINISH);

            compressed.insert(compressed.end(), out.data(), out.data() + CHUNK - strm.avail_out);
        } while (strm.avail_out == 0);

        deflateEnd(&strm);

        return string(compressed.begin(), compressed.end());
    }

    string decompressData(const string& compressed) {
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = 0;
        strm.next_in = Z_NULL;

        if (inflateInit(&strm) != Z_OK) {
            throw runtime_error("Decompression initialization failed");
        }

        vector<unsigned char> decompressed;
        const size_t CHUNK = 16384;
        vector<unsigned char> out(CHUNK);

        strm.avail_in = compressed.size();
        strm.next_in = (Bytef*)compressed.data();

        do {
            strm.avail_out = CHUNK;
            strm.next_out = out.data();

            inflate(&strm, Z_NO_FLUSH);

            decompressed.insert(decompressed.end(), out.data(), out.data() + CHUNK - strm.avail_out);
        } while (strm.avail_out == 0);

        inflateEnd(&strm);

        return string(decompressed.begin(), decompressed.end());
    }

    void writeObject(const string& type, const string& content) {
        string header = type + " " + to_string(content.length()) + "\0";
        string store = header + content;
        string sha = calculateSHA1(store);
        
        string compressed = compressData(store);
        
        string dir = OBJECTS_DIR + "/" + sha.substr(0, 2);
        string path = dir + "/" + sha.substr(2);
        
        fs::create_directories(dir);
        
        ofstream file(path, ios::binary);
        file.write(compressed.data(), compressed.length());
    }

    pair<string, string> readObject(const string& sha) {
        string path = OBJECTS_DIR + "/" + sha.substr(0, 2) + "/" + sha.substr(2);
        
        ifstream file(path, ios::binary);
        if (!file) {
            throw runtime_error("Object not found: " + sha);
        }
        
        stringstream buffer;
        buffer << file.rdbuf();
        string compressed = buffer.str();
        
        string decompressed = decompressData(compressed);
        
        size_t null_pos = decompressed.find('\0');
        string header = decompressed.substr(0, null_pos);
        string content = decompressed.substr(null_pos + 1);

        string len = to_string(content.size()); 
        
        size_t space_pos = header.find(' ');
        string type = header.substr(0, space_pos);
        content = content.substr(space_pos+1+len.size()); 
        
        return {type, content};
    }

public:
    void init() {
        if (fs::exists(GIT_DIR)) {
            throw runtime_error("Repository already exists");
        }

        fs::create_directories(GIT_DIR + "/objects");
        fs::create_directories(GIT_DIR + "/refs/heads");
        
        ofstream head(HEAD_FILE);
        head << "ref: refs/heads/master";
    }

    string hashObject(const string& path, bool write = false) {
        ifstream file(path, ios::binary);
        if (!file) {
            throw runtime_error("File not found: " + path);
        }
        
        stringstream buffer;
        buffer << file.rdbuf();
        string content = buffer.str();
        
        string header = "blob " + to_string(content.length()) + "\0";
        string store = header + content;
        
        string sha = calculateSHA1(store);
        
        if (write) {
            writeObject("blob", content);
        }
        
        return sha;
    }

    void catFile(const string& sha, char flag) {
        auto [type, content] = readObject(sha);
        
        switch (flag) {
            case 'p':
                cout << content;
                break;
            case 't':
                cout << type;
                break;
            case 's':
                cout << content.length();
                break;
            default:
                throw runtime_error("Invalid flag");
        }
    }

    string writeTree() {
        stringstream tree_content;
        
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.path().filename().string() == GIT_DIR) continue;
            
            string path = entry.path().string();
            string name = entry.path().filename().string();
            
            if (fs::is_directory(entry)) {
                // Recursively handle directories
                fs::current_path(path);
                string subtree_sha = writeTree();
                fs::current_path("..");
                tree_content << "40000 " << name << " " << subtree_sha << "\n";
            } else {
                string sha = hashObject(path, true);
                tree_content << "100644 " << name << " " << sha << "\n";
            }
        }
        
        string content = tree_content.str();
        writeObject("tree", content);
        return calculateSHA1("tree " + to_string(content.length()) + "\0" + content);
    }

    void lsTree(const string& sha, bool nameOnly = false) {
        // Read and validate the tree object
        auto [type, content] = readObject(sha);
        if (type != "tree") {
            throw runtime_error("Not a tree object");
        }

        istringstream ss(content);
        string mode, type1, hash, name;
        
        // Tree entries are stored as: <mode> <type> <hash>\t<name>
        // We need to parse each component carefully
        string line;
        while (getline(ss, line)) {
            istringstream entry(line);

            // cout<<line<<endl;
            
            // Extract components
            entry >> mode >> name >> hash;
            
            // Skip the tab character
            // char tab;
            // entry.get(tab);
            
            // Get the rest as name (might contain spaces)
            // getline(entry, name);
            
            if (nameOnly) {
                // Only output the name when --name-only flag is set
                cout << name << "\n";
            } else {
                // Format the output similar to git ls-tree
                // Convert mode to proper format (ensure 6 digits)
                string formattedMode = string(6 - mode.length(), '0') + mode;
                
                // Determine if it's a directory (tree) or file (blob)
                string entryType = (formattedMode == "040000") ? "tree" : "blob";
                
                // Output in format: <mode> <type> <hash>\t<name>
                cout << formattedMode << " " << entryType << " " << hash << "\t" << name << "\n";
            }
        }
    }

    void add(const vector<string>& paths) {
        for (const auto& path : paths) {
            if (path == ".") {
                // Add all files in current directory
                for (const auto& entry : fs::directory_iterator(".")) {
                    if (entry.path().filename().string() != GIT_DIR) {
                        string entry_path = entry.path().string();
                        string sha = hashObject(entry_path, true);
                        IndexEntry index_entry = {
                            entry_path,
                            sha,
                            static_cast<time_t>(fs::last_write_time(entry_path).time_since_epoch().count())
                        };
                        index.push_back(index_entry);
                    }
                }
            } else {
                if (!fs::exists(path)) {
                    throw runtime_error("File not found: " + path);
                }
                string sha = hashObject(path, true);
                IndexEntry entry = {
                    path,
                    sha,
                    static_cast<time_t>(fs::last_write_time(path).time_since_epoch().count())
                };
                index.push_back(entry);
            }
        }
        
        // Write index to file
        ofstream index_file(INDEX_FILE);
        for (const auto& entry : index) {
            index_file << entry.path << " " << entry.sha1 << " " << entry.timestamp << "\n";
        }
    }

    string commit(const string& message) {
        string tree_sha = writeTree();
        
        // Read HEAD to get parent commit
        string parent_sha;
        ifstream head(HEAD_FILE);
        string head_content;
        getline(head, head_content);
        
        if (head_content.substr(0, 5) == "ref: ") {
            string ref_path = GIT_DIR + "/" + head_content.substr(5);
            if (fs::exists(ref_path)) {
                ifstream ref_file(ref_path);
                getline(ref_file, parent_sha);
            }
        }
        
        // Get current timestamp
        time_t current_time = time(nullptr);
        
        // Create commit object with timestamp
        stringstream commit_content;
        commit_content << "tree " << tree_sha << "\n";
        if (!parent_sha.empty()) {
            commit_content << "parent " << parent_sha << "\n";
        }
        commit_content << "author User <user@example.com> " << current_time << "\n";
        commit_content << "committer User <user@example.com> " << current_time << "\n";
        commit_content << "timestamp " << current_time << "\n\n";  // Add explicit timestamp
        commit_content << message;
        
        string content = commit_content.str();
        writeObject("commit", content);
        string commit_sha = calculateSHA1("commit " + to_string(content.length()) + "\0" + content);
        
        // Update HEAD
        string ref_path = GIT_DIR + "/refs/heads/master";
        fs::create_directories(fs::path(ref_path).parent_path());  // Ensure directory exists
        ofstream ref_file(ref_path);
        ref_file << commit_sha;
        
        // Set proper permissions for the working directory
        fs::permissions(".", fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::add);
        
        return commit_sha;
    }

    void log() {
        // Read current HEAD
        string current_sha;
        ifstream head(HEAD_FILE);
        string head_content;
        getline(head, head_content);
        
        if (head_content.substr(0, 5) == "ref: ") {
            string ref_path = GIT_DIR + "/" + head_content.substr(5);
            if (fs::exists(ref_path)) {
                ifstream ref_file(ref_path);
                getline(ref_file, current_sha);
            }
        }
        
        // Traverse commit history
        while (!current_sha.empty()) {
            auto [type, content] = readObject(current_sha);
            if (type != "commit") break;
            
            cout << "\033[33mcommit " << current_sha << "\033[0m\n";  // Yellow color for commit hash
            
            stringstream ss(content);
            string line;
            string parent_sha;
            time_t commit_time = 0;
            
            bool message_next = false;
            while (getline(ss, line)) {
                if (line.substr(0, 7) == "parent ") {
                    parent_sha = line.substr(7);
                }
                else if (line.substr(0, 10) == "timestamp ") {
                    commit_time = stoll(line.substr(10));
                    // Convert timestamp to human-readable format
                    char time_buf[100];
                    struct tm* timeinfo = localtime(&commit_time);
                    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S %z", timeinfo);
                    cout << "Date:   " << time_buf << "\n";
                }
                else if (line.empty()) {
                    message_next = true;
                }
                else if (message_next) {
                    cout << "\n    " << line << "\n\n";
                    break;
                }
            }
            
            current_sha = parent_sha;
        }
    }

    void checkout(const string& commit_sha) {
    auto [type, content] = readObject(commit_sha);
    if (type != "commit") {
        throw runtime_error("Not a commit object");
    }
    
    // Parse commit to get tree SHA
    stringstream ss(content);
    string line;
    string tree_sha;
    
    while (getline(ss, line)) {
        if (line.substr(0, 5) == "tree ") {
            tree_sha = line.substr(5);
            break;
        }
    }
    
    // Get the name of the current executable
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer)-1);
    if (len != -1) {
        buffer[len] = '\0';
    }
    string executable_path = buffer;
    string executable_name = fs::path(executable_path).filename().string();
    
    // Clear working directory (except .mygit and the executable)
    for (const auto& entry : fs::directory_iterator(".")) {
        if (entry.path().filename().string() != GIT_DIR && 
            entry.path().filename().string() != executable_name) {
            fs::remove_all(entry.path());
        }
    }
    
    // Recursively restore files from tree
    restoreTree(tree_sha, ".", executable_name);
    
    // Update HEAD
    ofstream head(HEAD_FILE);
    head << commit_sha;
    
    // Set proper permissions for all restored files and directories
    for (const auto& entry : fs::recursive_directory_iterator(".")) {
        if (entry.path().filename().string() != GIT_DIR &&
            entry.path().filename().string() != executable_name) {
            fs::permissions(entry.path(), 
                        fs::perms::owner_all | 
                        fs::perms::group_read | 
                        fs::perms::others_read,
                        fs::perm_options::add);
        }
    }
}


private:
     void restoreTree(const string& tree_sha, const string& path, const string& executable_name) {
        auto [type, content] = readObject(tree_sha);
        if (type != "tree") {
            throw runtime_error("Not a tree object");
        }
        
        stringstream ss(content);
        string line;
        
        while (getline(ss, line)) {
            stringstream line_ss(line);
            string mode, name, sha;
            line_ss >> mode >> name >> sha;
            
            // Skip if this is the executable
            if (name == executable_name) {
                continue;
            }
            
            string full_path = path + "/" + name;
            
            if (mode == "40000") {
                // Directory
                fs::create_directories(full_path);
                restoreTree(sha, full_path, executable_name);
            } else {
                // File
                auto [blob_type, blob_content] = readObject(sha);
                if (blob_type != "blob") {
                    throw runtime_error("Not a blob object");
                }
                
                ofstream file(full_path, ios::binary);
                file.write(blob_content.data(), blob_content.length());
            }
        }
    }
};

// Main function to handle command-line arguments
int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: ./mygit <command> [arguments]\n";
        return 1;
    }

    MiniVCS vcs;
    string command = argv[1];

    try {
        if (command == "init") {
            vcs.init();
            cout << "Initialized empty repository in .mygit/\n";
        }
        else if (command == "hash-object") {
            bool write = false;
            string file_path;
            
            if (argc < 3) {
                cerr << "Usage: ./mygit hash-object [-w] <file>\n";
                return 1;
            }
            
            if (string(argv[2]) == "-w") {
                write = true;
                if (argc < 4) {
                    cerr << "No file specified\n";
                    return 1;
                }
                file_path = argv[3];
            } else {
                file_path = argv[2];
            }
            
            string sha = vcs.hashObject(file_path, write);
            cout << sha << "\n";
        }
        else if (command == "cat-file") {
            if (argc < 4) {
                cerr << "Usage: ./mygit cat-file <flag> <sha>\n";
                return 1;
            }
            
            string flag_str = argv[2];
            if (flag_str.length() != 2 || flag_str[0] != '-') {
                cerr << "Invalid flag\n";
                return 1;
            }
            
            char flag = flag_str[1];
            string sha = argv[3];
            
            vcs.catFile(sha, flag);
        }
        else if (command == "write-tree") {
            string sha = vcs.writeTree();
            cout << sha << "\n";
        }
        else if (command == "ls-tree") {
            if (argc < 3) {
                cerr << "Usage: ./mygit ls-tree [--name-only] <sha>\n";
                return 1;
            }
            
            bool name_only = false;
            string sha;
            
            if (string(argv[2]) == "--name-only") {
                name_only = true;
                if (argc < 4) {
                    cerr << "No SHA specified\n";
                    return 1;
                }
                sha = argv[3];
            } else {
                sha = argv[2];
            }
            
            vcs.lsTree(sha, name_only);
        }
        else if (command == "add") {
            if (argc < 3) {
                cerr << "Usage: ./mygit add <file1> [file2 ...]\n";
                return 1;
            }
            
            vector<string> paths;
            for (int i = 2; i < argc; i++) {
                paths.push_back(argv[i]);
            }
            
            vcs.add(paths);
        }
        else if (command == "commit") {
            string message;
            
            if (argc < 3) {
                message = "Default commit message";
            } else if (string(argv[2]) == "-m") {
                if (argc < 4) {
                    cerr << "No commit message provided\n";
                    return 1;
                }
                message = argv[3];
            } else {
                message = argv[2];
            }
            
            string sha = vcs.commit(message);
            cout << "Created commit " << sha << "\n";
        }
        else if (command == "log") {
            vcs.log();
        }
        else if (command == "checkout") {
            if (argc < 3) {
                cerr << "Usage: ./mygit checkout <commit_sha>\n";
                return 1;
            }
            
            string commit_sha = argv[2];
            vcs.checkout(commit_sha);
            cout << "Checked out commit " << commit_sha << "\n";
        }
        else {
            cerr << "Unknown command: " << command << "\n";
            return 1;
        }
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}