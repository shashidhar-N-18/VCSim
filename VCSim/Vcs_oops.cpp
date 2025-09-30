#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <ctime>
#include <memory>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using namespace std;

// --------------------- File Classes ---------------------
class File {
protected:
    string name;
    string content;
    string stagedContent; // holds edited content before commit
    bool modified;        // track if file has been edited
public:
    File(const string& n, const string& c = "") 
        : name(n), content(c), stagedContent(c), modified(false) {}

    virtual ~File() {}

    virtual void showContent() const = 0;
    virtual File* clone() const = 0;

    void updateContent(const string& c) { 
        stagedContent = c; 
        modified = true;   
    }

    string getContent() const { return content; } // Original file content
    string getStagedContent() const { return stagedContent; } // Edited version
    string getName() const { return name; }

    bool isModified() const { return modified; }
    void clearModified() { 
        modified = false; 
        content = stagedContent; // commit the staged content
    }

    virtual void saveToDisk() const {
        ofstream out(name);
        out << content;  // Only original content saved
        out.close();
    }

    static File* loadFromDisk(const string& fname);
};

class TextFile : public File {
public:
    TextFile(const string& n, const string& c = "") : File(n, c) {}
    void showContent() const override {
        cout << "[TextFile] " << name << ": " << stagedContent << endl; // show staged/edited content
    }
    File* clone() const override { return new TextFile(*this); }
};

File* File::loadFromDisk(const string& fname) {
    if (!fs::exists(fname)) return nullptr;
    ifstream in(fname);
    string content, line;
    while (getline(in, line)) content += line + "\n";
    in.close();
    return new TextFile(fname, content);
}

// --------------------- Commit Class ---------------------
class Commit {
    int id;
    string message;
    string timestamp;
    map<string, unique_ptr<File>> files;
public:
    Commit(int i, const string& msg, const vector<File*>& staged) : id(i), message(msg) {
        time_t now = time(nullptr);
        timestamp = ctime(&now);
        if (!timestamp.empty() && timestamp.back() == '\n') timestamp.pop_back();

        for (File* f : staged) files[f->getName()] = unique_ptr<File>(f->clone());
    }

    void showDetails() const {
        cout << "Commit " << id << ": " << message << " at " << timestamp << endl;
        for (const auto& p : files) p.second->showContent();
    }

    map<string, File*> getSnapshot() const {
        map<string, File*> snapshot;
        for (const auto& p : files) snapshot[p.first] = p.second->clone();
        return snapshot;
    }

    int getId() const { return id; }
};

// --------------------- Repository (Singleton) ---------------------
class Repository {
    vector<unique_ptr<Commit>> commits;
    vector<File*> stagedFiles;
    int nextCommitId;

    Repository() : nextCommitId(1) { 
        fs::create_directory(".vcs"); 
    }

public:
    static Repository& getInstance() {
        static Repository instance;
        return instance;
    }

    void addFile(File* f) {
        if (find(stagedFiles.begin(), stagedFiles.end(), f) == stagedFiles.end()) {
            stagedFiles.push_back(f);
            cout << "Added file to staging: " << f->getName() << endl;
        }
    }

    void commit(const string& msg) {
        vector<File*> editableFiles;

        // Only commit files that are modified
        for (File* f : stagedFiles) {
            if (f->isModified()) editableFiles.push_back(f);
        }

        if (editableFiles.empty()) {
            cout << "No edited files to commit! Edit files first." << endl;
            return;
        }

        string commitPath = ".vcs/commit_" + to_string(nextCommitId);
        fs::create_directory(commitPath);

        for (File* f : editableFiles) {
            string filePath = commitPath + "/" + f->getName();
            ofstream out(filePath);
            out << f->getStagedContent();
            out.close();

            // Update original file content after commit
            f->clearModified();
            f->saveToDisk();
        }

        commits.push_back(make_unique<Commit>(nextCommitId++, msg, editableFiles));

        for (File* f : editableFiles) {
            stagedFiles.erase(remove(stagedFiles.begin(), stagedFiles.end(), f), stagedFiles.end());
        }

        cout << "Commit done! Changes saved to .vcs and original files updated." << endl;
    }

