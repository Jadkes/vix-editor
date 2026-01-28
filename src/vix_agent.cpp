#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <chrono>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>

// ANSI Colors
#define MAGENTA "\033[1;35m"
#define CYAN    "\033[1;36m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define RED     "\033[1;31m"
#define BLUE    "\033[1;34m"
#define WHITE   "\033[1;37m"
#define GREY    "\033[1;90m"
#define RESET   "\033[0m"

using namespace std;

// --- Helper Tools ---

// Simple tokenizer
vector<string> split_string(const string& str, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(str);
    while (getline(tokenStream, token, delimiter)) {
        if(!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

// --- The Agent Class ---

class JarvisAgent {
private:
    PyObject *pModule;
    PyObject *pFuncProcess;
    PyObject *pFuncGenerate;
    string lastGeneratedCode;

    void initPython() {
        cout << GREY << "[System] Initializing Neural Interface (Python)..." << RESET << endl;
        Py_Initialize();
        
        // Get path to executable
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        string exePath = (count != -1) ? string(result, count) : "";
        string exeDir = exePath.substr(0, exePath.find_last_of("/"));

        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        string addPathCmd = "sys.path.append('" + exeDir + "')";
        PyRun_SimpleString(addPathCmd.c_str());

        PyObject *pName = PyUnicode_DecodeFSDefault("vix_brain");
        pModule = PyImport_Import(pName);
        Py_DECREF(pName);

        if (pModule == nullptr) {
            PyErr_Print();
            cerr << RED << "[CRITICAL] Brain module missing!" << RESET << endl;
            exit(1);
        }

        pFuncProcess = PyObject_GetAttrString(pModule, "process_input");
        pFuncGenerate = PyObject_GetAttrString(pModule, "generate_cpp");

        if (!pFuncProcess || !pFuncGenerate) {
            cerr << RED << "[CRITICAL] Brain functions missing!" << RESET << endl;
            exit(1);
        }
        cout << GREY << "[System] Interface Linked." << RESET << endl;
    }

    string callPython(PyObject* func, string arg) {
        if (!func) return "Error: Function not loaded";
        
        PyObject *pArgs = PyTuple_New(1);
        PyObject *pValue = PyUnicode_FromString(arg.c_str());
        PyTuple_SetItem(pArgs, 0, pValue);

        PyObject *pResult = PyObject_CallObject(func, pArgs);
        Py_DECREF(pArgs);

        string result = "";
        if (pResult != nullptr) {
            result = PyUnicode_AsUTF8(pResult);
            Py_DECREF(pResult);
        } else {
            PyErr_Print();
            result = "[Python Error]";
        }
        return result;
    }

public:
    JarvisAgent() {
        initPython();
        lastGeneratedCode = "";
    }

    ~JarvisAgent() {
        Py_XDECREF(pFuncProcess);
        Py_XDECREF(pFuncGenerate);
        Py_XDECREF(pModule);
        Py_Finalize();
    }

    void cmd_time() {
        time_t now = time(0);
        char* dt = ctime(&now);
        string time_str(dt);
        time_str.pop_back();
        cout << CYAN << ">> " << RESET << time_str << endl;
    }

    void cmd_ls() {
        DIR *dir;
        struct dirent *ent;
        cout << CYAN << ">> Listing current directory:" << RESET << endl;
        if ((dir = opendir (".")) != NULL) {
            int count = 0;
            while ((ent = readdir (dir)) != NULL) {
                string name = ent->d_name;
                if(name == "." || name == "..") continue;
                if (ent->d_type == DT_DIR) cout << BLUE << name << "/" << RESET << "  ";
                else cout << WHITE << name << RESET << "  ";
                
                count++;
                if(count % 5 == 0) cout << endl;
            }
            cout << endl;
            closedir (dir);
        } else {
            perror ("");
        }
    }

    void cmd_stats() {
        cout << CYAN << ">> Agent Status Report:" << RESET << endl;
        // Read memory usage from /proc/self/status
        ifstream status_file("/proc/self/status");
        string line;
        while(getline(status_file, line)) {
            if(line.find("VmRSS") != string::npos || line.find("VmSize") != string::npos) {
                cout << "   " << line << endl;
            }
        }
        status_file.close();
        cout << "   Core: Online (C++ Native)" << endl;
        cout << "   Brain: Connected (Python 3.12 Embed)" << endl;
    }

    void cmd_code(string topic) {
        cout << YELLOW << "[Generating C++ Code for '" << topic << "']..." << RESET << endl;
        string code = callPython(pFuncGenerate, topic);
        lastGeneratedCode = code;
        
        cout << "------------------------------------------" << endl;
        cout << GREEN << code << RESET << endl;
        cout << "------------------------------------------" << endl;
        cout << GREY << "(Type 'save <filename>' to save this snippet)" << RESET << endl;
    }

    void cmd_save(string filename) {
        if(lastGeneratedCode.empty()) {
            cout << RED << ">> No code to save! Generate something first." << RESET << endl;
            return;
        }
        
        ofstream out(filename);
        if(out.is_open()) {
            out << lastGeneratedCode;
            out.close();
            cout << GREEN << ">> Code saved to " << filename << RESET << endl;
        } else {
            cout << RED << ">> Error writing to file." << RESET << endl;
        }
    }

    void chat(string input) {
        string response = callPython(pFuncProcess, input);
        cout << MAGENTA << "Jarvis: " << RESET << response << endl;
    }

    void run() {
        cout << MAGENTA << "\n╔════════════════════════════════════════════╗" << endl;
        cout << "║  JARVIS AGENT: CODE & ASSIST               ║" << endl;
        cout << "║  C++ Core | Python Intelligence            ║" << endl;
        cout << "╚════════════════════════════════════════════╝" << RESET << endl;
        cout << GREY << "Type 'help' for agent commands." << RESET << endl;

        string input;
        while(true) {
            cout << CYAN << "\nYou: " << RESET;
            getline(cin, input);
            if(input.empty()) continue;

            vector<string> tokens = split_string(input, ' ');
            string cmd = tokens[0];
            for(auto& c : cmd) c = tolower(c);

            if(cmd == "exit" || cmd == "quit") {
                break;
            }
            else if(cmd == "help") {
                cout << YELLOW << "Agent Commands:" << RESET << endl;
                cout << "  code <topic>   : Generate C++ code (e.g., 'code class', 'code loop')" << endl;
                cout << "  save <file>    : Save the generated code to a file" << endl;
                cout << "  ls             : List files in current directory" << endl;
                cout << "  stats          : Show agent memory usage" << endl;
                cout << "  time           : Show current time" << endl;
                cout << "  clear          : Clear screen" << endl;
                cout << "  [text]         : Chat with the vix_brain" << endl;
            }
            else if(cmd == "time") cmd_time();
            else if(cmd == "ls" || cmd == "list") cmd_ls();
            else if(cmd == "stats") cmd_stats();
            else if(cmd == "clear") system("clear");
            else if(cmd == "code") {
                if(tokens.size() < 2) cout << RED << "Usage: code <topic>" << RESET << endl;
                else cmd_code(tokens[1]);
            }
            else if(cmd == "save") {
                if(tokens.size() < 2) cout << RED << "Usage: save <filename>" << RESET << endl;
                else cmd_save(tokens[1]);
            }
            else {
                // Pass full string to chat
                chat(input);
            }
        }
        cout << MAGENTA << "Jarvis: Systems Disengaging." << RESET << endl;
    }
};

int main() {
    JarvisAgent agent;
    agent.run();
    return 0;
}