    void log() const {
        if (commits.empty()) { 
            cout << "No commits yet." << endl; 
            return; 
        }
        for (const auto& c : commits) {
            c->showDetails();
            cout << "--------------------" << endl;
        }
    }

    void checkout(int commitId, vector<File*>& workingFiles) {
        for (const auto& c : commits) {
            if (c->getId() == commitId) {
                map<string, File*> snapshot = c->getSnapshot();
                workingFiles.clear();
                for (auto& p : snapshot) {
                    workingFiles.push_back(p.second);
                    ofstream out(p.second->getName());
                    out << p.second->getContent();
                    out.close();
                }
                cout << "Checked out commit " << commitId << ", files restored on disk." << endl;
                return;
            }
        }
        cout << "Commit ID not found!" << endl;
    }

    void cleanup() {
        if (fs::exists(".vcs")) fs::remove_all(".vcs");
    }

    friend class VCS;
};

// --------------------- VCS Controller ---------------------
class VCS {
    vector<File*> workingFiles;
    Repository& repo;
public:
    VCS() : repo(Repository::getInstance()) {}

    void runCommand(const string& cmd) {
        if (cmd.rfind("add ", 0) == 0) {
            string fname = cmd.substr(4);
            File* f = File::loadFromDisk(fname);
            if (!f) {
                cout << "File does not exist on disk. Create new? (y/n): ";
                char ch; cin >> ch; cin.ignore();
                if (ch == 'y' || ch == 'Y') {
                    f = new TextFile(fname);
                    ofstream out(fname); out.close();
                    cout << "File created." << endl;
                    cout << "Do you want to edit now? (y/n): ";
                    cin >> ch; cin.ignore();
                    if (ch == 'y' || ch == 'Y') editFile(f);
                } else return;
            }

            if (find(workingFiles.begin(), workingFiles.end(), f) == workingFiles.end())
                workingFiles.push_back(f);

            repo.addFile(f);
        } 
        else if (cmd.rfind("edit ", 0) == 0) {
            string fname = cmd.substr(5);
            for (File* f : workingFiles) {
                if (f->getName() == fname) { 
                    editFile(f); 
                    return; 
                }
            }
            cout << "File not found in working directory!" << endl;
        } 
        else if (cmd.rfind("commit ", 0) == 0) {
            string msg = cmd.substr(7);
            repo.commit(msg);
        } 
        else if (cmd == "log") {
            repo.log();
        } 
        else if (cmd.rfind("checkout ", 0) == 0) {
            int id = stoi(cmd.substr(9));
            repo.checkout(id, workingFiles);
        } 
        else {
            cout << "Unknown command!" << endl;
        }
    }

    void editFile(File* f) {
        cout << "Enter new content for " << f->getName() << ": ";
        string newContent;
        getline(cin, newContent);
        f->updateContent(newContent);

        cout << f->getName() << " updated in memory (not saved to disk)." << endl;
        cout << "Note: Changes are staged. Use 'commit <msg>' to save these changes permanently." << endl;

        repo.addFile(f);
    }

    void showWorkingFiles() {
        cout << "Current Working Files:" << endl;
        for (File* f : workingFiles) f->showContent();
    }

    ~VCS() { repo.cleanup(); }
};

// --------------------- Main ---------------------
int main() {
    VCS vcs;
    string cmd;
    cout << "Mini VCS running. Commands: add <file>, edit <file>, commit <msg>, log, checkout <id>, exit" << endl;
    while (true) {
        cout << ">> ";
        getline(cin, cmd);
        if (cmd == "exit") break;
        vcs.runCommand(cmd);
    }
    return 0;
}
